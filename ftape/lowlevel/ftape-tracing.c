/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-tracing.c,v $
 * $Revision: 1.11 $
 * $Date: 1999/02/07 00:24:42 $
 *
 *      This file contains the reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/ftape.h>
#include "ftape-tracing.h"

#undef function_nest_level

/*      Global vars.
 */
/*      tracing
 *      set it to:     to log :
 *       0              bugs
 *       1              + errors
 *       2              + warnings
 *       3              + information
 *       4              + more information
 *       5              + program flow
 *       6              + fdc/dma info
 *       7              + data flow
 *       8              + everything else
 */

atomic_t ftape_function_nest_levels[5];
ft_trace_t ftape_tracings[5] = {
 	ft_t_info, ft_t_info, ft_t_info, ft_t_info, ft_t_info
};

/*      Local vars.
 */
static __u8 trace_id = 0;
static char spacing[] = "*                              ";

void ftape_trace_call(atomic_t *function_nest_level,
		      const char *file, const char *name,
		      int sel)
{
	char *indent;

	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (atomic_read(function_nest_level) < 0) {
		printk(KERN_INFO "function nest level (%d) < 0\n",
		       atomic_read(function_nest_level));
		atomic_set(function_nest_level, 0);
	}
	if (atomic_read(function_nest_level) < sizeof(spacing)) {
		indent = (spacing +
			  sizeof(spacing) - 1 -
			  atomic_read(function_nest_level));
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d] %c%s+%s (%s)\n",
	       (int) trace_id++,
	       sel == 4 ? ' ' : '0' + sel,	       
	       indent, file, name);
}

void ftape_trace_exit(atomic_t *function_nest_level,
		      const char *file, const char *name,
		      int sel)
{
	char *indent;

	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (atomic_read(function_nest_level) < 0) {
		printk(KERN_INFO "function nest level (%d) < 0\n",
		       atomic_read(function_nest_level));
		atomic_set(function_nest_level, 0);
	}
	if (atomic_read(function_nest_level) < sizeof(spacing)) {
		indent = (spacing +
			  sizeof(spacing) - 1 -
			  atomic_read(function_nest_level));
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d] %c%s-%s (%s)\n",
	       (int) trace_id++,
	       sel == 4 ? ' ' : '0' + sel,
	       indent, file, name);
}

void ftape_trace_log(atomic_t *function_nest_level,
		     const char *file, const char *function,
		     int sel)
{
	char *indent;
	
	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (atomic_read(function_nest_level) <= 0) {
		printk(KERN_INFO "function nest level (%d) <= 0\n",
		       atomic_read(function_nest_level));
		atomic_set(function_nest_level, 1);
	}
	if (atomic_read(function_nest_level) < sizeof(spacing)) {
		indent = (spacing + 
			  sizeof(spacing) - 1 - 
			  atomic_read(function_nest_level));
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d] %c%s%s (%s) - ", 
	       (int) trace_id++,
	       sel == 4 ? ' ' : '0' + sel,
	       indent, file, function);
}
