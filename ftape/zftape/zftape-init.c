/*
 *      Copyright (C) 1996-1998 Claus-Justus Heine.

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
 *      This file contains the code that registers the zftape frontend 
 *      to the ftape floppy tape driver for Linux
 */


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/major.h>
#include <linux/fcntl.h>


#include <linux/zftape.h>

/* DevFS was removed from modern kernels - disabled for now */
#if 0 
#if LINUX_VERSION_CODE >= KERNEL_VER(2,3,46)
# include <linux/devfs_fs_kernel.h>

static devfs_handle_t ftape_devfs_handle;
static devfs_handle_t devfs_handle[4];

static devfs_handle_t devfs_q[4];
static devfs_handle_t devfs_qn[4];

# if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
static devfs_handle_t devfs_zq[4];
static devfs_handle_t devfs_zqn[4];
# endif	
# if CONFIG_ZFT_OBSOLETE
static devfs_handle_t devfs_raw[4];
static devfs_handle_t devfs_rawn[4];
# endif

#endif
#endif /* DevFS disabled */

#define SEL_TRACING
#include "zftape-init.h"
#include "zftape-read.h"
#include "zftape-write.h"
#include "zftape-ctl.h"
#include "zftape-buffers.h"
#include "zftape_syms.h"

char zft_src[] __initdata = "$RCSfile: zftape-init.c,v $";
char zft_rev[] __initdata = "$Revision: 1.38 $";
char zft_dat[] __initdata = "$Date: 2000/07/20 13:18:05 $";

int ft_major_device_number = QIC117_TAPE_MAJOR;

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,18)
#define FT_MOD_PARM(var,type,desc) \
	MODULE_PARM(var,type); MODULE_PARM_DESC(var,desc)
MODULE_AUTHOR("(c) 1996-2000 Claus-Justus Heine "
	      "<heine@instmath.rwth-aachen.de)");
MODULE_DESCRIPTION(ZFTAPE_VERSION " - "
		   "VFS interface for the Linux floppy tape driver. "
		   "Support for QIC-113 compatible volume table ");
MODULE_SUPPORTED_DEVICE("char-major-27");
FT_MOD_PARM(ft_major_device_number, "i",
	    "Major device number to use. Use with caution ...");
#endif
/*      Global vars.
 */

/*      Local vars.
 */

static int busy_flag[4];

/*  the interface to the kernel vfs layer
 */

/* Note about llseek():
 *
 * st.c and tpqic.c update fp->f_pos but don't implment llseek() and
 * initialize the llseek component of the file_ops struct with NULL.
 * This means that the user will get the default seek, but the tape
 * device will not respect the new position, but happily read from the
 * old position. Think a zftape specific llseek() function is better,
 * returning -ESPIPE.
 */

static int  zft_open (struct inode *ino, struct file *filep);
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,31)
static int zft_close(struct inode *ino, struct file *filep);
#else
static void zft_close(struct inode *ino, struct file *filep);
#endif
static int  zft_ioctl(struct inode *ino, struct file *filep,
		      unsigned int command, unsigned long arg);
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
static ssize_t zft_read (struct file *fp, char *buff,
			 size_t req_len, loff_t *ppos);
static ssize_t zft_write(struct file *fp, const char *buff,
			 size_t req_len, loff_t *ppos);
#elif LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long zft_read (struct inode *ino, struct file *fp, char *buff,
		      unsigned long req_len);
static long zft_write(struct inode *ino, struct file *fp, const char *buff,
		      unsigned long req_len);
#else
static int  zft_read (struct inode *ino, struct file *fp, char *buff,
		      int req_len); 
#if LINUX_VERSION_CODE >= KERNEL_VER(1,3,0)
static int  zft_write(struct inode *ino, struct file *fp, const char *buff,
		      int req_len);
#else
static int  zft_write(struct inode *ino, struct file *fp, char *buff,
		      int req_len);
#endif
#endif
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
static loff_t zft_seek(struct file * file, loff_t offset, int origin);
#else
static int zft_seek(struct inode *, struct file *, off_t, int);
#endif

static struct file_operations zft_cdev =
{
#if LINUX_VERSION_CODE >= KERNEL_VER(2,4,0)
	owner:		THIS_MODULE,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
	llseek:		zft_seek,
#else
	lseek:		zft_seek,
#endif
	read:		zft_read,
	write:		zft_write,
	ioctl:		zft_ioctl,
	open:		zft_open,
	release:	zft_close,
};

/*      Open floppy tape device
 */
static int zft_open(struct inode *ino, struct file *filep)
{
	int result;
	sigset_t orig_sigmask;
	int sel = FTAPE_SEL(MINOR(ino->i_rdev));
	TRACE_FUN(ft_t_flow);

	MOD_INC_USE_COUNT; /*  sets MOD_VISITED and MOD_USED_ONCE,
			    *  locking is done with can_unload()
			    */
	TRACE(ft_t_flow, "called for minor %d", MINOR(ino->i_rdev));
	if (busy_flag[sel]) {
		MOD_DEC_USE_COUNT;
		TRACE(ft_t_warn, "failed: already busy");
		TRACE_EXIT -EBUSY;
	}
	busy_flag[sel] = 1;
	if ((MINOR(ino->i_rdev) & ~(ZFT_MINOR_OP_MASK | FTAPE_NO_REWIND))
	     > 
	    FTAPE_SEL_D) {
		busy_flag[sel] = 0;
		MOD_DEC_USE_COUNT; /* unlock module in memory */
		TRACE(ft_t_err, "failed: illegal unit nr");
		TRACE_EXIT -ENXIO;
	}
	ft_sigblockall(&orig_sigmask);
	result = _zft_open(MINOR(ino->i_rdev), filep->f_flags & O_ACCMODE);
	ft_sigrestore(&orig_sigmask); /* restore mask */
	if (result < 0) {
		busy_flag[sel] = 0;
		MOD_DEC_USE_COUNT; /* unlock module in memory */
		TRACE(ft_t_err, "_zft_open failed");
		TRACE_EXIT result;
	}
	if (zft_dirty(zftapes[sel])) { /* was already locked */
		MOD_DEC_USE_COUNT;
#if LINUX_VERSION_CODE < KERNEL_VER(2,1,50)
#if 1 || ZFT_PARANOID
		if (!MOD_IN_USE) {
			TRACE(ft_t_err, "Geeh! Use count is 0!: 0x%08x",
			      mod_use_count_);

		}
#endif
#endif
	}
	TRACE_EXIT 0;
}

/*      Close floppy tape device
 */
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,31)
static int zft_close(struct inode *ino, struct file *filep)
#else
static void zft_close(struct inode *ino, struct file *filep)
#endif
{
	sigset_t old_sigset;
	int result;
	int sel = FTAPE_SEL(MINOR(ino->i_rdev));
	zftape_info_t *zftape = zftapes[sel];
	TRACE_FUN(ft_t_flow);

	if (!busy_flag[sel] || !zftape || MINOR(ino->i_rdev) != zftape->unit) {
		TRACE(ft_t_err, "failed: not busy or wrong unit");
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,31)
		TRACE_EXIT 0;
#else
		TRACE_EXIT; /* keep busy_flag !! */
#endif
	}
	ft_sigblockall(&old_sigset);
	result = _zft_close(zftape, ino->i_rdev);
	ft_sigrestore(&old_sigset);
	if (result < 0) {
		TRACE(ft_t_err, "_zft_close failed: %d", result);
	}
#if defined(MODULE) && LINUX_VERSION_CODE < KERNEL_VER(2,1,18)
	if (!zft_dirty(zftape)) {
		MOD_DEC_USE_COUNT; /* unlock module in memory */
	}
#endif
	busy_flag[sel] = 0;
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,31)
	TRACE_EXIT 0;
#else
	TRACE_EXIT;
#endif
}

/*      Ioctl for floppy tape device
 */
static int zft_ioctl(struct inode *ino, struct file *filep,
		     unsigned int command, unsigned long arg)
{
	int result = -EIO;
	sigset_t old_sigmask;
	int sel = FTAPE_SEL(MINOR(ino->i_rdev));
	zftape_info_t *zftape = zftapes[sel];
	TRACE_FUN(ft_t_flow);

	if (!busy_flag[sel] ||
	    !zftape || MINOR(ino->i_rdev) != zftape->unit ||
	    !zftape->ftape || zftape->ftape->failure) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "failed: not busy, failure or wrong unit");
	}
	ft_sigblockall(&old_sigmask); /* save mask */
	/* This will work as long as sizeof(void *) == sizeof(long) */
	result = _zft_ioctl(zftape, command, (void *) arg);
	ft_sigrestore(&old_sigmask); /* restore mask */
	TRACE_EXIT result;
}

/*      Read from floppy tape device
 */
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
static ssize_t zft_read(struct file *fp, char *buff,
			size_t req_len, loff_t *ppos)
#elif LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long zft_read(struct inode *ino, struct file *fp, char *buff,
		     unsigned long req_len)
#else
static int  zft_read(struct inode *ino, struct file *fp, char *buff,
		     int req_len)
#endif
{
	int result = -EIO;
	sigset_t old_sigmask;
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
	struct inode *ino = fp->f_dentry->d_inode;
#endif
	int sel = FTAPE_SEL(MINOR(ino->i_rdev));
	zftape_info_t *zftape = zftapes[sel];
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_data_flow, "called with count: %ld", (unsigned long)req_len);

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
	if (ppos != &fp->f_pos) {
		/* "A request was outside the capabilities of the device." */
		TRACE_EXIT -ENXIO;
	}
#endif

	if (!busy_flag[sel] ||
	    !zftape || MINOR(ino->i_rdev) != zftape->unit ||
	    !zftape->ftape || zftape->ftape->failure) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "failed: not busy, failure or wrong unit");
	}
	ft_sigblockall(&old_sigmask); /* save mask */
	result = _zft_read(zftape, buff, req_len);
	ft_sigrestore(&old_sigmask);
	TRACE(ft_t_data_flow, "return with count: %d", result);
	TRACE_EXIT result;
}

/*      Write to tape device
 */
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
static ssize_t zft_write(struct file *fp, const char *buff,
			 size_t req_len, loff_t *ppos)
#elif LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long zft_write(struct inode *ino, struct file *fp, const char *buff,
		      unsigned long req_len)
#elif LINUX_VERSION_CODE >= KERNEL_VER(1,3,0)
static int  zft_write(struct inode *ino, struct file *fp, const char *buff,
		      int req_len)
#else
static int  zft_write(struct inode *ino, struct file *fp, char *buff,
		      int req_len)
#endif
{
	int result = -EIO;
	sigset_t old_sigmask;
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
	struct inode *ino = fp->f_dentry->d_inode;
#endif
	int sel = FTAPE_SEL(MINOR(ino->i_rdev));
	zftape_info_t *zftape = zftapes[sel];
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_data_flow, "called with count: %ld", (unsigned long)req_len);
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
	if (ppos != &fp->f_pos) {
		/* "A request was outside the capabilities of the device." */
		TRACE_EXIT -ENXIO;
	}
#endif
	if (!busy_flag[sel] ||
	    !zftape || MINOR(ino->i_rdev) != zftape->unit ||
	    !zftape->ftape || zftape->ftape->failure) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "failed: not busy, failure or wrong unit");
	}
	ft_sigblockall(&old_sigmask); /* save mask */
	result = _zft_write(zftape, buff, req_len);
	ft_sigrestore(&old_sigmask); /* restore mask */
	TRACE(ft_t_data_flow, "return with count: %d", result);
	TRACE_EXIT result;
}

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,60)
static loff_t zft_seek(struct file * file, loff_t offset, int origin)
#else
static int zft_seek(struct inode * inode, struct file * file, off_t offset, int origin)
#endif
{
        return -ESPIPE;
}



/*                    END OF VFS INTERFACE 
 *          
 *****************************************************************************/

/*  driver/module initialization
 */

#define GLOBAL_TRACING
#include "../lowlevel/ftape-real-tracing.h"

#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
struct zft_cmpr_ops *zft_cmpr_ops = NULL;

/*  the compression module has to call this function to hook into the
 *  zftape code
 */
int zft_cmpr_register(struct zft_cmpr_ops *new_ops)
{
	TRACE_FUN(ft_t_flow);
	
	if (zft_cmpr_ops != NULL) {
		TRACE_EXIT -EBUSY;
	} else {
		zft_cmpr_ops = new_ops;
		TRACE_EXIT 0;
	}
}

struct zft_cmpr_ops *zft_cmpr_unregister(void)
{
	struct zft_cmpr_ops *old_ops = zft_cmpr_ops;
	TRACE_FUN(ft_t_flow);

	zft_cmpr_ops = NULL;
	TRACE_EXIT old_ops;
}

/*  lock the zft-compressor() module.
 */
int zft_cmpr_lock(zftape_info_t *zftape, int try_to_load)
{
	if (zft_cmpr_ops == NULL) {
# ifdef FT_MODULE_AUTOLOAD
		if (try_to_load) {
			request_module("zft-compressor");
			if (zft_cmpr_ops == NULL) {
				return -ENOSYS;
			}
		} else {
			return -ENOSYS;
		}
# else
		return -ENOSYS;
# endif
	}
	zftape->cmpr_handle = (*zft_cmpr_ops->lock)(zftape);
	return 0;
}

# ifdef CONFIG_ZFT_COMPRESSOR
extern int zft_compressor_init(void);
# endif
#endif /* CONFIG_ZFT_COMPRESSOR || CONFIG_ZFT_COMPRESSOR_MODULE */

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

#if 0 /* DevFS support disabled for modern kernels */
static int __init zft_devfs_register(void)
{
#if LINUX_VERSION_CODE < KERNEL_VER(2,3,46)
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(register_chrdev(ft_major_device_number,
				    "zft", &zft_cdev),);
#else
	int sel;
	TRACE_FUN(ft_t_flow);
	
	TRACE_CATCH(register_chrdev(ft_major_device_number,
					  "zft", &zft_cdev),);
	ftape_devfs_handle = devfs_mk_dir(NULL, "ftape", NULL);

	for (sel = 0; sel < 4; sel++) {
		char tmpname[40];

		sprintf(tmpname, "%d", sel);
		devfs_handle[sel] = devfs_mk_dir(ftape_devfs_handle,
						 tmpname, NULL);
		
		devfs_q[sel] = devfs_register(devfs_handle[sel], "mt",
					      DEVFS_FL_DEFAULT,
					      ft_major_device_number,
					      sel,
					      S_IFCHR | S_IRUSR | S_IWUSR,
					      &zft_cdev, NULL);
		devfs_qn[sel] = devfs_register(devfs_handle[sel], "mtn",
					       DEVFS_FL_DEFAULT,
					       ft_major_device_number,
					       sel|FTAPE_NO_REWIND ,
					       S_IFCHR | S_IRUSR | S_IWUSR,
					       &zft_cdev, NULL);

# if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
		devfs_zq[sel] =
			devfs_register(devfs_handle[sel], "mtz",
				       DEVFS_FL_DEFAULT,
				       ft_major_device_number,
				       sel|ZFT_ZIP_MODE,
				       S_IFCHR | S_IRUSR | S_IWUSR,
				       &zft_cdev, NULL);
		devfs_zqn[sel] =
			devfs_register(devfs_handle[sel], "mtzn",
				       DEVFS_FL_DEFAULT,
				       ft_major_device_number,
				       sel|ZFT_ZIP_MODE|FTAPE_NO_REWIND,
				       S_IFCHR | S_IRUSR | S_IWUSR,
				       &zft_cdev, NULL);
# endif
# if CONFIG_ZFT_OBSOLETE
		devfs_raw[sel] =
			devfs_register(devfs_handle[sel], "mtr",
				       DEVFS_FL_DEFAULT,
				       ft_major_device_number,
				       sel|ZFT_RAW_MODE,
				       S_IFCHR | S_IRUSR | S_IWUSR,
				       &zft_cdev, NULL);
		devfs_rawn[sel] =
			devfs_register(devfs_handle[sel], "mtrn",
				       DEVFS_FL_DEFAULT,
				       ft_major_device_number,
				       sel|ZFT_RAW_MODE|FTAPE_NO_REWIND,
				       S_IFCHR | S_IRUSR | S_IWUSR,
				       &zft_cdev, NULL);
# endif
		devfs_register_tape(devfs_q[sel]);
	}
#endif
	TRACE_EXIT 0;
}
#endif /* DevFS disabled */

/*  Called by modules package when installing the driver or by kernel
 *  during the initialization phase
 */
int __init zft_init(void)
{
	TRACE_FUN(ft_t_flow);

#ifdef MODULE
	printk(KERN_INFO ZFTAPE_VERSION "\n");
        if (TRACE_LEVEL >= ft_t_info) {
		printk(
KERN_INFO
"(c) 1996-2000 Claus-Justus Heine <heine@instmath.rwth-aachen.de>\n"
KERN_INFO
"vfs interface for ftape floppy tape driver.\n"
KERN_INFO
"Support for QIC-113 compatible volume table.\n"
KERN_INFO
"Compiled for Linux version %s"
#ifdef MODVERSIONS
		       " with versioned symbols"
#endif
		       "\n", UTS_RELEASE);
        }
#else /* !MODULE */
	/* print a short no-nonsense boot message */
	printk(KERN_INFO ZFTAPE_VERSION " for Linux " UTS_RELEASE "\n");
#endif /* MODULE */
	TRACE(ft_t_info, "zft_init @ 0x%p", zft_init);
	TRACE(ft_t_info,
	      "installing zftape VFS interface for ftape driver ...");
	/* DevFS disabled - register character device directly */
	TRACE_CATCH(register_chrdev(ft_major_device_number, "zft", &zft_cdev),);

#ifdef CONFIG_ZFT_COMPRESSOR
	(void)zft_compressor_init();
#elif defined(CONFIG_ZFT_COMPRESSOR_MODULE)
# if LINUX_VERSION_CODE >= KERNEL_VER(1,2,0) &&\
     LINUX_VERSION_CODE < KERNEL_VER(2,1,18)
	register_symtab(&zft_symbol_table); /* add global zftape symbols */
# endif
#endif /* CONFIG_ZFT_COMPRESSOR */

	TRACE_EXIT 0;
}

#undef FT_TRACE_ATTR
#define FT_TRACE_ATTR /**/

#ifdef MODULE
# if LINUX_VERSION_CODE <= KERNEL_VER(1,2,13) && defined(MODULE)
char kernel_version[] = UTS_RELEASE;
# endif
# if LINUX_VERSION_CODE >= KERNEL_VER(2,1,18)
/* Called by modules package before trying to unload the module
 */
static int can_unload(void)
{
	int sel;
	TRACE_FUN(ft_t_flow);

	for (sel = 0; sel < 4; sel++) {
		if ((zftapes[sel] && zft_dirty(zftapes[sel])) || busy_flag[sel]) {
			TRACE(ft_t_flow, "unit %d is busy", sel);
			TRACE_EXIT -EBUSY;
		}
	}
	TRACE_EXIT 0;
}
# endif
/* Called by modules package when installing the driver
 */
int init_module(void)
{
# if !defined(CONFIG_ZFT_COMPRESSOR_MODULE)
#  if LINUX_VERSION_CODE >= KERNEL_VER(1,1,85) && \
     LINUX_VERSION_CODE < KERNEL_VER(2,1,18)
	register_symtab(0); /* remove global ftape symbols */
#  else
	EXPORT_NO_SYMBOLS;
#  endif
# endif /* CONFIG_ZFT_COMPRESSOR_MODULE */
# if LINUX_VERSION_CODE >= KERNEL_VER(2,1,18)
	if (!mod_member_present(&__this_module, can_unload)) {
		return -EBUSY;
	}
	__this_module.can_unload = can_unload;
# endif
	return zft_init();
}

/* Called by modules package when removing the driver 
 */
void cleanup_module(void)
{
	int sel;
	TRACE_FUN(ft_t_flow);

# if LINUX_VERSION_CODE >= KERNEL_VER(2,3,46)
	if (devfs_unregister_chrdev(ft_major_device_number, "zft") != 0) {
		TRACE(ft_t_warn, "failed");
	} else {
		TRACE(ft_t_info, "successful");
	}
# else
	if (unregister_chrdev(ft_major_device_number, "zft") != 0) {
		TRACE(ft_t_warn, "failed");
	} else {
		TRACE(ft_t_info, "successful");
	}
# endif
	for (sel = 0; sel < 4; sel++) {
		if (zftapes[sel]) {
			/* release remaining memory, if any */
			zft_uninit_mem(zftapes[sel]);
			ftape_kfree(FTAPE_SEL(sel), &zftapes[sel], sizeof(*zftapes[sel]));
		}
#if 0 /* DevFS cleanup disabled */
# if LINUX_VERSION_CODE >= KERNEL_VER(2,3,46)
		devfs_unregister (devfs_q[sel]);
		devfs_q[sel] = NULL;
		devfs_unregister (devfs_qn[sel]);
		devfs_qn[sel] = NULL;
#  if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
		devfs_unregister(devfs_zq[sel]);
		devfs_zq[sel] = NULL;
		devfs_unregister(devfs_zqn[sel]);
		devfs_zqn[sel] = NULL;
#  endif
#  ifdef CONFIG_ZFT_OBSOLETE
		devfs_unregister(devfs_raw[sel]);
		devfs_raw[sel] = NULL;
		devfs_unregister(devfs_rawn[sel]);
		devfs_rawn[sel] = NULL;
#endif
		devfs_unregister(devfs_handle[sel]);
		devfs_handle[sel] = NULL;
# endif
	}
# if LINUX_VERSION_CODE >= KERNEL_VER(2,3,46)
	devfs_unregister(ftape_devfs_handle);
	ftape_devfs_handle = NULL;
# endif
#endif /* DevFS cleanup disabled */
        printk(KERN_INFO "zftape successfully unloaded.\n");
	TRACE_EXIT;
}

MODULE_LICENSE("GPL");

#endif /* MODULE */
