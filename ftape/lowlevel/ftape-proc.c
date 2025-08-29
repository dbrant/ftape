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
 * $RCSfile: ftape-proc.c,v $
 * $Revision: 1.24 $
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

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/proc_fs.h>

#include <linux/ftape.h>
#include <linux/qic117.h>

#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-tracing.h"
#include "ftape-buffer.h"
#include "ftape-init.h"
#include "fdc-io.h"

static int ft_read_proc_tapedrive(char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	unsigned int sel    = (unsigned int)((unsigned long)data & 0xffffffff);
	ftape_info_t *ftape = ftapes[sel];
	char *ptr           = page;
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

static int ft_read_proc_cartridge(char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	unsigned int sel    = (unsigned int)((unsigned long)data & 0xffffffff);
	ftape_info_t *ftape = ftapes[sel];
	char *ptr           = page;
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

static int ft_read_proc_controller(char *page, char **start, off_t off,
				   int count, int *eof, void *data)
{
	const char *fdc_name[] = { "no fdc",
				   "i8272",
				   "i82077",
				   "i82077AA",
				   "Colorado FC-10 or FC-20",
				   "i82078",
				   "i82078_1",
				   "Iomega Dash EZ"};
	unsigned int sel = (unsigned int)((unsigned long)data & 0xffffffff);
	fdc_info_t *fdc  = fdc_infos[sel];
	char *ptr        = page;
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

static int ft_read_proc_history(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	unsigned int sel    = (unsigned int)((unsigned long)data & 0xffffffff);
	ftape_info_t *ftape = ftapes[sel];
	char *ptr           = page;
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

#define FT_CREATE_PROC_READ_ENTRY(sel, name)				\
(void)create_proc_read_entry(#name,					\
			     0, proc_ft_##sel, ft_read_proc_##name,	\
			     (void *)(sel))

static struct proc_dir_entry *proc_ftape;
static struct proc_dir_entry *proc_ft_driver;
static struct proc_dir_entry *proc_ft_sel[4];
static const char *sel_names[4] = { "0", "1", "2", "3" };

int ftape_proc_init(void)
{
	int i;

	if ((proc_ftape = proc_mkdir("ftape", NULL)) == NULL) {
		return -EIO;
	}
	/* proc entry owner is now handled automatically */
	for (i = 0; i < 4; i++) {
		if ((proc_ft_sel[i] = proc_mkdir(sel_names[i], proc_ftape))) {
			(void)create_proc_read_entry("history", 0,
						     proc_ft_sel[i],
						     ft_read_proc_history,
						     (void *)(unsigned long)i);
			(void)create_proc_read_entry("controller", 0,
						     proc_ft_sel[i],
						     ft_read_proc_controller,
						     (void *)(unsigned long)i);
			(void)create_proc_read_entry("tapedrive", 0,
						     proc_ft_sel[i],
						     ft_read_proc_tapedrive,
						     (void *)(unsigned long)i);
			(void)create_proc_read_entry("cartridge", 0,
						     proc_ft_sel[i],
						     ft_read_proc_cartridge,
						     (void *)(unsigned long)i);
		}
	}
	if ((proc_ft_driver = proc_mkdir("driver", proc_ftape)) != NULL) {
		(void)create_proc_read_entry("version",
					     0, proc_ft_driver,
					     ft_read_proc_driver_version,
					     NULL);
		(void)create_proc_read_entry("memory",
					     0, proc_ft_driver,
					     ft_read_proc_driver_memory,
					     NULL);
		(void)create_proc_read_entry("drivers",
					     0, proc_ft_driver,
					     ft_read_proc_driver_drivers,
					     NULL);
	}
	return 0;
}

#ifdef MODULE
void ftape_proc_destroy(void)
{
	int i;

	if (proc_ftape != NULL) {
		if (proc_ft_driver != NULL) {
			remove_proc_entry("driver", proc_ftape);
		}
		for (i = 0; i < 4; i++) {
			remove_proc_entry(sel_names[i], proc_ftape);
		}
		remove_proc_entry("ftape", &proc_root);
	}
}
#endif

#endif /* defined(CONFIG_PROC_FS) && defined(CONFIG_FT_PROC_FS) */
