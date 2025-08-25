/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-1998 Claus-Justus Heine,
 *           (C)      1997 Jochen Hoenicke.

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
 * $RCSfile: fdc-io.c,v $
 * $Revision: 1.42 $
 * $Date: 2000/07/10 21:23:32 $
 *
 *      This file contains the low-level floppy disk interface code
 *      for the QIC-40/80/3010/3020 floppy-tape driver "ftape" for
 *      Linux.
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/slab.h>
/* #include <asm/system.h> - removed in modern kernels */
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/atomic.h>

#include <linux/ftape.h>
#include <linux/qic117.h>

/* 
 * We need this definition for the sole purpose of keeping the trace
 * stuff working
 */
#define FDC_TRACING
#include "ftape-tracing.h"

#include "fdc-io.h"
#include "fdc-isr.h"
#include "ftape-io.h"
#include "ftape-rw.h"
#include "ftape-ctl.h"
#include "ftape-calibr.h"
#include "ftape-buffer.h"


void fdc_set_drive_specs(fdc_info_t *fdc);
int fdc_probe(fdc_info_t *fdc);


/*      Global vars.
 */

/* We support no more than four different tape drives
 * for historical reasons.
 */

/* initialize the fdc configuration parms for the 4 different tape
 * drives. Note that we only support one tape drive/interface for
 * internal drives (except, of course, if you set jumpers on the tape
 * drive to hard-wire its driver selection.
 *
 *
 * we rely on the low level fdc drivers (or the user) to supply
 * reasonable defaults
 */
char *ft_fdc_driver[4] = { CONFIG_FT_FDC_DRIVER_0,
			   CONFIG_FT_FDC_DRIVER_1,
			   CONFIG_FT_FDC_DRIVER_2,
			   CONFIG_FT_FDC_DRIVER_3};
int ft_fdc_driver_no[4];

fdc_info_t *fdc_infos[4] = { NULL, };

/*      Local vars.
 */
/* linked list of truly low-level fdc drivers. Identified by a unique
 * id-string that is used for module-autoloading (incase
 * CONFIG_KMOD is defined ...)
 *
 * Hoping to add support for more parallel port tape drives in the
 * future we allow arbitrary many fdc drivers to be loaded, useful for
 * compiling the whole stuff into the kernel.
 */
LIST_HEAD(fdc_drivers);

/* fdc->irq_level makes it possible to nest pairs of enable/disable
 * irq calls.
 */
inline void fdc_enable_irq(fdc_info_t *fdc)
{
	if (in_interrupt()) {
		return;
	}
#if 1
	if (fdc->irq_level <= 0) {
		printk("%s : negativ irq_level: %d\n", __func__,
		       fdc->irq_level);
		fdc->irq_level = 1;
	}
#endif
	if (!(--fdc->irq_level)) {
		fdc->ops->enable_irq(fdc);
	}
	return;
}

inline void fdc_disable_irq(fdc_info_t *fdc)
{
	if (in_interrupt()) {
		return;
	}
	if (!(fdc->irq_level++)) {
		fdc->ops->disable_irq(fdc); 
	}
	return;
}

/*  Wait during a timeout period for a given FDC status.
 *  If usecs == 0 then just test status, else wait at least for usecs.
 *  Returns -ETIME on timeout. Function must be calibrated first !
 */
int fdc_wait(fdc_info_t *fdc, unsigned int usecs, __u8 mask, __u8 state)
{
	int count_1 = (fdc->calibr_count * usecs +
                       fdc->calibr_count - 1) / fdc->calibr_time;

	do {
		fdc->status = fdc->ops->inp(fdc, fdc->msr);
		if (((__u8)fdc->status & mask) == state) {
			return count_1;
		}
	} while (count_1-- >= 0);
	return -ETIME;
}

int fdc_ready_wait(fdc_info_t *fdc, unsigned int usecs)
{
	return fdc_wait(fdc, usecs, FDC_DATA_READY | FDC_BUSY, FDC_DATA_READY);
}

static void fdc_usec_wait(fdc_info_t *fdc, unsigned int usecs)
{
	fdc_wait(fdc, usecs, 0, 1);	/* will always timeout ! */
}

static int fdc_ready_out_wait(fdc_info_t *fdc, unsigned int usecs)
{
	fdc_usec_wait(fdc, FT_RQM_DELAY); /* wait for valid RQM status */
	return fdc_wait(fdc, usecs, FDC_DATA_OUT_READY, FDC_DATA_OUT_READY);
}

static int fdc_ready_in_wait(fdc_info_t *fdc, unsigned int usecs)
{
	fdc_usec_wait(fdc, FT_RQM_DELAY); /* wait for valid RQM status */
	return fdc_wait(fdc, usecs, FDC_DATA_OUT_READY, FDC_DATA_IN_READY);
}

static void fdc_wait_calibrate(fdc_info_t *fdc)
{
	fdc_disable_irq(fdc);
	ftape_calibrate(fdc->ftape, "fdc_wait",
			(void (*)(void *, unsigned int))fdc_usec_wait,
			fdc, &fdc->calibr_count, &fdc->calibr_time);
	fdc_enable_irq(fdc);
}

/*  Output a cmd_len long command string to the FDC->
 *  The FDC should be ready to receive a new command or
 *  an error (EBUSY or ETIME) will occur.
 */
static int __fdc_command(fdc_info_t *fdc, const __u8 * cmd_data, int cmd_len)
{
	int result = 0;
	int count = cmd_len;
	int retry = 0;
#ifdef TESTING
	static unsigned int last_time = 0;
	unsigned int time;
#endif
	TRACE_FUN(ft_t_any);

	fdc_usec_wait(fdc, FT_RQM_DELAY);	/* wait for valid RQM status */
	fdc->status = fdc->ops->in(fdc, fdc->msr);
	if ((fdc->status & FDC_DATA_READY_MASK) != FDC_DATA_IN_READY) {
		TRACE_ABORT(-EBUSY, ft_t_err, "fdc not ready");
	} 
	fdc->mode = *cmd_data;	/* used by isr */
	while (count) {
		/*  Wait for a (short) while for the FDC to become ready
		 *  and transfer the next command byte.
		 */
		result = fdc_ready_in_wait(fdc, 150);
		if (result < 0) {
			TRACE(ft_t_fdc_dma,
			      "fdc_mode = %02x, status = %02x at index %d",
			      (int) fdc->mode, (int) fdc->status,
			      cmd_len - count);
			if (++retry <= 3) {
				TRACE(ft_t_warn, "fdc_write timeout, retry");
				continue;
			}
			TRACE(ft_t_err, "fdc_write timeout, fatal");
			/* recover ??? */
			break;
		} 
		fdc->ops->out(fdc, *cmd_data++, fdc->fifo);
		count--;
        }
	TRACE_EXIT result;
}

/*  Input a res_len long result string from the FDC->
 *  The FDC must be ready to send the result!
 */
int fdc_result(fdc_info_t *fdc, __u8 * res_data, int res_len)
{
	int result = 0;
	int count = res_len;
	int retry = 0;
	TRACE_FUN(ft_t_any);

	/* will take 24 - 30 usec for fdc_sense_drive_status and
	 * fdc_sense_interrupt_status commands.
	 *    35 fails sometimes (5/9/93 SJL)
	 * On a loaded system it incidentally takes longer than
	 * this for the fdc to get ready ! ?????? WHY ??????
	 * So until we know what's going on use a very long timeout.
	 */
	TRACE_CATCH(fdc_ready_out_wait(fdc, 500 /* usec */),);
	*res_data = fdc->ops->in(fdc, fdc->fifo);
	if (*res_data == 0x80) {
		/* unknown command */
		fdc_usec_wait(fdc, FT_RQM_DELAY);	/* allow FDC to negate BSY */
		TRACE_EXIT 0;
	}
	res_data++;
	count--;
	while (count) {
		/*  Wait for a (short) while for the FDC to become ready
		 *  and transfer the next result byte.
		 */
		result = fdc_ready_out_wait(fdc, 150);
		if (result < 0) {
			TRACE(ft_t_fdc_dma,
			      "fdc_mode = %02x, status = %02x at index %d",
			      (int) fdc->mode,
			      (int) fdc->status,
			      res_len - count);
			if (!(fdc->status & FDC_BUSY)) {
				TRACE_ABORT(-EIO, ft_t_err, 
					    "premature end of result phase");
			}
			if (++retry <= 3) {
				TRACE(ft_t_warn, "fdc_read timeout, retry");
				continue;
			} 
			TRACE(ft_t_err, "fdc_read timeout, fatal");
			/* recover ??? */
			break;
		} 
		*res_data++ = fdc->ops->in(fdc, fdc->fifo);
		count--;
	}
	fdc_usec_wait(fdc, FT_RQM_DELAY);	/* allow FDC to negate BSY */
	TRACE_EXIT result;
}

/*      Handle command and result phases for
 *      commands without data phase.
 */
int __fdc_issue_command(fdc_info_t *fdc,
			const __u8 * out_data, int out_count,
			__u8 * in_data, int in_count)
{
	TRACE_FUN(ft_t_any);

	if (out_count > 0) {
		TRACE_CATCH(__fdc_command(fdc, out_data, out_count),);
	}
	if (in_count > 0) {
		TRACE_CATCH(fdc_result(fdc, in_data, in_count),
			    TRACE(ft_t_err, "result phase aborted"));
	}
	TRACE_EXIT 0;
}

/* Yet another bug in the original driver. All that havoc is caused by
 * the fact that the isr() sends itself a command to the floppy tape
 * driver (pause, micro step pause).  Now, the problem is that
 * commands are transmitted via the fdc_seek command. But: the fdc
 * performs seeks in the background i.e. it doesn't signal busy while
 * sending the step pulses to the drive. Therefore the non-interrupt
 * level driver has no chance to tell whether the isr() just has
 * issued a seek. Therefore we HAVE TO have a look at the the
 * ft_hide_interrupt flag: it signals the non-interrupt level part of
 * the driver that it has to wait for the fdc until it has completed
 * seeking.
 *
 * THIS WAS PRESUMABLY THE REASON FOR ALL THAT "fdc_read timeout"
 * errors, I HOPE :-)
 *
 * BIG FAT WARNING: this routine HAS to be called with the fdc interrupt 
 * disabled.
 */
/*  Why not check for the seek bits in the MSR???
 *
 */
static int fdc_seek_check(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_any);

	if (fdc->hide_interrupt) {
		TRACE(ft_t_info,
		      "Waiting for the isr() completing fdc_seek()");
		/* fdc_interrupt_wait() enables the fdc irq */
		if (fdc_interrupt_wait(fdc, 2 * FT_SECOND) < 0) {
			TRACE(ft_t_warn, "Warning: "
			      "timeout waiting for isr() seek to complete");
		}
		fdc_disable_irq(fdc);
		if (fdc->hide_interrupt) {
			/* There cannot be another interrupt. The
			 * isr() only stops the tape and the next
			 * interrupt won't come until we have send our
			 * command to the drive.
			 */
			TRACE_ABORT(-EIO, ft_t_bug,
				    "BUG? isr() is still seeking?\n"
				    KERN_INFO "hide: %d\n"
				    KERN_INFO "seek: %x",
				    fdc->hide_interrupt,
				    fdc->seek_result);

		}
	}
	/* shouldn't be cleared if called from isr
	 */
	fdc->interrupt_seen = 0;
	TRACE_EXIT 0;
}

int fdc_issue_command(fdc_info_t *fdc,
			     const __u8 * out_data, int out_count,
			     __u8 * in_data, int in_count)
{
	int result = -EIO;

	fdc_disable_irq(fdc);
	if (!in_interrupt() && fdc_seek_check(fdc)) {
		goto out;
	}

	result =__fdc_issue_command(fdc,
				    out_data, out_count, in_data, in_count);
 out:
	fdc_enable_irq(fdc);
	return result;
}

/*      Wait for FDC interrupt with timeout (in milliseconds).
 *      Signals are blocked so the wait will not be aborted.
 *      Note: interrupts must be enabled ! (23/05/93 SJL)
 */
/* WARNING: it seems that sometimes an interrupt comes very fast. We
 * need to protect the entire code with the interrupt lock until we
 * have put ourselves on the waitqueue. Otherwise the fdc_isr() will
 * report stray interrupts. I.e.: this routine has to be called with
 * interrupts DISABLED It will enable them again, so the caller should
 * NOT call fdc_enable_irq() again.
 */
int fdc_interrupt_wait(fdc_info_t *fdc, unsigned int time)
{
	DECLARE_WAITQUEUE(wait, current);
	long timeout;
	TRACE_FUN(ft_t_fdc_dma);

	if (fdc->irq_level > 1) {
		TRACE(ft_t_warn,
		      "Geeh! Calling %s() with irq's off %d", __func__,
		      fdc->irq_level);
	}
	if (fdc->irq_level < 1) {
		TRACE(ft_t_warn,
		      "Geeh! Calling %s() with irq's on %d", __func__,
		      fdc->irq_level);
	}

 	if (waitqueue_active(&fdc->wait_intr)) {
		fdc_enable_irq(fdc);
		TRACE_ABORT(-EIO, ft_t_err, "error: nested call");
	}
 	/* timeout time will be up to USPT microseconds too long ! */
	timeout = (1000 * time + FT_USPT - 1) / FT_USPT;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&fdc->wait_intr, &wait);	
	fdc_enable_irq(fdc);
	while (!fdc->interrupt_seen && get_current_state() != TASK_RUNNING) {
		timeout = schedule_timeout(timeout);
	}
	remove_wait_queue(&fdc->wait_intr, &wait);
	/*  the following IS necessary. True: as well
	 *  wake_up_interruptible() as the schedule() set TASK_RUNNING
	 *  when they wakeup a task, BUT: it may very well be that
	 *  ft_interrupt_seen is already set to 1 when we enter here
	 *  in which case schedule() gets never called, and
	 *  TASK_RUNNING never set. This has the funny effect that we
	 *  execute all the code until we leave kernel space, but then
	 *  the task is stopped (a task CANNOT be preempted while in
	 *  kernel mode. Sending a pair of SIGSTOP/SIGCONT to the
	 *  tasks wakes it up again. Funny! :-)
	 */
	set_current_state(TASK_RUNNING);
	if (fdc->interrupt_seen) { /* woken up by interrupt */
		fdc->interrupt_seen = 0;
		TRACE_EXIT 0;
	}
	/*  Original comment:
	 *  In first instance, next statement seems unnecessary since
	 *  it will be cleared in fdc_command. However, a small part of
	 *  the software seems to rely on this being cleared here
	 *  (ftape_close might fail) so stick to it until things get fixed !
	 */
	/*  My deeply sought of knowledge:
	 *  Behold NO! It is obvious. fdc_reset() doesn't call fdc_command()
	 *  but nevertheless uses fdc_interrupt_wait(). OF COURSE this needs to
	 *  be reset here.
	 *
	 *  Shouldn't be needed any longer, as fdc_reset() clears the
	 *  flag itself.  As well as it checks for fdc->resetting
	 */
	TRACE(ft_t_noise, "cleanup reset");
	fdc_reset(fdc);

	TRACE_EXIT ft_killed() ? -EINTR : -ETIME;
}

/*      Start/stop drive motor. Enable DMA mode.
 */
void fdc_motor(fdc_info_t *fdc, int motor)
{
	int data = fdc->unit | FDC_RESET_NOT | FDC_DMA_MODE;
	TRACE_FUN(ft_t_any);

	fdc->motor = motor;
	if (fdc->motor) {
		data |= FDC_MOTOR_0 << fdc->unit;
		TRACE(ft_t_noise, "turning motor %d on", fdc->unit);
	} else {
		TRACE(ft_t_noise, "turning motor %d off", fdc->unit);
	}
	if (fdc->dor2 != 0xffff && fdc->type != fc10) { /* Mountain MACH-2 */
		fdc->ops->outp(fdc, data, fdc->dor2);
	} else {
		fdc->ops->outp(fdc, data, fdc->dor);
	}
	ftape_sleep_a_tick(10 * FT_MILLISECOND);
	TRACE_EXIT;
}

static void fdc_update_dsr(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "rate = %d Kbps, precomp = %d ns",
	      fdc->data_rate, fdc->precomp);
	if (fdc->type >= i82077) {
		fdc->ops->outp(fdc, (fdc->rate_code & 0x03) | fdc->prec_code, fdc->dsr);
	} else {
		fdc->ops->outp(fdc, fdc->rate_code & 0x03, fdc->ccr);
	}
	TRACE_EXIT;
}

void fdc_set_write_precomp(fdc_info_t *fdc, int precomp)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_noise, "New precomp: %d nsec", precomp);
	fdc->precomp = precomp;
	/*  write precompensation can be set in multiples of 41.67 nsec.
	 *  round the parameter to the nearest multiple and convert it
	 *  into a fdc setting. Note that 0 means default to the fdc,
	 *  7 is used instead of that.
	 */
	fdc->prec_code = ((fdc->precomp + 21) / 42) << 2;
	if (fdc->prec_code == 0 || fdc->prec_code > (6 << 2)) {
		fdc->prec_code = 7 << 2;
	}
	fdc_update_dsr(fdc);
	TRACE_EXIT;
}

/*  Reprogram the 82078 registers to use Data Rate Table 1 on all drives.
 */
void fdc_set_drive_specs(fdc_info_t *fdc)
{
	__u8 cmd[] = { FDC_DRIVE_SPEC, 0x00, 0x00, 0x00, 0x00, 0xc0};
	int result;
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_flow, "Setting of drive specs called");
	if (fdc->type == i82078_1) {
		cmd[1] = (0 << 5) | (2 << 2);
		cmd[2] = (1 << 5) | (2 << 2);
		cmd[3] = (2 << 5) | (2 << 2);
		cmd[4] = (3 << 5) | (2 << 2);
		result = fdc_command(fdc, cmd, NR_ITEMS(cmd));
		if (result < 0) {
			TRACE(ft_t_err, "Setting of drive specs failed");
		}
	}
	TRACE_EXIT;
}

/* Select clock for fdc, must correspond with tape drive setting !
 * This also influences the fdc timing so we must adjust some values.
 */
int fdc_set_data_rate(fdc_info_t *fdc, int rate)
{
	int bad_rate = 0;
	TRACE_FUN(ft_t_any);

	/* Select clock for fdc, must correspond with tape drive setting !
	 * This also influences the fdc timing so we must adjust some values.
	 */
	TRACE(ft_t_fdc_dma, "new rate = %d", rate);
	switch (rate) {
	case 250:
		fdc->rate_code = fdc_data_rate_250;
		break;
	case 500:
		fdc->rate_code = fdc_data_rate_500;
		break;
	case 1000:
		if (fdc->type < i82077) {
			bad_rate = 1;
                } else {
			fdc->rate_code = fdc_data_rate_1000;
		}
		break;
	case 2000:
		if (fdc->type < i82078_1) {
			bad_rate = 1;
                } else {
			fdc->rate_code = fdc_data_rate_2000;
		}
		break;
        case 3000:
                if (fdc->type != DITTOEZ) {
                        bad_rate = 1;
                } else {
                        fdc->rate_code = fdc_data_rate_3000;
                }
                break;
        case 4000:
                if (fdc->type != DITTOEZ) {
                        bad_rate = 1;
                } else {
                        fdc->rate_code = fdc_data_rate_4000;
                }
                break; 
	default:
		bad_rate = 1;
        }
	if (bad_rate) {
		TRACE_ABORT(-EIO,
			    ft_t_fdc_dma, "%d is not a valid data rate", rate);
	}
	fdc->data_rate = rate;
	fdc_update_dsr(fdc);
	fdc_set_seek_rate(fdc, fdc->seek_rate);  /* clock changed! */
#if 1
	ftape_sleep_a_tick(10 * FT_MILLISECOND);
#else
	udelay(1000);
#endif
	TRACE_EXIT 0;
}

/*  keep the unit select if keep_select is != 0,
 */
static void dor_reset(fdc_info_t *fdc, int keep_select)
{
	__u8 fdc_ctl = 0;

	if (keep_select) {
		fdc_ctl  = fdc->unit | FDC_DMA_MODE;
		if (fdc->motor) {
			fdc_ctl |= FDC_MOTOR_0 << fdc->unit;
		}
	}
	fdc_usec_wait(fdc, FT_RQM_DELAY);	/* allow FDC to negate BSY */
	if (fdc->dor2 != 0xffff && fdc->type != fc10) {  /* Mountain MACH-2 */
		fdc->ops->outp(fdc, fdc_ctl & 0x0f, fdc->dor);
		fdc->ops->outp(fdc, fdc_ctl, fdc->dor2);
	} else {
		fdc->ops->outp(fdc, fdc_ctl, fdc->dor);
	}
	udelay(10); /* delay >= 14 fdc clocks
		     * Mmmh. The 82078 spec. gives a value of 500 ns
		     * at most?
		     */
	fdc_ctl |= FDC_RESET_NOT;
	if (fdc->dor2 != 0xffff && fdc->type != fc10) {  /* Mountain MACH-2 */
		fdc->ops->outp(fdc, fdc_ctl & 0x0f, fdc->dor);
		fdc->ops->outp(fdc, fdc_ctl, fdc->dor2);
	} else {
		fdc->ops->outp(fdc, fdc_ctl, fdc->dor);
	}
}

/*      Reset the floppy disk controller. Leave the ftape_unit selected.
 */
void fdc_reset(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_any);

	fdc_disable_irq(fdc);
	
	if (fdc->in_reset ++) {
		fdc->in_reset --;
		/* This happens if we are called as a cleanup reset
		 * from fdc_interrupt_wait() every time when we are
		 * disabling the FDC on a non-Max tape drive.
		 */
		fdc_enable_irq(fdc);
		TRACE_EXIT;
	}

	fdc->resetting = 1;

	dor_reset(fdc, 1); /* keep unit selected */

	fdc->mode = fdc_idle;

	/*  Program data rate
	 */
	fdc_update_dsr(fdc);               /* restore data rate and precomp */

	/*  maybe the cli()/sti() pair is not necessary, BUT:
	 *  the following line MUST be here. Otherwise fdc_interrupt_wait()
	 *  won't wait. Note that fdc_reset() is called from 
	 *  ftape_dumb_stop() when the fdc is busy transferring data. In this
	 *  case fdc_isr() MOST PROBABLY sets ft_interrupt_seen, and tries
	 *  to get the result bytes from the fdc etc. CLASH.
	 */
	fdc->interrupt_seen = 0;
	
        /*
         *	Wait for first polling cycle to complete
	 * fdc_interrupt_wait() enables the fdc irq
	 */
	for (;;) {
		if (fdc_interrupt_wait(fdc, 1 * FT_SECOND) < 0) {
			TRACE(ft_t_err, "no drive polling interrupt!");
			/* fdc->resetting = 0; */
			break;
		}
		if (!fdc->resetting) {
			break;
		}
		fdc_disable_irq(fdc);
	}
	/*
         *	SPECIFY COMMAND
	 */
	fdc_set_seek_rate(fdc, fdc->seek_rate);

	ftape_sleep_a_tick(10 * FT_MILLISECOND);

	/*
	 *	DRIVE SPECIFICATION COMMAND (if fdc type known)
	 */
	if (fdc->type >= i82078_1) {
		fdc_set_drive_specs(fdc);
	}
	fdc->in_reset = 0;
	TRACE_EXIT;
}

#if !defined(CLK_48MHZ)
# define CLK_48MHZ 1
#endif

/*  When we're done, put the fdc into reset mode so that the regular
 *  floppy disk driver will figure out that something is wrong and
 *  initialize the controller the way it wants.
 */
void fdc_disable(fdc_info_t *fdc)
{
	__u8 cmd1[] = {FDC_CONFIGURE, 0x00, 0x00, 0x00};
	__u8 cmd2[] = {FDC_LOCK};
	__u8 cmd3[] = {FDC_UNLOCK};
	__u8 stat[1];
	TRACE_FUN(ft_t_flow);

	if (!fdc->fifo_locked) {
		goto out;
	}
	if (fdc_issue_command(fdc, cmd3, 1, stat, 1) < 0 || stat[0] != 0x00) {
		TRACE(ft_t_bug,
		      "couldn't unlock fifo, configuration remains changed");
		goto out;
	}
	fdc->fifo_locked = 0;
	if (CLK_48MHZ && fdc->type >= i82078) {
		cmd1[0] |= FDC_CLK48_BIT;
	}
	cmd1[2] = ((fdc->fifo_state) ? 0 : 0x20) + (fdc->fifo_thr - 1);
	if (fdc_command(fdc, cmd1, NR_ITEMS(cmd1)) < 0) {
		TRACE(ft_t_bug, "couldn't reconfigure fifo to old state");
		goto out;
	}
	if (fdc->lock_state &&
	    fdc_issue_command(fdc, cmd2, 1, stat, 1) < 0) {
		TRACE(ft_t_bug, "couldn't lock old state again");
		goto out;
	}
	TRACE(ft_t_noise, "fifo restored: %sabled, thr. %d, %slocked",
	      fdc->fifo_state ? "en" : "dis",
	      fdc->fifo_thr, (fdc->lock_state) ? "" : "not ");
out:
	/*  The "DOR reset" will restore the fdc's power on default
	 *  settings and clear the FIFO. Mmh. Why is the reset
	 *  necessary at this stage? Also, the data rate isn't changed
	 *  by the DOR reset. Well. But maybe better leave it in a
	 *  defined state.
	 *
	 *  Note:
	 * 
	 *  dor_reset(0) will clear the motor and drive selection bits
	 *  of the DOR. For "normal" fdc's this has the following
	 *  strange effect: an interrupt is generated by the fdc, but
	 *  the drive polling cycle isn't executed. But on the Ditto
	 *  EZ controller the drive polling cycle IS executed. Seems a
	 *  bit fishy. But who cares.
	 */
	fdc->resetting = 1;
	fdc_disable_irq(fdc);
	fdc->interrupt_seen = 0;
	dor_reset(fdc, 0);
        /*
         *	Wait for first polling cycle to complete
	 *      fdc_interrupt_wait() enables the fdc irq again
	 */
	if (fdc_interrupt_wait(fdc, 1 * FT_SECOND) < 0) {
		TRACE(ft_t_flow, "no drive polling interrupt! "
		      " (ignore message if everything works)");
		fdc_disable_irq(fdc);
		dor_reset(fdc, 0);
		fdc_enable_irq(fdc);
	}
	fdc->ops->release(fdc);
	fdc->resetting = 0;
	fdc->active = 0;
	TRACE_EXIT;
}

/*      Specify FDC seek-rate (milliseconds)
 */
int fdc_set_seek_rate(fdc_info_t *fdc, int seek_rate)
{
	/* set step rate, dma mode, and minimal head load and unload times
	 */
	__u8 in[3] = { FDC_SPECIFY, 1, (1 << 1)};
 
	fdc->seek_rate = seek_rate;
	in[1] |= (16 - (fdc->data_rate * fdc->seek_rate) / 500) << 4;

	return fdc_command(fdc, in, 3);
}

/*      Sense drive status: get unit's drive status (ST3)
 */
int fdc_sense_drive_status(fdc_info_t *fdc, int *st3)
{
	__u8 out[2];
	__u8 in[1];
	TRACE_FUN(ft_t_any);

	out[0] = FDC_SENSED;
	out[1] = fdc->unit;
	TRACE_CATCH(fdc_issue_command(fdc, out, 2, in, 1),);
	*st3 = in[0];
	TRACE_EXIT 0;
}

/*      Sense Interrupt Status command:
 *      should be issued at the end of each seek.
 *      get ST0 and current cylinder.
 *
 * N.B.: This routine is called form the fdc_isr() only!
 */
int fdc_sense_interrupt_status(fdc_info_t *fdc, int *st0, int *current_cylinder)
{
	__u8 out[1];
	__u8 in[2];
	TRACE_FUN(ft_t_any);

	out[0] = FDC_SENSEI;
	TRACE_CATCH(__fdc_issue_command(fdc, out, 1, in, 2),);
	*st0 = in[0];
	*current_cylinder = in[1];
	TRACE_EXIT 0;
}

/*      step to track
 */
int fdc_seek(fdc_info_t *fdc, int track)
{
	__u8 out[3];
#ifdef TESTING
	unsigned int time;
	unsigned int steps = ABS(track - fdc->current_cylinder);
#endif
	TRACE_FUN(ft_t_any);

	out[0] = FDC_SEEK;
	out[1] = fdc->unit;
	out[2] = track;

	/*  We really need this command to work !
	 */
	fdc->seek_result = 0;
	fdc_disable_irq(fdc); /* will be reset by fdc_interrupt_wait() */
	TRACE_CATCH(fdc_command(fdc, out, 3),
		    fdc_enable_irq(fdc);
		    TRACE(ft_t_noise, "destination was: %d, resetting FDC->..",
			  track);
		    fdc_reset(fdc));
	/*    Handle interrupts until ft_seek_result or timeout.
	 */
	TRACE_CATCH(fdc_interrupt_wait(fdc, 2 * FT_SECOND),);
	if ((fdc->seek_result & ST0_SEEK_END) == 0) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "no seek-end after seek completion !??");
	}
	/*    Verify whether we issued the right tape command.
	 */
	/* Verify that we seek to the proper track. */
	if (fdc->current_cylinder != track) {
		TRACE_ABORT(-EIO, ft_t_err, "bad seek..");
	}
	TRACE_EXIT 0;
}

static int perpend_off(fdc_info_t *fdc)
{
 	__u8 perpend[] = {FDC_PERPEND, 0x00};
	TRACE_FUN(ft_t_any);
	
	if (fdc->perpend_mode) {
		/* Turn off perpendicular mode */
		perpend[1] = 0x80;
		TRACE_CATCH(fdc_command(fdc, perpend, 2),
			    TRACE(ft_t_err,"Perpendicular mode exit failed!"));
		fdc->perpend_mode = 0;
	}
	TRACE_EXIT 0;
}

static int handle_perpend(fdc_info_t *fdc, int segment_id)
{
 	__u8 perpend[] = {FDC_PERPEND, 0x00};
	TRACE_FUN(ft_t_any);

	/* When writing QIC-3020 tapes, turn on perpendicular mode
	 * if tape is moving in forward direction (even tracks).
	 */
	if ((fdc->ftape->qic_std == QIC_3020 ||
	     fdc->ftape->qic_std == DITTO_MAX) &&
	    ((segment_id / fdc->ftape->segments_per_track) & 1) == 0) {
		if (fdc->type < i82077) {
			/*  fdc does not support perpendicular mode: complain 
			 */
			TRACE_ABORT(-EIO, ft_t_err,
				    "Your FDC does not support QIC-3020");
		}
		if (fdc->data_rate < 1000) {
			TRACE_ABORT(-EIO, ft_t_err,
				    "Datarate %d too low for QIC-3020",
				    fdc->data_rate);
		}
		perpend[1] = 0x03 /* 0x83 + (0x4 << fdc->unit) */ ;
		TRACE_CATCH(fdc_command(fdc, perpend, 2),
			   TRACE(ft_t_err,"Perpendicular mode entry failed!"));
		TRACE(ft_t_flow, "Perpendicular mode set");
		fdc->perpend_mode = 1;
		TRACE_EXIT 0;
	}
	TRACE_EXIT perpend_off(fdc);
}

/*  Setup fdc and dma for formatting the next segment
 */
int fdc_setup_formatting(fdc_info_t *fdc, buffer_struct * buff)
{
	__u8 out[6] = {
		FDC_FORMAT, 0x00, 3, 4 * FT_SECTORS_PER_SEGMENT, 0x00, 0x6b
	};
	TRACE_FUN(ft_t_any);
	
	fdc_disable_irq(fdc); /* could be called from ISR ! */
	
	/* Program the DMA controller. */
	fdc->ops->setup_dma(fdc,
			    DMA_MODE_WRITE, buff->ptr, 
			    FT_SECTORS_PER_SEGMENT * 4);
	
	if ((fdc->setup_error = handle_perpend(fdc, buff->segment_id)) < 0) {
		goto err_out;
	}

        TRACE(ft_t_fdc_dma, "phys. addr. = %lx", buff->ptr);

	/* Issue FDC command to start reading/writing.
	 */
	out[1] = fdc->unit;
	out[4] = fdc->gap3;
	out[5] = fdc->ffb;
	if ((fdc->setup_error = fdc_command(fdc, out, sizeof(out))) >= 0) {
		fdc_enable_irq(fdc);
		TRACE_EXIT 0;
	}
 err_out:
	(void)fdc->ops->terminate_dma(fdc, no_error);
	fdc->mode = fdc_idle;
	fdc_enable_irq(fdc);
	TRACE_EXIT fdc->setup_error;	
}


/*      Setup Floppy Disk Controller and DMA to read or write the next cluster
 *      of good sectors from or to the current segment.
 */
int fdc_setup_read_write(fdc_info_t *fdc, buffer_struct * buff, __u8 operation)
{
	__u8 out[9];
	int dma_mode;
	TRACE_FUN(ft_t_any);

	fdc_disable_irq(fdc); /* could be called from ISR ! */
	fdc->setup_error = 0;

	switch(operation) {
	case FDC_VERIFY:
		if (fdc->type < i82077 || fdc->type == DITTOEZ) {
			operation = FDC_READ;
		}
		// fall through
	case FDC_READ:
	case FDC_READ_DELETED:
		dma_mode = DMA_MODE_READ;
		TRACE(ft_t_fdc_dma, "xfer %d sectors to 0x%lx",
		      buff->sector_count, buff->ptr);
		if (operation != FDC_VERIFY) {
			fdc->ops->setup_dma(fdc, dma_mode, buff->ptr,
					    FT_SECTOR_SIZE*buff->sector_count);
		}
		fdc->setup_error = perpend_off(fdc);
		break;
	case FDC_WRITE_DELETED:
		TRACE(ft_t_noise, "deleting segment %d", buff->segment_id);
		// fall through
	case FDC_WRITE:
		dma_mode = DMA_MODE_WRITE;
		TRACE(ft_t_fdc_dma, "xfer %d sectors from 0x%lx",
		      buff->sector_count, buff->ptr);
		fdc->ops->setup_dma(fdc, dma_mode, buff->ptr,
				    FT_SECTOR_SIZE*buff->sector_count);
		/* When writing QIC-3020 tapes, turn on perpendicular mode
		 * if tape is moving in forward direction (even tracks).
		 */
		fdc->setup_error = handle_perpend(fdc, buff->segment_id);
		break;
	default:
		TRACE(ft_t_bug, "bug: illegal operation parameter");
		fdc->setup_error = -EIO;
		fdc_enable_irq(fdc);
		TRACE_EXIT -EIO;
	}

	if (fdc->setup_error < 0) {
		goto err_out;
	}

	TRACE(ft_t_fdc_dma, "phys. addr. = %lx", buff->ptr);

	/* Issue FDC command to start reading/writing.
	 */
	out[0] = operation;
	out[1] = fdc->unit;
	out[2] = buff->cyl;
	out[3] = buff->head;
	out[4] = buff->sect + buff->sector_offset;
	out[5] = 3;		/* Sector size of 1K. */
	out[6] = out[4] + buff->sector_count - 1;	/* last sector */
	out[7] = 109;		/* Gap length. */
	out[8] = 0xff;		/* No limit to transfer size. */
	TRACE(ft_t_fdc_dma, "C: 0x%02x, H: 0x%02x, R: 0x%02x, cnt: 0x%02x",
		out[2], out[3], out[4], out[6] - out[4] + 1);

	if ((fdc->setup_error = fdc_command(fdc, out, 9)) >= 0) {
		fdc_enable_irq(fdc);
		TRACE_EXIT 0;
	}
 err_out:
	(void)fdc->ops->terminate_dma(fdc, no_error);
	fdc->mode = fdc_idle;
	fdc_enable_irq(fdc);
	TRACE_EXIT fdc->setup_error;
}

int fdc_fifo_threshold(fdc_info_t *fdc, __u8 threshold,
		       int *fifo_state, int *lock_state, int *fifo_thr)
{
	const __u8 cmd0[] = {FDC_DUMPREGS};
	__u8 cmd1[] = {FDC_CONFIGURE, 0, (0x0f & (threshold - 1)), 0};
	const __u8 cmd2[] = {FDC_LOCK};
	const __u8 cmd3[] = {FDC_UNLOCK};
	__u8 reg[10];
	__u8 stat;
	int i;
	int result;
	TRACE_FUN(ft_t_any);

	if (CLK_48MHZ && fdc->type >= i82078) {
		cmd1[0] |= FDC_CLK48_BIT;
	}
	/*  Dump fdc internal registers for examination
	 */
	TRACE_CATCH(fdc_issue_command(fdc, cmd0, NR_ITEMS(cmd0), reg, NR_ITEMS(reg)),
		    TRACE(ft_t_warn, "dumpreg cmd failed, fifo unchanged"));
	/*  Now dump fdc internal registers
	 */
	for (i = 0; i < (int)NR_ITEMS(reg); ++i) {
		TRACE(ft_t_fdc_dma, "Register %d = 0x%02x", i, reg[i]);
	}
	if (fifo_state && lock_state && fifo_thr) {
		*fifo_state = (reg[8] & 0x20) == 0;
		*lock_state = reg[7] & 0x80;
		*fifo_thr = 1 + (reg[8] & 0x0f);
	}
	TRACE(ft_t_noise,
	      "original fifo state: %sabled, threshold %d, %slocked",
	      ((reg[8] & 0x20) == 0) ? "en" : "dis",
	      1 + (reg[8] & 0x0f), (reg[7] & 0x80) ? "" : "not ");
	/*  If fdc is already locked, unlock it first ! */
	if (reg[7] & 0x80) {
		fdc_disable_irq(fdc);
		fdc_ready_wait(fdc, 100);	/* needed ??? move to fdc_command ??? */
		fdc_enable_irq(fdc);
		TRACE_CATCH(fdc_issue_command(fdc, cmd3, NR_ITEMS(cmd3), &stat, 1),
			    TRACE(ft_t_bug, "FDC unlock command failed, "
				  "configuration unchanged"));
	}
	fdc->fifo_locked = 0;
	/*  Enable fifo and set threshold at xx bytes to allow a
	 *  reasonably large latency and reduce number of dma bursts.
	 */
	fdc_disable_irq(fdc);
	fdc_ready_wait(fdc, 100);	/* needed ??? move to fdc_command ??? */
	fdc_enable_irq(fdc);
	if ((result = fdc_command(fdc, cmd1, NR_ITEMS(cmd1))) < 0) {
		TRACE(ft_t_bug, "configure cmd failed, fifo unchanged");
	}
	/*  Now lock configuration so reset will not change it
	 */
        if(fdc_issue_command(fdc, cmd2, NR_ITEMS(cmd2), &stat, 1) < 0 ||
	   stat != 0x10) {
		TRACE_ABORT(-EIO, ft_t_bug,
			    "FDC lock command failed, stat = 0x%02x", stat);
	}
	fdc->fifo_locked = 1;
	TRACE_EXIT result;
}

static int fdc_fifo_enable(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_any);

	if (fdc->fifo_locked) {
		TRACE_ABORT(0, ft_t_warn, "Fifo not enabled because locked");
	}
	TRACE_CATCH(fdc_fifo_threshold(fdc, fdc->threshold /* bytes */,
				       &fdc->fifo_state,
				       &fdc->lock_state,
				       &fdc->fifo_thr),);
	TRACE_EXIT 0;
}

/*   Determine fd controller type 
 */

int fdc_probe(fdc_info_t *fdc)
{
	__u8 cmd[1];
	__u8 stat[16]; /* must be able to hold dumpregs & save results */
	int i;
	TRACE_FUN(ft_t_any);

	/*  Try to find out what kind of fd controller we have to deal with
	 *  Scheme borrowed from floppy driver:
	 *  first try if FDC_DUMPREGS command works
	 *  (this indicates that we have a 82072 or better)
	 *  then try the FDC_VERSION command (82072 doesn't support this)
	 *  then try the FDC_UNLOCK command (some older 82077's don't support this)
	 *  then try the FDC_PARTID command (82078's support this)
	 */
	cmd[0] = FDC_DUMPREGS;
	if (fdc_issue_command(fdc, cmd, 1, stat, 10) != 0) {
		TRACE_ABORT(no_fdc, ft_t_bug, "No FDC found");
	}
	if (stat[0] == 0x80) {
		/* invalid command: must be pre 82072 */
		TRACE_ABORT(i8272,
			    ft_t_warn, "Type 8272A/765A compatible FDC found");
	}
	fdc->save_state[0] = stat[7];
	fdc->save_state[1] = stat[8];
	cmd[0] = FDC_VERSION;
	if (fdc_issue_command(fdc, cmd, 1, stat, 1) < 0 || stat[0] == 0x80) {
		TRACE_ABORT(i8272, ft_t_warn, "Type 82072 FDC found");
	}
	if (*stat != 0x90) {
		TRACE_ABORT(i8272, ft_t_warn, "Unknown FDC found");
	}
	cmd[0] = FDC_UNLOCK;
	if(fdc_issue_command(fdc, cmd, 1, stat, 1) < 0 || stat[0] != 0x00) {
		TRACE_ABORT(i8272, ft_t_warn,
			    "Type pre-1991 82077 FDC found, "
			    "treating it like a 82072");
	}
	if (fdc->save_state[0] & 0x80) { /* was locked */
		cmd[0] = FDC_LOCK; /* restore lock */
		(void)fdc_issue_command(fdc, cmd, 1, stat, 1);
		TRACE(ft_t_warn, "FDC is already locked");
	}
	/* Test for a i82078 FDC */
	cmd[0] = FDC_PARTID;
	if (fdc_issue_command(fdc, cmd, 1, stat, 1) < 0 || stat[0] == 0x80) {
		/* invalid command: not a i82078xx type FDC */
		fdc_disable_irq(fdc);
		for (i = 0; i < 4; ++i) {
			fdc->ops->outp(fdc, i, fdc->tdr);
			if ((fdc->ops->inp(fdc, fdc->tdr) & 0x03) != i) {
				fdc_enable_irq(fdc);
				TRACE_ABORT(i82077,
					    ft_t_warn, "Type 82077 FDC found");
			}
		}
		fdc_enable_irq(fdc);
		TRACE_ABORT(i82077AA, ft_t_warn, "Type 82077AA FDC found");
	}
	/* FDC_PARTID cmd succeeded */
	switch (stat[0] >> 5) {
	case 0x0:
		/* i82078SL or i82078-1.  The SL part cannot run at
		 * 2Mbps (the SL and -1 dies are identical; they are
		 * speed graded after production, according to Intel).
		 * Some SL's can be detected by doing a SAVE cmd and
		 * look at bit 7 of the first byte (the SEL3V# bit).
		 * If it is 0, the part runs off 3Volts, and hence it
		 * is a SL.
		 */
		cmd[0] = FDC_SAVE;
		if(fdc_issue_command(fdc, cmd, 1, stat, 16) < 0) {
			TRACE(ft_t_err, "FDC_SAVE failed. Dunno why");
			/* guess we better claim the fdc to be a i82078 */
			TRACE_ABORT(i82078,
				    ft_t_warn,
				    "Type i82078 FDC (i suppose) found");
		}
		if ((stat[0] & FDC_SEL3V_BIT)) {
			/* fdc running off 5Volts; Pray that it's a i82078-1
			 */
			TRACE_ABORT(i82078_1, ft_t_warn,
				  "Type i82078-1 or 5Volt i82078SL FDC found");
		}
		TRACE_ABORT(i82078, ft_t_warn,
			    "Type 3Volt i82078SL FDC (1Mbps) found");
	case 0x1:
	case 0x2: /* S82078B  */
		/* The '78B  isn't '78 compatible.  Detect it as a '77AA */
		TRACE_ABORT(i82077AA, ft_t_warn, "Type i82077AA FDC found");
	case 0x3: /* NSC PC8744 core; used in several super-IO chips */
		TRACE_ABORT(i82077AA,
			    ft_t_warn, "Type 82077AA compatible FDC found");
	case 0x4: /* taj - Iomega DASH EZ 4Mbs PnP card */
			TRACE_ABORT(DITTOEZ, ft_t_warn, 
			  "Iomega DASH EZ FDC found");
	default:
		TRACE(ft_t_warn, "A previously undetected FDC found");
		TRACE_ABORT(i82077AA, ft_t_warn,
			  "Treating it as a 82077AA. Please report partid= %d",
			    stat[0]);
	} /* switch(stat[ 0] >> 5) */
	TRACE_EXIT no_fdc;
}

#define SEL_TRACING
#include "ftape-real-tracing.h"

static fdc_info_t *get_new_fdc(int sel)
{
	fdc_info_t *info = fdc_infos[sel];
	
	if (info == NULL) {
		fdc_infos[sel] = info = ftape_kmalloc(sel, sizeof(*info), 1);
		if (info == NULL) {
			printk("info == NULL\n");
			return NULL;
		}
	} else if (info->initialized) {
		return NULL;
	}
	memset(info, 0, sizeof(*info));
	info->magic      = FT_FDC_MAGIC;
	/* fill in configuration parameters */
	info->sra        = -1;
	info->irq        = -1;
	info->dma        = -1;
	info->threshold  = 8;
	info->rate_limit = 4000;
	info->driver     = NULL;
	/************************************/
	info->data_rate = 500;	/* data rate (Kbps) */
	info->rate_code = 0;	/* data rate code (0 == 500 Kbps) */
	info->seek_rate = 2;	/* step rate (msec) */
	info->fifo_locked = 0;	/* has fifo && lock set ? */
	info->precomp = 0;	/* default precomp. value (nsec) */
	info->prec_code = 0;	/* fdc precomp. select code */
	info->motor = 0;
	info->current_cylinder = -1;
	info->mode = fdc_idle;
	info->resetting = 0;
	init_waitqueue_head(&info->wait_intr);
	info->unit = sel;
	info->type = no_fdc;
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
	info->tracing             = &ftape_tracings[sel];
	info->function_nest_level = &ftape_function_nest_levels[sel];
#endif
	return info;
}

void fdc_destroy(int sel)
{
	TRACE_FUN(ft_t_flow);

	if (fdc_infos[sel]) {
		TRACE(ft_t_noise, "destroying fdc %d", sel);
		if (fdc_infos[sel]->active) {
			TRACE(ft_t_err, "Trying to destroy active fdc");
			if (fdc_infos[sel]->ops && 
			    fdc_infos[sel]->ops->release) {
				fdc_infos[sel]->ops->release(fdc_infos[sel]);
			}
			fdc_infos[sel]->active = 0;
		}
		(void)fdc_set_nr_buffers(fdc_infos[sel], 0);
		if (fdc_infos[sel]->ftape) {
			fdc_infos[sel]->ftape->fdc = NULL;
		}
		ftape_kfree(sel, &fdc_infos[sel], sizeof(*fdc_infos[sel]));
	}
	TRACE_EXIT;
}

static fdc_operations *find_driver(const char *driver)
{
	struct list_head *tmp;

	for (tmp = fdc_drivers.next;
	     tmp != &fdc_drivers;
	     tmp = tmp->next) {
		fdc_operations *ops = list_entry(tmp, fdc_operations, node);

		if (!strcmp(driver, ops->driver)) {
			return ops;
		}
	}
	return NULL;
}

/* We misuse the drive selection to switch between different kinds of
 * hardware, i.e. we misuse the drive selection, which is ignored by
 * all tapedrives I know of (ok, one can set some jumpers so that they
 * no longer ignore the drive selection ...)
 *
 * If selection is "-1" we pick the first slot available and return
 * its number as the calling module needs it as offset into the
 * fdc_info structure.
 *
 */
#define GLOBAL_TRACING
#include "ftape-real-tracing.h"

/* expensive, but only called when registering resp. searching for fdc
 * drivers
 */
static int fdc_driver_in_list(const char *driver, int sel)
{
	char *ptr;
	int i;
	TRACE_FUN(ft_t_flow);

	for (i = 0, ptr = ft_fdc_driver[sel];
	     i < ft_fdc_driver_no[sel];
	     i++, ptr += strlen(ptr) + 1) {
		if (strcmp(driver, ptr) == 0) {
			TRACE(ft_t_noise, "Driver: %s", ptr);
			TRACE_EXIT 1;
		}		
	}
	TRACE_EXIT 0;
}

int fdc_register(fdc_operations *info)
{
	int sel;
	int success = 0;
	TRACE_FUN(ft_t_flow);

	printk(KERN_INFO "fdc_register: Registering driver \"%s\"\n", info->driver ? info->driver : "NULL");

	if (find_driver(info->driver) != NULL) {
		TRACE(ft_t_err,
		      "driver for \"%s\" like fdc already registered",
		      info->driver);
		printk(KERN_ERR "fdc_register: Driver \"%s\" already registered\n", info->driver);
		TRACE_EXIT -EBUSY;
	}

	/* now probe if there is a tape drive that can be driven by
	 * this driver ... 
	 *
	 * NOTE: this does NOT check whether there really is a tape drive
	 *       attached to the fdc in the case of internal tape drives.
	 *       It simply checks whether the fdc is there.
	 *
	 * The autodetection + memory allocation will be tried a
	 * second time for the benefit of external tape drives which
	 * need not be attached at kernel boot time.
	 */
	for (sel = FTAPE_SEL_A; sel <= FTAPE_SEL_D; sel++) {
		fdc_info_t *fdc;
		int force = fdc_driver_in_list(info->driver, sel);

		if (force) {
			success ++;
		}

		if (ft_fdc_driver[sel] == NULL || force) {

			if ((fdc = get_new_fdc(sel)) == NULL) {
				/* no memory or FDC table full */
				printk(KERN_WARNING "fdc_register: Failed to get new fdc for slot %d\n", sel);
				continue;
			}

			fdc->ops   = info;
			TRACE(ft_t_info,
			      "Probing for %s tape drive slot %d",
			      info->driver, fdc->unit);
			printk(KERN_INFO "fdc_register: Probing for %s tape drive slot %d\n", 
			       info->driver, fdc->unit);

			if (!info->detect) {
				printk(KERN_WARNING "fdc_register: No detect function for driver %s\n", info->driver);
				fdc_destroy(sel);
				continue;
			}
			
			printk(KERN_INFO "fdc_register: Calling detect function for slot %d\n", sel);
			if (info->detect(fdc) < 0) {
				printk(KERN_INFO "fdc_register: Detect function failed for slot %d\n", sel);
				fdc_destroy(sel);
				continue;
			}
			printk(KERN_INFO "fdc_register: Detect function successful for slot %d\n", sel);
			success ++;
		} else {
			printk(KERN_INFO "fdc_register: Skipping slot %d (already in use or not forced)\n", sel);
			continue;
		}
		fdc->initialized = 1; /* got all resources */
		fdc->driver = info->driver;

		/* Ok. Got it. Now get the
		 * memory. fdc->buffers_needed MUST be initialized
		 * by the detect_fdc() function.
		 */
		if (fdc_set_nr_buffers(fdc, fdc->buffers_needed) < 0) {
			/* okay, retry on open */
		}
	}
	if (success) {
		/* Ok, it's new. Simply insert it into the driver chain
		 */
		list_add(&info->node, &fdc_drivers);
		printk(KERN_INFO "fdc_register: Successfully registered driver \"%s\" with %d successful probes\n", info->driver, success);
		TRACE_EXIT 0;
	} else {
		printk(KERN_ERR "fdc_register: Failed to register driver \"%s\" - no successful probes\n", info->driver);
		TRACE_EXIT -ENXIO;
	}
}

void fdc_unregister(fdc_operations *info)
{
	int sel;
	TRACE_FUN(ft_t_flow);

	/* No locks needed.
	 */
	if (find_driver(info->driver) != info) {
		TRACE(ft_t_err, "\"%s\" not registered (fdc_ops @ %p)",
		      info->driver, info);
		TRACE_EXIT;
	}
	for (sel = FTAPE_SEL_A; sel <= FTAPE_SEL_D; sel++) {
		if (fdc_infos[sel] && (fdc_infos[sel]->ops == info)) {
			fdc_destroy(sel);
		}
	}
	list_del(&info->node);
	TRACE_EXIT;
}

/* try to initialize an already allocated fdc. If this fails, we try other
 * fdc drivers for the benefit of parallel port devices.
 *
 * Return values:
 * -ENXIO : hardware failure
 * -EAGAIN: no memory
 * 0      : success
 *
 * Side effects:
 * on -ENXIO the fdc is destroyed (i.e. its memory is released)
 */

#define GLOBAL_TRACING
#include "ftape-real-tracing.h"

static int fdc_init_one(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_flow);

	if (fdc->nr_buffers < fdc->buffers_needed) {
		/* We don't try as hard as in fdc_register() here ...
		 */		
		if (fdc_set_nr_buffers(fdc, fdc->buffers_needed) < 0) {
			TRACE(ft_t_err, "Couldn't allocate memory for \"%s\"",
			      fdc->driver);
			TRACE_EXIT -EAGAIN;
		}
	}

	/* try to grab the hardware */
	TRACE_CATCH(fdc->ops->grab(fdc),fdc_destroy(fdc->unit));

	fdc->active = 1; /* mark active */

	if (fdc->calibr_count == 0 && fdc->calibr_time == 0) {
		fdc_wait_calibrate(fdc);
	}
	
	fdc->hook = fdc_isr;	/* hook our handler in */
	fdc->motor = 0;
	fdc->expected_stray_interrupts = 0;
	/* fdc->ops->enable_irq(fdc); */
	fdc_reset(fdc);			/* init fdc & clear track counters */
	if (fdc->type == no_fdc) {
		fdc->type = fdc_probe(fdc);
		fdc_reset(fdc);		/* update with new knowledge */
	}
	if (fdc->type == no_fdc) {
		fdc->ops->release(fdc);
		fdc->active = 0;
		fdc_destroy(fdc->unit);
		TRACE_EXIT -ENXIO;
	}
	TRACE_EXIT 0;
}

/* Search a suitable driver for this fdc. If a list of suitable driver
 * has been specified in ft_fdc_driver[sel], then this drivers are
 * searched and we try to load them with kerneld or kmod.
 *
 * If ft_fdc_driver[sel} == NULL then all currently loaded fdc drivers
 * are tried before we give up.
 *
 * Return values:
 * -ENXIO : hardware failure
 * -EAGAIN: no memory
 * 0      : success
 *
 * return the value of fdc->ops->detect() if there is only a single
 * FDC driver for this devive.
 *
 * Side effects:
 * on -ENXIO the fdc is destroyed (i.e. its memory is released). On
 * -EAGAIN there was no memory for this fdc, so it doesn't exist.
 */

#define SEL_TRACING
#include "ftape-real-tracing.h"

static int fdc_search_driver(int sel)
{
	const char *ptr = ft_fdc_driver[sel];
	int result = -ENXIO;
	int cnt = 0;
	struct list_head *pos = fdc_drivers.next;
	fdc_info_t *fdc = fdc_infos[sel];
	TRACE_FUN(ft_t_flow);

	while (!fdc || !fdc->initialized) {
		if (ft_fdc_driver_no[sel] == 0) {
			/* no specific driver, try all registered ones. */
			if (pos == &fdc_drivers) {
				/* all entries processed or list empty. */
				fdc_destroy(sel);
				TRACE_EXIT -ENXIO;
			}
			fdc = get_new_fdc(sel);
			if (fdc == NULL) {
				TRACE_EXIT -EAGAIN;
			}
			fdc->ops = list_entry(pos, fdc_operations, node);
			pos = pos->next;
		} else {
			/* loop over desired drivers, try to autoload */
			if (cnt >= ft_fdc_driver_no[sel]) {
				/* no more drivers to try ... */
				fdc_destroy(sel);
				TRACE_EXIT (ft_fdc_driver_no[sel] == 1
					    ? result : -ENXIO);
			}
			fdc = get_new_fdc(sel);
			if (fdc == NULL) {
				TRACE_EXIT -EAGAIN;
			}
			fdc->ops = find_driver(ptr);
#ifdef FT_MODULE_AUTOLOAD
			if (fdc->ops == NULL) {
				(void)request_module(ptr);
				if ((fdc = fdc_infos[sel]) &&
				    (fdc->ops = find_driver(ptr)) &&
				    fdc->initialized) {
					/* success */
					TRACE(ft_t_flow, "success %d",
					      __LINE__);
					TRACE_EXIT 0;
				}
			}
#endif
			ptr += strlen(ptr) + 1;
			cnt ++;
			if (!fdc || !fdc->ops) { /* autoloading failed */
				continue;
			}
		}
		fdc->driver = fdc->ops->driver;
		if (fdc->ops->detect &&
		    (result = fdc->ops->detect(fdc)) >= 0) {
			TRACE(ft_t_flow, "success %d",
			      __LINE__);
			fdc->initialized = 1;
			TRACE_EXIT 0;
		} else {
			TRACE(ft_t_err,
			      "No tape drive found for \"%s\" driver",
			      fdc->driver);
			fdc->initialized = 0;
		}
	}
	TRACE_EXIT 0;
}

/* try to initialize the fdc for ftape->drive_sel. If there is non,
 * try to create one. If initialization fails, try to use different
 * drivers
 */

#define FTAPE_TRACING
#include "ftape-real-tracing.h"

int fdc_init(ftape_info_t *ftape)
{
	fdc_info_t *fdc = fdc_infos[ftape->drive_sel];
	TRACE_FUN(ft_t_flow);

	if (fdc) {
		fdc->ftape = ftape;
		ftape->fdc = fdc;
		switch(fdc_init_one(fdc)) {
		case 0:
			break; /* success */
		case -EAGAIN:
			ftape->fdc = NULL;
			TRACE_EXIT -EAGAIN; /* no memory */
		default:
		case -ENXIO:
			fdc = NULL;
			break;
		}
	}
	if (!fdc) { /* either not initialized yet, or parallel port
		     * device has changed ...
		     */
		ftape->fdc = NULL;
		TRACE_CATCH(fdc_search_driver(ftape->drive_sel),);
		fdc        = fdc_infos[ftape->drive_sel];
		fdc->ftape = ftape;
		ftape->fdc = fdc;
		TRACE_CATCH(fdc_init_one(fdc),ftape->fdc = NULL);
	}

	if (fdc->type >= i82077) {
		if (fdc_fifo_enable(fdc) < 0) {
			TRACE(ft_t_err, "couldn't enable fdc fifo !");
		}
	}

	/* set data rate in case the low level fdc module had been
	 * unloaded intermediately. This doesn't help in case the tape
	 * drive had been unplugged meanwhile. This is handled by
	 * ftape_init_drive() in ftape-ctl.c
	 */
	if (fdc->data_rate != ftape->data_rate) {
		fdc_set_data_rate(fdc, ftape->data_rate);
	}
	TRACE_EXIT 0;
}
