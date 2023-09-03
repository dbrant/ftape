/*
 *      Copyright (C) 1996-1998 Claus-Justus Heine

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $RCSfile: zftape-rw.c,v $
 * $Revision: 1.21 $
 * $Date: 2000/07/10 21:24:28 $
 *
 *      This file contains some common code for the r/w code for
 *      zftape.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/segment.h>

#define ZFTAPE_TRACING

#include <linux/zftape.h>
#include "zftape-init.h"
#include "zftape-eof.h"
#include "zftape-ctl.h"
#include "zftape-write.h"
#include "zftape-read.h"
#include "zftape-rw.h"
#include "zftape-vtbl.h"
#include "zftape-inline.h"

/*      Global vars.
 */

/*      Local vars.
 */

unsigned int zft_get_seg_sz(zftape_info_t *zftape,
			    unsigned int segment)
{
	int size;
	TRACE_FUN(ft_t_any);
	
	size = FT_SEGMENT_SIZE - 
		count_ones(ftape_get_bad_sector_entry(zftape->ftape, segment))*FT_SECTOR_SIZE;
	if (size > 0) {
		TRACE_EXIT (unsigned)size; 
	} else {
		TRACE_EXIT 0;
	}
}

int zft_set_flags(zftape_info_t *zftape, unsigned minor_unit)
{     
	TRACE_FUN(ft_t_flow);

#if defined(CONFIG_ZFT_OBSOLETE) || \
    defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
# if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	zftape->use_compression = 0;
# endif
	switch (minor_unit & ZFT_MINOR_OP_MASK) {
# if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	case (ZFT_Q80_MODE | ZFT_ZIP_MODE):
	case ZFT_ZIP_MODE:
		zftape->use_compression = 1;
# endif
# if defined(CONFIG_ZFT_OBSOLETE)
	case 0:
	case ZFT_Q80_MODE:
	case ZFT_RAW_MODE:
		break;
# endif
	default:
		TRACE(ft_t_warn, "Warning:\n"
		      KERN_INFO "Wrong combination of minor device bits.\n"
		      KERN_INFO "Bailing out.");
		TRACE_EXIT -ENXIO;
		break;
	}
#endif
	TRACE_EXIT 0;
}

/* computes the segment and byte offset inside the segment
 * corresponding to tape_pos.
 *
 * tape_pos gives the offset in bytes from the beginning of the
 * ft_first_data_segment *seg_byte_pos is the offset in the current
 * segment in bytes
 *
 * Ok, if this routine was called often one should cache the last data
 * pos it was called with, but actually this is only needed in
 * ftape_seek_block(), that is, almost never.
 */
int zft_calc_seg_byte_coord(zftape_info_t *zftape,
			    int *seg_byte_pos, __s64 tape_pos)
{
	int segment;
	int seg_sz;
	TRACE_FUN(ft_t_flow);
	
	if (tape_pos == 0) {
		*seg_byte_pos = 0;
		segment = zftape->ftape->first_data_segment;
	} else {
		seg_sz = 0;
		
		for (segment = zftape->ftape->first_data_segment; 
		     ((tape_pos > 0) && (segment <= zftape->ftape->last_data_segment));
		     segment++) {
			seg_sz = zft_get_seg_sz(zftape, segment); 
			tape_pos -= seg_sz;
		}
		if(tape_pos >= 0) {
			/* the case tape_pos > != 0 means that the
			 * argument tape_pos lies beyond the EOT.
			 */
			*seg_byte_pos= 0;
		} else { /* tape_pos < 0 */
			segment--;
			*seg_byte_pos= tape_pos + seg_sz;
		}
	}
	TRACE_EXIT(segment);
}

/* ftape_calc_tape_pos().
 *
 * computes the offset in bytes from the beginning of the
 * ft_first_data_segment inverse to ftape_calc_seg_byte_coord
 *
 * We should do some caching. But how:
 *
 * Each time the header segments are read in, this routine is called
 * with ft_tracks_per_tape*segments_per_track argumnet. So this should be
 * the time to reset the cache.
 *
 * Also, it might be in the future that the bad sector map gets
 * changed.  -> reset the cache
 */

__s64 zft_get_capacity(zftape_info_t *zftape)
{
	zftape->seg_pos  = zftape->ftape->first_data_segment;
	zftape->tape_pos = 0;

	while (zftape->seg_pos <= zftape->ftape->last_data_segment) {
		zftape->tape_pos += zft_get_seg_sz(zftape, zftape->seg_pos ++);
	}
	return zftape->tape_pos;
}

__s64 zft_calc_tape_pos(zftape_info_t *zftape, int segment)
{
	ftape_info_t *ftape = zftape->ftape;
	int d1, d2, d3;
	TRACE_FUN(ft_t_any);
	
	if (segment > ftape->last_data_segment) {
	        TRACE_EXIT zftape->capacity;
	}
	if (segment < ftape->first_data_segment) {
		TRACE_EXIT 0;
	}
	d2 = segment - zftape->seg_pos;
	if (-d2 > 10) {
		d1 = segment - ftape->first_data_segment;
		if (-d2 > d1) {
			zftape->tape_pos = 0;
			zftape->seg_pos = ftape->first_data_segment;
			d2 = d1;
		}
	}
	if (d2 > 10) {
		d3 = ftape->last_data_segment - segment;
		if (d2 > d3) {
			zftape->tape_pos = zftape->capacity;
			zftape->seg_pos  = ftape->last_data_segment + 1;
			d2 = -d3;
		}
	}		
	if (d2 > 0) {
		while (zftape->seg_pos < segment) {
			zftape->tape_pos +=  zft_get_seg_sz(zftape, (zftape->seg_pos)++);
		}
	} else {
		while (zftape->seg_pos > segment) {
			zftape->tape_pos -=  zft_get_seg_sz(zftape, --(zftape->seg_pos));
		}
	}
	TRACE(ft_t_noise, "new cached pos: %d", zftape->seg_pos);
	
	TRACE_EXIT zftape->tape_pos;
}

/* copy Z-label string to buffer, keeps track of the correct offset in
 * `buffer' 
 */
void zft_update_label(zftape_info_t *zftape, __u8 *buffer)
{ 
	TRACE_FUN(ft_t_flow);
	
	if (strncmp(&buffer[FT_LABEL], ZFTAPE_LABEL, 
		    sizeof(ZFTAPE_LABEL)-1) != 0) {
		TRACE(ft_t_info, "updating label from \"%s\" to \"%s\"",
		      &buffer[FT_LABEL], ZFTAPE_LABEL);
		strcpy(&buffer[FT_LABEL], ZFTAPE_LABEL);
		memset(&buffer[FT_LABEL] + sizeof(ZFTAPE_LABEL) - 1, ' ', 
		       FT_LABEL_SZ - sizeof(ZFTAPE_LABEL + 1));
		PUT4(buffer, FT_LABEL_DATE, 0);
		zftape->label_changed = zftape->header_changed = 1; /* changed */
	}
	TRACE_EXIT;
}

/* release the deblock buffer s.t. the lowlevel FDC modules can be unloaded.
 */
void zft_release_deblock_buffer(zftape_info_t *zftape, __u8 **buffer)
{
	TRACE_FUN(ft_t_any);

	zft_zap_read_buffers(zftape); /* just in case */

	zftape->deblock_segment = -1;

	if (*buffer != NULL) {
		if (zftape->ftape && zftape->ftape->fdc &&
		    fdc_put_deblock_buffer(zftape->ftape->fdc, buffer) < 0) {
			TRACE(ft_t_bug,"fdc_put_deblock_buffer(%p, %p) failed",
			      zftape->ftape->fdc, *buffer);
		}
		*buffer = NULL; /* isn't valid any more in either case */
	}
	TRACE_EXIT;
}

/* We don't handle hard write errors here.
 */
int zft_verify_write_segments(zftape_info_t *zftape,
			      unsigned int segment, __u8 *data, size_t size)
{
	int result;
	__u8 *src_buf;
	int single;
	int seg_pos;
	int seg_sz;
	int remaining;
	ft_write_mode_t write_mode;
	TRACE_FUN(ft_t_flow);

	zftape->deblock_segment = -1;
	
	seg_pos   = segment;
	seg_sz    = zft_get_seg_sz(zftape, seg_pos);
	src_buf   = data;
	single    = size <= seg_sz;
	remaining = size;
	do {
		TRACE(ft_t_noise, "\n"
		      KERN_INFO "remaining: %d\n"
		      KERN_INFO "seg_sz   : %d\n"
		      KERN_INFO "segment  : %d",
		      remaining, seg_sz, seg_pos);
		if ((zftape->deblock_buf = fdc_get_deblock_buffer(zftape->ftape->fdc)) == NULL) {
			TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
		}
		if (remaining == seg_sz) {
			memcpy(zftape->deblock_buf, src_buf, seg_sz);
			write_mode = single ? FT_WR_SINGLE : FT_WR_MULTI;
			remaining = 0;
		} else if (remaining > seg_sz) {
			memcpy(zftape->deblock_buf, src_buf, seg_sz);
			write_mode = FT_WR_ASYNC; /* don't start tape */
			remaining -= seg_sz;
		} else { /* remaining < seg_sz */
			memcpy(zftape->deblock_buf, src_buf, remaining);
			memset(zftape->deblock_buf + remaining, '\0',
			       seg_sz - remaining);
			write_mode = single ? FT_WR_SINGLE : FT_WR_MULTI;
			remaining = 0;
		}
		if ((result = ftape_write_segment(zftape->ftape, seg_pos, 
						  &zftape->deblock_buf, 
						  write_mode)) != seg_sz) {
			TRACE(ft_t_err, "Error: "
			      "Couldn't write segment %d", seg_pos);
			TRACE_EXIT result < 0 ? result : -EIO; /* bail out */
		}
		zftape->written_segments ++;
		seg_sz = zft_get_seg_sz(zftape, ++seg_pos);
		src_buf += result;
	} while (remaining > 0);
	if (zftape->ftape->driver_state == writing) {
		TRACE_CATCH(ftape_loop_until_writes_done(zftape->ftape),);
		TRACE_CATCH(ftape_abort_operation(zftape->ftape),);
		zft_prevent_flush(zftape);
	}
	seg_pos = segment;
	src_buf = data;
	remaining = size;
	do {
		TRACE_CATCH(result = ftape_read_segment(zftape->ftape,
							seg_pos,
							&zftape->deblock_buf, 
							single ? FT_RD_SINGLE
							: FT_RD_AHEAD),);
		if (zftape->deblock_buf == NULL) {
			TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
		}
		if (memcmp(src_buf, zftape->deblock_buf, 
			   remaining > result ? result : remaining) != 0) {
			TRACE_ABORT(-EIO, ft_t_err,
				    "Failed to verify written segment %d",
				    seg_pos);
		}
		remaining -= result;
		TRACE(ft_t_noise, "verify successful:\n"
		      KERN_INFO "segment  : %d\n"
		      KERN_INFO "segsize  : %d\n"
		      KERN_INFO "remaining: %d",
		      seg_pos, result, remaining);
		src_buf   += seg_sz;
		seg_pos++;
	} while (remaining > 0);
	TRACE_EXIT size;
}


/* zft_erase().  implemented compression-handling
 *
 * calculate the first data-segment when using/not using compression.
 *
 * update header-segment and compression-map-segment.
 */
int zft_erase(zftape_info_t *zftape)
{
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (!zftape->header_read) {
		TRACE_CATCH(ftape_vmalloc(FTAPE_SEL(zftape->unit), (void **)&zftape->hseg_buf,
					  FT_SEGMENT_SIZE),);
		TRACE_CATCH(zft_read_header_segments(zftape),);
		TRACE(ft_t_noise,
		      "ft_first_data_segment: %d, ft_last_data_segment: %d", 
		      zftape->ftape->first_data_segment,
		      zftape->ftape->last_data_segment);
	}
	if (zftape->old_ftape) {
		zft_clear_ftape_file_marks(zftape);
		zftape->old_ftape = 0; /* no longer old ftape */
	}
	PUT2(zftape->hseg_buf, FT_CMAP_START, 0);
	zftape->volume_table_changed = 1;
	zftape->capacity = zft_get_capacity(zftape);
	zft_init_vtbl(zftape);
	/* the remainder must be done in ftape_update_header_segments 
	 */
	zftape->header_read =
		zftape->header_changed       =
		zftape->vtbl_read            =
		zftape->volume_table_changed = 1;
	result = zft_update_header_segments(zftape);

	ftape_abort_operation(zftape->ftape);

	zft_reset_position(zftape, &zftape->pos);
	zft_set_flags (zftape, zftape->unit);
	TRACE_EXIT result;
}

unsigned int zft_get_time(void) 
{
	unsigned int date = FT_TIME_STAMP(2097, 11, 30, 23, 59, 59); /* fun */
	return date;
}

