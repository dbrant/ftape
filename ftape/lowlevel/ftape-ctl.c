/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                    1996-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-ctl.c,v $
 * $Revision: 1.26 $
 * $Date: 1999/03/17 11:19:35 $
 *
 *      This file contains the non-read/write ftape functions for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 *
 */


#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>


#include <linux/ftape.h>
#include <linux/qic117.h>
#include <asm/uaccess.h>
#include <asm/segment.h>
#include <asm/io.h>

#include "ftape-tracing.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "ftape-rw.h"
#include "ftape-bsm.h"
#include "ftape-buffer.h"

/*      Global vars.
 */

/* These data areas hold all needed global variables.  The ftapes[sel]
 * pointers will be passed down through all the function call
 * stack. It need to be global to make the proc interface
 * possible.
 */
ftape_info_t *ftapes[4] = { NULL, };

/*      Local vars.  */

/* rien */

static const vendor_struct vendors[] = QIC117_VENDORS;
static const wakeup_method methods[] = WAKEUP_METHODS;

ftape_info_t *ftape_get_status(int sel)
{
	return ftapes[sel]; /*  maybe return only a copy of it to assure 
			     *  read only access
			     */
}

void ftape_set_status(const ftape_info_t *ftape, int sel)
{
	*ftapes[sel] = *ftape;
}

static int ftape_not_operational(int status)
{
	/* return true if status indicates tape can not be used.
	 */
	return ((status ^ QIC_STATUS_CARTRIDGE_PRESENT) &
		(QIC_STATUS_ERROR |
		 QIC_STATUS_CARTRIDGE_PRESENT |
		 QIC_STATUS_NEW_CARTRIDGE));
}

int ftape_seek_to_eot(ftape_info_t *ftape)
{
	int status;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
	while ((status & QIC_STATUS_AT_EOT) == 0) {
		if (ftape_not_operational(status)) {
			TRACE_EXIT -EIO;
		}
		TRACE_CATCH(ftape_command_wait(ftape, QIC_PHYSICAL_FORWARD,
					       ftape->timeout.rewind,&status),);
	}
	TRACE_EXIT 0;
}

int ftape_seek_to_bot(ftape_info_t *ftape)
{
	int status;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
	while ((status & QIC_STATUS_AT_BOT) == 0) {
		if (ftape_not_operational(status)) {
			TRACE_EXIT -EIO;
		}
		TRACE_CATCH(ftape_command_wait(ftape, QIC_PHYSICAL_REVERSE,
					       ftape->timeout.rewind,&status),);
	}
	TRACE_EXIT 0;
}

static int ftape_new_cartridge(ftape_info_t *ftape)
{
	ftape->location.track = -1; /* force seek on first access */
	ftape_zap_read_buffers(ftape);
	ftape_zap_write_buffers(ftape);
	return 0;
}

int ftape_abort_operation(ftape_info_t *ftape)
{
	int result = 0;
	int status;
	TRACE_FUN(ft_t_flow);

	if (ftape->runner_status == running) {
		TRACE(ft_t_noise, "aborting runner, waiting");
		
		ftape->runner_status = do_abort;
	}
	result = ftape_dumb_stop(ftape);
	if (ftape->runner_status != idle) {
		if (ftape->runner_status == do_abort) {
			TRACE(ft_t_noise, "forcing runner abort");
		}
		TRACE(ft_t_noise, "stopping tape");
		result = ftape_stop_tape(ftape, &status);
		ftape->location.known = 0;
		ftape->runner_status  = idle;
	}
#if 0 || defined(HACK)
	TRACE(ft_t_info, "Resetting FDC and reprogramming FIFO threshold");
	fdc_reset(ftape->fdc);	
	fdc_fifo_threshold(ftape->fdc, ftape->fdc->threshold,
			   NULL, NULL, NULL);
#endif
	ftape_zap_read_buffers(ftape);
	ftape_set_state(ftape, waiting);
	TRACE_EXIT result;
}

static int lookup_vendor_id(unsigned int vendor_id)
{
	int i = 0;

	while (vendors[i].vendor_id != vendor_id) {
		if (++i >= NR_ITEMS(vendors)) {
			return -1;
		}
	}
	return i;
}

static void ftape_detach_drive(ftape_info_t *ftape)
{
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_flow, "disabling tape drive and fdc");
	ftape_put_drive_to_sleep(ftape, ftape->drive_type.wake_up);
	fdc_disable(ftape->fdc);
	ftape->fdc = NULL;
	TRACE_EXIT;
}

static void clear_history(ftape_info_t *ftape)
{
	memset(&ftape->history, 0, sizeof(ftape->history));
}

static int ftape_activate_drive(ftape_info_t *ftape,
				vendor_struct * drive_type)
{
	int result = 0;
	wake_up_types method;
	const ft_trace_t old_tracing = TRACE_LEVEL;
	TRACE_FUN(ft_t_flow);

	/* If we already know the drive type, wake it up.
	 * Else try to find out what kind of drive is attached.
	 */
	if (drive_type->wake_up != unknown_wake_up) {
		TRACE(ft_t_flow, "enabling tape drive and fdc");
		result = ftape_wakeup_drive(ftape, drive_type->wake_up);
		if (result < 0) {
			TRACE(ft_t_err,
			      "known wakeup method failed"
			      "Trying all the (four) others ...");
		} else {
			TRACE_EXIT result;
		}
	}		
	if (TRACE_LEVEL < ft_t_flow) {
		SET_TRACE_LEVEL(ft_t_bug);
	}
	
	/*  Try to awaken the drive using all known methods.
	 *  Lower tracing for a while.
	 */
	for (method=no_wake_up; method < NR_ITEMS(methods); ++method) {
		drive_type->wake_up = method;
		result = ftape_wakeup_drive(ftape, drive_type->wake_up);
		if (result >= 0) {
			TRACE(ft_t_warn, "drive wakeup method: %s",
			      methods[drive_type->wake_up].name);
			break;
		}
	}
	SET_TRACE_LEVEL(old_tracing);
	
	if (method >= NR_ITEMS(methods)) {
		/* no response at all, cannot open this drive */
		drive_type->wake_up = unknown_wake_up;
		TRACE(ft_t_err, "No tape drive found");
		result = -ENODEV;
	}
	TRACE_EXIT result;
}

int ftape_get_drive_status(ftape_info_t *ftape)
{
	int result;
	int status;
	TRACE_FUN(ft_t_flow);

	ftape->no_tape = ftape->write_protected = ftape->new_tape = 0;
	/*    Tape drive is activated now.
	 *    First clear error status if present.
	 */
	do {
		result = ftape_ready_wait(ftape, ftape->timeout.reset, &status);
		if (result < 0) {
			if (result == -ETIME) {
				TRACE(ft_t_err, "ftape_ready_wait timeout");
			} else if (result == -EINTR) {
				TRACE(ft_t_err, "ftape_ready_wait aborted");
			} else {
				TRACE(ft_t_err, "ftape_ready_wait failed");
			}
			TRACE_EXIT -EIO;
		}
		/*  Clear error condition (drive is ready !)
		 */
		if (status & QIC_STATUS_ERROR) {
			unsigned int error;
			qic117_cmd_t command;

			TRACE(ft_t_err, "error status set");
			result = ftape_report_error(ftape, &error, &command, 1);
			if (result < 0) {
				TRACE(ft_t_err,
				      "report_error_code failed: %d", result);
				/* hope it's working next time */
				ftape_reset_drive(ftape);
				TRACE_EXIT -EIO;
			} else if (error != 0) {
				TRACE(ft_t_noise, "error code   : %d", error);
				TRACE(ft_t_noise, "error command: %d", command);
			}
		}
		if (status & QIC_STATUS_NEW_CARTRIDGE) {
			unsigned int error;
			qic117_cmd_t command;
			const ft_trace_t old_tracing = TRACE_LEVEL;
			SET_TRACE_LEVEL(ft_t_bug);

			/*  Undocumented feature: Must clear (not present!)
			 *  error here or we'll fail later.
			 */
			ftape_report_error(ftape, &error, &command, 1);

			SET_TRACE_LEVEL(old_tracing);
			TRACE(ft_t_info, "status: new cartridge");
			ftape->new_tape = 1;
		}
		/* no "else" clause. We don't want to set new_tape to
		 *  zero again only because the drive was in error
		 *  status.
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	} while (status & QIC_STATUS_ERROR);

	ftape->no_tape = !(status & QIC_STATUS_CARTRIDGE_PRESENT);
	ftape->write_protected = (status & QIC_STATUS_WRITE_PROTECT) != 0;
	if (ftape->no_tape) {
		TRACE(ft_t_warn, "no cartridge present");
	} else {
		if (ftape->write_protected) {
			TRACE(ft_t_noise, "Write protected cartridge");
		}
	}
	TRACE_EXIT 0;
}

static void ftape_log_vendor_id(ftape_info_t *ftape,
				vendor_struct * drive_type)
{
	int vendor_index;
	TRACE_FUN(ft_t_flow);

	ftape_report_vendor_id(ftape, &drive_type->vendor_id);
	vendor_index = lookup_vendor_id(drive_type->vendor_id);
	if (drive_type->vendor_id == UNKNOWN_VENDOR &&
	    drive_type->wake_up == wake_up_colorado) {
		vendor_index = 0;
		/* hack to get rid of all this mail */
		drive_type->vendor_id = 0;
	}
	if (vendor_index < 0) {
		/* Unknown vendor id, first time opening device.  The
		 * drive_type remains set to type found at wakeup
		 * time, this will probably keep the driver operating
		 * for this new vendor.  
		 */
		TRACE(ft_t_warn, "\n"
		      "============ unknown vendor id ===========\n"
		      "A new, yet unsupported tape drive is found\n"
		      "Please report the following values:\n"
		      "   Vendor id     : 0x%04x\n"
		      "   Wakeup method : %s\n"
		      "And a description of your tape drive\n"
		      "to "THE_FTAPE_MAINTAINER"\n"
		      "==========================================",
		      drive_type->vendor_id,
		      methods[drive_type->wake_up].name);
		drive_type->speed = 0;		/* unknown */
	} else {
		drive_type->name  = vendors[vendor_index].name;
		drive_type->speed = vendors[vendor_index].speed;
		drive_type->flags = vendors[vendor_index].flags;
		TRACE(ft_t_info, "tape drive type: %s", drive_type->name);
		/* scan all methods for this vendor_id in table */
		while(drive_type->wake_up != vendors[vendor_index].wake_up) {
			if (vendor_index < NR_ITEMS(vendors) - 1 &&
			    vendors[vendor_index + 1].vendor_id 
			    == 
			    drive_type->vendor_id) {
				++vendor_index;
			} else {
				break;
			}
		}
		if (drive_type->wake_up != vendors[vendor_index].wake_up) {
			TRACE(ft_t_warn, "\n"
		     "==========================================\n"
		     "wakeup type mismatch:\n"
		     "found: %s, expected: %s\n"
		     "please report this to "THE_FTAPE_MAINTAINER"\n"
		     "==========================================",
			      methods[drive_type->wake_up].name,
			      methods[vendors[vendor_index].wake_up].name);
		}
	}
	TRACE_EXIT;
}

void ftape_calc_timeouts(ftape_info_t *ftape,
			 qic_std_t qic_std,
			 unsigned int data_rate,
			 unsigned int tape_len)
{
	int speed;		/* deci-ips ! */
	int ff_speed;
	int length;
	TRACE_FUN(ft_t_any);

	/*                           tape transport speed
	 *  data rate:        QIC-40   QIC-80   QIC-3010 QIC-3020  Ditto-MAX
	 *
	 *    250 Kbps        25 ips     n/a      n/a      n/a         n/a
	 *    500 Kbps        50 ips   34 ips   22.6 ips   n/a         n/a
	 *      1 Mbps          n/a    68 ips   45.2 ips 22.6 ips      n/a
	 *      2 Mbps          n/a      n/a      n/a    45.2 ips    34 ips
	 *      3 Mbps          n/a      n/a      n/a      n/a       51 ips
	 *      3 Mbps          n/a      n/a      n/a      n/a       68 ips
	 *
	 *  fast tape transport speed is at least 68 ips.
	 */
	switch (qic_std) {
	case QIC_40:
		speed = (data_rate == 250) ? 250 : 500;
		break;
	case QIC_80:
		speed = (data_rate == 500) ? 340 : 680;
		break;
	case QIC_3010:
		speed = (data_rate == 500) ? 226 : 452;
		break;
	case QIC_3020:
		speed = (data_rate == 1000) ? 226 : 452;
		break;
	case DITTO_MAX:
		switch(data_rate) {
		default:
		case 2000: speed = 340; break;
		case 3000: speed = 510; break;
		case 4000: speed = 680; break;
		}
		break;
	default:
		TRACE(ft_t_bug, "Unknown qic_std (bug) ?");
		speed = 500;
		break;
	}
	if (ftape->drive_type.speed == 0) {
		unsigned long t0;

		/*  Measure the time it takes to wind to EOT and back to BOT.
		 *  If the tape length is known, calculate the rewind speed.
		 *  Else keep the time value for calculation of the rewind
		 *  speed later on, when the length _is_ known.
		 *  Ask for a report only when length and speed are both known.
		 */
		if (ftape->timeout.first_time) {
			ftape_seek_to_bot(ftape);
			t0 = jiffies;
			ftape_seek_to_eot(ftape);
			ftape_seek_to_bot(ftape);
			ftape->timeout.dt = (int) (((jiffies - t0) * FT_USPT) / 1000);
			if (ftape->timeout.dt < 1) {
				ftape->timeout.dt = 1;	/* prevent div by zero on failures */
			}
			ftape->timeout.first_time = 0;
			TRACE(ft_t_info,
			      "trying to determine seek timeout, got %d msec",
			      ftape->timeout.dt);
		}
		if (tape_len != 0) {
			ftape->drive_type.speed = 
				(2 * 12 * tape_len * 1000) / ftape->timeout.dt;
			TRACE(ft_t_warn, "\n"
		     "==========================================\n"
		     "drive type: %s\n"
		     "delta time = %d ms, length = %d ft\n"
		     "has a maximum tape speed of %d ips\n"
		     "please report this to "THE_FTAPE_MAINTAINER"\n"
		     "==========================================",
			      ftape->drive_type.name, ftape->timeout.dt, tape_len, 
			      ftape->drive_type.speed);
		}
	}
	/*  Handle unknown length tapes as very long ones. We'll
	 *  determine the actual length from a header segment later.
	 *  This is normal for all modern (Wide,TR1/2/3) formats.
	 */
	if (tape_len <= 0) {
		TRACE(ft_t_noise,
		      "Unknown tape length, using maximal timeouts");
		length = QIC_TOP_TAPE_LEN;	/* use worst case values */
	} else {
		length = tape_len;		/* use actual values */
	}
	if (ftape->drive_type.speed == 0) {
		ff_speed = speed; 
	} else {
		ff_speed = ftape->drive_type.speed;
	}
	/*  time to go from bot to eot at normal speed (data rate):
	 *  time = (1+delta) * length (ft) * 12 (inch/ft) / speed (ips)
	 *  delta = 10 % for seek speed, 20 % for rewind speed.
	 */
	ftape->timeout.seek = (length * 132 * FT_SECOND) / speed;
	ftape->timeout.rewind = (length * 144 * FT_SECOND) / (10 * ff_speed);
	ftape->timeout.reset = 20 * FT_SECOND + ftape->timeout.rewind;
	TRACE(ft_t_noise, "timeouts for speed = %d, length = %d\n"
	      "seek timeout  : %d sec\n"
	      "rewind timeout: %d sec\n"
	      "reset timeout : %d sec",
	      speed, length,
	      (ftape->timeout.seek + 500) / 1000,
	      (ftape->timeout.rewind + 500) / 1000,
	      (ftape->timeout.reset + 500) / 1000);
	TRACE_EXIT;
}

/* This function calibrates the datarate (i.e. determines the maximal
 * usable data rate) and sets the global variable ft_qic_std to qic_std
 *
 */
int ftape_calibrate_data_rate(ftape_info_t *ftape, qic_std_t qic_std)
{
	int rate = ftape->fdc->rate_limit;
	int result;
	TRACE_FUN(ft_t_flow);

	ftape->qic_std = qic_std;

	if (ftape->qic_std == QIC_UNKNOWN) {
		TRACE_ABORT(-EIO, ft_t_err,
		"Unable to determine data rate if QIC standard is unknown");
	}

	/*  Select highest rate supported by both fdc and drive.
	 *  Start with highest rate supported by the fdc.
	 */
	while (fdc_set_data_rate(ftape->fdc, rate) < 0 && rate > 250) {
		if (rate > 2000) {
			rate -= 1000;
		} else {
			rate /= 2;
		}
	}
	TRACE(ft_t_info,
	      "Highest FDC supported data rate: %d Kbps", rate);
	ftape->fdc->max_rate = rate;
	while (((result = ftape_set_data_rate(ftape, rate, qic_std))
		== -EINVAL) && rate > 250) {
		if (rate > 2000) {
			rate -= 1000;
		} else {
 			rate /= 2;
 		}
	}
	if (result < 0) {
		TRACE_ABORT(-EIO, ft_t_err, "set datarate failed");
	}
	ftape->data_rate = rate;
	TRACE_EXIT 0;
}

static int ftape_init_drive(ftape_info_t *ftape)
{
	int status;
	qic_model model;
	qic_std_t qic_std;
	unsigned int data_rate;
	TRACE_FUN(ft_t_flow);

	ftape->init_drive_needed = 0; /* don't retry if this fails ? */
	TRACE_CATCH(ftape_report_raw_drive_status(ftape, &status),);
	if (status & QIC_STATUS_CARTRIDGE_PRESENT) {
		if (!(status & QIC_STATUS_AT_BOT)) {
			/*  Antique drives will get here after a soft reset,
			 *  modern ones only if the driver is loaded when the
			 *  tape wasn't rewound properly.
			 */
			/* Tape should be at bot if new cartridge ! */
			ftape_seek_to_bot(ftape);
		}
		if (!(status & QIC_STATUS_REFERENCED)) {
			TRACE(ft_t_flow, "starting seek_load_point");
			TRACE_CATCH(ftape_command_wait(ftape, QIC_SEEK_LOAD_POINT,
						       ftape->timeout.reset,
						       &status),);
		}
	}
	ftape->formatted = (status & QIC_STATUS_REFERENCED) != 0;
	if (!ftape->formatted) {
		TRACE(ft_t_warn, "Warning: tape is not formatted !");
	}

	/*  report configuration aborts when ftape_tape_len == -1
	 *  unknown qic_std is okay if not formatted.
	 */
	TRACE_CATCH(ftape_report_configuration(ftape,
					       &model,
					       &data_rate,
					       &qic_std,
					       &ftape->tape_len),);

	/*  Maybe add the following to the /proc entry
	 */
	TRACE(ft_t_info, "%s drive @ %d Kbps",
	      (model == prehistoric) ? "prehistoric" :
	      ((model == pre_qic117c) ? "pre QIC-117C" :
	       ((model == post_qic117b) ? "post QIC-117B" :
		"post QIC-117D")), data_rate);

	if (ftape->formatted) {
		/*  initialize ft_used_data_rate to maximum value 
		 *  and set ft_qic_std
		 */
		TRACE_CATCH(ftape_calibrate_data_rate(ftape, qic_std),);
		if (ftape->tape_len == 0) {
			TRACE(ft_t_info, "unknown length %s tape",
			      (qic_std == QIC_40) ? "QIC-40" :
			      ((qic_std == QIC_80) ? "QIC-80" :
			       ((qic_std == QIC_3010) ? "QIC-3010" :
				((qic_std == QIC_3020) ? "QIC-3020" :
				 "Ditto-Max"))));
		} else {
			TRACE(ft_t_info, "%d ft. %s tape", ftape->tape_len,
			      (qic_std == QIC_40) ? "QIC-40" :
			      ((qic_std == QIC_80) ? "QIC-80" :
			       ((qic_std == QIC_3010) ? "QIC-3010" :
				((qic_std == QIC_3020) ? "QIC-3020" :
				 "Ditto-Max"))));
		}
		ftape_calc_timeouts(ftape, qic_std, ftape->data_rate, ftape->tape_len);
		/* soft write-protect QIC-40/QIC-80 cartridges used with a
		 * Colorado T3000 drive. Buggy hardware!
		 */
		if ((ftape->drive_type.vendor_id == 0x011c6) &&
		    ((qic_std == QIC_40 || qic_std == QIC_80) &&
		     !ftape->write_protected)) {
			TRACE(ft_t_warn, "\n"
			      "The famous Colorado T3000 bug:\n"
			      "%s drives can't write QIC40 and QIC80\n"
			      "cartridges but don't set the write-protect flag!",
			      ftape->drive_type.name);
			ftape->write_protected = 1;
		}
	} else {
		/*  Doesn't make too much sense to set the data rate
		 *  because we don't know what to use for the write
		 *  precompensation.
		 *  Need to do this again when formatting the cartridge.
		 */
		ftape->data_rate = data_rate;
		ftape_calc_timeouts(ftape, QIC_40,
				    data_rate,
				    ftape->tape_len);
	}
	ftape_new_cartridge(ftape);
	TRACE_EXIT 0;
}

/*
 *  free memory
 */
int ftape_destroy(int sel)
{
	if (!ftapes[sel] || ftapes[sel]->active) {
		return -EBUSY;
	}
	fdc_destroy(sel);
	ftape_kfree(sel, &ftapes[sel], sizeof(*ftapes[sel]));
	return 0;
}

/*      OPEN routine called by kernel-interface code
 */
int ftape_enable(int drive_selection)
{
	int sel = FTAPE_SEL(drive_selection);
	ftape_info_t *ftape = ftapes[sel];
	TRACE_FUN(ft_t_flow);

	if (ftape == NULL) {
		ftapes[sel] =
			ftape = ftape_kmalloc(sel, sizeof(*ftapes[sel]), 1);
		ftape_init_driver(ftape, sel);		
	} else if (ftape->active) { /* already enabled */
		TRACE_EXIT -EBUSY;
	}
	ftape->active    = 1;     /* this is cleared by ftape_disable */
	ftape->failure   = 0;     /* clear and pray */
	ftape->drive_sel = sel;
	/*
	 * init & detect fdc
	 */
	TRACE_CATCH(fdc_init(ftape), ftape->active = 0);
	TRACE_CATCH(ftape_activate_drive(ftape, &ftape->drive_type),
		    fdc_disable(ftape->fdc); ftape->active = 0);
	if (ftape->drive_type.vendor_id == UNKNOWN_VENDOR) {
		ftape_log_vendor_id(ftape, &ftape->drive_type);
	}
	TRACE_CATCH(ftape_get_drive_status(ftape),
		    ftape_detach_drive(ftape); ftape->active = 0);
	if (ftape->new_tape) {
		ftape->init_drive_needed = 1;
	}
	if (!ftape->no_tape          &&
	    ftape->init_drive_needed) {
		TRACE_CATCH(ftape_init_drive(ftape),
			    ftape_detach_drive(ftape);
			    ftape->active = 0);
	}

	clear_history(ftape);
	TRACE_EXIT 0;
}

static void ftape_print_history(ftape_info_t *ftape)
{
	TRACE_FUN(ft_t_flow);

	if (ftape->history.used) {
		TRACE(ft_t_info, "== Non-fatal errors this run: ==");
		TRACE(ft_t_info, "fdc isr statistics:\n"
		      " id_am_errors     : %3d\n"
		      " id_crc_errors    : %3d\n"
		      " data_am_errors   : %3d\n"
		      " data_crc_errors  : %3d\n"
		      " overrun_errors   : %3d\n"
		      " no_data_errors   : %3d\n"
		      " retries          : %3d",
		      ftape->history.id_am_errors,
		      ftape->history.id_crc_errors,
		      ftape->history.data_am_errors,
		      ftape->history.data_crc_errors,
		      ftape->history.overrun_errors,
		      ftape->history.no_data_errors,
		      ftape->history.retries);
		if (ftape->history.used & FT_HISTORY_READ) {
			TRACE(ft_t_info, "ecc statistics:\n"
			      " crc_errors       : %3d\n"
			      " crc_failures     : %3d\n"
			      " ecc_failures     : %3d\n"
			      " sectors corrected: %3d",
			      ftape->history.crc_errors,
			      ftape->history.crc_failures,
			      ftape->history.ecc_failures,
			      ftape->history.corrected);
		}
		if (ftape->history.defects > 0) {
			TRACE(ft_t_warn, "Warning: %d media defects!",
			      ftape->history.defects);
		}
		if (ftape->history.rewinds > 0) {
			TRACE(ft_t_info, "tape motion statistics:\n"
			      KERN_INFO "repositions       : %3d",
			      ftape->history.rewinds);
		}
	}
	TRACE_EXIT;
}

/* release routine called by the high level interface module zftape
 */
void ftape_disable(int drive_selection)
{
	int sel = FTAPE_SEL(drive_selection);
	ftape_info_t *ftape = ftapes[sel];
	TRACE_FUN(ft_t_flow);

	ftape_set_state(ftape, waiting);
	ftape_detach_drive(ftape);
	ftape_print_history(ftape);
	ftape->active = 0;
	TRACE_EXIT;
}

void ftape_init_driver(ftape_info_t *ftape, int sel)
{

	memset(ftape, 0, sizeof(*ftape));

	ftape->drive_type.vendor_id = UNKNOWN_VENDOR;
	ftape->drive_type.speed     = 0;
	ftape->drive_type.wake_up   = unknown_wake_up;
	ftape->drive_type.name      = "Unknown";

	ftape->timeout.seek       = 650 * FT_SECOND;
	ftape->timeout.reset      = 670 * FT_SECOND;
	ftape->timeout.rewind     = 650 * FT_SECOND;
	ftape->timeout.head_seek  =  15 * FT_SECOND;
	ftape->timeout.stop       =   5 * FT_SECOND;
	ftape->timeout.pause      =  16 * FT_SECOND;
	ftape->timeout.first_time = 1;

	ftape->qic_std          = QIC_UNKNOWN;
	ftape->tape_len         = 0;  /* unknown */
	ftape->current_command  = 0;

	ftape->segments_per_track    = 102;
	ftape->segments_per_head     = 1020;
	ftape->segments_per_cylinder = 4;
	ftape->tracks_per_tape       = 20;

	ftape->failure = 1;

	ftape->formatted       = 0;
	ftape->no_tape         = 1;
	ftape->write_protected = 1;
	ftape->new_tape        = 1;

	ftape->driver_state = waiting;

	ftape->data_rate = 500;
	ftape->drive_max_rate = 0; /* triggers set_rate_test() */

	ftape->init_drive_needed = 1;

	ftape->header_segment_1    = -1;
	ftape->header_segment_2    = -1;
	ftape->used_header_segment = -1;
	ftape->first_data_segment  = -1;
	ftape->last_data_segment   = -1;

	ftape->location.track = -1;
	ftape->location.known = 0;

	ftape->tape_running = 0;
	ftape->might_be_off_track = 1;

	ftape->ffw_overshoot = 1;
	ftape->min_count = 8;
	
	ftape->rew_overshoot = 1;
	ftape->min_rewind = 2;	/* 1 + overshoot */
	ftape->rew_eot_margin = 4; /* best guess */

	ftape->start_offset = 1;

	ftape->last_reference = -1;

	ftape_init_bsm(ftape);

#ifndef CONFIG_FT_NO_TRACE_AT_ALL
	ftape->tracing             = &ftape_tracings[sel];
	ftape->function_nest_level = &ftape_function_nest_levels[sel]; 
#endif
}
