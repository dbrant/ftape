/*
 *      Copyright (C) 1999 Claus-Justus Heine.

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
 * $RCSfile: fdc-parport.c,v $
 * $Revision: 1.4 $
 * $Date: 2000/06/29 17:26:45 $
 *
 *      This file contains the general parport interface code.
 *
 */

#include <linux/config.h>
#include <linux/ftape.h>

#ifdef CONFIG_FT_TRAKKER
extern int trakker_setup(char *str) __init;
extern int trakker_register(void) __init;
#endif
#ifdef CONFIG_FT_BPCK
extern int bpck_fdc_setup(char *str) __init;
extern int bpck_fdc_register(void) __init;
#endif

void __init ftape_parport_init(void)
{
#ifdef CONFIG_FT_TRAKKER
	(void)trakker_register();
#endif
#ifdef CONFIG_FT_BPCK
	(void)bpck_fdc_register();
#endif
}

void __init ftape_parport_setup(char *str)
{
#ifdef CONFIG_FT_TRAKKER
	if (trakker_setup(str) <= 0) { /* error or no need to proceed */
		return;
	}
#endif
#ifdef CONFIG_FT_BPCK
	if (bpck_fdc_setup(str) <= 0) { /* error or no need to proceed */
		return;
	}
#endif
}
