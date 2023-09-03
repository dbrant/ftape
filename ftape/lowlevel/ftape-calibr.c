/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-calibr.c,v $
 * $Revision: 1.9 $
 * $Date: 1998/12/18 22:02:16 $
 *
 *      GP calibration routine for processor speed dependent
 *      functions. Stolen from init/main.c of the Linux kernel source tree
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ftape.h>
#include "ftape-tracing.h"
#include "ftape-calibr.h"
#include "ftape-ctl.h"


/*
 *      Input:  function taking int count as parameter.
 *              pointers to calculated calibration variables.
 */
/* This is the number of bits of precision for the loops_per_second.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 12 /* overkill, probably */
#if HZ < 100 /* safeguard, unneeded */
# define CALIBRATION_TIME HZ
#else
# define CALIBRATION_TIME 100
#endif

void ftape_calibrate(ftape_info_t *ftape,
		     char *name,
		     void (*fun) (void *, unsigned int), 
		     void *data,
		     unsigned int *calibr_count, 
		     unsigned int *calibr_time)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;
	int once_loops;
	int multiple_loops;
	TRACE_FUN(ft_t_flow);

	/*
	 * First calibrate the function call overhead
	 * 
	 * Shouldn't we mask all interrupts on all processors,
	 * except of the timer interrupt?
	 *
	 */
	once_loops = (1 << 6);
	while (once_loops <<= 1) {
		int i;
		
		*calibr_count =
			*calibr_time = once_loops;	/* set TC to 1 */
		ticks = jiffies;
		while (ticks == jiffies) /* nothing */;
		ticks = jiffies;
		for (i = once_loops; i > 0; i--) {
			fun(data, 0);
		}
		ticks = jiffies - ticks;
		if (ticks >= HZ/CALIBRATION_TIME) {/* wait until 1/100  sec. */
			break;
		}
	}

	TRACE(ft_t_noise, "once_loops before interpolation: %d", once_loops);

	/* Do a binary approximation to get loops_per_second set to
	 * equal one clock (up to lps_precision bits)
	 */
	once_loops >>= 1;
	loopbit = once_loops;
	lps_precision = LPS_PREC;
	while ( lps_precision-- && (loopbit >>= 1) ) {
		int i;

		*calibr_count =
			*calibr_time = once_loops;	/* set TC to 1 */
		once_loops |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		for (i = once_loops; i > 0; i--) {
			fun(data, 0);
		}
		ticks = jiffies - ticks;
		if (ticks >= HZ/CALIBRATION_TIME) { /* longer than 1/100 sec */
			once_loops &= ~loopbit;
		}
	}

	TRACE(ft_t_noise, "once_loops after interpolation: %d", once_loops);

	/* Now we know that one function call approximately takes
	 * 1/(100 * once_loops) seconds, i.e. 10/once_loops ms,
	 * i.e. 10000/once_loops us.
	 */

	/* now go for multiple calls.
	 */

	multiple_loops = (1 << 6);
	while (multiple_loops <<= 1) {
		*calibr_count =
			*calibr_time = multiple_loops;	/* set TC to 1 */
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies) /* nothing */;
		ticks = jiffies;
		fun(data, multiple_loops);
		ticks = jiffies - ticks;
		if (ticks >= HZ/CALIBRATION_TIME) {/* wait until 1/100  sec. */
			break;
		}
	}

	TRACE(ft_t_noise,
	      "multiple_loops before interpolation: %d", multiple_loops);

	/* Do a binary approximation to get loops_per_second set to
	 * equal one clock (up to lps_precision bits)
	 *
	 * Will this work?
	 */
	multiple_loops >>= 1;
	loopbit = multiple_loops;
	lps_precision = LPS_PREC;
	while ( lps_precision-- && (loopbit >>= 1) ) {
		*calibr_count =
			*calibr_time = multiple_loops;	/* set TC to 1 */
		multiple_loops |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		fun(data, multiple_loops);
		ticks = jiffies - ticks;
		if (ticks >= HZ/CALIBRATION_TIME) { /* longer than 1/100 sec */
			multiple_loops &= ~loopbit;
		}
	}

	TRACE(ft_t_noise,
	      "multiple_loops after interpolation: %d", multiple_loops);

	/* Now we know that call overhead + looping take approximately
	 * 2/100 sec. for multiple_loops loops. Now compute the calibration
	 * values.
	 */

	*calibr_count = multiple_loops;
	/*
	 *  convert to us
	 */
	*calibr_time  = (1000000/CALIBRATION_TIME -
			 (((1000000/CALIBRATION_TIME << 16)/once_loops)>>16));

	TRACE(ft_t_info, "calibr_count: %d, calibr_time: %d us",
	      *calibr_count, *calibr_time);
	TRACE(ft_t_info, "TC for `%s()' = %d nsec (at %d counts)",
	     name, (1000 * *calibr_time) / *calibr_count, *calibr_count);
	TRACE_EXIT;
}
