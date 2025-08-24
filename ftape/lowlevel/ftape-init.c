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
 *      This file contains the code that interfaces the kernel
 *      for the QIC-40/80/3010/3020 floppy-tape driver for Linux.
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/major.h>


#include <linux/ftape.h>
#include <linux/qic117.h>
#ifdef CONFIG_ZFTAPE
#include <linux/zftape.h>
#endif

#include "ftape-init.h"
#include "ftape_syms.h"
#include "ftape-io.h"
#include "ftape-read.h"
#include "ftape-write.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "fdc-io.h"
#include "ftape-buffer.h"
#include "ftape-proc.h"
#include "ftape-tracing.h"

/*      Global vars.
 */
char ft_src[] __initdata = "$RCSfile: ftape-init.c,v $";
char ft_rev[] __initdata = "$Revision: 1.28 $";
char ft_dat[] __initdata = "$Date: 2000/06/30 12:02:37 $";

int ft_ignore_ecc_err = 0;
int ft_soft_retries = FT_SOFT_RETRIES;


/* every ft_fdc_driver[sel] string may contain a colon separated list
 * of drivers wanted for a specific device. Replace the colons by zero
 * bytes and set ft_fdc_driver_no[sel] to the number of available
 * drivers.
 */
#define GLOBAL_TRACING
#include "ftape-real-tracing.h"

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

static void __init ftape_build_driver_list(void)
{
	char *ptr, *newptr;
	int sel;
	TRACE_FUN(ft_t_flow);

	ftape_sleep(FT_SECOND);

	for (sel = FTAPE_SEL_A; sel <= FTAPE_SEL_D; sel++) {
		if ((ptr = ft_fdc_driver[sel]) == NULL ||
		    *ptr == '\0' ||
		    strncmp(ptr, "auto", 4) == 0) {
			ft_fdc_driver_no[sel] = 0;
			continue;
		} else if (strncmp(ptr, "none", 4) == 0) {
			ft_fdc_driver_no[sel] = -1; /* disable */
			continue;
		}
		ft_fdc_driver_no[sel] = 1;
		while ((newptr = strchr(ptr, ':')) != NULL) {
			*newptr ++ = '\0';
			TRACE(ft_t_noise, "Found driver %s for device %d",
			      ptr, sel);
			ptr = newptr;
			if (*ptr != '\0') {
				ft_fdc_driver_no[sel] ++;
			}
		}
		TRACE(ft_t_noise, "Found driver %s for device %d",
		      ptr, sel);
	}
	TRACE_EXIT;
}

extern int fdc_internal_register(void) __init;
extern void ftape_parport_init(void) __init;

/*  Called by modules package when installing the driver
 *  or by kernel during the initialization phase
 */
int __init ftape_init(void)
{

#ifdef MODULE
	printk(FTAPE_BANNER); /* defined in ftape-init.h */
#else /* !MODULE */
	/* print a short no-nonsense boot message */
	printk(KERN_INFO FTAPE_VERSION " for Linux " UTS_RELEASE "\n");
#endif /* MODULE */
	printk("installing QIC-117 floppy tape driver ... \n");
#ifdef MODULE
	printk("ftape_init @ 0x%p.\n", ftape_init);
#endif


	ftape_build_driver_list(); /* parse ft_fdc_driver[] parameters */

#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)
	(void)ftape_proc_init();
#endif

#ifndef MODULE
#ifdef CONFIG_FT_INTERNAL
	(void)fdc_internal_register();
#endif
#ifdef CONFIG_FT_PARPORT
	(void)ftape_parport_init();
#endif
#ifdef CONFIG_ZFTAPE
	(void)zft_init();
#endif
#endif

	printk(">>> ft_soft_retries: %d\n", ft_soft_retries);
	printk(">>> ft_ignore_ecc_err: %d\n", ft_ignore_ecc_err);

	return 0;
}

#undef FT_TRACE_ATTR
#define FT_TRACE_ATTR /**/

#ifndef CONFIG_FT_NO_TRACE_AT_ALL
static ft_trace_t ft_tracings[5] = {
	ft_t_info, ft_t_info, ft_t_info, ft_t_info, ft_t_info
};
#endif

module_param_array(ft_fdc_driver, charp, NULL, 0644);
MODULE_PARM_DESC(ft_fdc_driver, "Colon separated list of FDC low level drivers");

#ifndef CONFIG_FT_NO_TRACE_AT_ALL
module_param_array(ft_tracings, int, NULL, 0644);
MODULE_PARM_DESC(ft_tracings, "Amount of debugging output, 0 <= tracing <= 8, default 3.");
#endif

/* [DB 2023] Added the following parameters:  */
module_param(ft_ignore_ecc_err, int, 0644);
MODULE_PARM_DESC(ft_ignore_ecc_err, "Whether to ignore ECC errors and proceed to the next segment (0 or 1).");

module_param(ft_soft_retries, int, 0644);
MODULE_PARM_DESC(ft_soft_retries, "Number of low-level soft retries (Default=6, set to 1 to skip over bad segments faster.).");

MODULE_LICENSE("GPL");

MODULE_AUTHOR(
	"(c) 1993-1996 Bas Laarhoven, "
	"(c) 1995-1996 Kai Harrekilde-Petersen, "
	"(c) 1996-2000 Claus-Justus Heine <heine@instmath.rwth-aachen.de>");
MODULE_DESCRIPTION(
	"QIC-117 driver for QIC-40/80/3010/3020/Ditto floppy tape drives.");

#if LINUX_VERSION_CODE <= KERNEL_VER(1,2,13)
char kernel_version[] = UTS_RELEASE;
#endif

/*  Called by modules package when installing the driver
 */
static int __init ftape_module_init(void)
{
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
	memcpy(ftape_tracings, ft_tracings, sizeof(ft_tracings));
#endif
	return ftape_init();
}
module_init(ftape_module_init);

/*  Called by modules package when removing the driver
 */
static void __exit ftape_module_exit(void)
{
	int sel;
#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)
	ftape_proc_destroy();
#endif
	for (sel = 0; sel < 4; sel++) {
		ftape_destroy(sel);
	}
        printk(KERN_INFO "ftape: unloaded.\n");
}
module_exit(ftape_module_exit);

#ifndef MODULE

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

int ftape_lowlevel_setup(char *str)
{	
	static __initlocaldata int ints[6] = { 0, };
	size_t offset = strlen(str) - strlen("driver");
	TRACE_FUN(ft_t_flow);

	str = get_options(str, ARRAY_SIZE(ints), ints);

	/* this ought to be the ft_fdc_drivers setup ... */
	if (strcmp(str+offset, "driver") == 0) {
		/* this is supposed to be a comma separated list of
		 * low-level FDC drivers ...
		 */
		int cnt = 0;
		char *last = strchr(str, ',');

		while (last && cnt <= FTAPE_SEL_D) {

			ft_fdc_driver[cnt ++] = str;
			str = last + 1;
			*last = '\0';
			last = strchr(str, ',');
		}
		TRACE(ft_t_noise, "fdc drivers: %s %s %s %s",
		      ft_fdc_driver[0],
		      ft_fdc_driver[1],
		      ft_fdc_driver[2],
		      ft_fdc_driver[3])
		if (last) {
			TRACE(ft_t_err, "Too many parameters in string %s\n",
			      str);
			TRACE_EXIT -EINVAL;
		} else {
			TRACE_EXIT 0;
		}
	}
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
	else if (strcmp(str, "tracing") == 0) {
		int i;
		if (ints[0] > 5) {
			TRACE(ft_t_err, "too many trace values: %d", ints[0]);
		}
		for (i = 1; i <= ints[0]; i ++) {
			ftape_tracings[i-1] = ints[i];
		}
		TRACE_EXIT 0;
	}
#endif
	TRACE_EXIT 1; /* wasn't our option, pass it to other ftape
		       * setup funcs
		       */
}

#endif
