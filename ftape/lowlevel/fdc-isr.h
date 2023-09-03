#ifndef _FDC_ISR_H
#define _FDC_ISR_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

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
 * $RCSfile: fdc-isr.h,v $
 * $Revision: 1.8 $
 * $Date: 1998/12/18 22:02:16 $
 *
 *      This file declares the global variables necessary to
 *      synchronize the interrupt service routine (isr) with the
 *      remainder of the QIC-40/80/3010/3020 floppy-tape driver
 *      "ftape" for Linux.
 */

#include "../lowlevel/fdc-io.h"

/*
 *      fdc-isr.c defined public variables
 */

/* rien */

/*
 *      fdc-io.c defined public functions
 */
extern void fdc_isr(fdc_info_t *fdc);

#endif
