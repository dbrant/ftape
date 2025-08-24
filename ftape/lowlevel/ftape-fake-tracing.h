#ifndef _FTAPE_FAKE_TRACING_H
#define _FTAPE_FAKE_TRACING_H
#endif

/*
 * Copyright (C) 1997-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-fake-tracing.h,v $
 * $Revision: 1.4 $
 * $Date: 1998/12/18 22:02:17 $
 *
 *      This file contains definitions that defines dummy versions of
 *      the trace information. This is useful as we maintain per-ftape
 *      device tracing information to allow some kind of trace output
 *      for the low level fdc modules when no device exists.
 */

#undef TRACE_FUN
#undef TRACE_EXIT
#undef TRACE
#undef SET_TRACE_LEVEL
#undef TRACE_LEVEL

#define TRACE_FUN(level)	do {} while(0)
#define TRACE_EXIT		return
#define TRACE(l, m, i...)						\
{									\
	if ((ft_trace_t)(l) <= FT_TRACE_TOP_LEVEL) {			\
		printk(KERN_INFO"ftape"__FILE__":(%s):\n"		\
		       KERN_INFO m".\n", __func__ ,##i);		\
	}								\
}
#define SET_TRACE_LEVEL(l)      if ((l) == (l)) do {} while(0)
#define TRACE_LEVEL		FT_TRACE_TOP_LEVEL

