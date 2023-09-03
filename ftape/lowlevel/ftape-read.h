#ifndef _FTAPE_READ_H
#define _FTAPE_READ_H

/*
 * Copyright (C) 1994-1996 Bas Laarhoven,
 *           (C) 1996-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-read.h,v $
 * $Revision: 1.10 $
 * $Date: 1998/12/18 22:02:17 $
 *
 *      This file contains the definitions for the read functions
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */

#include "../lowlevel/fdc-io.h"

/*      ftape-read.c defined global functions.
 */
typedef enum {
	FT_RD_SINGLE = 0,
	FT_RD_AHEAD  = 1,
} ft_read_mode_t;

extern int ftape_read_header_segment(struct ftape_info *ftape,
				     __u8 *address);
extern int ftape_decode_header_segment(struct ftape_info *ftape,
				       __u8 *address);
extern int ftape_ecc_correct(struct ftape_info *ftape, buffer_struct *buff);
extern int ftape_read_segment(struct ftape_info *ftape,
			      int segment,
			      __u8  **address, 
			      const ft_read_mode_t read_mode);

extern void ftape_zap_read_buffers(struct ftape_info *ftape);

#endif				/* _FTAPE_READ_H */
