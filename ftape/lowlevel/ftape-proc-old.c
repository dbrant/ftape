/*
 *      Copyright (C) 1997-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-proc-old.c,v $
 * $Revision: 1.1 $
 * $Date: 2000/06/23 12:11:21 $
 *
 *      This file contains the procfs interface for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */



#if defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS)

/*
 *  /proc/ftape layout:
 *
 *
 *                           ftape
 *                         /  | | \______
 *                        /   | |  \     \
 *                       /   /  \   \     \
 *                      0   1   2    3     driver
 *                      |                      \____________
 *              ________|_____________                      \________
 *             /    |   |      |      \                     |   |    \
 *       history   fdc drive  cart.   fdc-driver        version |  fdc-drivers
 *                                                              |
 *                                                             memory
 *
 */


#include <linux/proc_fs.h>

#include <linux/ftape.h>
#if LINUX_VERSION_CODE <= KERNEL_VER(1,2,13) /* bail out */
#error \
Please disable CONFIG_FT_PROC_FS in "MCONFIG" or upgrade to a newer kernel!
#endif
#include <linux/qic117.h>

#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-proc-old.h"
#include "ftape-tracing.h"
#include "ftape-buffer.h"
#include "ftape-init.h"
#include "fdc-io.h"

#if LINUX_VERSION_CODE < KERNEL_VER(2,1,28)

#include <asm/segment.h> /* for memcpy_tofs() */

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long ftape_proc_read(struct inode* inode, struct file* file,
			    char* buf, unsigned long count);
#else
static int ftape_proc_read(struct inode* inode, struct file* file,
			   char* buf, int count);
#endif

#define FT_PROC_REGISTER(parent, child) proc_register_dynamic(parent, child)

static struct file_operations ftape_proc_fops =
{
	NULL,			/* lseek   */
	ftape_proc_read,	/* read	   */
	NULL,			/* write   */
	NULL,			/* readdir */
	NULL,			/* select  */
	NULL,			/* ioctl   */
	NULL,			/* mmap	   */
	NULL,			/* no special open code	   */
	NULL,			/* no special release code */
	NULL,			/* can't fsync */
};

static struct inode_operations ftape_proc_inode_operations =
{
	&ftape_proc_fops,
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
};

/*
 * Proc filesystem directory entries.
 */

static struct proc_dir_entry proc_ftape =
{
        0,                           /* low_ino */
        sizeof("ftape") - 1,         /* namelen */
        "ftape",                     /* name */
        S_IFDIR | S_IRUGO | S_IXUGO, /* mode */
        2,                           /* nlink */
        0,                           /* uid */
        0,                           /* gid */
        0,                           /* size */
        NULL,                        /* ops */
        NULL,                        /* get_info */
        NULL,                        /* fill_inode */
        NULL,                        /* next */
        NULL,                        /* parent */
        NULL,                        /* subdir */
        NULL,                        /* data */
};

#define FTAPE_PROC_DIR_ENTRY(name)			\
static struct proc_dir_entry proc_ft_##name =	\
{							\
        0,                           /* low_ino */	\
        sizeof(#name) - 1,           /* namelen */	\
        #name,                       /* name */		\
        S_IFDIR | S_IRUGO | S_IXUGO, /* mode */		\
        2,                           /* nlink */	\
        0,                           /* uid */		\
        0,                           /* gid */		\
        0,                           /* size */		\
        NULL,                        /* ops */		\
        NULL,                        /* get_info */	\
        NULL,                        /* fill_inode */	\
        NULL,                        /* next */		\
        NULL,                        /* parent */	\
        NULL,                        /* subdir */	\
        NULL,                        /* data */		\
};

#define FTAPE_PROC_ENTRY(dir, name)						\
static int ftape_get_info_##dir ## _ ##name(char *page, char **start,		\
                                            off_t off, int count, int dummy)	\
{										\
        int dummy_eof;								\
										\
        return ft_read_proc_##dir ## _ ##name(page, start, off, count,		\
                                   &dummy_eof, NULL);				\
}										\
static struct proc_dir_entry proc_ft_##dir ## _ ##name = {			\
        0,                   /* low_ino    */					\
        sizeof(#name)-1,     /* namelen    */					\
        #name,               /* name       */					\
        S_IFREG | S_IRUGO,   /* mode       */					\
        1,                   /* nlink      */					\
        0,                   /* uid        */					\
        0,                   /* gid        */					\
        0,                   /* size       */					\
        &ftape_proc_inode_operations, /* ops */					\
        ftape_get_info_##dir ## _ ##name, /* get_info   */			\
        NULL,                /* fill_inode */					\
        NULL,                /* next       */					\
        NULL,                /* parent     */					\
        NULL,                /* subdir     */					\
        NULL                 /* data       */					\
}

/*  Read ftape proc directory entry.
 */

#define PROC_BLOCK_SIZE	PAGE_SIZE

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,0)
static long ftape_proc_read(struct inode * inode, struct file * file,
			    char * buf, unsigned long nbytes)
#else
static int ftape_proc_read(struct inode * inode, struct file * file,
			   char * buf, int nbytes)
#endif
{
	char 	*page;
	int	retval=0;
	int	eof=0;
	int	n, count;
	char	*start;
	struct proc_dir_entry * dp;

	if (nbytes < 0)
		return -EINVAL;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	while ((nbytes > 0) && !eof)
	{
		count = PROC_BLOCK_SIZE <= nbytes ? PROC_BLOCK_SIZE : nbytes;

		start = NULL;
		if (dp->get_info) {
			/*
			 * Handle backwards compatibility with the old net
			 * routines.
			 * 
			 * XXX What gives with the file->f_flags & O_ACCMODE
			 * test?  Seems stupid to me....
			 */
			n = dp->get_info(page, &start, file->f_pos, count,
				 (file->f_flags & O_ACCMODE) == O_RDWR);
			if (n < count)
				eof = 1;
		} else
			break;
			
		if (!start) {
			/*
			 * For proc files that are less than 4k
			 */
			start = page + file->f_pos;
			n -= file->f_pos;
			if (n <= 0)
				break;
			if (n > count)
				n = count;
		}
		if (n == 0)
			break;	/* End of file */
		if (n < 0) {
			if (retval == 0)
				retval = n;
			break;
		}
#if LINUX_VERSION_CODE > KERNEL_VER(2,1,3)
		copy_to_user(buf, start, n);
#else
		memcpy_tofs(buf, start, n);
#endif
		file->f_pos += n;	/* Move down the file */
		nbytes -= n;
		buf += n;
		retval += n;
	}
	free_page((unsigned long) page);
	return retval;
}

#else /* LINUX_VERSION_CODE < KERNEL_VER(2,1,28) */

#define FT_PROC_REGISTER(parent, child) proc_register(parent, child)

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,58)
#define __NO_VERSION__
#include <linux/module.h>
/*
 * This is called as the fill_inode function when an inode
 * is going into (fill = 1) or out of service (fill = 0).
 * We use it here to manage the module use counts.
 *
 * Note: only the top-level directory needs to do this; if
 * a lower level is referenced, the parent will be as well.
 */
static void proc_ftape_fill_inode(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill) {
		MOD_INC_USE_COUNT;
	} else {
		MOD_DEC_USE_COUNT;
	}
#endif
}
#else
#define proc_ftape_fill_inode NULL
#endif
/*
 * Proc filesystem directory entries.
 */

static struct proc_dir_entry proc_ftape =
{
        0,                           /* low_ino */
        sizeof("ftape") - 1,         /* namelen */
        "ftape",                     /* name */
        S_IFDIR | S_IRUGO | S_IXUGO, /* mode */
        2,                           /* nlink */
        0,                           /* uid */
        0,                           /* gid */
        0,                           /* size */
        NULL,                        /* ops */
        NULL,                        /* get_info */
        proc_ftape_fill_inode,       /* fill_inode */
        NULL,                        /* next */
        NULL,                        /* parent */
        NULL,                        /* subdir */
        NULL,                        /* data */
        NULL,                        /* read_proc  */
        NULL                         /* write_proc */
};

#define FTAPE_PROC_DIR_ENTRY(name)			\
static struct proc_dir_entry proc_ft_##name =	\
{							\
        0,                           /* low_ino */	\
        sizeof(#name) - 1,           /* namelen */	\
        #name,                       /* name */		\
        S_IFDIR | S_IRUGO | S_IXUGO, /* mode */		\
        2,                           /* nlink */	\
        0,                           /* uid */		\
        0,                           /* gid */		\
        0,                           /* size */		\
        NULL,                        /* ops */		\
        NULL,                        /* get_info */	\
        NULL,                        /* fill_inode */	\
        NULL,                        /* next */		\
        NULL,                        /* parent */	\
        NULL,                        /* subdir */	\
        NULL,                        /* data */		\
        NULL,                        /* read_proc  */	\
        NULL                         /* write_proc */	\
};

#define FTAPE_PROC_ENTRY(dir, name)			\
static struct proc_dir_entry proc_ft_ ##dir ## _ ##name =	\
{							 \
        0,                         /* low_ino    */	\
        sizeof(#name)-1,           /* namelen    */	\
        #name,                     /* name       */	\
        S_IFREG | S_IRUGO,         /* mode       */	\
        1,                         /* nlink      */	\
        0,                         /* uid        */	\
        0,                         /* gid        */	\
        0,                         /* size       */	\
        NULL,                      /* ops        */	\
        NULL,                      /* get_info   */	\
        NULL,                      /* fill_inode */	\
        NULL,                      /* next       */	\
        NULL,                      /* parent     */	\
        NULL,                      /* subdir     */	\
        NULL,                      /* data       */	\
        ft_read_proc_##dir ## _ ##name, /* read_proc  */	\
        NULL                       /* write_proc */	\
}

#endif

int ftape_read_proc_tapedrive(int sel, char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	ftape_info_t *ftape = ftapes[sel];
	char *ptr = page;
	size_t len;
	
	*start = 0;
	if (!ftape || ftape->init_drive_needed) {
		len = sprintf(ptr, "\nUNINITIALIZED\n");
		goto out;
	} 
	len = sprintf(ptr,
		      "vendor id : 0x%04x\n"
		      "drive name: %s\n"
		      "wind speed: %d ips\n"
		      "wakeup    : %s\n"
		      "max. rate : %d kbit/sec\n"
		      "used rate : %d kbit/sec\n",
		      ftape->drive_type.vendor_id,
		      ftape->drive_type.name,
		      ftape->drive_type.speed,
		      ((ftape->drive_type.wake_up == no_wake_up)
		       ? "No wakeup needed" :
		       ((ftape->drive_type.wake_up == wake_up_colorado)
			? "Colorado" :
			((ftape->drive_type.wake_up == wake_up_mountain)
			 ? "Mountain" :
			 ((ftape->drive_type.wake_up == wake_up_insight)
			  ? "Motor on" :
			   "Unknown")))),
		      ftape->drive_max_rate, ftape->data_rate);
out:
	if (off + count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	} 
	return len;
}

int ftape_read_proc_cartridge(int sel, char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	ftape_info_t *ftape = ftapes[sel];
	char *ptr = page;
	size_t len;
	
	*start = 0;
	if (!ftape || ftape->init_drive_needed) {
		len = sprintf(ptr, "\nUNINITIALIZED\n");
		goto out;
	} 
	if (ftape->no_tape) {
		len = sprintf(ptr, "no cartridge inserted\n");
		goto out;
	}
	if (!ftape->formatted) {
		len = sprintf(ptr, "cartridge not formatted\n");
		goto out;
	}
	if (ftape->qic_std == QIC_UNKNOWN) {
		len = sprintf(ptr, "unknown tape format\n");
		goto out;
	}
	len = sprintf(ptr,
		      "segments  : %5d\n"
		      "tracks    : %5d\n"
		      "length    : %5dft\n"
		      "formatted : %3s\n"
		      "writable  : %3s\n"
		      "QIC spec. : %s\n"
		      "fmt-code  : %1d\n",
		      ftape->segments_per_track,
		      ftape->tracks_per_tape,
		      ftape->tape_len,
		      (ftape->formatted == 1) ? "yes" : "no",
		      (ftape->write_protected == 1) ? "no" : "yes",
		      ((ftape->qic_std == QIC_40) ? "QIC-40" :
		       ((ftape->qic_std == QIC_80) ? "QIC-80" :
			((ftape->qic_std == QIC_3010) ? "QIC-3010" :
			 ((ftape->qic_std == QIC_3020) ? "QIC-3020" :
			  ((ftape->qic_std == DITTO_MAX) ? "Ditto Max" :
			   "???"))))),
		      ftape->format_code);
	
out:
	if (off + count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	} 
	return len;
}

int ftape_read_proc_controller(int sel, char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	const char  *fdc_name[] = { "no fdc",
				    "i8272",
				    "i82077",
				    "i82077AA",
				    "Colorado FC-10 or FC-20",
				    "i82078",
				    "i82078_1",
				    "Iomega Dash EZ"};
	fdc_info_t *fdc = fdc_infos[sel];
	char *ptr = page;
	size_t len;
	
	*start = 0;
	if (!fdc) {
		len = sprintf(ptr, "\nUNINITIALIZED\n");
		goto out;
	} 
	if (!fdc->initialized) {
		len = sprintf(ptr,
			      "FDC_driver: %s -- %s\n"
			      "FDC type  : * uninitialized *\n",
			      fdc->driver ? fdc->driver : "none",
			      (fdc->ops && fdc->ops->description) ?
			      fdc->ops->description : "description unavailable");
		goto out;
	}
	len = sprintf(ptr,
		      "FDC_driver: %s -- %s\n"
		      "FDC type  : %s\n"
		      "FDC base  : 0x%03x\n"
		      "FDC irq   : %d\n"
		      "FDC dma   : %d\n"
		      "FDC thr.  : %d\n"
		      "max. rate : %d kbit/sec\n"
		      "rate limit: %d kbit/sec\n"
		      "used rate : %d kbit/sec\n",
		      fdc->driver ? fdc->driver : "none",
		      (fdc->ops && fdc->ops->description) ?
		      fdc->ops->description : "description unavailable",
		      fdc_name[fdc->type],
		      fdc->sra, fdc->irq, fdc->dma,
		      fdc->threshold,
		      fdc->max_rate, fdc->rate_limit,
		      fdc->data_rate);
out:
	if (off + count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	} 
	return len;
}

int ftape_read_proc_history(int sel, char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	ftape_info_t *ftape = ftapes[sel];
	char *ptr = page;
	size_t len;
	
	*start = 0;
	if (!ftape) {
		len = sprintf(ptr, "\nUNINITIALIZED\n");
		goto out;
	} 
	ptr += sprintf(ptr,
		       "FDC isr statistics\n"
		       " id_am_errors     : %3d\n"
		       " id_crc_errors    : %3d\n"
		       " data_am_errors   : %3d\n"
		       " data_crc_errors  : %3d\n"
		       " overrun_errors   : %3d\n"
		       " no_data_errors   : %3d\n"
		       " retries          : %3d\n\n",
		       ftape->history.id_am_errors,
		       ftape->history.id_crc_errors,
		       ftape->history.data_am_errors,
		       ftape->history.data_crc_errors,
		       ftape->history.overrun_errors,
		       ftape->history.no_data_errors,
		       ftape->history.retries);
	ptr += sprintf(ptr,
		       "ECC statistics\n"
		       " crc_errors       : %3d\n"
		       " crc_failures     : %3d\n"
		       " ecc_failures     : %3d\n"
		       " sectors corrected: %3d\n\n",
		       ftape->history.crc_errors,
		       ftape->history.crc_failures,
		       ftape->history.ecc_failures,
		       ftape->history.corrected);
	ptr += sprintf(ptr,
		       "tape quality statistics\n"
		       " media defects    : %3d\n\n",
		       ftape->history.defects);
	ptr += sprintf(ptr,
		       "tape motion statistics\n"
		       " repositions      : %3d\n\n",
		       ftape->history.rewinds);

	len = strlen(page);
out:
	if (off+count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	}
	return len;
}

#define FT_READ_PROC_HISTORY(sel)							\
static int ft_read_proc_##sel ## _history(char *page, char **start, off_t off,		\
			      int count, int *eof, void *data)				\
{											\
	return ftape_read_proc_history(##sel, page, start, off, count, eof, data);	\
}
#define FT_READ_PROC_CONTROLLER(sel)							\
static int ft_read_proc_##sel ## _controller(char *page, char **start, off_t off,		\
			      int count, int *eof, void *data)				\
{											\
	return ftape_read_proc_controller(##sel, page, start, off, count, eof, data);	\
}
#define FT_READ_PROC_TAPEDRIVE(sel)							\
static int ft_read_proc_##sel ## _tapedrive(char *page, char **start, off_t off,		\
			      int count, int *eof, void *data)				\
{											\
	return ftape_read_proc_tapedrive(##sel, page, start, off, count, eof, data);	\
}
#define FT_READ_PROC_CARTRIDGE(sel)							\
static int ft_read_proc_##sel ## _cartridge(char *page, char **start, off_t off,		\
			      int count, int *eof, void *data)				\
{											\
	return ftape_read_proc_cartridge(##sel, page, start, off, count, eof, data);	\
}

static int ft_read_proc_driver_version(char *page, char **start, off_t off,
				       int count, int *eof, void *data)
{
	size_t len;

	len = sprintf(page, FTAPE_BANNER);
	*start = 0;
	if (off+count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	}
	return len;
}

static int ft_read_proc_driver_memory(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	size_t len;
	
	len = sprintf(page,
		      "\n"
		      "              kmalloc\n"		      
		      "\n"
		      "        driver    qft0    qft1    qft2    qft3    total\n"
		      "-------------------------------------------------------\n"
		      "used:  %7d %7d %7d %7d %7d  %7d \n"
		      "peak:  %7d %7d %7d %7d %7d  %7d \n",
		      ft_k_used_memory[4], ft_k_used_memory[0],
		      ft_k_used_memory[1], ft_k_used_memory[2],
		      ft_k_used_memory[3],
		      (ft_k_used_memory[4] + ft_k_used_memory[0] +
		       ft_k_used_memory[1] + ft_k_used_memory[2] +
		       ft_k_used_memory[3]),
		      ft_k_peak_memory[4],
		      ft_k_peak_memory[0], ft_k_peak_memory[1],
		      ft_k_peak_memory[2], ft_k_peak_memory[3],
		      (ft_k_peak_memory[4] + ft_k_peak_memory[0] +
		       ft_k_peak_memory[1] + ft_k_peak_memory[2] +
		       ft_k_peak_memory[3]));
	len += sprintf(page + len,
		       "\n"
		       "              vmalloc\n"
		       "\n"
		       "        driver    qft0    qft1    qft2    qft3    total\n"
		       "-------------------------------------------------------\n"
		       "used:  %7d %7d %7d %7d %7d  %7d \n"
		       "peak:  %7d %7d %7d %7d %7d  %7d \n",
		       ft_v_used_memory[4], ft_v_used_memory[0],
		       ft_v_used_memory[1], ft_v_used_memory[2],
		       ft_v_used_memory[3],
		       (ft_v_used_memory[4] + ft_v_used_memory[0] +
			ft_v_used_memory[1] + ft_v_used_memory[2] +
			ft_v_used_memory[3]),
		       ft_v_peak_memory[4],
		       ft_v_peak_memory[0], ft_v_peak_memory[1],
		       ft_v_peak_memory[2], ft_v_peak_memory[3],
		       (ft_v_peak_memory[4] + ft_v_peak_memory[0] +
			ft_v_peak_memory[1] + ft_v_peak_memory[2] +
			ft_v_peak_memory[3]));
	len += sprintf(page + len,
		       "\n"
		       "              dma memory\n"
		       "\n"
		       "           qft0    qft1    qft2    qft3    total\n"
		       "------------------------------------------------\n"
		       "used:   %7d %7d %7d %7d  %7d\n"
		       "peak:   %7d %7d %7d %7d  %7d\n"
		       "\n",
		       (fdc_infos[0] ? fdc_infos[0]->used_dma_memory : 0),
		       (fdc_infos[1] ? fdc_infos[1]->used_dma_memory : 0),
		       (fdc_infos[2] ? fdc_infos[2]->used_dma_memory : 0),
		       (fdc_infos[3] ? fdc_infos[3]->used_dma_memory : 0),
		       (fdc_infos[0] ? fdc_infos[0]->used_dma_memory : 0) +
		       (fdc_infos[1] ? fdc_infos[1]->used_dma_memory : 0) +
		       (fdc_infos[2] ? fdc_infos[2]->used_dma_memory : 0) +
		       (fdc_infos[3] ? fdc_infos[3]->used_dma_memory : 0),
		       (fdc_infos[0] ? fdc_infos[0]->peak_dma_memory : 0),
		       (fdc_infos[1] ? fdc_infos[1]->peak_dma_memory : 0),
		       (fdc_infos[2] ? fdc_infos[2]->peak_dma_memory : 0),
		       (fdc_infos[3] ? fdc_infos[3]->peak_dma_memory : 0),
		       (fdc_infos[0] ? fdc_infos[0]->peak_dma_memory : 0) +
		       (fdc_infos[1] ? fdc_infos[1]->peak_dma_memory : 0) +
		       (fdc_infos[2] ? fdc_infos[2]->peak_dma_memory : 0) +
		       (fdc_infos[3] ? fdc_infos[3]->peak_dma_memory : 0));
	*start = 0;
	if (off+count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	}
	return len;
}

static int ft_read_proc_driver_drivers(char *page, char **start, off_t off,
				       int count, int *eof, void *data)
{
	extern struct list_head fdc_drivers;
	int    cnt;
	size_t len;
	struct list_head *tmp;

	len = sprintf(page, "Lowlevel fdc drivers registered with ftape\n\n");
	for (tmp = fdc_drivers.next; tmp != &fdc_drivers; tmp = tmp->next) {
		fdc_operations *ops = list_entry(tmp, fdc_operations, node);
		
		len += sprintf(page + len, "%s -- %s\n",
			       ops->driver, ops->description);
	}

	for (cnt = 0; cnt <= FTAPE_SEL_D; cnt++) {
		char *ptr;
		int i;

		len += sprintf(page + len,
			       "\nDrivers registered for device %d: ", cnt);
		for (i = 0, ptr = ft_fdc_driver[cnt];
		     i < ft_fdc_driver_no[cnt];
		     i++, ptr += strlen(ptr) + 1) {
			len += sprintf(page + len, "%s ", ptr);
		}
		len += sprintf(page + len, "\n");
	}
	
	*start = 0;
	if (off+count >= len) {
		*eof = 1;
	} else {
		*eof = 0;
	}
	return len;
}

FT_READ_PROC_HISTORY(0)
FT_READ_PROC_CONTROLLER(0)
FT_READ_PROC_TAPEDRIVE(0)
FT_READ_PROC_CARTRIDGE(0)

FT_READ_PROC_HISTORY(1)
FT_READ_PROC_CONTROLLER(1)
FT_READ_PROC_TAPEDRIVE(1)
FT_READ_PROC_CARTRIDGE(1)

FT_READ_PROC_HISTORY(2)
FT_READ_PROC_CONTROLLER(2)
FT_READ_PROC_TAPEDRIVE(2)
FT_READ_PROC_CARTRIDGE(2)

FT_READ_PROC_HISTORY(3)
FT_READ_PROC_CONTROLLER(3)
FT_READ_PROC_TAPEDRIVE(3)
FT_READ_PROC_CARTRIDGE(3)

FTAPE_PROC_DIR_ENTRY(0);
FTAPE_PROC_DIR_ENTRY(1);
FTAPE_PROC_DIR_ENTRY(2);
FTAPE_PROC_DIR_ENTRY(3);
FTAPE_PROC_DIR_ENTRY(driver);

FTAPE_PROC_ENTRY(0, history);
FTAPE_PROC_ENTRY(0, controller);
FTAPE_PROC_ENTRY(0, tapedrive);
FTAPE_PROC_ENTRY(0, cartridge);

FTAPE_PROC_ENTRY(1, history);
FTAPE_PROC_ENTRY(1, controller);
FTAPE_PROC_ENTRY(1, tapedrive);
FTAPE_PROC_ENTRY(1, cartridge);

FTAPE_PROC_ENTRY(2, history);
FTAPE_PROC_ENTRY(2, controller);
FTAPE_PROC_ENTRY(2, tapedrive);
FTAPE_PROC_ENTRY(2, cartridge);

FTAPE_PROC_ENTRY(3, history);
FTAPE_PROC_ENTRY(3, controller);
FTAPE_PROC_ENTRY(3, tapedrive);
FTAPE_PROC_ENTRY(3, cartridge);

FTAPE_PROC_ENTRY(driver, version);
FTAPE_PROC_ENTRY(driver, memory);
FTAPE_PROC_ENTRY(driver, drivers);

int __init ftape_proc_init(void)
{
#if LINUX_VERSION_CODE < KERNEL_VER(2,1,28) && defined(MODULE)
        proc_ftape.ops =
		proc_ft_0.ops =
		proc_ft_1.ops =
		proc_ft_2.ops =
		proc_ft_3.ops =
		proc_ft_driver.ops = proc_net.ops; /* ugly hack */	
#endif

	if (FT_PROC_REGISTER(&proc_root, &proc_ftape) < 0) {
		return -EIO;
	}
	if (FT_PROC_REGISTER(&proc_ftape, &proc_ft_0) >= 0) {
		FT_PROC_REGISTER(&proc_ft_0, &proc_ft_0_history);
		FT_PROC_REGISTER(&proc_ft_0, &proc_ft_0_controller);
		FT_PROC_REGISTER(&proc_ft_0, &proc_ft_0_tapedrive);
		FT_PROC_REGISTER(&proc_ft_0, &proc_ft_0_cartridge);
	}
	if (FT_PROC_REGISTER(&proc_ftape, &proc_ft_1) >= 0) {
		FT_PROC_REGISTER(&proc_ft_1, &proc_ft_1_history);
		FT_PROC_REGISTER(&proc_ft_1, &proc_ft_1_controller);
		FT_PROC_REGISTER(&proc_ft_1, &proc_ft_1_tapedrive);
		FT_PROC_REGISTER(&proc_ft_1, &proc_ft_1_cartridge);
	}
	if (FT_PROC_REGISTER(&proc_ftape, &proc_ft_2) >= 0) {
		FT_PROC_REGISTER(&proc_ft_2, &proc_ft_2_history);
		FT_PROC_REGISTER(&proc_ft_2, &proc_ft_2_controller);
		FT_PROC_REGISTER(&proc_ft_2, &proc_ft_2_tapedrive);
		FT_PROC_REGISTER(&proc_ft_2, &proc_ft_2_cartridge);
	}
	if (FT_PROC_REGISTER(&proc_ftape, &proc_ft_3) >= 0) {
		FT_PROC_REGISTER(&proc_ft_3, &proc_ft_3_history);
		FT_PROC_REGISTER(&proc_ft_3, &proc_ft_3_controller);
		FT_PROC_REGISTER(&proc_ft_3, &proc_ft_3_tapedrive);
		FT_PROC_REGISTER(&proc_ft_3, &proc_ft_3_cartridge);
	}
	if (FT_PROC_REGISTER(&proc_ftape, &proc_ft_driver) >= 0) {
		FT_PROC_REGISTER(&proc_ft_driver, &proc_ft_driver_version);
		FT_PROC_REGISTER(&proc_ft_driver, &proc_ft_driver_memory);
		FT_PROC_REGISTER(&proc_ft_driver, &proc_ft_driver_drivers);
	}
	return 0;
}

#ifdef MODULE
void ftape_proc_destroy(void)
{
        if (proc_ftape.low_ino != 0) {
		if (proc_ft_0.low_ino != 0) {
			proc_unregister(&proc_ft_0, proc_ft_0_history.low_ino);
			proc_unregister(&proc_ft_0, proc_ft_0_controller.low_ino);
			proc_unregister(&proc_ft_0, proc_ft_0_tapedrive.low_ino);
			proc_unregister(&proc_ft_0, proc_ft_0_cartridge.low_ino);
		}
		proc_unregister(&proc_ftape, proc_ft_0.low_ino);
		if (proc_ft_1.low_ino != 0) {
			proc_unregister(&proc_ft_1, proc_ft_1_history.low_ino);
			proc_unregister(&proc_ft_1, proc_ft_1_controller.low_ino);
			proc_unregister(&proc_ft_1, proc_ft_1_tapedrive.low_ino);
			proc_unregister(&proc_ft_1, proc_ft_1_cartridge.low_ino);
		}
		proc_unregister(&proc_ftape, proc_ft_1.low_ino);
		if (proc_ft_2.low_ino != 0) {
			proc_unregister(&proc_ft_2, proc_ft_2_history.low_ino);
			proc_unregister(&proc_ft_2, proc_ft_2_controller.low_ino);
			proc_unregister(&proc_ft_2, proc_ft_2_tapedrive.low_ino);
			proc_unregister(&proc_ft_2, proc_ft_2_cartridge.low_ino);
		}
		proc_unregister(&proc_ftape, proc_ft_2.low_ino);
		if (proc_ft_3.low_ino != 0) {
			proc_unregister(&proc_ft_3, proc_ft_3_history.low_ino);
			proc_unregister(&proc_ft_3, proc_ft_3_controller.low_ino);
			proc_unregister(&proc_ft_3, proc_ft_3_tapedrive.low_ino);
			proc_unregister(&proc_ft_3, proc_ft_3_cartridge.low_ino);
		}
		proc_unregister(&proc_ftape, proc_ft_3.low_ino);
		if (proc_ft_driver.low_ino != 0) {
			proc_unregister(&proc_ft_driver, proc_ft_driver_memory.low_ino);
			proc_unregister(&proc_ft_driver, proc_ft_driver_version.low_ino);
			proc_unregister(&proc_ft_driver, proc_ft_driver_drivers.low_ino);
		}
		proc_unregister(&proc_ftape, proc_ft_driver.low_ino);
	}
	proc_unregister(&proc_root, proc_ftape.low_ino);
}
#endif

#endif /* defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS) */
