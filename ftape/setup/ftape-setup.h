#ifndef _FTAPE_SETUP_H
#define _FTAPE_SETUP_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
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
 * $RCSfile: ftape-setup.h,v $
 * $Revision: 1.3 $
 * $Date: 1999/03/28 21:52:15 $
 *
 * This file contains the definitions for the kernel setup routines
 * for ftape
 *
 */

typedef struct ftape_setup_struct {
	const char *name;  /* name of this option */
	int *var;          /* location of configuration variable */
	int min;           /* minimum value */
	int max;           /* maximum value */
	int shared;        /* are we the only one who needs this option */
} ftape_setup_t;

extern int ftape_setup_parse(char *str, int *ints,
				    ftape_setup_t *config_parms);

#endif /* _FTAPE_SETUP_H */
