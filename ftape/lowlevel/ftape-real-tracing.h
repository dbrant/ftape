#ifndef _FTAPE_REAL_TRACING_H
#define _FTAPE_REAL_TRACING_H
#endif

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
 * $RCSfile: ftape-real-tracing.h,v $
 * $Revision: 1.12 $
 * $Date: 1999/02/07 00:24:42 $
 *
 *      This file contains definitions that defines dummy versions of
 *      the trace information. This is useful as we maintain per-ftape
 *      device tracing information to allow some kind of trace output
 *      for the low level fdc modules when no device exists.
 */

#ifndef  CONFIG_FT_NO_TRACE_AT_ALL

#undef FT_TRACE_TOP_LEVEL
#undef TRACE_FUN
#undef TRACE_EXIT
#undef TRACE
#undef SET_TRACE_LEVEL
#undef TRACE_LEVEL
#undef TRACE_SEL
#undef ftape_function_nest_level
#undef ftape_tracing

#ifdef FDC_TRACING
# define TRACE_SEL fdc->unit
# undef FDC_TRACING
#elif defined(ZFTAPE_TRACING)
# define TRACE_SEL FTAPE_SEL(zftape->unit)
# undef ZFTAPE_TRACING
#elif defined(GLOBAL_TRACING)
# define TRACE_SEL 4
# undef GLOBAL_TRACING
#elif defined(SEL_TRACING)
# define TRACE_SEL sel
# undef SEL_TRACING
#elif defined(ZFTC_TRACING)
# if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
#  define TRACE_SEL FTAPE_SEL(zftc->zftape->unit)
#  undef ZFTC_TRACING
# else
#  error Not configured for reading compressed volumes
# endif
#else
# define TRACE_SEL ftape->drive_sel
#endif

#define ftape_function_nest_level ftape_function_nest_levels[TRACE_SEL]
#define ftape_tracing             ftape_tracings[TRACE_SEL]


#ifdef CONFIG_FT_NO_TRACE
/*  the compiler will optimize away many TRACE() macros
 *  the ftape_simple_trace_call() function simply increments 
 *  the function nest level.
 */ 
#define FT_TRACE_TOP_LEVEL	ft_t_warn
#define TRACE_FUN(level)	atomic_inc(&ftape_function_nest_level)
#define TRACE_EXIT		atomic_dec(&ftape_function_nest_level); return
#define TRACE(l, m, ...)					\
{								\
	if (ftape_tracing >= (ft_trace_t)(l) &&			\
	    (ft_trace_t)(l) <= FT_TRACE_TOP_LEVEL) {		\
		ftape_trace_log(&ftape_function_nest_level,	\
				__FILE__, __func__,		\
				TRACE_SEL);			\
		printk(m , ##__VA_ARGS__ );			\
	}							\
}



#else

#if defined(CONFIG_FT_FULL_DEBUG)
#define FT_TRACE_TOP_LEVEL ft_t_any
#else
#define FT_TRACE_TOP_LEVEL ft_t_flow
#endif

#define TRACE_FUN(level)						      \
	const ft_trace_t _tracing = level;				      \
	static char ft_trace_file[] = __FILE__;	      \
	if (ftape_tracing >= (ft_trace_t)(level) &&			      \
	    (ft_trace_t)(level) <= FT_TRACE_TOP_LEVEL)			      \
		ftape_trace_call(&ftape_function_nest_level,		      \
				 ft_trace_file, __FUNCTION__,	      \
				 TRACE_SEL);				      \
	atomic_inc(&ftape_function_nest_level);

#define TRACE_EXIT							\
	atomic_dec(&ftape_function_nest_level);				\
	if (ftape_tracing >= (ft_trace_t)(_tracing) &&			\
	    (ft_trace_t)(_tracing) <= FT_TRACE_TOP_LEVEL)		\
		ftape_trace_exit(&ftape_function_nest_level,		\
				 ft_trace_file, __FUNCTION__,	\
				 TRACE_SEL);				\
	return

#define TRACE(l, m, ...)						\
{									\
	if (ftape_tracing >= (ft_trace_t)(l) &&				\
	    (ft_trace_t)(l) <= FT_TRACE_TOP_LEVEL) {			\
		ftape_trace_log(&ftape_function_nest_level,		\
				ft_trace_file, __FUNCTION__,	\
				TRACE_SEL);				\
		printk(KERN_INFO m, ##__VA_ARGS__ );			\
	}								\
}

#endif

#define SET_TRACE_LEVEL(l) 				\
{							\
	if ((ft_trace_t)(l) <= FT_TRACE_TOP_LEVEL) {	\
		ftape_tracing = (ft_trace_t)(l);	\
	} else {					\
		ftape_tracing = FT_TRACE_TOP_LEVEL;	\
	}						\
}
#define TRACE_LEVEL    							     \
((ftape_tracing <= FT_TRACE_TOP_LEVEL) ? ftape_tracing : FT_TRACE_TOP_LEVEL)

#endif /* CONFIG_FT_NO_TRACE_AT_ALL */
