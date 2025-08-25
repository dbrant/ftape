/*
 *      Copyright (c) 1995-2000 Claus-Justus Heine 

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.

 *
 * $RCSfile: zftape-vtbl.c,v $
 * $Revision: 1.25 $
 * $Date: 2000/07/20 13:19:21 $
 *
 *      This file defines a volume table as defined in various QIC
 *      standards.
 * 
 *      This is a minimal implementation, just allowing ordinary DOS
 *      :( prgrams to identify the cartridge as used.
 */


#include <linux/errno.h>

#include <linux/zftape.h>

#include "zftape-init.h"
#include "zftape-eof.h"
#include "zftape-ctl.h"
#include "zftape-write.h"
#include "zftape-read.h"
#include "zftape-rw.h"
#include "zftape-vtbl.h"
#include "zftape-inline.h"

#define ZFT_CMAP_HACK 1 /* leave this defined to hide the compression map */


void zft_create_volume_headers(zftape_info_t *zftape, __u8 *buffer);


/*
 *  global variables 
 */

inline void zft_new_vtbl_entry(zftape_info_t *zftape)
{
	struct list_head *tmp = &zft_last_vtbl->node;
	zft_volinfo *new = ftape_kmalloc(FTAPE_SEL(zftape->unit),
					 sizeof(zft_volinfo), 1);

	list_add(&new->node, tmp);
	new->count = zft_eom_vtbl->count ++;
}

void zft_free_vtbl(zftape_info_t *zftape)
{
	/* this catches the case were next == prev == NULL
	 */
	if (zftape->vtbl.next == NULL ||
	    zftape->vtbl.prev == NULL) {
		INIT_LIST_HEAD(&zftape->vtbl);
	}
	for (;;) {
		struct list_head *tmp = zftape->vtbl.prev;
		zft_volinfo *vtbl;

		if (tmp == &zftape->vtbl)
			break;
		list_del(tmp);
		vtbl = list_entry(tmp, zft_volinfo, node);
		ftape_kfree(FTAPE_SEL(zftape->unit),
			    &vtbl,
			    sizeof(zft_volinfo));
	}
	INIT_LIST_HEAD(&zftape->vtbl);
	zftape->cur_vtbl = NULL;
}

/*  initialize vtbl, called by ftape_new_cartridge()
 */
void zft_init_vtbl(zftape_info_t *zftape)
{ 
	zft_volinfo *new;

	zftape->exvt_extension = 0;
	zftape->vtbl_broken    = 0;

	zft_free_vtbl(zftape);
	
	/*  Create the two dummy vtbl entries
	 */
	new = ftape_kmalloc(FTAPE_SEL(zftape->unit), sizeof(zft_volinfo), 1);
	list_add(&new->node, &zftape->vtbl);
	new = ftape_kmalloc(FTAPE_SEL(zftape->unit), sizeof(zft_volinfo), 1);
	list_add(&new->node, &zftape->vtbl);

	zft_head_vtbl->end_seg   = zftape->ftape->first_data_segment;
	zft_head_vtbl->blk_sz    = zftape->blk_sz;
	zft_head_vtbl->count     = -1;
	zft_head_vtbl->offset    = -128;

	zft_eom_vtbl->start_seg  = zftape->ftape->first_data_segment+1;
	zft_eom_vtbl->end_seg    = zftape->ftape->last_data_segment +1;
	zft_eom_vtbl->blk_sz     = zftape->blk_sz;
	zft_eom_vtbl->count      = 0;
	zft_eom_vtbl->offset     = 0;

	/*  Reset the pointer for zft_find_volume()
	 */
	zftape->cur_vtbl = zft_eom_vtbl;

	/* initialize the dummy vtbl entries for zftape->qic_mode == 0
	 */
	zftape->eot_vtbl.start_seg       = zftape->ftape->last_data_segment +1;
	zftape->eot_vtbl.end_seg         = zftape->ftape->last_data_segment +1;
	zftape->eot_vtbl.blk_sz          = zftape->blk_sz;
	zftape->eot_vtbl.size =
		zftape->eot_vtbl.rawsize = 0;
	zftape->eot_vtbl.count           = -1;
	zftape->eot_vtbl.offset          = -1;

	zftape->tape_vtbl.start_seg = zftape->ftape->first_data_segment;
	zftape->tape_vtbl.end_seg   = zftape->ftape->last_data_segment;
	zftape->tape_vtbl.blk_sz    = zftape->blk_sz;
	zftape->tape_vtbl.size      =
		zftape->tape_vtbl.rawsize = zftape->capacity;
	zftape->tape_vtbl.count     = 0;
	zftape->tape_vtbl.offset    = -1;
}

/* We used to store the block-size of the volume in the volume-label,
 * using the keyword "blocksize". The blocksize written to the
 * volume-label is in bytes.
 *
 * We use this now only for compatability with old zftape version. We
 * store the blocksize directly as binary number in the vendor
 * extension part of the volume entry.
 */
static int check_volume_label(zftape_info_t *zftape,
			      const char *label, int *blk_sz)
{ 
	int  valid_format;
	char *blocksize;
	char pr_label[VTBL_DESC_LEN+1];
	TRACE_FUN(ft_t_flow);
	
	memcpy(pr_label, label, VTBL_DESC_LEN);
	pr_label[VTBL_DESC_LEN] = '\0';
	TRACE(ft_t_noise, "called with \"%s\" / \"%s\"",
	      pr_label, ZFT_VOL_NAME);
	if (strncmp(label, ZFT_VOL_NAME, strlen(ZFT_VOL_NAME)) != 0) {
#if 0
		*blk_sz = 1; /* smallest block size that we allow */
#else
		*blk_sz = 1024; /* smallest block size that we allow
				 * and which works with cartridges >
				 * 2^32 bytes
				 */
#endif
		valid_format = 0;
	} else {
		TRACE(ft_t_noise, "got old style zftape vtbl entry");
		/* get the default blocksize */
		/* use the kernel strstr()   */
		blocksize= strstr(label, " blocksize ");
		if (blocksize) {
			blocksize += strlen(" blocksize ");
			for(*blk_sz= 0; 
			    *blocksize >= '0' && *blocksize <= '9'; 
			    blocksize++) {
				*blk_sz *= 10;
				*blk_sz += *blocksize - '0';
			}
			if (*blk_sz > ZFT_MAX_BLK_SZ) {
				*blk_sz= 1;
				valid_format= 0;
			} else {
				valid_format = 1;
			}
		} else {
#if 0
			*blk_sz = 1; /* smallest block size that we allow */
#else
			*blk_sz = 1024; /* smallest block size that we
					 * allow and which works with
					 * cartridges > 2^32 bytes
					 */
#endif
			valid_format= 0;
		}
	}
	TRACE_EXIT valid_format;
}

/*   check for a zftape volume
 */
static int get_type_and_block_size(zftape_info_t *zftape,
				   __u8 *entry, zft_volinfo *volume)
{
	TRACE_FUN(ft_t_flow);

	if(memcmp(&entry[VTBL_EXT+EXT_ZFT_SIG], ZFT_SIG, ZFT_SIGLEN) == 0) {
		TRACE(ft_t_noise, "got new style (v1) zftape vtbl entry");
		volume->blk_sz  = GET2(entry, VTBL_EXT+EXT_ZFT_BLKSZ);
		volume->qic113  = entry[VTBL_EXT+EXT_ZFT_QIC113];
		volume->version = 1;
		TRACE_EXIT 1;
	} else if (zftape->ftape->format_code == fmt_big &&
		   memcmp(&entry[VTBL_6_EXT+EXT_6_ZFT_SIG],
			  ZFT_SIG2, ZFT_SIGLEN2) == 0) {
		/* post v2 extension is required to be downwards compatible */
		volume->version = entry[VTBL_6_EXT+EXT_6_ZFT_SIGV];

		TRACE(ft_t_noise,
		      "got new style (v%d) zftape vtbl entry",
		      volume->version);

		volume->blk_sz  = GET2(entry, VTBL_6_EXT+EXT_6_ZFT_BLKSZ);
		volume->qic113  = 1;
		TRACE_EXIT 1;
	} else {
		volume->version = 0;
		TRACE_EXIT check_volume_label(zftape,
					      &entry[VTBL_DESC],
					      &volume->blk_sz);
	}
}


/* create zftape specific vtbl entry, the volume bounds are inserted
 * in the calling function, zft_create_volume_headers()
 */
static void create_zft_volume(zftape_info_t *zftape,
			      __u8 *entry, zft_volinfo *vtbl)
{
	int n;
	TRACE_FUN(ft_t_flow);

	memset(entry, 0, VTBL_SIZE);
	memcpy(&entry[VTBL_SIG], VTBL_ID, 4);
	n = sprintf(&entry[VTBL_DESC], ZFT_VOL_NAME" %03d", vtbl->count);
	/* This must be ' ' padded */
	memset(&entry[VTBL_DESC] + n, ' ', VTBL_DESC_LEN - n);
	entry[VTBL_FLAGS] = (VTBL_FL_NOT_VERIFIED | VTBL_FL_SEG_SPANNING);
	entry[VTBL_M_NO] = 1; /* multi_cartridge_count */
	if (zftape->ftape->format_code == fmt_big) {
		memcpy(&entry[VTBL_6_EXT+EXT_6_ZFT_SIG],ZFT_SIG2, ZFT_SIGLEN2);
		entry[VTBL_6_EXT+EXT_6_ZFT_SIGV] = 2;
		PUT2(entry, VTBL_6_EXT+EXT_6_ZFT_BLKSZ, vtbl->blk_sz);
	} else {
		memcpy(&entry[VTBL_EXT+EXT_ZFT_SIG], ZFT_SIG, ZFT_SIGLEN);
		PUT2(entry, VTBL_EXT+EXT_ZFT_BLKSZ, vtbl->blk_sz);
	}
	if (zftape->qic113) {
		PUT8(entry, VTBL_DATA_SIZE, vtbl->size);
		entry[VTBL_CMPR] = VTBL_CMPR_UNREG; 
		if (vtbl->use_compression) { /* use compression: */
			entry[VTBL_CMPR] |= VTBL_CMPR_USED;
		}
		if (zftape->ftape->format_code != fmt_big) {
			entry[VTBL_EXT+EXT_ZFT_QIC113] = 1;
		}
	} else {
		PUT4(entry, VTBL_DATA_SIZE, vtbl->size);
		entry[VTBL_K_CMPR] = VTBL_CMPR_UNREG; 
		if (vtbl->use_compression) { /* use compression: */
			entry[VTBL_K_CMPR] |= VTBL_CMPR_USED;
		}
	}
	if (zftape->ftape->format_code == fmt_big) {
		/* QIC-113, Rev. G */
		PUT2(entry, VTBL_6_MAJOR, 113);
		PUT2(entry, VTBL_6_MINOR, 7);
		PUT4(entry, VTBL_6_SEGS, vtbl->end_seg-vtbl->start_seg + 1);
		PUT4(entry, VTBL_6_START, vtbl->start_seg);
		PUT4(entry, VTBL_6_END, vtbl->end_seg);
	} else {
		/* normal, QIC-80MC like vtbl 
		 */
		PUT2(entry, VTBL_START, vtbl->start_seg);
		PUT2(entry, VTBL_END, vtbl->end_seg);
	}
	TRACE_EXIT;
}

/* this one creates the volume headers for each volume. It is assumed
 * that buffer already contains the old volume-table, so that vtbl
 * entries without the zft_volume flag set can savely be ignored.
 */
void zft_create_volume_headers(zftape_info_t *zftape, __u8 *buffer)
{   
	__u8 *entry;
	struct list_head *tmp;
	zft_volinfo *vtbl;
	TRACE_FUN(ft_t_flow);
	
#ifdef ZFT_CMAP_HACK
	if((memcmp(&buffer[VTBL_EXT+EXT_ZFT_SIG], ZFT_SIG, ZFT_SIGLEN) == 0)
	   && 
	   buffer[VTBL_EXT+EXT_ZFT_CMAP] != 0) {
		TRACE(ft_t_noise, "deleting cmap volume");
		memmove(buffer, buffer + VTBL_SIZE,
			FT_SEGMENT_SIZE - VTBL_SIZE);
	}
#endif
	entry = buffer;
	for (tmp = zft_head_vtbl->node.next;
	     tmp != &zft_eom_vtbl->node;
	     tmp = tmp->next) {
		vtbl = list_entry(tmp, zft_volinfo, node);

#if 1
		if (vtbl->offset >= FT_SEGMENT_SIZE) {
			TRACE(ft_t_bug, "BUG: Offset %d too large",
			      vtbl->offset);
			vtbl->offset = FT_SEGMENT_SIZE - VTBL_SIZE;
		}
#endif
		entry = &buffer[vtbl->offset];

		/* fill in the values only for new volumes */
		if (vtbl->new_volume) {
			create_zft_volume(zftape, entry, vtbl);
			vtbl->new_volume = 0; /* clear the flag */
		}
		
		DUMP_VOLINFO(ft_t_noise, &entry[VTBL_DESC], vtbl);
	}
	entry += VTBL_SIZE;
	memset(entry, 0, FT_SEGMENT_SIZE - zft_eom_vtbl->count * VTBL_SIZE);
	TRACE_EXIT;
}

/*  write volume table to tape. Calls zft_create_volume_headers()
 */
int zft_update_volume_table(zftape_info_t *zftape, unsigned int segment)
{
	TRACE_FUN(ft_t_flow);

	zft_create_volume_headers(zftape, zftape->vtbl_buf);
	TRACE(ft_t_noise, "writing volume table segment %d", segment);
	TRACE_CATCH(zft_verify_write_segments(zftape, segment,
					      zftape->vtbl_buf,
					      zft_get_seg_sz(zftape, segment)),);
	TRACE_EXIT 0;
}

/* non zftape volumes are handled in raw mode. Thus we need to
 * calculate the raw amount of data contained in those segments.  
 */
static void extract_alien_volume(zftape_info_t *zftape,
				 __u8 *entry, zft_volinfo *vtbl)
{
	TRACE_FUN(ft_t_flow);

	vtbl->rawsize =
		vtbl->size = (zft_calc_tape_pos(zftape, vtbl->end_seg+1)
			      -
			      zft_calc_tape_pos(zftape, vtbl->start_seg));
	vtbl->use_compression = 0;
	vtbl->qic113 = zftape->qic113;
	if (vtbl->qic113) {
		TRACE(ft_t_noise, 
		      "Fake alien volume's size from " LL_X " to " LL_X, 
		      LL(GET8(entry, VTBL_DATA_SIZE)), LL(vtbl->size));
	} else {
		TRACE(ft_t_noise,
		      "Fake alien volume's size from %d to " LL_X, 
		      (int)GET4(entry, VTBL_DATA_SIZE), LL(vtbl->size));
	}
	TRACE_EXIT;
}


/* extract an zftape specific volume
 */
static void extract_zft_volume(zftape_info_t *zftape,
			       __u8 *entry, zft_volinfo *vtbl)
{
	TRACE_FUN(ft_t_flow);

	if (vtbl->qic113) {
		vtbl->size = GET8(entry, VTBL_DATA_SIZE);
		vtbl->use_compression = 
			(entry[VTBL_CMPR] & VTBL_CMPR_USED) != 0; 
	} else {
		vtbl->size = GET4(entry, VTBL_DATA_SIZE);
		if (entry[VTBL_K_CMPR] & VTBL_CMPR_UNREG) {
			vtbl->use_compression = 
				(entry[VTBL_K_CMPR] & VTBL_CMPR_USED) != 0;
		} else if (entry[VTBL_CMPR] & VTBL_CMPR_UNREG) {
			vtbl->use_compression = 
				(entry[VTBL_CMPR] & VTBL_CMPR_USED) != 0; 
		} else {
			TRACE(ft_t_warn, "Geeh! There is something wrong:\n"
			      KERN_INFO "QIC compression (Rev = K): %x\n"
			      KERN_INFO "QIC compression (Rev > K): %x",
			      entry[VTBL_K_CMPR], entry[VTBL_CMPR]);
		}
	}
	vtbl->rawsize = (zft_calc_tape_pos(zftape, vtbl->end_seg+1)
			 -
			 zft_calc_tape_pos(zftape, vtbl->start_seg));
	TRACE_EXIT;
}

static void extract_segment_range(zftape_info_t *zftape,
				  zft_volinfo *vtbl, __u8 *entry)
{
	TRACE_FUN(ft_t_flow);
	
	if (zftape->ftape->format_code == fmt_big) {
		/* Compatibility with a bug in ftape-4.03 and earlier
		 * versions.
		 */
		if (vtbl->zft_volume && vtbl->version == 1) {
			unsigned int num_segments = GET4(entry, VTBL_6_SEGS);
			vtbl->start_seg = zft_eom_vtbl->start_seg;
			vtbl->end_seg   = (vtbl->start_seg + num_segments - 1);
		} else {
			vtbl->start_seg = GET4(entry, VTBL_6_START);
			vtbl->end_seg   = GET4(entry, VTBL_6_END);
		}
	} else {
		/* `normal', QIC-80 like vtbl */
		vtbl->start_seg = GET2(entry, VTBL_START);
		vtbl->end_seg   = GET2(entry, VTBL_END);
	}
	TRACE_EXIT;
}

/* extract the volume table from buffer. "buffer" must already contain
 * the vtbl-segment 
 */
void zft_extract_volume_headers(zftape_info_t *zftape, __u8 *buffer)
{                            
	zft_volinfo *prev;
        __u8 *entry;
	TRACE_FUN(ft_t_flow);
	
	zft_init_vtbl(zftape);
	entry = buffer;
#ifdef ZFT_CMAP_HACK
	if ((strncmp(&entry[VTBL_EXT+EXT_ZFT_SIG], ZFT_SIG,
		     strlen(ZFT_SIG)) == 0) &&
	    entry[VTBL_EXT+EXT_ZFT_CMAP] != 0) {
		TRACE(ft_t_noise, "ignoring cmap volume");
		entry += VTBL_SIZE;
	} 
#endif
	prev = zft_head_vtbl;
	/* the end of the vtbl is indicated by an invalid signature or
	 * the end of the buffer.
	 */
	while ((entry - buffer) < FT_SEGMENT_SIZE) {

		/* force read-only in the presence of the EXVT extension */
		if (memcmp(&entry[VTBL_SIG], "EXVT", 4) == 0) {
			TRACE(ft_t_warn, "EXVT signature found, force r/o");
			zftape->exvt_extension = 1;
			break;
		}
		/* ignore all non-VTBL entries */
		if (memcmp(&entry[VTBL_SIG], "XTBL", 4) == 0) {
			TRACE(ft_t_noise, "Ignoring \"XTBL\" entry");
			entry +=VTBL_SIZE;
			continue;
		}
		if (memcmp(&entry[VTBL_SIG], "UTID", 4) == 0) {
			TRACE(ft_t_noise, "Ignoring \"UTID\" entry");
			entry +=VTBL_SIZE;
			continue;
		}

		/* now must be VTBL, else stop */
		if (memcmp(&entry[VTBL_SIG], "VTBL", 4) != 0) {
			break;
		}

		zft_new_vtbl_entry(zftape);

		/* check if we created this volume and get the blk_sz */
		zft_last_vtbl->zft_volume =
			get_type_and_block_size(zftape,	entry, zft_last_vtbl);

		/* get start and end segment */
		extract_segment_range(zftape, zft_last_vtbl, entry);

		zft_eom_vtbl->start_seg  = zft_last_vtbl->end_seg + 1;

		if (!zft_last_vtbl->zft_volume) {
			extract_alien_volume(zftape, entry, zft_last_vtbl);
		} else {
			extract_zft_volume(zftape, entry, zft_last_vtbl);
		}

		zft_last_vtbl->offset = (unsigned int)(entry - buffer);

		/* sanity check: volumes should be in consecutive order,
		 * and the segments should be in range first_data_segment + 1
		 * to last_data_segment
		 */
		if (prev->end_seg >= zft_last_vtbl->start_seg ||
		    zft_last_vtbl->start_seg <= zft_head_vtbl->end_seg ||
		    zft_last_vtbl->end_seg >= zft_eom_vtbl->end_seg) {
			TRACE(ft_t_warn,
			      "WARNING: broken volume table, forcing r/o");
			zftape->vtbl_broken = 1;
		}
		prev = zft_last_vtbl;

		DUMP_VOLINFO(ft_t_noise, &entry[VTBL_DESC], zft_last_vtbl);
		entry +=VTBL_SIZE;

	}
#if 0
/*
 *  undefine to test end of tape handling
 */
	zft_new_vtbl_entry(zftape);
	zft_last_vtbl->start_seg = zft_eom_vtbl->start_seg;
	zft_last_vtbl->end_seg   = zftape->ftape->last_data_segment - 10;
	zft_last_vtbl->blk_sz          = zftape->blk_sz;
	zft_last_vtbl->zft_volume      = 1;
	zft_last_vtbl->qic113          = zftape->qic113;
	zft_last_vtbl->rawsize = zft_last_vtbl->size =
		(zft_calc_tape_pos(zftape, zft_last_vtbl->end_seg+1)
		 -
		 zft_calc_tape_pos(zftape, zft_last_vtbl->start_seg));
#endif

	TRACE_EXIT;
}

/* this functions translates the failed_sector_log, misused as
 * EOF-marker list, into a virtual volume table. The table mustn't be
 * written to tape, because this would occupy the first data segment,
 * which should be the volume table, but is actualy the first segment
 * that is filled with data (when using standard ftape).  We assume,
 * that we get a non-empty failed_sector_log.
 */
int zft_fake_volume_headers (zftape_info_t *zftape,
			     eof_mark_union *eof_map, int num_failed_sectors)
{
	unsigned int segment, sector;
	int have_eom = 0;
	int vol_no;
	TRACE_FUN(ft_t_flow);

	if ((num_failed_sectors >= 2) &&
	    (GET2(&eof_map[num_failed_sectors - 1].mark.segment, 0) 
	     == 
	     GET2(&eof_map[num_failed_sectors - 2].mark.segment, 0) + 1) &&
	    (GET2(&eof_map[num_failed_sectors - 1].mark.date, 0) == 1)) {
		/* this should be eom. We keep the remainder of the
		 * tape as another volume.
		 */
		have_eom = 1;
	}
	zft_init_vtbl(zftape);
	zft_eom_vtbl->start_seg = zftape->ftape->first_data_segment;
	for(vol_no = 0; vol_no < num_failed_sectors - have_eom; vol_no ++) {
		zft_new_vtbl_entry(zftape);

		segment = GET2(&eof_map[vol_no].mark.segment, 0);
		sector  = GET2(&eof_map[vol_no].mark.date, 0);

		zft_last_vtbl->start_seg  = zft_eom_vtbl->start_seg;
		zft_last_vtbl->end_seg    = segment;
		zft_eom_vtbl->start_seg   = segment + 1;
		zft_last_vtbl->blk_sz     = 1;
		zft_last_vtbl->size       = 
			(zft_calc_tape_pos(zftape, zft_last_vtbl->end_seg)
			 - zft_calc_tape_pos(zftape, zft_last_vtbl->start_seg)
			 + (sector-1) * FT_SECTOR_SIZE);
		zft_last_vtbl->rawsize    =
			(zft_calc_tape_pos(zftape, zft_last_vtbl->end_seg+1)
			 -
			 zft_calc_tape_pos(zftape, zft_last_vtbl->start_seg));
		TRACE(ft_t_noise, 
		      "failed sector log: segment: %d, sector: %d", 
		      segment, sector);
		DUMP_VOLINFO(ft_t_noise, "Faked volume", zft_last_vtbl);
	}
	if (!have_eom) {
		zft_new_vtbl_entry(zftape);
		zft_last_vtbl->start_seg = zft_eom_vtbl->start_seg;
		zft_last_vtbl->end_seg   = zftape->ftape->last_data_segment;
		zft_eom_vtbl->start_seg  = zftape->ftape->last_data_segment + 1;
		zft_last_vtbl->size      = zftape->capacity;
		zft_last_vtbl->size     -=
			zft_calc_tape_pos(zftape, zft_last_vtbl->start_seg);
		zft_last_vtbl->rawsize   = zft_last_vtbl->size;
		zft_last_vtbl->blk_sz    = 1;
		DUMP_VOLINFO(ft_t_noise, "Faked volume",
			     zft_last_vtbl);
	}
	TRACE_EXIT 0;
}

/* update the internal volume table
 *
 * if before start of last volume: erase all following volumes if
 * inside a volume: set end of volume to infinity
 *
 * this function is intended to be called every time _ftape_write() is
 * called
 *
 * return: 0 if no new volume was created, 1 if a new volume was
 * created
 *
 * NOTE: we don't need to check for zft_mode as ftape_write() does
 * that already. This function gets never called without accessing
 * zftape via the *qft* devices 
 */

int zft_open_volume(zftape_info_t *zftape,
		    zft_position *pos, int blk_sz)
{ 
	unsigned int offset;
	TRACE_FUN(ft_t_flow);
	
	if (!zftape->qic_mode) {
		TRACE_EXIT 0;
	}
	if (zft_tape_at_lbot(zftape, pos)) {
		zft_init_vtbl(zftape);
		if(zftape->old_ftape) {
			/* clear old ftape's eof marks */
			zft_clear_ftape_file_marks(zftape);
			zftape->old_ftape = 0; /* no longer old ftape */
		}
		zft_reset_position(zftape, pos);
	}
	if (pos->seg_pos != zft_last_vtbl->end_seg + 1) {
		TRACE_ABORT(-EIO, ft_t_bug, 
		      "BUG: seg_pos: %d, zft_last_vtbl->end_seg: %d", 
		      pos->seg_pos, zft_last_vtbl->end_seg);
	}                            
	TRACE(ft_t_noise, "create new volume");
	if (zft_eom_vtbl->count >= ZFT_MAX_VOLUMES) {
		TRACE_ABORT(-ENOSPC, ft_t_err,
			    "Error: maxmimal number of volumes exhausted "
			    "(maxmimum is %d)", ZFT_MAX_VOLUMES);
	}

	offset = zft_last_vtbl->offset + VTBL_SIZE;
	TRACE(ft_t_noise, "Offset is %d\n", offset);
	if (offset >= FT_SEGMENT_SIZE) {
		TRACE(ft_t_bug, "BUG: offset %d > FT_SEGMENT_SIZE", offset);
		TRACE_EXIT -EIO;
	}
	zft_new_vtbl_entry(zftape);
	pos->volume_pos = pos->seg_byte_pos = 0;
	zft_last_vtbl->start_seg       = pos->seg_pos;
	zft_last_vtbl->end_seg         = zftape->ftape->last_data_segment; /* infinity */
	zft_last_vtbl->blk_sz          = blk_sz;
	zft_last_vtbl->size            = zftape->capacity;
	zft_last_vtbl->zft_volume      = 1;
	zft_last_vtbl->use_compression = 0;
	zft_last_vtbl->qic113          = zftape->qic113;
	zft_last_vtbl->offset          = offset;
	zft_last_vtbl->version         = zftape->qic113 ? 2 : 0; /* unused */
	zft_last_vtbl->new_volume      = 1;
	zft_last_vtbl->open            = 1;
	zftape->volume_table_changed   = 1;
	zft_eom_vtbl->start_seg        = zftape->ftape->last_data_segment + 1;
	TRACE_EXIT 0;
}

/*  perform mtfsf, mtbsf, not allowed without zftape->qic_mode
 */
int zft_skip_volumes(zftape_info_t *zftape,
		     int count, zft_position *pos)
{ 
	const zft_volinfo *vtbl;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "count: %d", count);
	
	vtbl= zft_volume_by_segpos(zftape, pos->seg_pos);
	while (count > 0 && vtbl != zft_eom_vtbl) {
		vtbl = list_entry(vtbl->node.next, zft_volinfo, node);
		count --;
	}
	while (count < 0 && vtbl != zft_first_vtbl) {
		vtbl = list_entry(vtbl->node.prev, zft_volinfo, node);
		count ++;
	}
	pos->seg_pos        = vtbl->start_seg;
	pos->seg_byte_pos   = 0;
	pos->volume_pos     = 0;
	pos->tape_pos       = zft_calc_tape_pos(zftape, pos->seg_pos);
	zftape->just_before_eof = zft_volume_size(zftape, vtbl) == 0;
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	if (zft_cmpr_ops) {
		(*zft_cmpr_ops->reset)(zftape->cmpr_handle);
	}
#endif
	/* no need to keep cache */
	zft_release_deblock_buffer(zftape, &zftape->deblock_buf);
	TRACE(ft_t_noise, "repositioning to:\n"
	      KERN_INFO "zftape->seg_pos     : %d\n"
	      KERN_INFO "zftape->seg_byte_pos: %d\n"
	      KERN_INFO "zftape->tape_pos    : " LL_X "\n"
	      KERN_INFO "zftape->volume_pos  : " LL_X "\n"
	      KERN_INFO "file number         : %d",
	      pos->seg_pos, pos->seg_byte_pos, 
	      LL(pos->tape_pos), LL(pos->volume_pos), vtbl->count);
	zftape->resid = count < 0 ? -count : count;
	TRACE_EXIT zftape->resid ? -EINVAL : 0;
}

/* the following simply returns the raw data position of the EOM
 * marker, MTIOCSIZE ioctl 
 */
__s64 zft_get_eom_pos(zftape_info_t *zftape)
{
	if (zftape->qic_mode) {
		return zft_calc_tape_pos(zftape,
					 zft_eom_vtbl->start_seg);
	} else {
		/* there is only one volume in raw mode */
		return zftape->capacity;
	}
}

/* skip to eom, used for MTEOM
 */
void zft_skip_to_eom(zftape_info_t *zftape, zft_position *pos)
{
	TRACE_FUN(ft_t_flow);
	pos->seg_pos      = zft_eom_vtbl->start_seg;
	pos->seg_byte_pos = 
		pos->volume_pos     = 
		zftape->just_before_eof = 0;
	pos->tape_pos = zft_calc_tape_pos(zftape, pos->seg_pos);
	TRACE(ft_t_noise, "ftape positioned to segment %d, data pos " LL_X, 
	      pos->seg_pos, LL(pos->tape_pos));
	TRACE_EXIT;
}

/*  write an EOF-marker by setting zft_last_vtbl->end_seg to seg_pos.
 *  NOTE: this function assumes that zft_last_vtbl points to a valid
 *  vtbl entry
 *
 *  NOTE: this routine always positions before the EOF marker
 */
int zft_close_volume(zftape_info_t *zftape, zft_position *pos)
{
	TRACE_FUN(ft_t_any);

	if (zft_vtbl_empty || !zft_last_vtbl->open) { /* should not happen */
		TRACE(ft_t_noise, "There are no volumes to finish");
		TRACE_EXIT -EIO;
	}
	if (pos->seg_byte_pos == 0 && 
	    pos->seg_pos != zft_last_vtbl->start_seg) {
		pos->seg_pos --;
		pos->seg_byte_pos = zft_get_seg_sz(zftape, pos->seg_pos);
	}
	zft_last_vtbl->end_seg       = pos->seg_pos;
	zft_last_vtbl->size          = pos->volume_pos;
	zft_last_vtbl->rawsize       =
		(zft_calc_tape_pos(zftape, zft_last_vtbl->end_seg+1)
		 -
		 zft_calc_tape_pos(zftape, zft_last_vtbl->start_seg));
	zft_eom_vtbl->start_seg      = zft_last_vtbl->end_seg + 1;
	zft_last_vtbl->open          = 0; /* closed */
	zftape->volume_table_changed = 1;
	zftape->just_before_eof      = 1;
	TRACE_EXIT 0;
}

/* write count file-marks at current position. 
 *
 *  The tape is positioned after the eof-marker, that is at byte 0 of
 *  the segment following the eof-marker
 *
 *  this function is only allowed in zftape->qic_mode
 *
 *  Only allowed when tape is at BOT or EOD.
 */
int zft_weof(zftape_info_t *zftape,
	     unsigned int count, zft_position *pos)
{
	unsigned int offset;
	TRACE_FUN(ft_t_flow);

	if (!count) { /* write zero EOF marks should be a real no-op */
		TRACE_EXIT 0;
	}
	zftape->volume_table_changed = 1;
	if (zft_tape_at_lbot(zftape, pos)) {
		zft_init_vtbl(zftape);
		if(zftape->old_ftape) {
			/* clear old ftape's eof marks */
			zft_clear_ftape_file_marks(zftape);
			zftape->old_ftape = 0;    /* no longer old ftape */
		}
	}
	if (zft_last_vtbl->open) {
		zft_close_volume(zftape, pos);
		zft_move_past_eof(zftape, pos);
		count --;
	}
	/* now it's easy, just append eof-marks, that is empty
	 * volumes, to the end of the already recorded media.
	 */
	while (count > 0 && 
	       pos->seg_pos <= zftape->ftape->last_data_segment && 
	       zft_eom_vtbl->count < ZFT_MAX_VOLUMES) {
		TRACE(ft_t_noise,
		      "Writing zero sized file at segment %d", pos->seg_pos);
		offset = zft_last_vtbl->offset + VTBL_SIZE;
		TRACE(ft_t_noise, "Offset is %d\n", offset);
		if (offset >= FT_SEGMENT_SIZE) {
			TRACE(ft_t_bug,
			      "BUG: offset %d >= FT_SEGMENT_SIZE", offset);
			TRACE_EXIT -EIO;
		}
		zft_new_vtbl_entry(zftape);
		zft_last_vtbl->start_seg       = pos->seg_pos;
		zft_last_vtbl->end_seg         = pos->seg_pos;
		zft_last_vtbl->size            = 0;
		zft_last_vtbl->blk_sz          = zftape->blk_sz;
		zft_last_vtbl->zft_volume      = 1;
		zft_last_vtbl->use_compression = 0;
		zft_last_vtbl->qic113          = zftape->qic113;
		zft_last_vtbl->offset          = offset;
		zft_last_vtbl->version         = zftape->qic113 ? 2 : 0;
		zft_last_vtbl->new_volume      = 1;
		pos->tape_pos += zft_get_seg_sz(zftape, pos->seg_pos);
		zft_eom_vtbl->start_seg = ++ pos->seg_pos;
		count --;
	} 
	if (count > 0) {
		/*  there are two possibilities: end of tape, or the
		 *  maximum number of files is exhausted.
		 */
		zftape->resid = count;
		TRACE(ft_t_noise,"Number of marks NOT written: %d", zftape->resid);
		if (zft_eom_vtbl->count == ZFT_MAX_VOLUMES) {
			TRACE(ft_t_warn,
			      "maximum allowed number of files exhausted: %d",
			      ZFT_MAX_VOLUMES);
			TRACE_EXIT -EINVAL;
		} else {
			TRACE(ft_t_noise, "reached end of tape");
			TRACE_EXIT -ENOSPC;
		}
	}
	TRACE_EXIT 0;
}


/* return the block address of the first block of the volume
 */
__s64 zft_blk_offs(const struct zftape_info *zftape,
		   const zft_volinfo *vtbl)
{
	zft_volinfo *pos;
	struct list_head *tmp;
	__s64 offset;
	TRACE_FUN(ft_t_flow);

	if (!zftape->qic_mode) {
		if (vtbl == &zftape->tape_vtbl) {
			TRACE_EXIT 0;
		} else if (vtbl == &zftape->eot_vtbl) {
			TRACE_EXIT zft_div_blksz(zftape->capacity,
						 zftape->blk_sz);
		} else {
			TRACE_EXIT -EINVAL;
		}
	}
	for (offset = 0, tmp = zft_head_vtbl->node.next;
	     tmp != &vtbl->node && tmp != &zft_eom_vtbl->node;
	     tmp = tmp->next) {
	        pos = list_entry(tmp, zft_volinfo, node);
	
		offset += zft_div_blksz(zft_volume_size(zftape, pos),
					zft_block_size(zftape, pos));
		offset ++; /* one for the file mark */
	}
	TRACE_EXIT offset;
}

const zft_volinfo *zft_volume_by_blkpos(zftape_info_t *zftape,
					__s64 blk_pos,
					__s64 *blk_offs)
{
	zft_volinfo *vtbl;
	struct list_head *tmp;
	__s64 offset, vol_blocks;
	TRACE_FUN(ft_t_noise);
	
	TRACE(ft_t_noise, "called with blk_pos " LL_X "\n", LL(blk_pos));
	if (!zftape->qic_mode) {
		blk_pos *= zftape->blk_sz;
		if (blk_pos > zftape->capacity) {
			zftape->eot_vtbl.blk_sz =  zftape->blk_sz;
			TRACE_EXIT &zftape->eot_vtbl;
		}
		zftape->tape_vtbl.blk_sz =  zftape->blk_sz;
		TRACE_EXIT &zftape->tape_vtbl;
	}
	if (blk_pos <= 0) {
		TRACE_EXIT zft_first_vtbl;
	}	
	offset     = 0;
	vol_blocks = -1;
	tmp = &zft_head_vtbl->node;
	do {
		tmp        = tmp->next;
		offset    += vol_blocks + 1;
	        vtbl       = list_entry(tmp, zft_volinfo, node);
		vol_blocks = zft_div_blksz(zft_volume_size(zftape, vtbl),
					   zft_block_size(zftape, vtbl));
	} while ((offset + vol_blocks) < blk_pos &&
		 tmp != &zft_eom_vtbl->node);

	if (blk_offs) {
		*blk_offs = offset;
		TRACE(ft_t_noise, "offset: " LL_X, LL(offset));
	}
	DUMP_VOLINFO(ft_t_noise, "", vtbl);
	TRACE_EXIT vtbl;
}

const zft_volinfo *zft_volume_by_segpos(zftape_info_t *zftape,
					unsigned int seg_pos)
{
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_any, "called with seg_pos %d", seg_pos);
	if (!zftape->qic_mode) {
		if (seg_pos > zftape->ftape->last_data_segment) {
			TRACE_EXIT &zftape->eot_vtbl;
		}
		zftape->tape_vtbl.blk_sz =  zftape->blk_sz;
		TRACE_EXIT &zftape->tape_vtbl;
	}
	if (seg_pos < zft_first_vtbl->start_seg) {
		TRACE_EXIT (zftape->cur_vtbl = zft_first_vtbl);
	}
	TRACE(ft_t_noise, "cur_vtbl: %p", zftape->cur_vtbl);
	while (seg_pos > zftape->cur_vtbl->end_seg) {
		zftape->cur_vtbl = list_entry(zftape->cur_vtbl->node.next, zft_volinfo, node);
		TRACE(ft_t_noise, "%d - %d", zftape->cur_vtbl->start_seg, zftape->cur_vtbl->end_seg);
	}
	while (seg_pos < zftape->cur_vtbl->start_seg) {
		zftape->cur_vtbl = list_entry(zftape->cur_vtbl->node.prev, zft_volinfo, node);
		TRACE(ft_t_noise, "%d - %d", zftape->cur_vtbl->start_seg, zftape->cur_vtbl->end_seg);
	}
	if (seg_pos > zftape->cur_vtbl->end_seg || seg_pos < zftape->cur_vtbl->start_seg) {
		TRACE(ft_t_bug, "This cannot happen");
	}
	DUMP_VOLINFO(ft_t_noise, "", zftape->cur_vtbl);
	TRACE_EXIT zftape->cur_vtbl;
}

/* this function really assumes that we are just before eof
 */
void zft_move_past_eof(zftape_info_t *zftape, zft_position *pos)
{ 
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "old seg. pos: %d", pos->seg_pos);
	pos->tape_pos += zft_get_seg_sz(zftape, pos->seg_pos++) - pos->seg_byte_pos;
	pos->seg_byte_pos = 0;
	pos->volume_pos   = 0;
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	if (zft_cmpr_ops) {
		(*zft_cmpr_ops->reset)(zftape->cmpr_handle);
	}
#endif
	zftape->just_before_eof =  0;
	/* no need to cache it anymore */
	zft_release_deblock_buffer(zftape, &zftape->deblock_buf);
	TRACE(ft_t_noise, "new seg. pos: %d", pos->seg_pos);
	TRACE_EXIT;
}
