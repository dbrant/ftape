#ifndef _FDC_INTERNEL_H
#define _FDC_INTERNEL_H

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
 * $RCSfile: fdc-internal.h,v $
 * $Revision: 1.11 $
 * $Date: 2000/06/29 17:26:27 $
 *
 *     Configuration fill-in for internal floppy taper drives.
 */

/* #include <linux/config.h> - not needed in modern kernels */
#include <linux/ftape.h>

/* Initialize missing configuration parameters.
 * threshold, base, irq and dma are initialized in ftape.h
 */
#ifndef CONFIG_FT_NR_BUFFERS
# define CONFIG_FT_NR_BUFFERS 3
#endif

/* do the ugly bottom half of the kernel configuration menu ...
 */

/*  first ftape device ...
 */
#if defined(CONFIG_FT_INT_0) || defined(CONFIG_FT_AUTO_0)
# if   defined(CONFIG_FT_ALT_FDC_0)
#  ifndef CONFIG_FT_INT_HW_0 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_0 0x370
#   define CONFIG_FT_FDC_IRQ_0  6
#   define CONFIG_FT_FDC_DMA_0  2
#  endif
# elif defined(CONFIG_FT_PNP_FDC_0)
#  ifndef CONFIG_FT_INT_HW_0 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_0 -1
#   define CONFIG_FT_FDC_IRQ_0  -1
#   define CONFIG_FT_FDC_DMA_0  -1
#  endif
# elif defined(CONFIG_FT_MACH2_0)
#  ifndef CONFIG_FT_INT_HW_0 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_0 0x1e0
#   define CONFIG_FT_FDC_IRQ_0  6
#   define CONFIG_FT_FDC_DMA_0  2
#  endif
# elif defined(CONFIG_FT_FC10_0)
#  ifndef CONFIG_FT_INT_HW_0 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_0 0x180
#   define CONFIG_FT_FDC_IRQ_0  9
#   define CONFIG_FT_FDC_DMA_0  3
#  endif
# elif defined(CONFIG_FT_STD_FDC_0)
#  define CONFIG_FT_FDC_BASE_0 0x3f0
#  define CONFIG_FT_FDC_IRQ_0  6
#  define CONFIG_FT_FDC_DMA_0  2
# endif
#endif /* CONFIG_FT_NONE_0 */

/*  second ftape device ...
 */
#if defined(CONFIG_FT_INT_1) || defined(CONFIG_FT_AUTO_1)
# if   defined(CONFIG_FT_ALT_FDC_1)
#  ifndef CONFIG_FT_INT_HW_1 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_1 0x370
#   define CONFIG_FT_FDC_IRQ_1  6
#   define CONFIG_FT_FDC_DMA_1  2
#  endif
# elif defined(CONFIG_FT_PNP_FDC_1)
#  ifndef CONFIG_FT_INT_HW_1 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_1 -1
#   define CONFIG_FT_FDC_IRQ_1  -1
#   define CONFIG_FT_FDC_DMA_1  -1
#  endif
# elif defined(CONFIG_FT_MACH2_1)
#  ifndef CONFIG_FT_INT_HW_1 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_1 0x1e0
#   define CONFIG_FT_FDC_IRQ_1  6
#   define CONFIG_FT_FDC_DMA_1  2
#  endif
# elif defined(CONFIG_FT_FC10_1)
#  ifndef CONFIG_FT_INT_HW_1 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_1 0x180
#   define CONFIG_FT_FDC_IRQ_1  9
#   define CONFIG_FT_FDC_DMA_1  3
#  endif
# elif defined(CONFIG_FT_STD_FDC_1)
#  define CONFIG_FT_FDC_BASE_1 0x3f0
#  define CONFIG_FT_FDC_IRQ_1  6
#  define CONFIG_FT_FDC_DMA_1  2
# endif
#endif /* CONFIG_FT_NONE_1 */

/*  third ftape device ...
 */
#if defined(CONFIG_FT_INT_2) || defined(CONFIG_FT_AUTO_2)
# if   defined(CONFIG_FT_ALT_FDC_2)
#  ifndef CONFIG_FT_INT_HW_2 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_2 0x370
#   define CONFIG_FT_FDC_IRQ_2  6
#   define CONFIG_FT_FDC_DMA_2  2
#  endif
# elif defined(CONFIG_FT_PNP_FDC_2)
#  ifndef CONFIG_FT_INT_HW_2 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_2 -1
#   define CONFIG_FT_FDC_IRQ_2  -1
#   define CONFIG_FT_FDC_DMA_2  -1
#  endif
# elif defined(CONFIG_FT_MACH2_2)
#  ifndef CONFIG_FT_INT_HW_2 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_2 0x1e0
#   define CONFIG_FT_FDC_IRQ_2  6
#   define CONFIG_FT_FDC_DMA_2  2
#  endif
# elif defined(CONFIG_FT_FC10_2)
#  ifndef CONFIG_FT_INT_HW_2 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_2 0x180
#   define CONFIG_FT_FDC_IRQ_2  9
#   define CONFIG_FT_FDC_DMA_2  3
#  endif
# elif defined(CONFIG_FT_STD_FDC_2)
#  define CONFIG_FT_FDC_BASE_2 0x3f0
#  define CONFIG_FT_FDC_IRQ_2  6
#  define CONFIG_FT_FDC_DMA_2  2
# endif
#endif /* CONFIG_FT_NONE_2 */

/*  fourth ftape device ...
 */
#if defined(CONFIG_FT_INT_3) || defined(CONFIG_FT_AUTO_3)
# if   defined(CONFIG_FT_ALT_FDC_3)
#  ifndef CONFIG_FT_INT_HW_3 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_3 0x370
#   define CONFIG_FT_FDC_IRQ_3  6
#   define CONFIG_FT_FDC_DMA_3  2
#  endif
# elif defined(CONFIG_FT_PNP_FDC_3)
#  ifndef CONFIG_FT_INT_HW_3 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_3 -1
#   define CONFIG_FT_FDC_IRQ_3  -1
#   define CONFIG_FT_FDC_DMA_3  -1
#  endif
# elif defined(CONFIG_FT_MACH2_3)
#  ifndef CONFIG_FT_INT_HW_3 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_3 0x1e0
#   define CONFIG_FT_FDC_IRQ_3  6
#   define CONFIG_FT_FDC_DMA_3  2
#  endif
# elif defined(CONFIG_FT_FC10_3)
#  ifndef CONFIG_FT_INT_HW_3 /* no detaild HW setup chosen */
#   define CONFIG_FT_FDC_BASE_3 0x180
#   define CONFIG_FT_FDC_IRQ_3  9
#   define CONFIG_FT_FDC_DMA_3  3
#  endif
# elif defined(CONFIG_FT_STD_FDC_3)
#  define CONFIG_FT_FDC_BASE_3 0x3f0
#  define CONFIG_FT_FDC_IRQ_3  6
#  define CONFIG_FT_FDC_DMA_3  2
# endif
#endif /* CONFIG_FT_NONE_3 */

/* Turn some booleans into numbers.
 */
#ifndef CONFIG_FT_FC10_0
# define CONFIG_FT_FC10_0 0
#endif
#ifndef CONFIG_FT_FC10_1
# define CONFIG_FT_FC10_1 0
#endif
#ifndef CONFIG_FT_FC10_2
# define CONFIG_FT_FC10_2 0
#endif
#ifndef CONFIG_FT_FC10_3
# define CONFIG_FT_FC10_3 0
#endif

#ifndef CONFIG_FT_MACH2_0
# define CONFIG_FT_MACH2_0 0
#endif
#ifndef CONFIG_FT_MACH2_1
# define CONFIG_FT_MACH2_1 0
#endif
#ifndef CONFIG_FT_MACH2_2
# define CONFIG_FT_MACH2_2 0
#endif
#ifndef CONFIG_FT_MACH2_3
# define CONFIG_FT_MACH2_3 0
#endif

#ifndef CONFIG_FT_NR_BUFFERS
# define CONFIG_FT_NR_BUFFERS 3
#endif

/*  fallback values: nothing configured.
 */
#ifndef CONFIG_FT_FDC_BASE_0
# define CONFIG_FT_FDC_BASE_0 -1
#endif
#ifndef CONFIG_FT_FDC_BASE_1
# define CONFIG_FT_FDC_BASE_1 -1
#endif
#ifndef CONFIG_FT_FDC_BASE_2
# define CONFIG_FT_FDC_BASE_2 -1
#endif
#ifndef CONFIG_FT_FDC_BASE_3
# define CONFIG_FT_FDC_BASE_3 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_0
# define CONFIG_FT_FDC_IRQ_0 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_1
# define CONFIG_FT_FDC_IRQ_1 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_2
# define CONFIG_FT_FDC_IRQ_2 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_3
# define CONFIG_FT_FDC_IRQ_3 -1
#endif
#ifndef CONFIG_FT_FDC_DMA_0
# define CONFIG_FT_FDC_DMA_0 -1
#endif
#ifndef CONFIG_FT_FDC_DMA_1
# define CONFIG_FT_FDC_DMA_1 -1
#endif
#ifndef CONFIG_FT_FDC_DMA_2
# define CONFIG_FT_FDC_DMA_2 -1
#endif
#ifndef CONFIG_FT_FDC_DMA_3
# define CONFIG_FT_FDC_DMA_3 -1
#endif

typedef struct fdc_int {
	void *deblock_buffer;
	unsigned int locked:1;
} fdc_int_t;

int ftape_internal_setup(char *str);
int fdc_internal_register(void);

#endif
