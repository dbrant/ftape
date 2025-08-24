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
 * $RCSfile: zftape-read.c,v $
 * $Revision: 1.18 $
 * $Date: 2000/07/12 17:20:28 $
 *
 *      This file contains the high level reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 */


#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#define ZFTAPE_TRACING
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

void zft_zap_read_buffers(zftape_info_t *zftape)
{
	zftape->buf_len_rd      = 0;
}

int zft_read_header_segments(zftape_info_t *zftape)
{
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(ftape_vmalloc(FTAPE_SEL(zftape->unit),
				  &zftape->hseg_buf, FT_SEGMENT_SIZE),);

	zftape->deblock_buf     = NULL; /* ftape_read_header_segment() will mess it up */
	zftape->deblock_segment = -1;
	TRACE_CATCH(ftape_read_header_segment(zftape->ftape,
					      zftape->hseg_buf),);
	TRACE(ft_t_info, "Segments written since first format: %d",
	      (int)GET4(zftape->hseg_buf, FT_SEG_CNT));
	zftape->qic113 = (zftape->ftape->format_code != fmt_normal &&
			  zftape->ftape->format_code != fmt_1100ft &&
			  zftape->ftape->format_code != fmt_425ft);
	TRACE(ft_t_info,
	      "zftape->ftape->first_data_segment: %d, "
	      "zftape->ftape->last_data_segment: %d", 
	      zftape->ftape->first_data_segment, zftape->ftape->last_data_segment);
	zftape->capacity = zft_get_capacity(zftape);
	zftape->old_ftape = zft_ftape_validate_label(zftape, &zftape->hseg_buf[FT_LABEL]);
	zftape->header_read = 1;
	zftape->vtbl_read   = 0;
	zftape->vtbl_size   = 0;
	zft_set_flags(zftape, zftape->unit);
	zft_init_vtbl(zftape); /* create the dummy entries */
	TRACE_EXIT 0;
}

int zft_read_volume_table(zftape_info_t *zftape)
{
	__u8 *deblock_buf;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	zftape->deblock_buf     = NULL; /* we will mess it up */
	zftape->deblock_segment = -1;
	if (zftape->old_ftape) {
		TRACE(ft_t_info, 
		      "Found old ftaped tape, emulating eof marks, "
		      "entering read-only mode");
		zft_ftape_extract_file_marks(zftape, zftape->hseg_buf);
		TRACE_CATCH(zft_fake_volume_headers(zftape, zftape->eof_map, 
						    zftape->nr_eof_marks),);
		zftape->vtbl_read = 1;		
		TRACE_EXIT 0;
	}
	/* the specs say that the volume table must be initialized
	 * with zeroes during formatting, so it MUST be readable,
	 * i.e. contain vaid ECC information.
	 */
	result = ftape_read_segment(zftape->ftape,
				    zftape->ftape->first_data_segment, 
				    &deblock_buf,
				    FT_RD_SINGLE);
	if (result < 0) {
		TRACE(ft_t_err, "Error reading volume table segment at %d",
		      zftape->ftape->first_data_segment);
		TRACE_EXIT result;
	}
	zftape->vtbl_size = result;

	if (deblock_buf == NULL) { /* should not happen */
		TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
	}
	if ((result = ftape_abort_operation(zftape->ftape)) < 0) {
		goto out;
	}

	TRACE_CATCH(zft_vmalloc_once(zftape,
				     &zftape->vtbl_buf, FT_SEGMENT_SIZE),);
	memcpy(zftape->vtbl_buf, deblock_buf, FT_SEGMENT_SIZE);
	zft_extract_volume_headers(zftape, zftape->vtbl_buf);
	zftape->vtbl_read = 1;
 out:
	TRACE_CATCH(fdc_put_deblock_buffer(zftape->ftape->fdc, &deblock_buf),
		    zftape->vtbl_read = 0);
	TRACE_EXIT result;
}

int zft_fetch_segment(zftape_info_t *zftape,
		      unsigned int segment, __u8 **buffer,
		      ft_read_mode_t read_mode)
{
	int seg_sz;
	TRACE_FUN(ft_t_flow);

	if (segment == zftape->deblock_segment) {
		TRACE(ft_t_data_flow,
		      "re-using segment %d already in deblock buffer",
		      segment);
		TRACE_EXIT zft_get_seg_sz(zftape, segment);
	}
	zft_release_deblock_buffer(zftape, buffer);
	TRACE_CATCH(seg_sz = ftape_read_segment(zftape->ftape,
						segment, buffer, read_mode),);
	TRACE(ft_t_data_flow, "segment %d, result %d", segment, seg_sz);
	if (*buffer == NULL) {
		TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
	}
	/*  this implicitly assumes that we are always called with
	 *  buffer == &zftape->deblock_buf 
	 */
	zftape->deblock_segment = segment;
	TRACE_EXIT seg_sz;
}

/* req_len: gets clipped due to EOT of EOF.
 * req_clipped: is a flag indicating whether req_len was clipped or not
 * volume: contains information on current volume (blk_sz etc.)
 */
static int check_read_access(zftape_info_t *zftape,
			     int *req_len, 
			     const zft_volinfo **volume,
			     int *req_clipped, 
			     const zft_position *pos)
{
	TRACE_FUN(ft_t_flow);
	
	if (zftape->io_state != zft_reading) {
		if (zftape->offline) { /* offline includes no_tape */
			TRACE_ABORT(-ENXIO, ft_t_warn,
				    "tape is offline or no cartridge");
		}
		if (!zftape->ftape->formatted) {
			TRACE_ABORT(-EACCES,
				    ft_t_warn, "tape is not formatted");
		}
		/*  now enter defined state, read header segment if not
		 *  already done and flush write buffers
		 */
		TRACE_CATCH(zft_def_idle_state(zftape),);
		zftape->io_state = zft_reading;
		if (zft_tape_at_eod(zftape, pos)) {
			zftape->eod = 1;
			TRACE_EXIT 1;
		}
		zftape->eod = 0;
		*volume = zft_volume_by_segpos(zftape, pos->seg_pos);
		/* get the space left until EOF */
		zftape->remaining = zft_check_for_eof(*volume, pos);
		zftape->buf_len_rd = 0;
		TRACE(ft_t_noise, "remaining: " LL_X ", vol_no: %d",
		      LL(zftape->remaining), (*volume)->count);
	} else if (zft_tape_at_eod(zftape, pos)) {
		if (++zftape->eod > 2) {
			TRACE_EXIT -EIO; /* st.c also returns -EIO */
		} else {
			TRACE_EXIT 1;
		}
	}
	if ((*req_len % (*volume)->blk_sz) != 0) {
		/*  this message is informational only. The user gets the
		 *  proper return value
		 */
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "req_len %d not a multiple of block size %d",
			    *req_len, (*volume)->blk_sz);
	}
	/* As GNU tar doesn't accept partial read counts when the
	 * multiple volume flag is set, we make sure to return the
	 * requested amount of data. Except, of course, at the end of
	 * the tape or file mark.  
	 */
	zftape->remaining -= *req_len;
	if (zftape->remaining <= 0) {
		TRACE(ft_t_noise, 
		      "clipped request from %d to %d.", 
		      *req_len, (int)(*req_len + zftape->remaining));
		*req_len += zftape->remaining;
		*req_clipped = 1;
	} else {
		*req_clipped = 0;
	}
	TRACE_EXIT 0;
}

/* this_segs_size: the current segment's size.
 * buff: the USER-SPACE buffer provided by the calling function.
 * req_len: how much data should be read at most.
 * volume: contains information on current volume (blk_sz etc.)
 */  
static int empty_deblock_buf(zftape_info_t *zftape,
			     __u8 *usr_buf, const int req_len,
			     const __u8 *src_buf, const int seg_sz,
			     zft_position *pos,
			     const zft_volinfo *volume)
{
	int cnt = 0;
	int result = 0;
	TRACE_FUN(ft_t_flow);

#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	if (zftape->use_compression && volume->use_compression) {
		TRACE_CATCH(zft_cmpr_lock(zftape, 1 /* try to load */),);
		TRACE_CATCH(result= (*zft_cmpr_ops->read)(zftape->cmpr_handle,
							  &cnt,
							  usr_buf, req_len,
							  src_buf, seg_sz,
							  pos, volume),);
	} else	
#endif
	{
		if (seg_sz - pos->seg_byte_pos < req_len) {
			cnt = seg_sz - pos->seg_byte_pos;
		} else {
			cnt = req_len;
		}
		if (copy_to_user(usr_buf,
				 src_buf + pos->seg_byte_pos, cnt) != 0) {
			TRACE_EXIT -EFAULT;
		}
		result = cnt;
	}
	TRACE(ft_t_data_flow, "nr bytes just read: %d", cnt);
	pos->volume_pos    += result;
        pos->tape_pos      += cnt;
	pos->seg_byte_pos  += cnt;
	zftape->buf_len_rd -= cnt; /* remaining bytes in buffer */
	TRACE(ft_t_data_flow, "buf_len_rd: %d, cnt: %d",
	      zftape->buf_len_rd, cnt);
	if(pos->seg_byte_pos >= seg_sz) {
		pos->seg_pos++;
		pos->seg_byte_pos = 0;
	}
	TRACE(ft_t_data_flow, "bytes moved out of deblock-buffer: %d", cnt);
	TRACE_EXIT result;
}


/* note: we store the segment id of the segment that is inside the
 * deblock buffer. This spares a lot of ftape_read_segment()s when we
 * use small block-sizes. The block-size may be 1kb (SECTOR_SIZE). In
 * this case a MTFSR 28 maybe still inside the same segment.
 */
int _zft_read(zftape_info_t *zftape, char* buff, int req_len)
{
	int req_clipped;
	int result     = 0;
	int bytes_read = 0;
	TRACE_FUN(ft_t_flow);
	
	zftape->resid = req_len;
	result = check_read_access(zftape, &req_len, &zftape->volume,
				   &req_clipped, &zftape->pos);
	switch(result) {
	case 0: 
		break; /* nothing special */
	case 1: 
		TRACE(ft_t_noise, "EOD reached");
		TRACE_EXIT 0;   /* EOD */
	default:
		TRACE_ABORT(result, ft_t_noise,
			    "check_read_access() failed with result %d",
			    result);
		TRACE_EXIT result;
	}
	while (req_len > 0) { 
		/*  Allow escape from this loop on signal !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/* buf_len_rd == 0 means that we need to read a new
		 * segment.
		 */
		if (zftape->buf_len_rd == 0) {
			while((result = zft_fetch_segment(zftape,
							  zftape->pos.seg_pos,
							  &zftape->deblock_buf,
							  FT_RD_AHEAD)) == 0) {
				zftape->pos.seg_pos ++;
				zftape->pos.seg_byte_pos = 0;
			}
			if (result < 0) {
				zftape->resid -= bytes_read;
				TRACE_ABORT(result, ft_t_noise,
					    "zft_fetch_segment(): %d",
					    result);
			}
			zftape->seg_sz = result;
			zftape->buf_len_rd =
				zftape->seg_sz - zftape->pos.seg_byte_pos;
		}
		TRACE_CATCH(result = empty_deblock_buf(zftape,
						       buff, 
						       req_len,
						       zftape->deblock_buf, 
						       zftape->seg_sz, 
						       &zftape->pos,
						       zftape->volume),
			    zftape->resid -= bytes_read);
		TRACE(ft_t_data_flow, "bytes just read: %d", result);
		bytes_read += result; /* what we got so far       */
		buff       += result; /* index in user-buffer     */
		req_len    -= result; /* what's left from req_len */
	} /* while (req_len  > 0) */
	if (req_clipped) {
		TRACE(ft_t_data_flow,
		      "maybe partial count because of eof mark");
		if (zftape->just_before_eof && bytes_read == 0) {
			/* req_len was > 0, but user didn't get
			 * anything the user has read in the eof-mark 
			 */
			zft_move_past_eof(zftape, &zftape->pos);
			ftape_abort_operation(zftape->ftape);
			zftape->io_state = zft_idle;
		} else {
			/* don't skip to the next file before the user
			 * tried to read a second time past EOF Just
			 * mark that we are at EOF and maybe decrement
			 * zft_seg_pos to stay in the same volume;
			 */
			zftape->just_before_eof = 1;
			zft_position_before_eof(zftape,
						&zftape->pos, zftape->volume);
			TRACE(ft_t_noise, "just before eof");
		}
	}
	zftape->resid -= result; /* for MTSTATUS       */
	TRACE_EXIT bytes_read;
}
