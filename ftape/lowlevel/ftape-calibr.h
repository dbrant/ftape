#ifndef _FTAPE_CALIBR_H
#define _FTAPE_CALIBR_H

/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C)      1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-calibr.h,v $
 * $Revision: 1.5 $
 * $Date: 1998/12/18 22:02:16 $
 *
 *      This file contains a gp calibration routine for
 *      hardware dependent timeout functions.
 */

struct ftape_info; /* forward */

extern void ftape_calibrate(struct ftape_info *ftape,
			    char *name,
			    void (*fun) (void *, unsigned int),
			    void *data,
			    unsigned int *calibr_count,
			    unsigned int *calibr_time);

#endif /* _FTAPE_CALIBR_H */
