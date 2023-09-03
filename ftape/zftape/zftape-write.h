#ifndef _ZFTAPE_WRITE_H
#define _ZFTAPE_WRITE_H

/*
 * Copyright (C) 1996-1998 Claus-Justus Heine

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
 * $RCSfile: zftape-write.h,v $
 * $Revision: 1.5 $
 * $Date: 1998/12/18 22:39:05 $
 *
 *      This file contains the definitions for the write functions
 *      for the zftape driver for Linux.
 *
 */

extern void zft_zap_write_buffers(struct zftape_info *zftape);
extern int  zft_flush_buffers(struct zftape_info *zftape);
extern int  zft_update_header_segments(struct zftape_info *zftape);

/*  hook for the VFS interface 
 */
extern int _zft_write(struct zftape_info *zftape,
		      const char *buff, int req_len);
#endif /* _ZFTAPE_WRITE_H */
