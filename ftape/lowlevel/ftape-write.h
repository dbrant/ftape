#ifndef _FTAPE_WRITE_H
#define _FTAPE_WRITE_H

/*
 * Copyright (C) 1994-1995 Bas Laarhoven,
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
 $RCSfile: ftape-write.h,v $
 $Author: cvs $
 *
 $Revision: 1.8 $
 $Date: 1998/12/18 22:02:17 $
 $State: Exp $
 *
 *      This file contains the definitions for the write functions
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */


/*      ftape-write.c defined global functions.
 */
typedef enum {
	FT_WR_ASYNC  = 0, /* start tape only when all buffers are full */
	FT_WR_MULTI  = 1, /* start tape, but don't necessarily stop */
	FT_WR_SINGLE = 2, /* write a single segment and stop afterwards */
	FT_WR_DELETE = 3  /* write deleted data marks */
} ft_write_mode_t;

struct ftape_info; /* forward */

extern int  ftape_start_writing(struct ftape_info *ftape,
				const ft_write_mode_t mode);
extern int  ftape_write_segment(struct ftape_info *ftape,
				int segment,
				__u8 **address, 
				ft_write_mode_t flushing);
extern void ftape_zap_write_buffers(struct ftape_info *ftape);
extern int  ftape_loop_until_writes_done(struct ftape_info *ftape);
extern int  ftape_hard_error_recovery(struct ftape_info *ftape, void *scratch);

#endif				/* _FTAPE_WRITE_H */

