#ifndef _ZFTAPE_INLINE_H
#define _ZFTAPE_INLINE_H

/*
 *      Copyright (c) 1995-1998 Claus-Justus Heine

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
 * $RCSfile: zftape-inline.h,v $
 * $Revision: 1.11 $
 * $Date: 2000/07/20 11:17:16 $
 *
 *   This file contains some inline functions that would hose up
 *   the dependencies between header file pretty badly 
 */

/*
 * The following ordinarily would belong to zftape-vtbl.h
 */
#include "../zftape/zftape-init.h"
#include "../zftape/zftape-vtbl.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-rw.h"

extern inline int   zft_tape_at_eod    (struct zftape_info *zftape,
					const zft_position *pos);
extern inline int   zft_tape_at_lbot   (struct zftape_info *zftape,
					const zft_position *pos);
extern inline int   zft_vtbl_soft_ro(const struct zftape_info *zftape);
extern inline void  zft_position_before_eof(struct zftape_info *zftape,
					    zft_position *pos, 
					    const zft_volinfo *volume);
extern inline __s64 zft_check_for_eof(const zft_volinfo *vtbl,
				      const zft_position *pos);
extern inline __s64 zft_volume_size(const struct zftape_info *zftape,
				    const zft_volinfo *vtbl);
extern inline __s64 zft_block_size(const struct zftape_info *zftape,
				   const zft_volinfo *vtbl);

/* check whether we can alter the vtbl */
extern inline int zft_vtbl_soft_ro(const struct zftape_info *zftape)
{
	return (zftape->old_ftape |
		zftape->exvt_extension |
		zftape->vtbl_broken) != 0;
}

/* this function decrements the zft_seg_pos counter if we are right
 * at the beginning of a segment. This is to handel fsfm/bsfm -- we
 * need to position before the eof mark.  NOTE: zft_tape_pos is not
 * changed 
 */
extern inline void zft_position_before_eof(struct zftape_info *zftape,
					   zft_position *pos, 
					   const zft_volinfo *volume)
{ 
	if (pos->seg_pos == volume->end_seg + 1 &&  pos->seg_byte_pos == 0) {
		pos->seg_pos --;
		pos->seg_byte_pos = zft_get_seg_sz(zftape, pos->seg_pos);
	}
}

/*  Mmmh. Is the position at the end of the last volume, that is right
 *  before the last EOF mark also logical an EOD condition?
 */
extern inline int zft_tape_at_eod(struct zftape_info *zftape, const zft_position *pos)
{ 
	if (zftape->qic_mode) {
		return (pos->seg_pos >= zft_eom_vtbl->start_seg ||
			zft_last_vtbl->open);
	} else {
		return pos->seg_pos > zftape->ftape->last_data_segment;
	}
}

extern inline int zft_tape_at_lbot(struct zftape_info *zftape, const zft_position *pos)
{
	if (!zftape->header_read || !zftape->vtbl_read) {
		return 1; /* always at bot if new cartridge */
	} else if (zftape->qic_mode) {
		return (pos->seg_pos <= zft_first_vtbl->start_seg &&
			pos->volume_pos == 0);
	} else {
		return (pos->seg_pos <= zftape->ftape->first_data_segment && 
			pos->volume_pos == 0);
	}
}

/* This one checks for EOF.  return remaing space (may be negative) 
 */
extern inline __s64 zft_check_for_eof(const zft_volinfo *vtbl,
				      const zft_position *pos)
{     
	return (__s64)(vtbl->size - pos->volume_pos);
}

/* return the size of the volume, compute the raw consumed size in
 * case compression has been used, and we are reading the volume
 * uncompressed.
 */
extern inline __s64 zft_volume_size(const struct zftape_info *zftape,
				    const zft_volinfo *vtbl)
{
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	if (!vtbl->use_compression || zftape->use_compression) {
		return vtbl->size;
	}
#else
	if (!vtbl->use_compression) {
		return vtbl->size;
	}
#endif
	else {
		return vtbl->rawsize;
	}
}

/* return the block size of the volume. Return 1kb when reading a compressed
 * volume without compression.
 */
extern inline __s64 zft_block_size(const struct zftape_info *zftape,
				   const zft_volinfo *vtbl)
{
#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	if (vtbl->use_compression && !zftape->use_compression) {
		return 1024;
	}
#else
	if (vtbl->use_compression) {
		return 1024;
	}
#endif
	else {
		return vtbl->blk_sz;
	}
}

/* the next tiny little function belongs to zftape-write.c
 */
extern inline void zft_prevent_flush(struct zftape_info *zftape);

extern inline void zft_prevent_flush(struct zftape_info *zftape)
{
	zftape->need_flush = 0;
}


#endif
