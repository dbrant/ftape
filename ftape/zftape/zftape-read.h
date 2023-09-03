#ifndef _ZFTAPE_READ_H
#define _ZFTAPE_READ_H

/*
 * Copyright (C) 1996, 1997 Claus-Justus Heine

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
 * $RCSfile: zftape-read.h,v $
 * $Revision: 1.7 $
 * $Date: 1998/12/18 22:39:05 $
 *
 *      This file contains the definitions for the read functions
 *      for the zftape driver for Linux.
 *
 */

#include "../lowlevel/ftape-read.h"

struct zftape_info; /* forward */

/*      ftape-read.c defined global vars.
 */
extern int zft_just_before_eof;
	
/*      ftape-read.c defined global functions.
 */
extern void zft_zap_read_buffers(struct zftape_info *zftape);
extern int  zft_read_header_segments(struct zftape_info *zftape);
extern int  zft_read_volume_table(struct zftape_info *zftape);
extern int  zft_fetch_segment(struct zftape_info *zftape,
			      unsigned int segment,
			      __u8 **buffer,
			      ft_read_mode_t read_mode);

/*   hook for the VFS interface
 */
extern int  _zft_read(struct zftape_info *zftape,
		      char* buff, int req_len);

#endif /* _ZFTAPE_READ_H */
