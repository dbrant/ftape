#ifndef _ZFTAPE_RW_H
#define _ZFTAPE_RW_H

/*
 * Copyright (C) 1996-1998 Claus-Justus Heine.

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
 * $RCSfile: zftape-rw.h,v $
 * $Revision: 1.11 $
 * $Date: 1999/06/02 00:07:00 $
 *
 *      This file contains the definitions for the read and write
 *      functions for the QIC-117 floppy-tape driver for Linux.
 *
 */

#include "../zftape/zftape-buffers.h"

#define SEGMENTS_PER_TAPE  (ft_segments_per_track * ft_tracks_per_tape)

/*  QIC-113 Rev. G says that `a maximum of 63488 raw bytes may be
 *  compressed into a single frame'.
 *  Maybe we should stick to 32kb to make it more `beautiful'
 */
#define ZFT_MAX_BLK_SZ           (62*1024) /* bytes */
#if !defined(CONFIG_ZFT_DFLT_BLK_SZ)
# define CONFIG_ZFT_DFLT_BLK_SZ   (10*1024) /* bytes, default of gnu tar */
#elif CONFIG_ZFT_DFLT_BLK_SZ == 0
# undef  CONFIG_ZFT_DFLT_BLK_SZ
# define CONFIG_ZFT_DFLT_BLK_SZ 1
#elif CONFIG_ZFT_DFLT_BLK_SZ != 1 && (CONFIG_ZFT_DFLT_BLK_SZ % 1024) != 0
# error CONFIG_ZFT_DFLT_BLK_SZ must be 1 or a multiple of 1024
#endif

typedef enum
{ 
	zft_idle = 0,
	zft_reading,
	zft_writing,
} zft_status_enum;

typedef struct               /*  all values measured in bytes */
{
	int   seg_pos;       /*  segment currently positioned at */
	int   seg_byte_pos;  /*  offset in current segment */ 
	__s64 tape_pos;      /*  real offset from BOT */
	__s64 volume_pos;    /*  pos. in  data stream in
			      *  current volume 
			      */
} zft_position; 

struct zftape_info; /* forward */

/*  zftape-rw.c exported functions
 */
extern unsigned int zft_get_seg_sz(struct zftape_info *zftape,
				   unsigned int segment);
extern int   zft_set_flags(struct zftape_info *zftape,
			   unsigned int minor_unit);
extern int   zft_calc_seg_byte_coord(struct zftape_info *zftape,
				     int *seg_byte_pos, __s64 tape_pos);
extern __s64 zft_calc_tape_pos(struct zftape_info *zftape, int segment);
extern __s64 zft_get_capacity(struct zftape_info *zftape);
extern void  zft_update_label(struct zftape_info *zftape, __u8 *buffer);
extern int   zft_erase(struct zftape_info *zftape);
extern int   zft_verify_write_segments(struct zftape_info *zftape,
				       unsigned int segment, 
				       __u8 *data, size_t size);
extern void  zft_release_deblock_buffer(struct zftape_info *zftape,
					__u8 **buffer);

extern unsigned int zft_get_time(void);
#endif /* _ZFTAPE_RW_H */

