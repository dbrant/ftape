/*
 *      Copyright (C) 1994-1996 Bas Laarhoven,
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
 * $RCSfile: fdc-isr.c,v $
 * $Revision: 1.31 $
 * $Date: 2000/07/25 10:34:20 $
 *
 *      This file contains the interrupt service routine and
 *      associated code for the QIC-40/80/3010/3020 floppy-tape driver
 *      "ftape" for Linux.
 */

#include <asm/io.h>
#include <asm/dma.h>

#define volatile		/* */

#include <linux/ftape.h>
#include <linux/qic117.h>

#define FDC_TRACING
#include "ftape-tracing.h"

#include "fdc-isr.h"
#include "fdc-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-io.h"
#include "ftape-calibr.h"
#include "ftape-bsm.h"

/*      Global vars.
 */

/* rien */

/*      Local vars.
 */

static char *fdc_error_text(ft_error_cause_t cause)
{

	switch (cause) {
	case no_error:       return "no";
	case no_data_error:  return "no data";
	case id_am_error:    return "id am";
	case id_crc_error:   return "id crc";
	case data_am_error:  return "data am";
	case data_crc_error: return "data crc";
	case overrun_error:  return "overrun";
	default:             return "unknown";
	}
}

#define print_error_cause(cause) \
	TRACE(ft_t_noise, "%s error", fdc_error_text(cause))

static char *fdc_mode_txt(fdc_mode_enum mode)
{
	switch (mode) {
	case fdc_idle:
		return "fdc_idle";
	case fdc_reading_data:
		return "fdc_reading_data";
	case fdc_seeking:
		return "fdc_seeking";
	case fdc_writing_data:
		return "fdc_writing_data";
	case fdc_deleting:
		return "fdc_deleting";
	case fdc_reading_id:
		return "fdc_reading_id";
	case fdc_recalibrating:
		return "fdc_recalibrating";
	case fdc_formatting:
		return "fdc_formatting";
	case fdc_verifying:
		return "fdc_verifying";
	default:
		return "unknown";
	}
}

static inline ft_error_cause_t decode_irq_cause(fdc_info_t *fdc,
						fdc_mode_enum mode, __u8 st[])
{
	ft_error_cause_t cause = no_error;
	TRACE_FUN(ft_t_any);

	/*  Valid st[], decode cause of interrupt.
	 */
	switch (st[0] & ST0_INT_MASK) {
	case FDC_INT_NORMAL:
		TRACE(ft_t_fdc_dma,"normal completion: %s",fdc_mode_txt(mode));
		break;
	case FDC_INT_ABNORMAL:
		TRACE(ft_t_flow, "abnormal completion %s", fdc_mode_txt(mode));
		TRACE(ft_t_fdc_dma, "ST0: 0x%02x, ST1: 0x%02x, ST2: 0x%02x",
		      st[0], st[1], st[2]);
		TRACE(ft_t_fdc_dma,
		      "C: 0x%02x, H: 0x%02x, R: 0x%02x, N: 0x%02x",
		      st[3], st[4], st[5], st[6]);
		if (st[1] & 0x01) {
			if (st[2] & 0x01) {
				cause = data_am_error;
			} else {
				cause = id_am_error;
			}
		} else if (st[1] & 0x20) {
			if (st[2] & 0x20) {
				cause = data_crc_error;
			} else {
				cause = id_crc_error;
			}
		} else if (st[1] & 0x04) {
			cause = no_data_error;
		} else if (st[1] & 0x10) {
			cause = overrun_error;
		}
		print_error_cause(cause);
		break;
	case FDC_INT_INVALID:
		TRACE(ft_t_flow, "invalid completion %s", fdc_mode_txt(mode));
		break;
	case FDC_INT_READYCH:
		if (st[0] & ST0_SEEK_END) {
			TRACE(ft_t_flow, "drive poll completed");
		} else {
			TRACE(ft_t_flow, "ready change %s",fdc_mode_txt(mode));
		}
		break;
	default:
		break;
	}
	TRACE_EXIT cause;
}

static void update_history(ft_history_t *history, ft_error_cause_t cause)
{
	switch (cause) {
	case id_am_error:
		history->id_am_errors++;
		break;
	case id_crc_error:
		history->id_crc_errors++;
		break;
	case data_am_error:
		history->data_am_errors++;
		break;
	case data_crc_error:
		history->data_crc_errors++;
		break;
	case overrun_error:
		history->overrun_errors++;
		break;
	case no_data_error:
		history->no_data_errors++;
		break;
	default:
	}
}

static void skip_bad_sector(fdc_info_t *fdc, buffer_struct * buff)
{
	TRACE_FUN(ft_t_any);

	/*  Mark sector as soft error and skip it
	 */
	if (buff->remaining > 0) {
		++buff->sector_offset;
		++buff->data_offset;
		--buff->remaining;
		buff->ptr += FT_SECTOR_SIZE;
		buff->bad_sector_map >>= 1;
	} else {
		/*  Hey, what is this????????????? C code: if we shift
		 *  more than 31 bits, the shift wraps around.
		 */
		++buff->sector_offset;	/* hack for error maps */
		TRACE(ft_t_warn, "skipping past last sector in segment");
	}
	TRACE_EXIT;
}

static void update_error_maps(fdc_info_t *fdc, buffer_struct *buff,
			      unsigned int error_offset)
{
	int hard = 0;
	TRACE_FUN(ft_t_any);

	if (error_offset >= FT_SECTORS_PER_SEGMENT) {
		TRACE(ft_t_bug,
		      "BUG: offset %d would lead to an error map of 0x%08x",
		      error_offset, (1 << error_offset));
		TRACE_EXIT;
	}

	if (buff->retry < FT_SOFT_RETRIES) {
		buff->soft_error_map |= (1 << error_offset);
	} else {
		buff->hard_error_map |= (1 << error_offset);
		buff->soft_error_map &= ~buff->hard_error_map;
		buff->retry = -1;  /* will be set to 0 in setup_segment */
		hard = 1;
	}
	TRACE(ft_t_noise, "sector %d : %s error\n"
	      KERN_INFO "hard map: 0x%08lx\n"
	      KERN_INFO "soft map: 0x%08lx",
	      FT_SECTOR(error_offset), hard ? "hard" : "soft",
	      (long) buff->hard_error_map, (long) buff->soft_error_map);
	TRACE_EXIT;
}

static void print_progress(fdc_info_t *fdc,
			   buffer_struct *buff, ft_error_cause_t cause)
{
	TRACE_FUN(ft_t_any);

	switch (cause) {
	case no_error: 
		TRACE(ft_t_flow,"%d Sector(s) transfered", buff->sector_count);
		break;
	case no_data_error:
		TRACE(ft_t_flow, "Sector %d not found",
		      FT_SECTOR(buff->sector_offset));
		break;
	case overrun_error:
		/*  got an overrun error on the first byte, must be a
		 *  hardware problem
		 */
		TRACE(ft_t_bug,
		      "Unexpected error: failing DMA or FDC controller ?");
		break;
	case data_crc_error:
		TRACE(ft_t_flow, "Error in sector %d",
		      FT_SECTOR(buff->sector_offset - 1));
		break;
	case id_crc_error:
	case id_am_error:
	case data_am_error:
		TRACE(ft_t_flow, "Error in sector %d",
		      FT_SECTOR(buff->sector_offset));
		break;
	default:
		TRACE(ft_t_flow, "Unexpected error at sector %d",
		      FT_SECTOR(buff->sector_offset));
		break;
	}
	TRACE_EXIT;
}

/*
 *  Error cause:   Amount xferred:  Action:
 *
 *  id_am_error         0           mark bad and skip
 *  id_crc_error        0           mark bad and skip
 *  data_am_error       0           mark bad and skip
 *  data_crc_error    % 1024        mark bad and skip
 *  no_data_error       0           retry on write
 *                                  mark bad and skip on read
 *  overrun_error  [ 0..all-1 ]     mark bad and skip
 *  no_error           all          continue
 */

/* return the number of sectors transferred during read or write
 */

/* the Ditto EZ controller is different from the Intel FDCs. The
 * result bytes always give the new sector coordinats.
 *
 * all other FDCs unconditionally increment the cylinder address after
 * successfully transferring data, and set the sector address to 1.
 */
static inline int get_fdc_transfer_size(const buffer_struct *buff,
					const __u8 *fdc_result)
{
	if ((fdc_result[3] - buff->cyl) && fdc_result[5] == 1) {
		/* no error, all sectors transferred */
		return buff->sector_count;			
	}
	return fdc_result[5] - buff->sect - buff->sector_offset;
}

static inline int get_ez_transfer_size(const buffer_struct *buff,
				       const __u8 *fdc_result)
{
	int cyl_dist = fdc_result[3] - buff->cyl;
	
	if (cyl_dist < 0) {
		cyl_dist += 255;
	}
	return (cyl_dist * 128 + fdc_result[5] -
		buff->sect - buff->sector_offset);
}

static int get_transfer_size(fdc_info_t *fdc,
			     buffer_struct *buff,
			     const __u8 *fdc_result,
			     ft_error_cause_t cause)
{
	unsigned int fdc_count, dma_residue;
	TRACE_FUN(ft_t_any);

	fdc_count = (fdc->type == DITTOEZ)
		? get_ez_transfer_size(buff, fdc_result)
		: get_fdc_transfer_size(buff, fdc_result);

	dma_residue = fdc->ops->terminate_dma(fdc, cause);
	
	if (dma_residue == (unsigned int)(-1)) {
		/* no sanity check possible */
		TRACE_EXIT fdc_count;
	}
	
	dma_residue = ((dma_residue + (FT_SECTOR_SIZE - 1)) / FT_SECTOR_SIZE);
	
	/*  Sanity check. We allow fdc_count + dma_residue <
	 *  buff->sector_count. Conditions like these probably occur
	 *  when the DMA controller just writes the last byte to the
	 *  FIFO when the error happens. In this case the FDC has
	 *  precedence.
	 */
	if (fdc_count + dma_residue > buff->sector_count) {

		/* Wolf! This probably only happens with the Ditto EZ!
		 * While I think that I have understood the CRC error
		 * behaviour of the EZ controller, there still was one
		 * occasion where the FDC result was _REALLY_ one
		 * sector too large.
		 */
		TRACE(ft_t_err, "DMA residue <-> FDC result mismatch! "
		      "This is %s Ditto EZ FDC\n"
		      KERN_INFO "error       : %s\n"
		      KERN_INFO "fdc count   : %d\n"
		      KERN_INFO "dma residue : %d\n"
		      KERN_INFO "sector count: %d\n"
		      KERN_INFO "IRQ: C: %d, R: %d\n"
		      KERN_INFO "BUF: C: %d, R: %d/%d",
		      fdc->type == DITTOEZ ? "a" : "no",
		      fdc_error_text(cause),
		      fdc_count, dma_residue, buff->sector_count,
		      fdc_result[3], fdc_result[5],
		      buff->cyl, buff->sect, buff->sector_offset);

		buff->sector_count = 0; /* force retry */
		TRACE_EXIT -EAGAIN;
	}
	if (fdc_count == buff->sector_count && cause != no_error) {
		TRACE(ft_t_err, 
		      "ERROR: unexpected error after transfer had completed\n"
		      "This is %s Ditto EZ FDC\n"
		      KERN_INFO "segment     : %d\n"
		      KERN_INFO "error       : %s\n"
		      KERN_INFO "fdc count   : %d\n"
		      KERN_INFO "dma residue : %d\n"
		      KERN_INFO "sector count: %d\n"
		      KERN_INFO "IRQ: C: %d, R: %d\n"
		      KERN_INFO "BUF: C: %d, R: %d/%d",
		      fdc->type == DITTOEZ ? "a" : "no",
		      buff->segment_id,
		      fdc_error_text(cause),
		      fdc_count, dma_residue, buff->sector_count,
		      fdc_result[3], fdc_result[5],
		      buff->cyl, buff->sect, buff->sector_offset);
		
		buff->sector_count = 0; /* force retry */
		TRACE_EXIT -EAGAIN;
	}
	TRACE_EXIT fdc_count;
}

static void determine_verify_progress(fdc_info_t *fdc,
				      buffer_struct *buff,
				      ft_error_cause_t cause,
				      const __u8 *fdc_result)
{
	int sector_count;
	TRACE_FUN(ft_t_any);

	sector_count = get_transfer_size(fdc, buff, fdc_result, cause);

	if (sector_count == -EAGAIN) {
		sector_count = 0;
	}

	if (cause == no_error && sector_count == buff->sector_count) {
		buff->sector_offset = FT_SECTORS_PER_SEGMENT;
		buff->remaining     = 0;
		if (TRACE_LEVEL >= ft_t_flow) {
			print_progress(fdc, buff, cause);
		}
	} else {
		buff->sector_count   = sector_count;
		buff->sector_offset += sector_count;
		buff->remaining = FT_SECTORS_PER_SEGMENT - buff->sector_offset;

		TRACE(ft_t_noise, "%ssector offset: 0x%04x", 
		      (cause == no_error) ? "unexpected " : "",
		      buff->sector_offset);
		switch (cause) {
		case overrun_error:
			/* don't really count overrun errors
			 */
			if (buff->retry > 0) {
				buff->retry --;
				buff->short_start = 0;
			}
			break;
#if 0
		case no_data_error:
			break;
#endif
		default:
			buff->short_start = 1;
			buff->retry = FT_SOFT_RETRIES;
			break;
		}
		if (TRACE_LEVEL >= ft_t_flow) {
			print_progress(fdc, buff, cause);
		}
		/*  Sector_offset points to the problem area Now adjust
		 *  sector_offset so it always points one past he failing
		 *  sector. I.e. skip the bad sector.
		 */
		++buff->sector_offset;
		--buff->remaining;
		update_error_maps(fdc, buff, buff->sector_offset - 1);
		
		/* reduce possibility of no data errors caused by
		 * setup timing. ftformat surrounds each bad sector by
		 * one additional bad sector for safety, so we don't
		 * really need to check the following sector.
		 */
		if (buff->remaining > 0) {
			++buff->sector_offset;
			--buff->remaining;			
		}
	}
	TRACE_EXIT;
}

static void determine_progress(fdc_info_t *fdc,
			       buffer_struct *buff,
			       ft_error_cause_t cause,
			       int transferred)
{
	TRACE_FUN(ft_t_any);

	if (cause != no_error || buff->sector_count != transferred) {
		if (cause != no_error && buff->sector_count != transferred) {
			TRACE(ft_t_noise, "DMA residue: %d", 
			      (buff->sector_count - transferred) 
			      * FT_SECTOR_SIZE);
		} else { /* something strange is going on */
			if (cause == no_error) {
				TRACE(ft_t_warn,
				      "WARNING: unexpected DMA residue: %d"
				      "in segment %d",
				      (buff->sector_count - transferred
				       * FT_SECTOR_SIZE),
				      buff->segment_id);
			} else if (buff->sector_count == transferred) {
				TRACE(ft_t_warn,
				      "WARNING: unexpected error \"%s\" "
				      "after transfer had completed (%d/%d)"
				      "in segment %d",
				      fdc_error_text(cause),
				      buff->sector_count, transferred,
				      buff->segment_id);
				/* better retry entire segment ... :-( */
				TRACE(ft_t_warn, "Retrying last sector");
				transferred --;
			}
		}
		buff->sector_count = transferred;
	}
	/*  Update var's influenced by the DMA operation.
	 */
	if (buff->sector_count > 0) {
		buff->sector_offset   += buff->sector_count;
		buff->data_offset     += buff->sector_count;
		buff->ptr             += (buff->sector_count *
					  FT_SECTOR_SIZE);
		buff->remaining       -= buff->sector_count;
		buff->bad_sector_map >>= buff->sector_count;
	}
	if (TRACE_LEVEL >= ft_t_flow) {
		print_progress(fdc, buff, cause);
	}
	if (cause != no_error) {
#if 0
		TRACE(ft_t_info, "Error \"%s\" at %d/%d",
		      fdc_error_text(cause), buff->segment_id,
		      buff->sector_offset);

#endif
		switch (cause) {
		case overrun_error:
			/* don't really count overrun errors
			 */
			if (buff->retry > 0) {
				buff->retry --;
				buff->short_start = 0;
			}
			TRACE(ft_t_info, "Error \"%s\" at %d/%d",
			      fdc_error_text(cause), buff->segment_id,
			      buff->sector_offset);
#if 0 || HACK
			if (HEAD->status == writing) {
				TRACE(ft_t_warn,
				      "Overrun error, "
				      "forcing retry of entire segment");
				HEAD->status = fatal_error;
			}
#endif
			break;
		default:
			buff->short_start = 1;
 			break;
		}
		if (buff->remaining == 0) {
			TRACE(ft_t_warn,
			      "No data remaining, but \"%s\" error\n"
			      KERN_INFO "count : %d\n"
			      KERN_INFO "offset: %d\n",
			      fdc_error_text(cause),
			      buff->sector_count,
			      buff->sector_offset);
		}
		/*  Sector_offset points to the problem area, except
		 *  if we got a data_crc_error. In that case it points
		 *  one past the failing sector.
		 *
		 *  Now adjust sector_offset so it always points one
		 *  past he failing sector. I.e. skip the bad sector.
		 */
		skip_bad_sector(fdc, buff);
		update_error_maps(fdc, buff, buff->sector_offset - 1);
	}
	TRACE_EXIT;
}

static int calc_steps(fdc_info_t *fdc, int cmd)
{
	if (fdc->current_cylinder > cmd) {
		return fdc->current_cylinder - cmd;
	} else {
		return fdc->current_cylinder + cmd;
	}
}


static void schedule_pause_tape(fdc_info_t *fdc, int retry, int mode)
{
	ftape_info_t *ftape = fdc->ftape;
	TRACE_FUN(ft_t_any);

	/*  We'll use a raw seek command to get the tape to rewind and
	 *  stop for a retry.
	 */
	if (qic117_cmds[ftape->current_command].non_intr) {
		TRACE(ft_t_warn, "motion command may be issued too soon");
	}
	if (retry && (mode == fdc_reading_data ||
		      mode == fdc_reading_id   ||
		      mode == fdc_verifying)) {
		ftape->current_command = QIC_MICRO_STEP_PAUSE;
		ftape->might_be_off_track = 1;
		TRACE(ft_t_noise, "calling MICRO_STEP_PAUSE");
	} else {
		ftape->current_command = QIC_PAUSE;
		TRACE(ft_t_noise, "calling PAUSE");
	}
	fdc->hide_interrupt = 1;
	TRACE_EXIT;
}

void pause_tape(fdc_info_t *fdc)
{
	ftape_info_t *ftape = fdc->ftape;
	__u8 out[3] = {FDC_SEEK, ftape->drive_sel, 0};
	int result;
	TRACE_FUN(ft_t_any);

	/*  We'll use a raw seek command to get the tape to rewind and
	 *  stop for a retry.
	 */
	++(ftape->history.rewinds);
	fdc->seek_track = out[2] = calc_steps(fdc, ftape->current_command);
	/* issue QIC_117 command */		
	if ((result = __fdc_issue_command(fdc, out, 3, NULL, 0)) < 0) {
		TRACE(ft_t_noise, "qic-pause failed, status = %d", result);
		TRACE_EXIT;
	}
	fdc->seek_result      = 0;
	ftape->location.known = 0;
	ftape->tape_running   = 0;
	TRACE_EXIT;
}

static void continue_xfer(fdc_info_t *fdc,
			  fdc_mode_enum mode, 
			  unsigned int skip)
{
	ftape_info_t *ftape = fdc->ftape;
	int write = 0;
 	TRACE_FUN(ft_t_any);

	if (mode == fdc_writing_data || mode == fdc_deleting) {
		write = 1;
	}
	/*  This part can be removed if it never happens
	 */
	if (skip > 0 &&
	    (ftape->runner_status != running ||
	     (write && (HEAD->status != writing)) ||
	     (!write && (HEAD->status != reading && 
			 HEAD->status != verifying)))) {
		TRACE(ft_t_err, "unexpected runner/buffer state %d/%d",
		      ftape->runner_status, HEAD->status);
		HEAD->status = error;
		/* finish this buffer: */
		HEAD = HEAD->next;
		ftape->runner_status = aborting;
		fdc->mode     = fdc_idle;
	} else if (HEAD->remaining > 0 && ftape_calc_next_cluster(HEAD) > 0) {
		/*  still sectors left in current segment, continue
		 *  with this segment
		 */
		if (fdc_setup_read_write(fdc, HEAD, mode) < 0) {
			/* failed, abort operation
			 */
			HEAD->bytes = HEAD->ptr - HEAD->dma_address;
			HEAD->status = error;
			/* finish this buffer: */
			HEAD = HEAD->next;
			ftape->runner_status = aborting;
			fdc->mode     = fdc_idle;
		}
	} else {
		/* current segment completed
		 */
		unsigned int last_segment = HEAD->segment_id;
		int eot = ((last_segment + 1) % ftape->segments_per_track) == 0;
		unsigned int next = HEAD->next_segment;	/* 0 means stop ! */

		HEAD->bytes = HEAD->ptr - HEAD->dma_address;
		HEAD->status = done;
		HEAD = HEAD->next;
		if (eot) {
			/*  finished last segment on current track,
			 *  can't continue
			 */
			ftape->runner_status = logical_eot;
			fdc->mode     = fdc_idle;
			TRACE_EXIT;
		}
		if (next <= 0) {
			/*  don't continue with next segment
			 */
			TRACE(ft_t_noise, "no %s allowed, stopping tape",
			      (write) ? "write next" : "read ahead");
			schedule_pause_tape(fdc, 0, mode);
			ftape->runner_status = idle;  /*  not quite true until
						   *  next irq 
						   */
			TRACE_EXIT;
		}
		/*  continue with next segment
		 */
		if (HEAD->status != waiting) {
			TRACE(ft_t_noise, "all input buffers %s, pausing tape",
			      (write) ? "empty" : "full");
			schedule_pause_tape(fdc, 0, mode);
			ftape->runner_status = idle;  /*  not quite true until
						   *  next irq 
						   */
			TRACE_EXIT;
		}
		if (write && next != HEAD->segment_id) {
			TRACE(ft_t_noise, 
			      "segments out of order, aborting write");
			ftape->runner_status = do_abort;
			fdc->mode     = fdc_idle;
			TRACE_EXIT;
		}
		ftape_setup_new_segment(ftape, HEAD, next, 0);
		if (fdc->stop_read_ahead) {
			HEAD->next_segment = 0;
			fdc->stop_read_ahead = 0;
		}
		if (ftape_calc_next_cluster(HEAD) == 0 ||
		    fdc_setup_read_write(fdc, HEAD, mode) != 0) {
			TRACE(ft_t_err, "couldn't start %s-ahead",
			      write ? "write" : "read");
			ftape->runner_status = do_abort;
			fdc->mode     = fdc_idle;
		} else {
			/* keep on going */
			switch (ftape->driver_state) {
			case   reading: HEAD->status = reading;   break;
			case verifying: HEAD->status = verifying; break;
			case   writing: HEAD->status = writing;   break;
			case  deleting: HEAD->status = deleting;  break;
			default:
				TRACE(ft_t_err, 
		      "BUG: ftape->driver_state %d should be one out of "
		      "{reading, writing, verifying, deleting}",
				      ftape->driver_state);
				HEAD->status = write ? writing : reading;
				break;
			}
		}
	}
	TRACE_EXIT;
}

static void retry_sector(fdc_info_t *fdc,
			 buffer_struct *buff, 
			 int mode,
			 unsigned int skip)
{
	ftape_info_t *ftape = fdc->ftape;
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_noise, "%s error, will retry",
	      (mode == fdc_writing_data || mode == fdc_deleting)
	      ? "write" : "read");
	schedule_pause_tape(fdc, 1, mode);
	ftape->runner_status = aborting;
	buff->status         = error;
	buff->skip           = skip;
	TRACE_EXIT;
}

static unsigned int find_resume_point(fdc_info_t *fdc, buffer_struct *buff)
{
	int i = 0;
	SectorMap mask;
	SectorMap map;
	TRACE_FUN(ft_t_any);

	/*  This function is to be called after all variables have been
	 *  updated to point past the failing sector.
	 *  If there are any soft errors before the failing sector,
	 *  find the first soft error and return the sector offset.
	 *  Otherwise find the last hard error.
	 *  Note: there should always be at least one hard or soft error !
	 */
	if (buff->sector_offset < 1 || buff->sector_offset > 32) {
		TRACE(ft_t_bug, "BUG: sector_offset = %d",
		      buff->sector_offset);
		TRACE_EXIT 0;
	}
	if (buff->sector_offset >= 32) { /* C-limitation on shift ! */
		mask = 0xffffffff;
	} else {
		mask = (1 << buff->sector_offset) - 1;
	}
	map = buff->soft_error_map & mask;
	if (map) {
		while ((map & (1 << i)) == 0) {
			++i;
		}
		TRACE(ft_t_noise, "at sector %d", FT_SECTOR(i));
	} else {
		map = buff->hard_error_map & mask;
		i = buff->sector_offset - 1;
		if (map) {
			while ((map & (1 << i)) == 0) {
				--i;
			}
			TRACE(ft_t_noise, "after sector %d", FT_SECTOR(i));
			++i; /* first sector after last hard error */
		} else {
			TRACE(ft_t_bug, "BUG: no soft or hard errors");
		}
	}
	TRACE_EXIT i;
}

/*  check possible dma residue when formatting, update position record in
 *  buffer struct. This is, of course, modelled after determine_progress(), but
 *  we don't need to set up for retries because the format process cannot be
 *  interrupted (except at the end of the tape track).
 */
static int determine_fmt_progress(fdc_info_t *fdc,
				  buffer_struct *buff, ft_error_cause_t cause)
{
	ftape_info_t *ftape = fdc->ftape;
	unsigned int dma_residue;
	TRACE_FUN(ft_t_any);

	dma_residue = fdc->ops->terminate_dma(fdc, cause);
	if (dma_residue != (unsigned int)(-1) && dma_residue != 0) {
		TRACE(ft_t_err, "Unexpected dma residue %d at segment %d",
		      dma_residue, buff->segment_id);
		ftape->runner_status = do_abort;
		buff->status = error;
		TRACE_EXIT -EIO;
	}
	if (cause != no_error) {
		fdc->mode = fdc_idle;
		switch(cause) {
		case no_error:
			ftape->runner_status = aborting;
			buff->status = idle;
			break;
		case overrun_error:
			/*  got an overrun error on the first byte, must be a
			 *  hardware problem 
			 */
			TRACE(ft_t_bug, 
			      "Unexpected error: failing DMA controller ?");
			ftape->runner_status = do_abort;
			buff->status = error;
			break;
		default:
			TRACE(ft_t_noise, "Unexpected error at segment %d",
			      buff->segment_id);
			ftape->runner_status = do_abort;
			buff->status = error;
			break;
		}
		TRACE_EXIT -EIO; /* can only retry entire track in format mode
				  */
	}
	/*  Update var's influenced by the DMA operation.
	 */
	buff->ptr             += FT_SECTORS_PER_SEGMENT * 4;
	buff->bytes           -= FT_SECTORS_PER_SEGMENT * 4;
	buff->remaining       -= FT_SECTORS_PER_SEGMENT;
	buff->segment_id ++; /* done with segment */
	TRACE_EXIT 0;
}

/*
 *  Continue formatting, switch buffers if there is no data left in
 *  current buffer. This is, of course, modelled after
 *  continue_xfer(), but we don't need to set up for retries because
 *  the format process cannot be interrupted (except at the end of the
 *  tape track).
 */
static void continue_formatting(fdc_info_t *fdc)
{
	ftape_info_t *ftape = fdc->ftape;
	TRACE_FUN(ft_t_any);

	if (HEAD->remaining <= 0) { /*  no space left in dma buffer */
		unsigned int next = HEAD->next_segment; 

		if (next == 0) { /* end of tape track */
			HEAD->status     = done;
			ftape->runner_status = logical_eot;
			fdc->mode     = fdc_idle;
			TRACE(ft_t_noise, "Done formatting track %d",
			      ftape->location.track);
			TRACE_EXIT;
		}
		/*
		 *  switch to next buffer!
		 */
		HEAD->status   = done;
		HEAD = HEAD->next;

		if (HEAD->status != waiting  || next != HEAD->segment_id) {
			goto format_setup_error;
		}
	}
	if (fdc_setup_formatting(fdc, HEAD) < 0) {
		goto format_setup_error;
	}
	HEAD->status = formatting;
	TRACE(ft_t_fdc_dma, "Formatting segment %d on track %d",
	      HEAD->segment_id, ftape->location.track);
	TRACE_EXIT;
 format_setup_error:
	ftape->runner_status = do_abort;
	fdc->mode     = fdc_idle;
	HEAD->status         = error;
	TRACE(ft_t_err, "Error setting up for segment %d on track %d",
	      HEAD->segment_id, ftape->location.track);
	TRACE_EXIT;

}

/*  this handles writing, read id, reading and formatting
 */
static void handle_fdc_busy(fdc_info_t *fdc)
{
	ftape_info_t *ftape = fdc->ftape;
	int retry = 0;
	int transferred = 0;
	ft_error_cause_t cause;
	__u8 in[7];
	int skip;
	fdc_mode_enum fmode = fdc->mode;
	TRACE_FUN(ft_t_any);

	if (fdc_result(fdc, in, 7) < 0) { /* better get it fast ! */
		TRACE(ft_t_err,"Probably fatal error during FDC Result Phase\n"
		      KERN_INFO "drive may hang until (power on) reset :-(");
		/*  what to do next ????
		 */
		TRACE_EXIT;
	}
	cause = decode_irq_cause(fdc, fdc->mode, in);

	if (fmode == fdc_reading_data && ftape->driver_state == verifying) {
		fmode = fdc_verifying;
	}

	switch (fmode) {
	case fdc_verifying:
		if (ftape->runner_status == aborting ||
		    ftape->runner_status == do_abort) {
			TRACE(ft_t_noise,"aborting %s",
			      fdc_mode_txt(fdc->mode));
			break;
		}
		if (HEAD->retry > 0) {
			TRACE(ft_t_flow, "this is retry nr %d", HEAD->retry);
		}
		switch (cause) {
		case no_error:
			fdc->no_data_error_count = 0;
			determine_verify_progress(fdc, HEAD, cause, in);
			if (in[2] & 0x40) {
				/*  This should not happen when verifying
				 */
				TRACE(ft_t_warn,
				      "deleted data in segment %d/%d",
				      HEAD->segment_id,
				      FT_SECTOR(HEAD->sector_offset - 1));
				HEAD->remaining = 0; /* abort transfer */
				HEAD->hard_error_map = EMPTY_SEGMENT;
				skip = 1;
			} else {
				skip = 0;
			}
			continue_xfer(fdc, fdc->mode, skip);
			break;
		case no_data_error:
			fdc->no_data_error_count ++;
		case overrun_error:
			retry ++;
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case data_crc_error:
			determine_verify_progress(fdc, HEAD, cause, in); 
			if (cause == no_data_error) {
				if (fdc->no_data_error_count >= 2) {
					TRACE(ft_t_warn,
					      "retrying because of successive "
					      "no data errors");
					fdc->no_data_error_count = 0;
				} else {
					retry --;
				}
			} else {
				fdc->no_data_error_count = 0;
			}
			if (retry) {
				skip = find_resume_point(fdc, HEAD);
			} else {
				skip = HEAD->sector_offset;
			}
			if (retry && skip < 32) {
				retry_sector(fdc, HEAD, fdc->mode, skip);
			} else {
				continue_xfer(fdc, fdc->mode, skip);
			}
			update_history(&fdc->ftape->history, cause);
			break;
		default:
			/*  Don't know why this could happen 
			 *  but find out.
			 */
			determine_verify_progress(fdc, HEAD, cause, in);
			retry_sector(fdc, HEAD, fdc->mode, 0);
			TRACE(ft_t_err, "Error: unexpected error");
			break;
		}
		break;
	case fdc_reading_data:
#ifdef TESTING
		/* I'm sorry, but: NOBODY ever used this trace
		 * messages for ages. I guess that Bas was the last person
		 * that ever really used this (thank you, between the lines)
		 */
		if (cause == no_error) {
			TRACE(ft_t_flow,"reading segment %d",HEAD->segment_id);
		} else {
			TRACE(ft_t_noise, "error reading segment %d",
			      HEAD->segment_id);
			TRACE(ft_t_noise, "\n"
			      KERN_INFO
			     "IRQ:C: 0x%02x, H: 0x%02x, R: 0x%02x, N: 0x%02x\n"
			      KERN_INFO
			      "BUF:C: 0x%02x, H: 0x%02x, R: 0x%02x",
			      in[3], in[4], in[5], in[6],
			      HEAD->cyl, HEAD->head, HEAD->sect);
		}
#endif
		if (ftape->runner_status == aborting ||
		    ftape->runner_status == do_abort) {
			TRACE(ft_t_noise,"aborting %s",fdc_mode_txt(fdc->mode));
			break;
		}
		/* get_transfer_size() also terminates the dma
		 * transfer properly, which probably also should be done in 
		 * case of a FAKE_SEGMENT.
		 */
		transferred = get_transfer_size(fdc, HEAD, in, cause);

		/* This condition occurs when reading a `fake' sector
		 * that's not accessible. Doesn't really matter as we
		 * would have ignored it anyway !
		 *
		 * Chance is that we're past the next segment now, so
		 * the next operation may fail and result in a retry.
		 */
		if (HEAD->bad_sector_map == FAKE_SEGMENT) {
			HEAD->remaining = 0;	/* skip failing sector */
			/* HEAD->ptr       = HEAD->dma_address; */
			/* fake success: */
			continue_xfer(fdc, fdc->mode, 1);
			/*  trace calls are expensive: place them AFTER
			 *  the real stuff has been done.
			 *  
			 */
			TRACE(ft_t_noise, "skipping empty segment %d (read), size? %ld",
			      HEAD->segment_id, HEAD->ptr - HEAD->dma_address);
			break;
		}

		if (HEAD->retry > 0) {
			TRACE(ft_t_flow, "this is retry nr %d", HEAD->retry);
		}

		if (transferred == -EAGAIN) {
			/* fdc result <-> dma residue count */
			retry_sector(fdc, HEAD, fdc->mode, 0);
			break;
		}

		switch (cause) {
		case no_error:
			if (in[2] & 0x40) {
				/*  Handle deleted data in header segments.
				 * Skip segment and force read-ahead.  */
				TRACE(ft_t_warn,
				      "deleted data in segment %d/%d",
				      HEAD->segment_id,
				      FT_SECTOR(HEAD->sector_offset - 1));
				HEAD->deleted = 1;
				HEAD->remaining = 0;/*abort transfer */
				HEAD->soft_error_map |=
						(-1L << HEAD->sector_offset);
				if (HEAD->segment_id == 0) {
					/* stop on next segment */
					fdc->stop_read_ahead = 1;
				}
				/* force read-ahead: */
				HEAD->next_segment = 
					HEAD->segment_id + 1;
				skip = (FT_SECTORS_PER_SEGMENT - 
					HEAD->sector_offset);
			} else {
				skip = 0;
				determine_progress(fdc,
						   HEAD, cause, transferred);
			}
			continue_xfer(fdc, fdc->mode, skip);
			break;
		case no_data_error:
			/* Tape started too far ahead of or behind the
			 * right sector.  This may also happen in the
			 * middle of a segment !
			 *
			 * Handle no-data as soft error. If next
			 * sector fails too, a retry (with needed
			 * reposition) will follow.
			 */
			/* retry ++; */
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case data_crc_error:
		case overrun_error:
			retry += (HEAD->soft_error_map != 0 ||
				  HEAD->hard_error_map != 0);
			determine_progress(fdc, HEAD, cause, transferred); 
			if (retry) {
				skip = find_resume_point(fdc, HEAD);
			} else {
				skip = HEAD->sector_offset;
			}
			/*  Try to resume with next sector on single
			 *  errors (let ecc correct it), but retry on
			 *  no_data (we'll be past the target when we
			 *  get here so we cannot resume) or on
			 *  multiple errors (reduce chance on ecc
			 *  failure).
			 */
			/*  cH: 23/02/97: if the last sector in the
			 *  segment was a hard error, then it doesn't
			 *  make sense to retry. This occasion seldom
			 *  occurs but ... @:,`@%&$
			 */
			if (retry && skip < 32) {
				retry_sector(fdc, HEAD, fdc->mode, skip);
			} else {
				continue_xfer(fdc, fdc->mode, skip);
			}
			update_history(&fdc->ftape->history, cause);
			break;
		default:
			/*  Don't know why this could happen but find
			 *  out.
			 */
			determine_progress(fdc, HEAD, cause, transferred);
			retry_sector(fdc, HEAD, fdc->mode, 0);
			TRACE(ft_t_err, "Error: unexpected error");
			break;
		}
		break;
	case fdc_reading_id:
		if (cause == no_error) {
			fdc->cyl = in[3];
			fdc->head = in[4];
			fdc->sect = in[5];
			TRACE(ft_t_fdc_dma,
			      "id read: C: 0x%02x, H: 0x%02x, R: 0x%02x",
			      fdc->cyl, fdc->head, fdc->sect);
		} else {	/* no valid information, use invalid sector */
			fdc->cyl = fdc->head = fdc->sect = 0;
			TRACE(ft_t_noise /* flow */,
			      "Didn't find valid sector Id");
			print_error_cause(cause);
		}
		fdc->mode = fdc_idle;
		break;
	case fdc_deleting:
	case fdc_writing_data:
#if defined(TEST_HARD_ERRORS)
		if (HEAD->segment_id % 3 == 0 && HEAD->sector_offset == 0) {
			TRACE(ft_t_info, "faking error cause for segment %d",
			      HEAD->segment_id);
			cause = no_data_error;
		}
#endif
#ifdef TESTING
		if (cause == no_error) {
			TRACE(ft_t_flow, "writing segment %d", HEAD->segment_id);
		} else {
			TRACE(ft_t_noise, "error writing segment %d",
			      HEAD->segment_id);
		}
#endif
		if (ftape->runner_status == aborting ||
		    ftape->runner_status == do_abort) {
			TRACE(ft_t_flow, "aborting %s",fdc_mode_txt(fdc->mode));
			break;
		}

		/* get_transfer_size() also terminates the dma
		 * transfer properly, which probably also should be done in 
		 * case of a FAKE_SEGMENT.
		 */
		transferred = get_transfer_size(fdc, HEAD, in, cause);

		/* This condition occurs when trying to write to a
		 * `fake' sector that's not accessible. Doesn't really
		 * matter as it isn't used anyway ! Might be located
		 * at wrong segment, then we'll fail on the next
		 * segment.
		 */
		if (HEAD->bad_sector_map == FAKE_SEGMENT) {
			TRACE(ft_t_noise, "skipping empty segment (write)");
			HEAD->remaining = 0;	/* skip failing sector */
			/* fake success: */
			continue_xfer(fdc, fdc->mode, 1);
			break;
		}

		if (HEAD->retry > 0) {
			TRACE(ft_t_flow, "this is retry nr %d", HEAD->retry);
		}

		if (transferred == -EAGAIN) {
			/* fdc result <-> dma residue count
			 * mismatch. Probably caused by Ditto EZ
			 * controller
			 */
			retry_sector(fdc, HEAD, fdc->mode, 0);
			break;
		}
#if 0
No longer needed, I hope		
		/* move this check to determine progress or
		 * get_transfer_size
		 */
		if (cause == no_error && HEAD->sector_count != transferred) {
			TRACE(ft_t_warn,
			      "Warning: no error, "
			      "fdc_count %d, but fdc returned %d",
			      HEAD->sector_count, transferred);
			retry_sector(fdc, HEAD, fdc->mode, 0);
			break;
		}
#endif

		determine_progress(fdc, HEAD, cause, transferred);
		switch (cause) {
		case no_error:
			continue_xfer(fdc, fdc->mode, 0);
			break;
		case no_data_error:
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case overrun_error:
			skip = find_resume_point(fdc, HEAD);
			retry_sector(fdc, HEAD, fdc->mode, skip);
			update_history(&fdc->ftape->history, cause);
			break;
		default:
			if (in[1] & 0x02) {
				TRACE(ft_t_err, "media not writable");
			} else {
				TRACE(ft_t_bug, "unforeseen write error");
			}
			fdc->mode = fdc_idle;
			break;
		}
		break; /* fdc_deleting || fdc_writing_data */
	case fdc_formatting:
		/*  The interrupt comes after formatting a segment. We then
		 *  have to set up QUICKLY for the next segment. But
		 *  afterwards, there is plenty of time.
		 */
		switch (cause) {
		case no_error:
			/*  would like to keep most of the formatting stuff
			 *  outside the isr code, but timing is too critical
			 */
			if (determine_fmt_progress(fdc, HEAD, cause) >= 0) {
				continue_formatting(fdc);
			}
			break;
		case no_data_error:
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case overrun_error:
		default:
			determine_fmt_progress(fdc, HEAD, cause);
			update_history(&fdc->ftape->history, cause);
			if (in[1] & 0x02) {
				TRACE(ft_t_err, "media not writable");
			} else {
				TRACE(ft_t_bug, "unforeseen format error");
			}
			break;
		} /* cause */
		break;
	default:
		TRACE(ft_t_warn, "Warning: unexpected irq during: %s",
		      fdc_mode_txt(fdc->mode));
		fdc->mode = fdc_idle;
		break;
	}
	if (ftape->runner_status == do_abort) {
		/*      cease operation, remember tape position
		 */
		TRACE(ft_t_flow, "runner aborting");
		ftape->runner_status = aborting;
		++(fdc->expected_stray_interrupts);
	}
	TRACE_EXIT;
}

static void handle_fdc_reset(fdc_info_t *fdc, int st0, int pcn)
{
	int i = 0;
	TRACE_FUN(ft_t_any);

	do {
		if (st0 == 0x80) {
			TRACE(ft_t_warn, "no sense necessary for %d",i);
			break;
		} else if (i == fdc->ftape->drive_sel) {
			fdc->current_cylinder = pcn;
		}
		TRACE(ft_t_flow, "st0=%x, i=%d", st0, i);
		i++ ;
		if (fdc_sense_interrupt_status(fdc, &st0, &pcn) != 0) {
			TRACE(ft_t_err, "sense failed for %d", i);
			break;
		}
	} while (i < 4);
	if (i > 0) {
		TRACE(ft_t_noise, "drive polling completed");
	}
	fdc->resetting = 0;
	TRACE_EXIT;
}	

static void handle_fdc_seek(fdc_info_t *fdc, int st0, int pcn)
{
	TRACE_FUN(ft_t_any);

	fdc->current_cylinder = pcn;
	if (fdc->hide_interrupt) {
		TRACE(ft_t_noise /*flow*/, "handled hidden interrupt");
		if (fdc->seek_track != fdc->current_cylinder) {
			TRACE(ft_t_err, "seek_track: %d, current_cylinder: %d",
			      fdc->seek_track, fdc->current_cylinder);
			
		}
	} else {
		fdc->seek_result = st0;
	}
	TRACE_EXIT;
}

static void handle_stray_interrupt(fdc_info_t *fdc, int st0, int pcn)
{
	TRACE_FUN(ft_t_flow);


	if (pcn != fdc->current_cylinder) {
		TRACE(ft_t_err, "pcn %d, current_cylinder %d",
		      pcn, fdc->current_cylinder);
		fdc->current_cylinder = pcn;
	}
#if LINUX_VERSION_CODE >= KERNEL_VER(2,0,16)
	if (waitqueue_active(&fdc->wait_intr))
#else
	if (fdc->wait_intr)
#endif
	{
		TRACE(ft_t_fdc_dma, "awaited stray interrupt");
		TRACE_EXIT;
	}
	if (fdc->expected_stray_interrupts == 0) {
		TRACE(ft_t_warn, "unexpected stray interrupt");
	} else {
		TRACE(ft_t_flow, "expected stray interrupt");
		--(fdc->expected_stray_interrupts);
	}
	TRACE_EXIT;
}

/*      FDC interrupt service routine.
 */
void fdc_isr(fdc_info_t *fdc)
{
	int count = 0;
	TRACE_FUN(ft_t_any);

	if (fdc->isr_active++) {
		TRACE(ft_t_bug, "BUG: nested interrupt, not good !");
		goto out;
	}
	fdc->hide_interrupt = 0;
	if (!fdc->resetting &&
	    fdc->mode != fdc_idle &&
	    fdc->mode != fdc_seeking &&
	    fdc->mode != fdc_recalibrating) {
		fdc_wait(fdc, FT_RQM_DELAY, FDC_BUSY, FDC_BUSY);
	} else {
		fdc_wait(fdc, FT_RQM_DELAY, FDC_DATA_READY, FDC_DATA_READY);
	}
	if (fdc->status & FDC_BUSY) {	/*  Entering Result Phase */
		handle_fdc_busy(fdc);
	} else for (;;) {
		int st0, pcn;
		fdc_mode_enum old_mode;

		if (!fdc->resetting &&
		    count == 0 &&
		    fdc->mode != fdc_idle &&
		    fdc->mode != fdc_seeking &&
		    fdc->mode != fdc_recalibrating) {
			if (fdc->ops->inp(fdc, fdc->msr) & FDC_BUSY) {
				TRACE(ft_t_bug,
				      "***** FDC failure, busy too late");
			} else {
				TRACE(ft_t_bug,
				      "***** FDC failure, no busy");
			}
			TRACE(ft_t_bug, "status was 0x%02x, is 0x%02x",
			      fdc->status,
			      fdc->ops->inp(fdc, fdc->msr));
			TRACE(ft_t_bug, "fdc_mode: %s (0x%02x)",
			      fdc_mode_txt(fdc->mode), (int)fdc->mode);
		}
		old_mode = fdc->mode;
		/*  clear interrupt, cause should be gotten by issuing
		 *  a Sense Interrupt Status command.
		 */
		if (fdc_sense_interrupt_status(fdc, &st0, &pcn) < 0) {
			fdc->mode = fdc_idle;
			TRACE(ft_t_warn, "sense interrupt status failed");
			break;
		} else if (st0 == 0x80) {
			/* no new interrupt pending */
			TRACE(ft_t_any, "no interrupt pending");
			fdc->mode = fdc_idle;
			break;
		} else if (++count > 10) {
			TRACE(ft_t_bug, "BUG? Looping in fdc_isr()");
			fdc->mode = fdc_idle;
			break;
		} else if (fdc->resetting) {
			handle_fdc_reset(fdc, st0, pcn);
		} else if (old_mode == fdc_seeking ||
			   old_mode == fdc_recalibrating) {
			handle_fdc_seek(fdc, st0, pcn);
		} else {
			handle_stray_interrupt(fdc, st0, pcn);
		}
		fdc->mode           = fdc_idle;
	}
	/*    Handle sleep code.
	 */
	if (!fdc->hide_interrupt) {
		fdc->interrupt_seen ++;
#if LINUX_VERSION_CODE >= KERNEL_VER(2,0,16)
		if (waitqueue_active(&fdc->wait_intr)) {
			wake_up_interruptible(&fdc->wait_intr);
		}
#else
		if ((fdc->wait_intr)) {
			wake_up_interruptible(&fdc->wait_intr);
		}
#endif
	} else {
#if LINUX_VERSION_CODE >= KERNEL_VER(2,0,16)
		TRACE(ft_t_flow, "hiding interrupt while %s", 
		      waitqueue_active(&fdc->wait_intr) ? "waiting":"active");
#else
		TRACE(ft_t_flow, "hiding interrupt while %s", 
		      fdc->wait_intr ? "waiting" : "active");
#endif
		pause_tape(fdc); /* hide interrupt means pause_tape() */
	}
out:
	fdc->hook = fdc_isr;	/* hook our handler into the fdc code again */
	--(fdc->isr_active);
	TRACE_EXIT;
}
