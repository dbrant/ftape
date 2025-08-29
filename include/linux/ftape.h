#ifndef _FTAPE_H
#define _FTAPE_H

/*
 * Copyright (C) 1994-1996 Bas Laarhoven,
 *           (C) 1996-2000 Claus-Justus Heine.

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
 * $RCSfile: ftape.h,v $
 * $Revision: 1.60 $
 * $Date: 2000/07/25 10:48:00 $
 *
 *      This file contains global definitions, typedefs and macro's
 *      for the QIC-40/80/3010/3020 floppy-tape driver for Linux.
 *
 */

#define FTAPE_VERSION "ftape v4.05 08/28/2025"

#ifdef __KERNEL__
#include <linux/sched.h>
#include <linux/mm.h>
#endif
#include <linux/types.h>
#include <linux/version.h>
#include <linux/mtio.h>
# include <linux/init.h>

# ifdef CONFIG_KMOD
#  include <linux/kmod.h>
#  define FT_MODULE_AUTOLOAD
# endif

#define FT_SECTOR(x)		(x+1)	/* sector offset into real sector */
#define FT_SECTOR_SIZE		1024
#define FT_SECTORS_PER_SEGMENT	  32
#define FT_ECC_SECTORS		   3
#define FT_SEGMENT_SIZE		((FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS) * FT_SECTOR_SIZE)
#define FT_BUFF_SIZE    (FT_SECTORS_PER_SEGMENT * FT_SECTOR_SIZE)

/*
 *   bits of the minor device number that define drive selection
 *   methods. Could be used one day to access multiple tape
 *   drives on the same controller.
 */
#define FTAPE_SEL_A     0
#define FTAPE_SEL_B     1
#define FTAPE_SEL_C     2
#define FTAPE_SEL_D     3
#define FTAPE_SEL_MASK     3
#define FTAPE_SEL(unit) ((unit) & FTAPE_SEL_MASK)
#define FTAPE_NO_REWIND 4	/* mask for minor nr */

/* the following two may be reported when MTIOCGET is requested ... */
typedef union {
	struct {
		__u8 error;
		__u8 command;
	} error;
	long space;
} ft_drive_error;
typedef union {
	struct {
		__u8 drive_status;
		__u8 drive_config;
		__u8 tape_status;
	} status;
	long space;
} ft_drive_status;

#ifdef __KERNEL__

#define FT_RQM_DELAY    12
#define FT_MILLISECOND  1
#define FT_SECOND       1000
#define FT_FOREVER      -1
#ifndef HZ
#error "HZ undefined."
#endif
#define FT_USPT         (1000000/HZ) /* microseconds per tick */

/* This defines the number of retries that the driver will allow
 * before giving up (and letting a higher level handle the error).
 */
#ifdef TESTING
#define FT_SOFT_RETRIES 1	   /* number of low level retries */
#define FT_RETRIES_ON_ECC_ERROR 3  /* ecc error when correcting segment */
#else
#define FT_SOFT_RETRIES 6	   /* number of low level retries (triple) */
#define FT_RETRIES_ON_ECC_ERROR 3  /* ecc error when correcting segment */
#endif

#ifndef THE_FTAPE_MAINTAINER
#define THE_FTAPE_MAINTAINER "the ftape maintainer"
#endif

/* default hardware configuration. This is ugly. Maybe my
 * configuration menu should be more sparse ...
 */
/*  first ftape device
 */
#ifdef CONFIG_FT_NONE_0
# define CONFIG_FT_FDC_DRIVER_0 "none"
#elif defined(CONFIG_FT_INT_0)
# define CONFIG_FT_FDC_DRIVER_0 "ftape-internal"
#elif defined(CONFIG_FT_PAR_0)
# if defined(CONFIG_FT_TRAKKER_0)
#  define CONFIG_FT_FDC_DRIVER_0 "trakker"
# elif defined(CONFIG_FT_BPCK_0)
#  define CONFIG_FT_FDC_DRIVER_0 "bpck-fdc"
# elif defined(CONFIG_FT_TRAKKERBPCK_0)
#  define CONFIG_FT_FDC_DRIVER_0 "trakker:bpck-fdc"
# endif
#endif  /* CONFIG_FT_PAR_0 */

#ifdef CONFIG_FT_NONE_1
# define CONFIG_FT_FDC_DRIVER_1 "none"
#elif defined(CONFIG_FT_INT_1)
# define CONFIG_FT_FDC_DRIVER_1 "ftape-internal"
#elif defined(CONFIG_FT_PAR_1)
# if defined(CONFIG_FT_TRAKKER_1)
#  define CONFIG_FT_FDC_DRIVER_1 "trakker"
# elif defined(CONFIG_FT_BPCK_1)
#  define CONFIG_FT_FDC_DRIVER_1 "bpck-fdc"
# elif defined(CONFIG_FT_TRAKKERBPCK_1)
#  define CONFIG_FT_FDC_DRIVER_1 "trakker:bpck-fdc"
# endif
#endif  /* CONFIG_FT_PAR_1 */

#ifdef CONFIG_FT_NONE_2
# define CONFIG_FT_FDC_DRIVER_2 "none"
#elif defined(CONFIG_FT_INT_2)
# define CONFIG_FT_FDC_DRIVER_2 "ftape-internal"
#elif defined(CONFIG_FT_PAR_2)
# if defined(CONFIG_FT_TRAKKER_2)
#  define CONFIG_FT_FDC_DRIVER_2 "trakker"
# elif defined(CONFIG_FT_BPCK_2)
#  define CONFIG_FT_FDC_DRIVER_2 "bpck-fdc"
# elif defined(CONFIG_FT_TRAKKERBPCK_2)
#  define CONFIG_FT_FDC_DRIVER_2 "trakker:bpck-fdc"
# endif
#endif  /* CONFIG_FT_PAR_2 */

#ifdef CONFIG_FT_NONE_3
# define CONFIG_FT_FDC_DRIVER_3 "none"
#elif defined(CONFIG_FT_INT_3)
# define CONFIG_FT_FDC_DRIVER_3 "ftape-internal"
#elif defined(CONFIG_FT_PAR_3)
# if defined(CONFIG_FT_TRAKKER_3)
#  define CONFIG_FT_FDC_DRIVER_3 "trakker"
# elif defined(CONFIG_FT_BPCK_3)
#  define CONFIG_FT_FDC_DRIVER_3 "bpck-fdc"
# elif defined(CONFIG_FT_TRAKKERBPCK_3)
#  define CONFIG_FT_FDC_DRIVER_3 "trakker:bpck-fdc"
# endif
#endif  /* CONFIG_FT_PAR_3 */

#ifndef CONFIG_FT_FDC_DRIVER_0
# define CONFIG_FT_FDC_DRIVER_0 NULL
#endif
#ifndef CONFIG_FT_FDC_DRIVER_1
# define CONFIG_FT_FDC_DRIVER_1 NULL
#endif
#ifndef CONFIG_FT_FDC_DRIVER_2
# define CONFIG_FT_FDC_DRIVER_2 NULL
#endif
#ifndef CONFIG_FT_FDC_DRIVER_3
# define CONFIG_FT_FDC_DRIVER_3 NULL
#endif
#ifndef CONFIG_FT_FDC_THRESHOLD_0
# define CONFIG_FT_FDC_THRESHOLD_0 8
#endif
#ifndef CONFIG_FT_FDC_THRESHOLD_1
# define CONFIG_FT_FDC_THRESHOLD_1 8
#endif
#ifndef CONFIG_FT_FDC_THRESHOLD_2
# define CONFIG_FT_FDC_THRESHOLD_2 8
#endif
#ifndef CONFIG_FT_FDC_THRESHOLD_3
# define CONFIG_FT_FDC_THRESHOLD_3 8
#endif
#ifndef CONFIG_FT_FDC_MAX_RATE_0
# define CONFIG_FT_FDC_MAX_RATE_0 4000
#endif
#ifndef CONFIG_FT_FDC_MAX_RATE_1
# define CONFIG_FT_FDC_MAX_RATE_1 4000
#endif
#ifndef CONFIG_FT_FDC_MAX_RATE_2
# define CONFIG_FT_FDC_MAX_RATE_2 4000
#endif
#ifndef CONFIG_FT_FDC_MAX_RATE_3
# define CONFIG_FT_FDC_MAX_RATE_3 4000
#endif

#ifdef CONFIG_FTAPE_MODULE
/* This is just a hack to enable compilation with kernel which already
 * have a -- possibly different -- version of ftape compiled into the
 * kernel image.
 */
#undef CONFIG_FTAPE
#undef CONFIG_FT_INTERNAL
#undef CONFIG_FT_TRAKKER
#undef CONFIG_FT_BPCK
#undef CONFIG_ZFTAPE
#undef CONFIG_ZFT_COMPRESSOR
#endif

/*      some useful macro's
 */
#define ABS(a)          ((a) < 0 ? -(a) : (a))
#define NR_ITEMS(x)     (int)(sizeof(x)/ sizeof(*x))

extern int ftape_init(void);

#endif  /* __KERNEL__ */

#endif
