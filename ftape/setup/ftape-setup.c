/* 
 * Copyright (C) 1996-2000 Claus-Justus Heine.

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
 * $RCSfile: ftape-setup.c,v $
 * $Revision: 1.14 $
 * $Date: 2000/06/30 10:54:05 $
 *
 *      This file contains the code for processing the kernel command
 *      line options for the QIC-40/80/3010/3020 floppy-tape driver
 *      "ftape" for Linux.
 */


#include <linux/version.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ctype.h>

#include <linux/ftape.h>
#include "../lowlevel/ftape-tracing.h"

#include "ftape-setup.h"


int ftape_setup(char *str);


/* the setup function are required to return 0 if they have handled an
 * option which doesn't need further processing, 1 if they didn't
 * handle the option and -EINVAL when the option doesn't make any
 * sense and it is clear that the other modules can't make any sense
 * of it either.
 */
extern int ftape_lowlevel_setup(char *str);
#ifdef CONFIG_FT_INTERNAL
extern int ftape_internal_setup(char *str);
#endif
#ifdef CONFIG_FT_PARPORT
/* ftape_parport_setup is in separate module - skip for now */
/* extern int ftape_parport_setup(char *str); */
#endif

/* do some option parsing here. The kernel get_options() function
 * doesn't allow negative numbers, we use the keywords "a" and "n" for
 * "auto" and "none" and convert it to "-1" and "-2".
 *
 * If ints[0] is non-zero, then simply do nothing as the kernel
 * already did the right thing
 */

int ftape_setup(char *str)
{
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Called with %s", str);

	/* Temporarily disabled to resolve symbol issues */
	/*
	if (ftape_lowlevel_setup(str) <= 0) {
		// error or no need to proceed
		TRACE_EXIT 1;
	}
	*/
#ifdef CONFIG_FT_INTERNAL
	if (ftape_internal_setup(str) <= 0) {
		TRACE_EXIT 1;
	}
#endif
#ifdef CONFIG_FT_PARPORT
	/* ftape_parport_setup is in separate module - skip for now */
	/* ftape_parport_setup(str); */
#endif
	TRACE_EXIT 1;
}

int ftape_setup_parse(char *str, int *ints,
			     ftape_setup_t *config_params)
{
	int i, j;
	TRACE_FUN(ft_t_flow);

	if (str == NULL) {
		TRACE(ft_t_err, "Botched ftape option.");
		TRACE_EXIT -EINVAL;
	}
	if (ints[0] == 0) {
		TRACE_EXIT 1;
	}
	if (ints[0] > FTAPE_SEL_D + 1) {
		TRACE(ft_t_err, "Too many parameters: %d.", ints[0]);
		TRACE_EXIT -EINVAL;
	}

	TRACE(ft_t_noise, "str: %s", str);
	for (i = 0; config_params[i].name != NULL; i++) {
		TRACE(ft_t_noise, "name: %s", config_params[i].name);
		if (strcmp(str, config_params[i].name) == 0) {
			for (j = 1; j <= ints[0]; j++) {
				if (ints[j] < config_params[i].min ||
				    ints[j] > config_params[i].max) {
					TRACE(ft_t_err,
				      "parameter %s out of range %d ... %d",
					      config_params[i].name,
					      config_params[i].min,
					      config_params[i].max);
					if (config_params[i].shared) {
						return config_params[i].shared;
					} else {
						TRACE_EXIT -EINVAL;
					}
				}
				if (config_params[i].var) {
					TRACE(ft_t_noise, "%s[%d] = %d",
					      str, j-1, ints[j]);
					config_params[i].var[j-1] = ints[j];
				}
			}
			TRACE_EXIT config_params[i].shared;
		}
	}
	TRACE_EXIT 1; /* allow other modules to proceed, option is not
		       * for use
		       */
}

__setup("ftape=", ftape_setup);

