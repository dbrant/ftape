#ifndef _FC_10_H
#define _FC_10_H

/*
 * Copyright (C) 1994-1996 Bas Laarhoven.

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
 * $RCSfile: fc-10.h,v $
 * $Revision: 1.5 $
 * $Date: 1998/12/18 22:01:19 $
 *
 *      This file contains definitions for the FC-10 code
 *      of the QIC-40/80 floppy-tape driver for Linux.
 */

/*
 *      fc-10.c defined global vars.
 */

/*
 *      fc-10.c defined global functions.
 */
#include "../lowlevel/fdc-io.h"

extern int fc10_enable(fdc_info_t *info);

#endif
