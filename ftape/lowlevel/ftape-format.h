#ifndef _FTAPE_FORMAT_H
#define _FTAPE_FORMAT_H

/*
 * Copyright (C) 1996-1997 Claus-Justus Heine.

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
 * $RCSfile: ftape-format.h,v $
 * $Revision: 1.7 $
 * $Date: 1999/02/26 13:04:48 $
 *
 *      This file contains the low level definitions for the
 *      formatting support for the QIC-40/80/3010/3020 floppy-tape
 *      driver "ftape" for Linux.
 */

typedef struct {
	__u8 cyl; __u8 head; __u8 sect; __u8 size;
} __attribute__((packed)) ft_format_sector;

typedef struct {
	ft_format_sector sectors[FT_SECTORS_PER_SEGMENT];
} __attribute__((packed)) ft_format_segment;

#define FT_MFS 128 /* max floppy sector */

#if defined(TESTING)
#define FT_FMT_SEGS_PER_BUF 50
#else
#define FT_FMT_SEGS_PER_BUF (FT_BUFF_SIZE/(4*FT_SECTORS_PER_SEGMENT))
#endif

#ifdef __KERNEL__
extern int ftape_format_track(ftape_info_t *ftape, const unsigned int track);
extern int ftape_format_status(ftape_info_t *ftape, unsigned int *segment_id);
extern int ftape_verify_segment(ftape_info_t *ftape, unsigned int segment_id,
				SectorMap *bsm);
#endif /* __KERNEL__ */

#endif
