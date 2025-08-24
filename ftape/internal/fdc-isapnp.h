#ifndef _FDC_ISAPNP_H
#define _FDC_ISAPNP_H

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
 * $RCSfile: fdc-isapnp.h,v $
 * $Revision: 1.3 $
 * $Date: 2000/06/30 10:52:55 $
 *
 *     Prototypes for ISA PnP fdc controller support
 */

/* #include <linux/config.h> - not needed in modern kernels */
#include <linux/ftape.h>

#include "../lowlevel/fdc-io.h"

#ifdef MODULE
void fdc_int_isapnp_disable(void);
#else
int fdc_int_isapnp_setup(char *str);
#endif
int fdc_int_isapnp_init(fdc_info_t *fdc);

#endif
