/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996      Kai Harrekilde-Petersen,
 *                (C) 1997-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-io.c,v $
 * $Revision: 1.20 $
 * $Date: 1999/09/11 15:00:10 $
 *
 *      This file contains the general control functions for the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/errno.h>
#include <linux/delay.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "ftape-tracing.h"
#include "fdc-io.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "ftape-init.h"
#include "ftape-calibr.h"

/*      Global vars.
 */

/* NOTE: sectors start numbering at 1, all others at 0 ! */
const struct qic117_command_table qic117_cmds[] = QIC117_COMMANDS;

/*      Local vars.
 */

/*      Delay (msec) routine.
 */
void ftape_sleep(unsigned int time)
{

	time *= 1000;   /* msecs -> usecs */
	if (time < FT_USPT) {
		/*  Time too small for scheduler, do a busy wait ! */
		udelay(time);
	} else {
		unsigned int ticks = (time + FT_USPT - 1) / FT_USPT;

		/* Hey, here is a problem:
		 *
		 * In case we are called just before jiffies are to be
		 * incremented, this routine will fail to wait!
		 * Bad !!!!!!!!!
		 *
		 * Solution: Always wait one tick more than
		 * necessary. Thus the timeout will be at worst
		 * (FT_USPT - 1) us too long, but this routine is
		 * guaranteed to wait at least one tick! And it isn't
		 * accurate anyway as we give the control back to the
		 * scheduler and thus it may take arbitray long until
		 * we get control again.
		 */
		long timeout = ticks + 1; /* + 1 is NECESSARY !!!! */

		set_current_state(TASK_INTERRUPTIBLE);
		do {
			/*  Mmm. Isn't current->blocked == 0xffffffff ?
			 */
			if (ft_killed()) {
				printk("%s: awoken by non-blocked signal :-(\n", __func__);
				break;	/* exit on signal */
			}
			while (get_current_state() != TASK_RUNNING) {
				timeout = schedule_timeout(timeout);
			}
		} while (timeout > 0);
	}
}

/*  send a command or parameter to the drive
 *  Generates # of step pulses.
 */
static inline int ft_send_to_drive(ftape_info_t *ftape, int arg)
{
	/*  Always wait for a command_timeout period to separate
	 *  individuals commands and/or parameters.
	 */
#if 1
	ftape_sleep_a_tick(3 * FT_MILLISECOND);
#else
	ftape_sleep(3 * FT_MILLISECOND);
#endif
	/*  Keep cylinder nr within range, step towards home if possible.
	 */
	if (ftape->fdc->current_cylinder >= arg) {
		return fdc_seek(ftape->fdc, ftape->fdc->current_cylinder - arg);
	} else {
		return fdc_seek(ftape->fdc, ftape->fdc->current_cylinder + arg);
	}
}

/* forward */
int ftape_report_raw_drive_status(ftape_info_t *ftape, int *status);

static int ft_check_cmd_restrictions(ftape_info_t *ftape,
				     qic117_cmd_t command)
{
	int status = -1;
	TRACE_FUN(ft_t_any);
	
	TRACE(ft_t_flow, "%s", qic117_cmds[command].name);
	/* A new motion command during an uninterruptible (motion)
	 *  command requires a ready status before the new command can
	 *  be issued. Otherwise a new motion command needs to be
	 *  checked against required status.
	 */
	if (qic117_cmds[command].cmd_type == motion &&
	    qic117_cmds[ftape->current_command].non_intr) {
		TRACE_CATCH(ftape_report_raw_drive_status(ftape, &status),);
		if ((status & QIC_STATUS_READY) == 0) {
			TRACE(ft_t_noise,
			      "motion cmd (%d) during non-intr cmd (%d)",
			      command, ftape->current_command);
			TRACE(ft_t_noise, "waiting until drive gets ready");
			ftape_ready_wait(ftape, ftape->timeout.seek,
					 &status);
		}
	}
	if (qic117_cmds[command].mask != 0) {
		__u8 difference;
		/*  Some commands do require a certain status:
		 */
		if (status == -1) {	/* not yet set */
			TRACE_CATCH(ftape_report_raw_drive_status(ftape,
								  &status),);
		}
		difference = ((status ^ qic117_cmds[command].state) &
			      qic117_cmds[command].mask);
		/*  Wait until the drive gets
		 *  ready. This may last forever if
		 *  the drive never gets ready... 
		 */
		while ((difference & QIC_STATUS_READY) != 0) {
			TRACE(ft_t_noise, "command %d issued while not ready",
			      command);
			TRACE(ft_t_noise, "waiting until drive gets ready");
			if (ftape_ready_wait(ftape, ftape->timeout.seek,
					     &status) == -EINTR) {
				/*  Bail out on signal !
				 */
				TRACE_ABORT(-EINTR, ft_t_warn,
				      "interrupted by non-blockable signal");
			}
			difference = ((status ^ qic117_cmds[command].state) &
				      qic117_cmds[command].mask);
		}
		while ((difference & QIC_STATUS_ERROR) != 0) {
			int err;
			qic117_cmd_t cmd;

			TRACE(ft_t_noise,
			      "command %d issued while error pending",
			      command);
			TRACE(ft_t_noise, "clearing error status");
			ftape_report_error(ftape, &err, &cmd, 1);
			TRACE_CATCH(ftape_report_raw_drive_status(ftape,
								  &status),);
			difference = ((status ^ qic117_cmds[command].state) &
				      qic117_cmds[command].mask);
			if ((difference & QIC_STATUS_ERROR) != 0) {
				/*  Bail out on fatal signal !
				 */
				FT_SIGNAL_EXIT(_NEVER_BLOCK);
			}
		}
		if (difference) {
			/*  Any remaining difference can't be solved
			 *  here.  
			 */
			if (difference & (QIC_STATUS_CARTRIDGE_PRESENT |
					  QIC_STATUS_NEW_CARTRIDGE |
					  QIC_STATUS_REFERENCED)) {
				TRACE(ft_t_warn,
				      "Fatal: tape removed or reinserted !");
				ftape->failure = 1;
			} else {
				TRACE(ft_t_err, "wrong state: 0x%02x should be: 0x%02x",
				      status & qic117_cmds[command].mask,
				      qic117_cmds[command].state);
			}
			TRACE(ft_t_noise, "Command %s not allowed",
			      qic117_cmds[command].name);
			TRACE_EXIT -EIO;
		}
		if (~status & QIC_STATUS_READY & qic117_cmds[command].mask) {
			TRACE_ABORT(-EBUSY, ft_t_err, "Bad: still busy!");
		}
	}
	TRACE_EXIT 0;
}

/*      Issue a tape command:
 */
int ftape_command(ftape_info_t *ftape, qic117_cmd_t command)
{
	int result = 0;
	TRACE_FUN(ft_t_any);

	if ((unsigned int)command > NR_ITEMS(qic117_cmds)) {
		/*  This is a bug we'll want to know about too.
		 */
		TRACE_ABORT(-EIO, ft_t_bug, "bug - bad command: %d", command);
	}
	if (++(ftape->qic_cmd_level) > 5) { /* This is a bug we'll
					   * want to know about.
					   */
		--(ftape->qic_cmd_level);
		TRACE_ABORT(-EIO, ft_t_bug, "bug - recursion for command: %d",
			    command);
	}
	/*  disable logging and restriction check for some commands,
	 *  check all other commands that have a prescribed starting
	 *  status.
	 */
	if (command == QIC_ENTER_FORMAT_MODE &&
	    ftape->drive_type.flags & FT_CANT_FORMAT) {
		TRACE_ABORT(-EIO, ft_t_err,
	    "Sorry, your tape drive isn't able to format tape cartridges");
	}
	if (ftape->diagnostic_mode) {
		TRACE(ft_t_flow, "diagnostic command %d", command);
	} else if (command == QIC_REPORT_DRIVE_STATUS ||
		   command == QIC_REPORT_NEXT_BIT) {
		TRACE(ft_t_any, "%s", qic117_cmds[command].name);
	} else {
		TRACE_CATCH(ft_check_cmd_restrictions(ftape, command),
			    --(ftape->qic_cmd_level));
	}
	/*  Now all conditions are met or result was < 0.
	 */
	result = ft_send_to_drive(ftape, (unsigned int)command);
	if (qic117_cmds[command].cmd_type == motion &&
	    command != QIC_LOGICAL_FORWARD && command != QIC_STOP_TAPE) {
		ftape->location.known = 0;
	}
	ftape->current_command = command;
	--(ftape->qic_cmd_level);
	TRACE_EXIT result;
}

/*      Send a tape command parameter:
 *      Generates command # of step pulses.
 *      Skips tape-status call !
 */
int ftape_parameter(ftape_info_t *ftape, unsigned int parameter)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "called with parameter = %d", parameter);
	TRACE_EXIT ft_send_to_drive(ftape, parameter + 2);
}

/*      Wait for the drive to get ready.
 *      timeout time in milli-seconds
 *      Returned status is valid if result != -EIO
 *
 *      Should we allow to be killed by SIGINT?  (^C)
 *      Would be nice at least for large timeouts.
 */
int ftape_ready_wait(ftape_info_t *ftape,
		     unsigned int timeout, int *status)
{
	unsigned long t0;
	unsigned int poll_delay = 0;
	unsigned int poll_limit;
	int forever;
	int signal_retries;
	TRACE_FUN(ft_t_any);

	/*  the following ** REALLY ** reduces the system load when
	 *  e.g. one simply rewinds or retensions. The tape is slow 
	 *  anyway. It is really not necessary to detect error 
	 *  conditions with 1/10 seconds granularity
	 *
	 *  On my AMD 133MHZ 486: 100 ms: 23% system load
	 *                        1  sec:  5%
	 *                        5  sec:  0.6%, yeah
	 */
	if (timeout <= FT_SECOND) {
		poll_limit = 100 * FT_MILLISECOND;
		signal_retries = 20; /* two seconds */
	} else if (timeout < 20 * FT_SECOND) {
		TRACE(ft_t_flow, "setting poll delay to 1 second");
		poll_limit = FT_SECOND;
		signal_retries = 2; /* two seconds */
	} else {
		TRACE(ft_t_flow, "setting poll delay to 5 seconds");
		poll_limit = 5 * FT_SECOND;
		signal_retries = 1; /* five seconds */
	}
	forever = timeout < 0;
	for (;;) {
		if (poll_delay < poll_limit) {
			/* increase the poll_delay linearily to
			 * prevent unnecessary consumption of CPU
			 * time. 
			 */
			poll_delay += 100 * FT_MILLISECOND;
		}
		t0 = jiffies;
		TRACE_CATCH(ftape_report_raw_drive_status(ftape, status),);
		if (*status & QIC_STATUS_READY) {
			TRACE_EXIT 0;
		}
		if (!signal_retries--) {
			FT_SIGNAL_EXIT(_NEVER_BLOCK);
		}
		if (!forever) {
			/* this will fail when jiffies wraps around about
			 * once every year :-)
			 */
			timeout -= ((jiffies - t0) * FT_SECOND) / HZ;
			if (timeout <= 0) {
				TRACE_ABORT(-ETIME, ft_t_err, "timeout");
			}
			timeout -= poll_delay;
		}
		ftape_sleep(poll_delay);
	}
}

/*      Issue command and wait up to timeout milli seconds for drive ready
 */
int ftape_command_wait(ftape_info_t *ftape,
		       qic117_cmd_t command, unsigned int timeout, int *status)
{
	int result;

	/* Drive should be ready, issue command
	 */
	result = ftape_command(ftape, command);
	if (result >= 0) {
		result = ftape_ready_wait(ftape, timeout, status);
	}
	return result;
}

int ftape_parameter_wait(ftape_info_t *ftape,
			 unsigned int parm, unsigned int timeout, int *status)
{
	int result;

	/* Drive should be ready, issue command
	 */
	result = ftape_parameter(ftape, parm);
	if (result >= 0) {
		result = ftape_ready_wait(ftape, timeout, status);
	}
	return result;
}

/*--------------------------------------------------------------------------
 *      Report operations
 */

/* Query the drive about its status.  The command is sent and
   result_length bits of status are returned (2 extra bits are read
   for start and stop). */

int ftape_report_operation(ftape_info_t *ftape,
			   int *status,
			   qic117_cmd_t command,
			   int result_length)
{
	int i, st3;
	unsigned int t0;
	unsigned int dt;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_command(ftape, command),);
	t0 = jiffies;
	i = 0;
	do {
		++i;
		ftape_sleep(3 * FT_MILLISECOND);	/* see remark below */
		TRACE_CATCH(fdc_sense_drive_status(ftape->fdc, &st3),);
		dt = (jiffies - t0) * 1000000/HZ;
		/*  Ack should be asserted within Ttimout + Tack = 6 msec.
		 *  Looks like some drives fail to do this so extend this
		 *  period to 300 msec.
		 */
	} while (!(st3 & ST3_TRACK_0) && dt < 300000);
	if (!(st3 & ST3_TRACK_0)) {
		TRACE(ft_t_err,
		      "No acknowledge after %u msec. (%i iter)", dt / 1000, i);
		TRACE_ABORT(-EIO, ft_t_err, "timeout on Acknowledge");
	}
	/*  dt may be larger than expected because of other tasks
	 *  scheduled while we were sleeping.
	 */
	if (i > 1 && dt > 6000) {
		TRACE(ft_t_err, "Acknowledge after %u msec. (%i iter)",
		      dt / 1000, i);
	}
	*status = 0;
	for (i = 0; i < result_length + 1; i++) {
		TRACE_CATCH(ftape_command(ftape, QIC_REPORT_NEXT_BIT),);
		TRACE_CATCH(fdc_sense_drive_status(ftape->fdc, &st3),);
		if (i < result_length) {
			*status |= ((st3 & ST3_TRACK_0) ? 1 : 0) << i;
		} else if ((st3 & ST3_TRACK_0) == 0) {
			TRACE_ABORT(-EIO, ft_t_err, "missing status stop bit");
		}
	}
	/* this command will put track zero and index back into normal state */
	(void)ftape_command(ftape, QIC_REPORT_NEXT_BIT);
	TRACE_EXIT 0;
}

/* Report the current drive status. */

int ftape_report_raw_drive_status(ftape_info_t *ftape, int *status)
{
	int result;
	int count = 0;
	TRACE_FUN(ft_t_any);

	do {
		result = ftape_report_operation(ftape, status,
						QIC_REPORT_DRIVE_STATUS, 8);
	} while (result < 0 && ++count <= 3);
	if (result < 0) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "report_operation failed after %d trials", count);
	}
	if ((*status & 0xff) == 0xff) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "impossible drive status 0xff");
	}
	if (*status & QIC_STATUS_READY) {
		ftape->current_command = QIC_NO_COMMAND; /* completed */
	}
	ftape->last_status.status.drive_status = (__u8)(*status & 0xff);
	TRACE_EXIT 0;
}

int ftape_report_drive_status(ftape_info_t *ftape, int *status)
{
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_report_raw_drive_status(ftape, status),);
	if (*status & QIC_STATUS_NEW_CARTRIDGE ||
	    !(*status & QIC_STATUS_CARTRIDGE_PRESENT)) {
		TRACE(ft_t_err, "status: %d", *status);
		ftape->failure = 1;	/* will inhibit further operations */
		TRACE_EXIT -EIO;
	}
	if (*status & QIC_STATUS_READY && *status & QIC_STATUS_ERROR) {
		/*  Let caller handle all errors */
		TRACE_ABORT(1, ft_t_warn, "warning: error status set!");
	}
	TRACE_EXIT 0;
}

int ftape_report_error(ftape_info_t *ftape, unsigned int *error,
		       qic117_cmd_t *command, int report)
{
	static const ftape_error ftape_errors[] = QIC117_ERRORS;
	int code;
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(ftape_report_operation(ftape, &code, QIC_REPORT_ERROR_CODE, 16),);
	*error   = (unsigned int)(code & 0xff);
	*command = (qic117_cmd_t)((code>>8)&0xff);
	/*  remember hardware status, maybe useful for status ioctls
	 */
	ftape->last_error.error.command = (__u8)*command;
	ftape->last_error.error.error   = (__u8)*error;
	if (!report) {
		TRACE_EXIT 0;
	}
	if (*error == 0) {
		TRACE_ABORT(0, ft_t_info, "No error");
	}
	TRACE(ft_t_info, "errorcode: %d", *error);
	if (*error < NR_ITEMS(ftape_errors)) {
		TRACE(ft_t_noise, "%sFatal ERROR:",
		      (ftape_errors[*error].fatal ? "" : "Non-"));
		TRACE(ft_t_noise, "%s ...", ftape_errors[*error].message);
	} else {
		TRACE(ft_t_noise, "Unknown ERROR !");
	}
	if ((unsigned int)*command < NR_ITEMS(qic117_cmds) &&
	    qic117_cmds[*command].name != NULL) {
		TRACE(ft_t_noise, "... caused by command \'%s\'",
		      qic117_cmds[*command].name);
	} else {
		TRACE(ft_t_noise, "... caused by unknown command %d",
		      *command);
	}
	TRACE_EXIT 0;
}

int ftape_report_configuration(ftape_info_t *ftape,
			       qic_model *model,
			       unsigned int *rate,
			       qic_std_t *qic_std,
			       int *tape_len)
{
	int result;
	int config;
	int status;
	int ext_status;
	static const unsigned int qic_rates[ 4] = { 250, 2000, 500, 1000 };
	TRACE_FUN(ft_t_any);

	result = ftape_report_operation(ftape, &config,
					QIC_REPORT_DRIVE_CONFIGURATION, 8);
	if (result < 0) {
		ftape->last_status.status.drive_config = (__u8)0x00;
		*model = prehistoric;
		*rate = 500;
		*qic_std = QIC_40;
		*tape_len = 205;
		TRACE_EXIT 0;
	} else {
		ftape->last_status.status.drive_config = (__u8)(config & 0xff);
	}
	if (ftape->drive_type.flags & DITTO_MAX_EXT) {
                if (ftape_report_operation(ftape, &ext_status,
					   QIC_EXT_REPORT_DRIVE_CONFIG,
					   8) < 0) {
			TRACE_EXIT -EIO;
		}
		*rate = (ext_status & QIC_EXT_CONFIG_RATE_MASK) * 1000;
		TRACE(ft_t_noise, "ext. status: 0x%02x", ext_status & 0xff);
        } else {
		*rate = qic_rates[(config & QIC_CONFIG_RATE_MASK)
				 >> QIC_CONFIG_RATE_SHIFT];
        }
	result = ftape_report_operation(ftape,
					&status, QIC_REPORT_TAPE_STATUS, 8);
	if (result < 0) {
		ftape->last_status.status.tape_status = (__u8)0x00;
		/* pre- QIC117 rev C spec. drive, QIC_CONFIG_80 bit is valid.
		 */
		*qic_std = (config & QIC_CONFIG_80)
			? QIC_80 : QIC_40;
		/* ?? how's about 425ft tapes? */
		*tape_len = (config & QIC_CONFIG_LONG) ? 307 : 0;
		*model = pre_qic117c;
		result = 0;
	} else {
		ftape->last_status.status.tape_status = (__u8)(status & 0xff);
		*model = post_qic117b;
		TRACE(ft_t_noise, "report tape status result = %02x", status);
		/* post- QIC117 rev C spec. drive, QIC_CONFIG_80 bit is
		 * invalid. 
		 */
		switch (status & QIC_TAPE_STD_MASK) {
		case QIC_TAPE_QIC40:
			*qic_std = QIC_40; break;
		case QIC_TAPE_QIC80:
			*qic_std = QIC_80; break;
		case QIC_TAPE_QIC3010:
			*qic_std = QIC_3010; break;
		case QIC_TAPE_QIC3020:
			*qic_std = QIC_3020; break;
		default:
			*qic_std = QIC_UNKNOWN;	break;
		}
		switch (status & QIC_TAPE_LEN_MASK) {
		case QIC_TAPE_205FT:
			/* 205 or 425+ ft 550 Oe tape */
			*tape_len = 0;
			break;
		case QIC_TAPE_307FT:
			/* 307.5 ft 550 Oe Extended Length (XL) tape */
			*tape_len = 307;
			break;
		case QIC_TAPE_VARIABLE:
			/* Variable length 550 Oe tape */
			*tape_len = 0;
			break;
		case QIC_TAPE_1100FT:
			/* 1100 ft 550 Oe tape */
			*tape_len = 1100;
			break;
		case QIC_TAPE_FLEX:
			/* Variable length 900 Oe tape */
			*tape_len = 0;
			break;
		default:
			*tape_len = -1;
			break;
		}
		if (*qic_std == QIC_UNKNOWN || *tape_len == -1) {
			TRACE(ft_t_any,
			      "post qic-117b spec drive with unknown tape");
		}
		result = *tape_len == -1 ? -EIO : 0;
		if (status & QIC_TAPE_WIDE) {
			switch (*qic_std) {
			case QIC_80:
				TRACE(ft_t_info, "TR-1 tape detected");
				break;
			case QIC_3010:
				TRACE(ft_t_info, "TR-2 tape detected");
				break;
			case QIC_3020:
				TRACE(ft_t_info, "TR-3 tape detected");
				break;
			case DITTO_MAX:
				TRACE(ft_t_info, "Ditto-Max tape detected");
				break;
			default:
				TRACE(ft_t_warn,
				      "Unknown Travan tape type detected");
				break;
			}
		}
	}
	TRACE_EXIT (result < 0) ? -EIO : 0;
}

int ftape_report_rom_version(ftape_info_t *ftape, int *version)
{

	if (ftape_report_operation(ftape, version, QIC_REPORT_ROM_VERSION, 8) < 0) {
		return -EIO;
	} else {
		return 0;
	}
}

int ftape_report_signature(ftape_info_t *ftape, int *signature)
{
	int result;

	result = ftape_command(ftape, 28);
	result = ftape_report_operation(ftape, signature, 9, 8);
	result = ftape_command(ftape, 30);
	return (result < 0) ? -EIO : 0;
}

void ftape_report_vendor_id(ftape_info_t *ftape, unsigned int *id)
{
	int result;
	TRACE_FUN(ft_t_any);

	/* We'll try to get a vendor id from the drive.  First
	 * according to the QIC-117 spec, a 16-bit id is requested.
	 * If that fails we'll try an 8-bit version, otherwise we'll
	 * try an undocumented query.
	 */
	result = ftape_report_operation(ftape, (int *) id, QIC_REPORT_VENDOR_ID, 16);
	if (result < 0) {
		result = ftape_report_operation(ftape, (int *) id,
						QIC_REPORT_VENDOR_ID, 8);
		if (result < 0) {
			/* The following is an undocumented call found
			 * in the CMS code.
			 */
			result = ftape_report_operation(ftape, (int *) id, 24, 8);
			if (result < 0) {
				*id = UNKNOWN_VENDOR;
			} else {
				TRACE(ft_t_noise, "got old 8 bit id: %04x",
				      *id);
				*id |= 0x20000;
			}
		} else {
			TRACE(ft_t_noise, "got 8 bit id: %04x", *id);
			*id |= 0x10000;
		}
	} else {
		TRACE(ft_t_noise, "got 16 bit id: %04x", *id);
	}
	if (*id == 0x0047) {
		int version;
		int sign;

		if (ftape_report_rom_version(ftape, &version) < 0) {
			TRACE(ft_t_bug, "report rom version failed");
			TRACE_EXIT;
		}
		TRACE(ft_t_noise, "CMS rom version: %d", version);
		ftape_command(ftape, QIC_ENTER_DIAGNOSTIC_1);
		ftape_command(ftape, QIC_ENTER_DIAGNOSTIC_1);
		ftape->diagnostic_mode = 1;
		if (ftape_report_operation(ftape, &sign, 9, 8) < 0) {
			unsigned int error;
			qic117_cmd_t command;

			ftape_report_error(ftape, &error, &command, 1);
			ftape_command(ftape, QIC_ENTER_PRIMARY_MODE);
			ftape->diagnostic_mode = 0;
			TRACE_EXIT;	/* failure ! */
		} else {
			TRACE(ft_t_noise, "CMS signature: %02x", sign);
		}
		if (sign == 0xa5) {
			result = ftape_report_operation(ftape, &sign, 37, 8);
			if (result < 0) {
				if (version >= 63) {
					*id = 0x8880;
					TRACE(ft_t_noise,
					      "This is an Iomega drive !");
				} else {
					*id = 0x0047;
					TRACE(ft_t_noise,
					      "This is a real CMS drive !");
				}
			} else {
				*id = 0x0047;
				TRACE(ft_t_noise, "CMS status: %d", sign);
			}
		} else {
			*id = UNKNOWN_VENDOR;
		}
		ftape_command(ftape, QIC_ENTER_PRIMARY_MODE);
		ftape->diagnostic_mode = 0;
	}
	TRACE_EXIT;
}

static int qic_rate_code(unsigned int rate)
{
	switch (rate) {
	case 250:
		return QIC_CONFIG_RATE_250;
	case 500:
		return QIC_CONFIG_RATE_500;
	case 1000:
		return QIC_CONFIG_RATE_1000;
	case 2000:
		return QIC_CONFIG_RATE_2000;
	default:
		return QIC_CONFIG_RATE_500;
	}
}

static int ftape_set_rate_test(ftape_info_t *ftape,
			       unsigned int *max_rate)
{
	unsigned int error;
	qic117_cmd_t command;
	int status;
	int supported = 0;
	TRACE_FUN(ft_t_any);

	/*  Check if the drive does support the select rate command
	 *  by testing all different settings. If any one is accepted
	 *  we assume the command is supported, else not.
	 *
	 *  taj - if a DITTO MAX, start the tests at 4000 for the EZ CARD
	 */
	if (ftape->drive_type.flags & DITTO_MAX_EXT) {
                for (*max_rate = 4000; *max_rate >= 1000; *max_rate -= 1000) {
                        if (ftape_command(ftape, QIC_EXT_SELECT_RATE) < 0) {
                                continue;
                        }
                        if (ftape_parameter_wait(ftape, *max_rate / 1000,
						 1 * FT_SECOND, &status) < 0) {
                                continue;
                        }
                        if (status & QIC_STATUS_ERROR) {
                                ftape_report_error(ftape, &error, &command, 0);
                                continue;
                        }
                        supported = 1; /* did accept a request */
                        break;
                }
        } else {
	for (*max_rate = 2000; *max_rate >= 250; *max_rate /= 2) {
		if (ftape_command(ftape, QIC_SELECT_RATE) < 0) {
			continue;
		}		
		if (ftape_parameter_wait(ftape, qic_rate_code(*max_rate),
					 1 * FT_SECOND, &status) < 0) {
			continue;
		}
		if (status & QIC_STATUS_ERROR) {
			ftape_report_error(ftape, &error, &command, 0);
			continue;
		}
		supported = 1; /* did accept a request */
		break;
	}
        } /* end of if EXT_RATE_SELECT */
	TRACE(ft_t_noise, "Select Rate command is%s supported", 
	      supported ? "" : " not");
	TRACE_EXIT supported;
}

int ftape_set_data_rate(ftape_info_t *ftape,
			unsigned int new_rate /* Kbps */, qic_std_t qic_std)
{
	int status;
	int result = 0;
	unsigned int data_rate = new_rate;
	int rate_changed = 0;
	qic_model dummy_model;
	qic_std_t dummy_qic_std;
	unsigned int dummy_tape_len;
	TRACE_FUN(ft_t_any);

	if (ftape->drive_max_rate == 0) { /* first time */
		ftape->set_rate_supported =
			ftape_set_rate_test(ftape, &ftape->drive_max_rate);
	}
	if (ftape->set_rate_supported) {
		if (ftape->drive_type.flags & DITTO_MAX_EXT) {
                        ftape_command(ftape, QIC_EXT_SELECT_RATE);
                        result = ftape_parameter_wait(ftape, new_rate / 1000,
						      1 * FT_SECOND, &status);
                } else {
			ftape_command(ftape, QIC_SELECT_RATE);
			result = ftape_parameter_wait(ftape, qic_rate_code(new_rate),
						      1 * FT_SECOND, &status);
                } /* end of if EXT_RATE_SELECT */
		if (result >= 0 && !(status & QIC_STATUS_ERROR)) {
			rate_changed = 1;
		}
		if (status & QIC_STATUS_ERROR) {
			unsigned int error;
			qic117_cmd_t command;

			TRACE(ft_t_flow, "error status set");
			ftape_report_error(ftape, &error, &command, 1);
		}
	}
	TRACE_CATCH(result = ftape_report_configuration(ftape,
							&dummy_model,
							&data_rate, 
							&dummy_qic_std,
							&dummy_tape_len),);
	if (data_rate != new_rate) {
		if (!ftape->set_rate_supported) {
			TRACE(ft_t_warn, "Rate change not supported!");
		} else if (rate_changed) {
			TRACE(ft_t_warn, "Requested: %d, got %d",
			      new_rate, data_rate);
		} else {
			TRACE(ft_t_warn, "Rate change failed!");
		}
		result = -EINVAL;
	}
	/*
	 *  Set data rate and write precompensation as specified:
	 *
	 *            |  QIC-40/80  | QIC-3010/3020
	 *            |             | Ditto Max
	 *   rate     |   precomp   |    precomp
	 *  ----------+-------------+--------------
	 *  250 Kbps. |   250 ns.   |     0 ns.
	 *  500 Kbps. |   125 ns.   |     0 ns.
	 *    1 Mbps. |    42 ns.   |     0 ns.
	 *    2 Mbps  |      N/A    |     0 ns.
	 *    3 Mbps  |      N/A    |     0 ns.
	 *    4 Mbps  |      N/A    |     0 ns.
	 */
	if ((qic_std == QIC_40 && data_rate > 500) || 
	    (qic_std == QIC_80 && data_rate > 1000)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_warn, "Datarate too high for QIC-mode");
	}
	TRACE_CATCH(fdc_set_data_rate(ftape->fdc, data_rate),_res = -EINVAL);
	ftape->data_rate = data_rate;
	if (qic_std == QIC_40 || qic_std == QIC_80) {
		switch (data_rate) {
		case 250:
			fdc_set_write_precomp(ftape->fdc, 250);
			break;
		default:
		case 500:
			fdc_set_write_precomp(ftape->fdc, 125);
			break;
		case 1000:
			fdc_set_write_precomp(ftape->fdc, 42);
			break;
		}
	} else {
		fdc_set_write_precomp(ftape->fdc, 0);
	}
	TRACE_EXIT result;
}

/*  The next two functions are used to cope with excessive overrun errors
 */
int ftape_increase_threshold(ftape_info_t *ftape)
{
	TRACE_FUN(ft_t_flow);

	if (ftape->fdc->type < i82077 || ftape->fdc->threshold >= 16) {
		TRACE_ABORT(-EIO, ft_t_err, "cannot increase fifo threshold");
	}
#if 0 || defined(HACK)
	fdc_reset(ftape->fdc);	
#endif
	if (fdc_fifo_threshold(ftape->fdc,
			       ++(ftape->fdc->threshold),
			       NULL, NULL, NULL) < 0) {
		TRACE(ft_t_err, "cannot increase fifo threshold");
		ftape->fdc->threshold --;
		fdc_reset(ftape->fdc);
	}
	TRACE(ft_t_info, "New FIFO threshold: %d", ftape->fdc->threshold);
	TRACE_EXIT 0;
}

int ftape_half_data_rate(ftape_info_t *ftape)
{
	TRACE_FUN(ft_t_flow);

	if (ftape->data_rate < 500) {
		TRACE_EXIT -1;
	}
	if (ftape->data_rate > 2000) {
		if (ftape_set_data_rate(ftape, ftape->data_rate - 1000, ftape->qic_std) < 0) {
			TRACE_EXIT -EIO;
		}
	} else if (ftape_set_data_rate(ftape, ftape->data_rate / 2, ftape->qic_std) < 0) {
		TRACE_EXIT -EIO;
	}
	ftape_calc_timeouts(ftape, ftape->qic_std, ftape->data_rate, ftape->tape_len);
#if 0 || defined(HACK)
	TRACE(ft_t_info,
	      "Resetting FDC and reprogramming FIFO threshold");
	fdc_reset(ftape->fdc);	
	fdc_fifo_threshold(ftape->fdc, ftape->fdc->threshold,
			   NULL, NULL, NULL);
#endif
	TRACE_EXIT 0;
}

/*      Seek the head to the specified track.
 */
int ftape_seek_head_to_track(ftape_info_t *ftape, unsigned int track)
{
	int status;
	TRACE_FUN(ft_t_any);

	ftape->location.track = -1; /* remains set in case of error */
	if (track >= ftape->tracks_per_tape) {
		TRACE_ABORT(-EINVAL, ft_t_bug, "track out of bounds");
	}
	TRACE(ft_t_flow, "seeking track %d", track);
	TRACE_CATCH(ftape_command(ftape, QIC_SEEK_HEAD_TO_TRACK),);
	TRACE_CATCH(ftape_parameter_wait(ftape, track, ftape->timeout.head_seek,
					 &status),);
	ftape->location.track = track;
	ftape->might_be_off_track = 0;
	TRACE_EXIT 0;
}

int ftape_wakeup_drive(ftape_info_t *ftape, wake_up_types method)
{
	int status;
	int motor_on = 0;
	TRACE_FUN(ft_t_any);

	switch (method) {
	case wake_up_colorado:
		TRACE_CATCH(ftape_command(ftape, QIC_PHANTOM_SELECT),);
		TRACE_CATCH(ftape_parameter(ftape, 0 /* ft_drive_sel ?? */),);
		break;
	case wake_up_mountain:
		TRACE_CATCH(ftape_command(ftape, QIC_SOFT_SELECT),);
		ftape_sleep_a_tick(FT_MILLISECOND);	/* NEEDED */
		TRACE_CATCH(ftape_parameter(ftape, 18),);
		break;
	case wake_up_insight:
		ftape_sleep(100 * FT_MILLISECOND);
		motor_on = 1;
		fdc_motor(ftape->fdc, motor_on);	/* enable is done by motor-on */
	case no_wake_up:
		break;
	default:
		TRACE_EXIT -ENODEV;	/* unknown wakeup method */
		break;
	}
	/*  If wakeup succeeded we shouldn't get an error here..
	 */
	TRACE_CATCH(ftape_report_raw_drive_status(ftape, &status),
		    if (motor_on) {
			    fdc_motor(ftape->fdc, 0);
		    });
	TRACE_EXIT 0;
}

int ftape_put_drive_to_sleep(ftape_info_t *ftape, wake_up_types method)
{
	TRACE_FUN(ft_t_flow);

	switch (method) {
	case wake_up_colorado:
		TRACE_CATCH(ftape_command(ftape, QIC_PHANTOM_DESELECT),);
		break;
	case wake_up_mountain:
		TRACE_CATCH(ftape_command(ftape, QIC_SOFT_DESELECT),);
		break;
	case wake_up_insight:
		fdc_motor(ftape->fdc, 0);	/* enable is done by motor-on */
	case no_wake_up:	/* no wakeup / no sleep ! */
		break;
	default:
		TRACE_EXIT -ENODEV;	/* unknown wakeup method */
	}
	TRACE_EXIT 0;
}

int ftape_reset_drive(ftape_info_t *ftape)
{
	int result = 0;
	int status;
	unsigned int err_code;
	qic117_cmd_t err_command;
	int i;
	TRACE_FUN(ft_t_any);

	/*  We want to re-establish contact with our drive.  Fire a
	 *  number of reset commands (single step pulses) and pray for
	 *  success.
	 */
	for (i = 0; i < 2; ++i) {
		TRACE(ft_t_flow, "Resetting fdc");
		fdc_reset(ftape->fdc);
		ftape_sleep_a_tick(10 * FT_MILLISECOND);
		TRACE(ft_t_flow, "Reset command to drive");
		result = ftape_command(ftape, QIC_RESET);
		if (result == 0) {
			ftape_sleep(1 * FT_SECOND); /*  drive not
						     *  accessible
						     *  during 1 second
						     */
			TRACE(ft_t_flow, "Re-selecting drive");

			/* Strange, the QIC-117 specs don't mention
			 * this but the drive gets deselected after a
			 * soft reset !  So we need to enable it
			 * again.
			 */
			if (ftape_wakeup_drive(ftape, ftape->drive_type.wake_up) < 0) {
				TRACE(ft_t_err, "Wakeup failed !");
			}
			TRACE(ft_t_flow, "Waiting until drive gets ready");
			result= ftape_ready_wait(ftape, ftape->timeout.reset, &status);
			if (result == 0 && (status & QIC_STATUS_ERROR)) {
				result = ftape_report_error(ftape, &err_code,
							    &err_command, 1);
				if (result == 0 && err_code == 27) {
					/*  Okay, drive saw reset
					 *  command and responded as it
					 *  should
					 */
					break;
				} else {
					result = -EIO;
				}
			} else {
				result = -EIO;
			}
		}
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}
	if (result != 0) {
		TRACE(ft_t_err, "General failure to reset tape drive");
	} else {
		/*  Restore correct settings: keep original rate 
		 */
		ftape_set_data_rate(ftape, ftape->data_rate, ftape->qic_std);
	}
	ftape->init_drive_needed = 1;
	TRACE_EXIT result;
}

/* taj ---
 *
 *     This function provides locking and unlocking of the Iomega DITTO MAX
 *     it accepts either a 1 or 0 for LOCK or UNLOCK.  All other drives
 *     cause this to be a NOP.
 */
int ftape_door_lock(ftape_info_t *ftape, int flag)
{
	int status;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Called with %d", flag);
	
	/*  if the drive is not id'd as a DITTO MAX, then just return
	 *
	 *  Also, treat this as a no-op if no cartridge is present.
	 */
	if (!(ftape->drive_type.flags & DITTO_MAX_EXT) || ftape->no_tape) {
		TRACE_EXIT 0;
	}
	TRACE(ft_t_noise, "Trying to %slock the door", flag ? "" : "un");

	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
	TRACE_CATCH(ftape_report_operation(ftape, &status,
					   QIC_LOADER_PARTITION_STATUS, 8),);

	TRACE(ft_t_any, "loader partition status: 0x%02x", status);

	if (flag != ((status & QIC_LOADER_LOCKED) != 0)) {
		TRACE_CATCH(ftape_command_wait(ftape, QIC_TOGGLE_LOCK,
					       ftape->timeout.pause, &status),);
	}
	TRACE_EXIT 0;
}

/*  flag == 1: open the door
 *  flag == 0: close the door
 *
 *  We refuse to do anything if the door is locked.
 */
int ftape_door_open(ftape_info_t *ftape, int flag)
{
	int status;
	TRACE_FUN(ft_t_flow);
	
	/* if the drive is not id'd as a DITTO MAX, then just return */
	if (!(ftape->drive_type.flags & DITTO_MAX_EXT)) {
		TRACE_EXIT 0;
	}
	TRACE(ft_t_noise, "Trying to %s the door", flag ? "open" : "close" );

	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
	TRACE_CATCH(ftape_report_operation(ftape, &status,
					   QIC_LOADER_PARTITION_STATUS, 8),)

	TRACE(ft_t_any, "loader partition status: 0x%02x", status);

	if (status & QIC_LOADER_LOCKED) {
		TRACE_ABORT(-EIO, ft_t_warn, "Door is locked");
	}
	if (flag != ((status & QIC_LOADER_DOOR_OPEN) != 0)) {
		int count = 0;

		ftape_command_wait(ftape, QIC_LOAD_UNLOAD, ftape->timeout.pause, &status);
		do {
			/* a simple ready wait won't suffice, unluckily.
			 * At least no when closing the door, as the tape drive
			 * annoyingly remains in the ready state until the door
			 * has finished closing.
			 */
			ftape_sleep(1 * FT_SECOND);
			TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
			TRACE_CATCH(ftape_report_operation(ftape, &status,
							   QIC_LOADER_PARTITION_STATUS, 8),);
			TRACE(ft_t_any, "loader partition status: 0x%02x", status);
			if ( ++count > 10) { /* hack */
				break;
			}
		} while (flag != ((status & QIC_LOADER_DOOR_OPEN) != 0));
	}
	TRACE_EXIT (flag == ((status & QIC_LOADER_DOOR_OPEN) != 0)) ? 0 : -EIO;
}

/*  
 */
int ftape_set_partition(ftape_info_t *ftape, int partition)
{
	int status;
	TRACE_FUN(ft_t_flow);
	
	/* if the drive is not id'd as a DITTO MAX, then just return */
	if (!(ftape->drive_type.flags & DITTO_MAX_EXT)) {
		TRACE_EXIT 0;
	}
	TRACE(ft_t_noise, "Trying to seek to partition %d", partition);

	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
	TRACE_CATCH(ftape_report_operation(ftape, &status,
					   QIC_LOADER_PARTITION_STATUS, 8),)

	TRACE(ft_t_any, "loader partition status: 0x%02x", status);

	TRACE_CATCH(ftape_command(ftape, QIC_SEEK_TO_PARTITION),);
	TRACE_CATCH(ftape_parameter_wait(ftape, partition,
					 ftape->timeout.pause, &status),);
	
	if (status & QIC_STATUS_ERROR) {
		unsigned int error;
		qic117_cmd_t command;
		
		TRACE(ft_t_flow, "error status set");
		ftape_report_error(ftape, &error, &command, 1);
		ftape_ready_wait(ftape, ftape->timeout.reset, &status);
		TRACE_EXIT -EIO;
	} else {
		/* disable any further operation. Must re-open first.
		 */
		ftape->init_drive_needed = 1;
		ftape->failure = 1;
		TRACE_EXIT 0;
	}
}

