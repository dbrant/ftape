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
 * $RCSfile: ftape-rw.c,v $
 * $Revision: 1.23 $
 * $Date: 1999/02/25 00:11:17 $
 *
 *      This file contains some common code for the segment read and
 *      segment write routines for the QIC-117 floppy-tape driver for
 *      Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "ftape-tracing.h"
#include "ftape-rw.h"
#include "fdc-io.h"
#include "ftape-init.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-read.h"
#include "ftape-ecc.h"
#include "ftape-bsm.h"

/*      Global vars.
 */

extern int ft_soft_retries;

/*      Local vars.
 */

/* rien */

/*  maxmimal allowed overshoot when fast seeking
 *
 * maybe we should make this a per tape drive parameter and a field to
 * the "vendor_struct" variable
 */
#define OVERSHOOT_LIMIT 20 

/*  To ease hard write error recovery, we preserve, the value of
 *  fdc->buffer_tail.
 */
void ftape_reset_buffer(ftape_info_t *ftape)
{
	ftape->HEAD = ftape->TAIL = ftape->fdc->buffers;
	ftape->old_segment_id = -1;
}

buffer_state_enum ftape_set_state(ftape_info_t *ftape,
				  buffer_state_enum new_state)
{
	buffer_state_enum old_state = ftape->driver_state;

	ftape->driver_state = new_state;
	return old_state;
}
/*      Calculate Floppy Disk Controller and DMA parameters for a segment.
 *      head:   selects buffer struct in array.
 *      offset: number of physical sectors to skip (including bad ones).
 *      count:  number of physical sectors to handle (including bad ones).
 */
static int setup_segment(ftape_info_t *ftape,
			 buffer_struct * buff, 
			 int segment_id,
			 unsigned int sector_offset, 
			 unsigned int sector_count, 
			 int retry)
{
	SectorMap offset_mask;
	SectorMap mask;
	TRACE_FUN(ft_t_any);

	buff->segment_id = segment_id;
	buff->sector_offset = sector_offset;
	buff->remaining = sector_count;
	buff->head = segment_id / ftape->segments_per_head;
	buff->cyl = (segment_id % ftape->segments_per_head) / ftape->segments_per_cylinder;
	buff->sect = (segment_id % ftape->segments_per_cylinder) * FT_SECTORS_PER_SEGMENT + 1;
	buff->deleted = 0;
	offset_mask = (1 << buff->sector_offset) - 1;
	mask = ftape_get_bad_sector_entry(ftape, segment_id) & offset_mask;
	while (mask) {
		if (mask & 1) {
			offset_mask >>= 1;	/* don't count bad sector */
		}
		mask >>= 1;
	}
	buff->data_offset = count_ones(offset_mask);	/* good sectors to skip */
	buff->ptr = buff->dma_address + buff->data_offset * FT_SECTOR_SIZE;
	TRACE(ft_t_flow, "data offset = %d sectors", buff->data_offset);
	if (retry) {
		buff->soft_error_map &= offset_mask;	/* keep skipped part */
	} else {
		buff->hard_error_map = buff->soft_error_map = 0;
	}
	buff->bad_sector_map = ftape_get_bad_sector_entry(ftape, buff->segment_id);
	if (buff->bad_sector_map != 0) {
		TRACE(ft_t_noise, "segment: %d, bad sector map: %08lx",
			buff->segment_id, (long)buff->bad_sector_map);
	} else {
		TRACE(ft_t_flow, "segment: %d", buff->segment_id);
	}
	if (buff->sector_offset > 0) {
		buff->bad_sector_map >>= buff->sector_offset;
	}
	if (buff->sector_offset != 0 || buff->remaining != FT_SECTORS_PER_SEGMENT) {
		TRACE(ft_t_flow, "sector offset = %d, count = %d",
			buff->sector_offset, buff->remaining);
	}
	/*    Segments with 3 or less sectors are not written with valid
	 *    data because there is no space left for the ecc.  The
	 *    data written is whatever happens to be in the buffer.
	 *    Reading such a segment will return a zero byte-count.
	 *    To allow us to read/write segments with all bad sectors
	 *    we fake one readable sector in the segment. This
	 *    prevents having to handle these segments in a very
	 *    special way.  It is not important if the reading of this
	 *    bad sector fails or not (the data is ignored). It is
	 *    only read to keep the driver running.
	 *
	 *    The QIC-40/80 spec. has no information on how to handle
	 *    this case, so this is my interpretation.  
	 */
	if (buff->bad_sector_map == EMPTY_SEGMENT) {
		TRACE(ft_t_flow, "empty segment %d, fake first sector good",
		      buff->segment_id);
		if (buff->ptr != buff->dma_address) {
			TRACE(ft_t_bug, "This is a bug: %lx/%lx",
			      buff->ptr, buff->dma_address);
		}
		buff->bad_sector_map = FAKE_SEGMENT;
	}
	ftape->fdc->setup_error = 0;
	buff->next_segment = segment_id + 1;
	TRACE_EXIT 0;
}

/*      Calculate Floppy Disk Controller and DMA parameters for a new segment.
 */
int ftape_setup_new_segment(ftape_info_t *ftape,
			    buffer_struct * buff, int segment_id, int skip)
{
	int result = 0;
	int retry = 0;
	unsigned int offset = 0;
	int count = FT_SECTORS_PER_SEGMENT;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_flow, "%s segment %d (old = %d)",
	      (ftape->driver_state == reading || ftape->driver_state == verifying) 
	      ? "reading" : "writing",
	      segment_id, ftape->old_segment_id);
	if (ftape->driver_state != ftape->old_driver_state) {	/* when verifying */
		ftape->old_segment_id = -1;
		ftape->old_driver_state = ftape->driver_state;
	}
	if (segment_id == ftape->old_segment_id) {
		++buff->retry;
		++ftape->history.retries;
		TRACE(ft_t_flow, "setting up for retry nr %d", buff->retry);
		retry = 1;
		if (skip && buff->skip > 0) {	/* allow skip on retry */

			if (ft_ignore_ecc_err && (buff->skip > 2)) {
				TRACE(ft_t_flow, ">>> skipping to last sector in segment.");
				buff->skip = FT_SECTORS_PER_SEGMENT - 2;
			}

			offset = buff->skip;
			count -= offset;
			TRACE(ft_t_flow, "skipping %d sectors", offset);
		}
		buff->short_start = buff->short_start && (buff->skip > 0);
	} else {
		buff->short_start = 0;
		buff->retry       = 0;
		buff->skip        = 0;
		ftape->old_segment_id = segment_id;
	}
	result = setup_segment(ftape, buff, segment_id, offset, count, retry);
	TRACE_EXIT result;
}

/*      Determine size of next cluster of good sectors.
 */
int ftape_calc_next_cluster(buffer_struct * buff)
{
	/* Skip bad sectors.
	 */
	while (buff->remaining > 0 && (buff->bad_sector_map & 1) != 0) {
		buff->bad_sector_map >>= 1;
		++buff->sector_offset;
		--buff->remaining;
	}
	/* Find next cluster of good sectors
	 */
	if (buff->bad_sector_map == 0) {	/* speed up */
		buff->sector_count = buff->remaining;
	} else {
		SectorMap map = buff->bad_sector_map;

		buff->sector_count = 0;
		while (buff->sector_count < buff->remaining && (map & 1) == 0) {
			++buff->sector_count;
			map >>= 1;
		}
	}
	return buff->sector_count;
}

/*  if just passed the last segment on a track, wait for BOT
 *  or EOT mark.
 */
int ftape_handle_logical_eot(ftape_info_t *ftape)
{
	TRACE_FUN(ft_t_flow);

	if (ftape->runner_status == logical_eot) {
		int status;

		TRACE(ft_t_noise, "tape at logical EOT");
		TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.seek, &status),);
		if ((status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) == 0) {
			TRACE_ABORT(-EIO, ft_t_err, "eot/bot not reached");
		}
		ftape->runner_status = end_of_tape;
	}
	if (ftape->runner_status == end_of_tape) {
		TRACE(ft_t_noise, "runner stopped because of logical EOT");
		ftape->runner_status = idle;
	}
	TRACE_EXIT 0;
}

static int check_bot_eot(ftape_info_t *ftape, int status)
{
	TRACE_FUN(ft_t_flow);

	if (status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) {
		ftape->location.bot = ((ftape->location.track & 1) == 0 ?
				(status & QIC_STATUS_AT_BOT) != 0:
				(status & QIC_STATUS_AT_EOT) != 0);
		ftape->location.eot = !ftape->location.bot;
		ftape->location.segment = (ftape->location.track +
			(ftape->location.bot ? 0 : 1)) * ftape->segments_per_track - 1;
		ftape->location.sector = -1;
		ftape->location.known  = 1;
		TRACE(ft_t_flow, "tape at logical %s",
		      ftape->location.bot ? "bot" : "eot");
		TRACE(ft_t_flow, "segment = %d", ftape->location.segment);
	} else {
		ftape->location.known = 0;
	}
	TRACE_EXIT ftape->location.known;
}

/*      Read Id of first sector passing tape head.
 */
int ftape_read_id(ftape_info_t *ftape)
{
	int status;
	__u8 out[2];
	TRACE_FUN(ft_t_any);

	/* Assume tape is running on entry, be able to handle
	 * situation where it stopped or is stopping.
	 */
	ftape->location.known = 0;	/* default is location not known */
	out[0] = FDC_READID;
	out[1] = ftape->drive_sel;
	fdc_disable_irq(ftape->fdc);/* will be reset by fdc_interrupt_wait() */
	TRACE_CATCH(fdc_command(ftape->fdc, out, 2),
		    fdc_enable_irq(ftape->fdc));
	switch (fdc_interrupt_wait(ftape->fdc, 20 * FT_SECOND)) {
	case 0:
		if (ftape->fdc->sect == 0) {
			TRACE(ft_t_noise, "sect == 0");
			if (ftape_report_drive_status(ftape, &status) >= 0 &&
			    (status & QIC_STATUS_READY)) {
				ftape->tape_running = 0;
				TRACE(ft_t_flow, "tape has stopped");
				check_bot_eot(ftape, status);
			}
		} else {
			ftape->location.known = 1;
			ftape->location.segment = (ftape->segments_per_head
					       * ftape->fdc->head
					       + ftape->segments_per_cylinder
					       * ftape->fdc->cyl
					       + (ftape->fdc->sect - 1)
					       / FT_SECTORS_PER_SEGMENT);
			ftape->location.sector = ((ftape->fdc->sect - 1)
					      % FT_SECTORS_PER_SEGMENT);
			ftape->location.eot = ftape->location.bot = 0;
		}
		break;
	case -ETIME:
		TRACE(ft_t_noise, "timeout");
		/*  Didn't find id on tape, must be near end: Wait
		 *  until stopped.
		 */
		if (ftape_ready_wait(ftape, FT_FOREVER, &status) >= 0) {
			ftape->tape_running = 0;
			TRACE(ft_t_flow, "tape has stopped");
			check_bot_eot(ftape, status);
		}
		break;
	default:
		/*  Interrupted or otherwise failing
		 *  fdc_interrupt_wait() 
		 */
		TRACE(ft_t_err, "fdc_interrupt_wait failed");
		break;
	}
	if (!ftape->location.known) {
		TRACE_ABORT(-EIO, ft_t_flow, "no id found");
	}
	
	TRACE(ft_t_fdc_dma, "passing segment %d/%d",
	      ftape->location.segment, ftape->location.sector);

	TRACE_EXIT 0;
}

static int logical_forward(ftape_info_t *ftape)
{
	ftape->tape_running = 1;
	return ftape_command(ftape, QIC_LOGICAL_FORWARD);
}

int ftape_stop_tape(ftape_info_t *ftape, int *pstatus)
{
	int retry = 0;
	int result;
	TRACE_FUN(ft_t_flow);

	do {
		result = ftape_command_wait(ftape, QIC_STOP_TAPE,
					    ftape->timeout.stop, pstatus);
		if (result == 0) {
			if ((*pstatus & QIC_STATUS_READY) == 0) {
				result = -EIO;
			} else {
				ftape->tape_running = 0;
			}
		}
	} while (result < 0 && ++retry <= 3);
	if (result < 0) {
		TRACE(ft_t_err, "failed ! (fatal)");
	}
	TRACE_EXIT result;
}

int ftape_dumb_stop(ftape_info_t *ftape)
{
	int result;
	int status;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "HEAD->segment_id: %d", ftape->HEAD->segment_id);
	/*  Abort current fdc operation if it's busy (probably read
	 *  or write operation pending) with a reset.
	 */
	fdc_disable_irq(ftape->fdc);
	result = fdc_ready_wait(ftape->fdc, 100 /* usec */);
	fdc_enable_irq(ftape->fdc);
	if (result < 0) {
		TRACE(ft_t_noise, "aborting fdc operation");
		fdc_reset(ftape->fdc);
	}
	/*  Reading id's after the last segment on a track may fail
	 *  but eventually the drive will become ready (logical eot).
	 */
	result = ftape_report_drive_status(ftape, &status);
	ftape->location.known = 0;
	do {
		if (result == 0 && status & QIC_STATUS_READY) {
			/* Tape is not running any more.
			 */
			TRACE(ft_t_noise, "tape already halted");
			check_bot_eot(ftape, status);
			ftape->tape_running = 0;
		} else if (ftape->tape_running) {
			/*  Tape is (was) still moving.
			 */
#ifdef TESTING
			ftape_read_id();
#endif
			result = ftape_stop_tape(ftape, &status);
		} else {
			/*  Tape not yet ready but stopped.
			 */
			result = ftape_ready_wait(ftape, ftape->timeout.pause,&status);
		}
	} while (ftape->tape_running && !ft_sigtest(_NEVER_BLOCK));
#ifndef TESTING
	ftape->location.known = 0;
#endif
	if (ftape->runner_status == aborting || ftape->runner_status == do_abort) {
		ftape->runner_status = idle;
	}
	TRACE_EXIT result;
}

/*      Wait until runner has finished tail buffer.
 *
 */
int ftape_wait_segment(ftape_info_t *ftape, buffer_state_enum state)
{
	int status;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	for (;;) {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		fdc_disable_irq(ftape->fdc);
		if (ftape->fdc->buffer_tail->status != state) {
			fdc_enable_irq(ftape->fdc);
			break;
		}
		/*  fdc_interrupt_wait() will call fdc_enable_irq()
		 */
		TRACE(ft_t_flow, "state: %d", ftape->fdc->buffer_tail->status);
		/*  First buffer still being worked on, wait up to timeout.
		 */
		result = fdc_interrupt_wait(ftape->fdc, 50 * FT_SECOND);
		if (result < 0) {
			TRACE_ABORT(result,
				    ft_t_err, "fdc_interrupt_wait failed");
		}
		if (ftape->fdc->setup_error) {
			/* recover... FIXME */
			TRACE_ABORT(-EIO, ft_t_err, "setup error");
		}
	}
	if (ftape->fdc->buffer_tail->status != error) {
		TRACE_EXIT 0;
	}
	TRACE_CATCH(ftape_report_drive_status(ftape, &status),);
	TRACE(ft_t_noise, "ftape_report_drive_status: 0x%02x", status);
	if ((status & QIC_STATUS_READY) && 
	    (status & QIC_STATUS_ERROR)) {
		unsigned int error;
		qic117_cmd_t command;
		
		/*  Report and clear error state.
		 *  In case the drive can't operate at the selected
		 *  rate, select the next lower data rate.
		 */
		ftape_report_error(ftape, &error, &command, 1);
		if (error == 31 && command == QIC_LOGICAL_FORWARD) {
			/* drive does not accept this data rate */
			if (ftape->data_rate > 250) {
				TRACE(ft_t_info,
				      "Probable data rate conflict");
				TRACE(ft_t_info,
				      "Lowering data rate to %d Kbps",
				      ftape->data_rate / 2);
				ftape_half_data_rate(ftape);
				if (ftape->fdc->buffer_tail->retry > 0) {
					/* give it a chance */
					--(ftape->fdc->buffer_tail->retry);
				}
			} else {
				/* no rate is accepted... */
				TRACE(ft_t_err, "We're dead :(");
			}
		} else {
			TRACE(ft_t_err, "Unknown error");
		}
		TRACE_EXIT -EIO;   /* g.p. error */
	}
	TRACE_EXIT 0;
}

/* forward */
static int seek_forward(ftape_info_t *ftape, int segment_id, int fast);

static int fast_seek(ftape_info_t *ftape, int count, int reverse,
		     int *rew_eot_flag)
{
	int status;
	int i;
	int nibbles = count > 255 ? 3 : 2;
	TRACE_FUN(ft_t_flow);

	if (count <= 0) {
		TRACE(ft_t_flow, "warning: zero or negative count: %d", count);
	}
	/*  If positioned at begin or end of tape, fast seeking needs
	 *  special treatment.
	 *  Starting from logical bot needs a (slow) seek to the first
	 *  segment before the high speed seek. Most drives do this
	 *  automatically but some older don't, so we treat them all
	 *  the same.
	 *  Starting from logical eot is even more difficult because
	 *  we cannot (slow) reverse seek to the last segment.
	 *  TO BE IMPLEMENTED.
	 *
	 *  Note on Iomega Ditto drives (especially Ditto Max) It
	 *  seems that these drives (sometimes?) have REAL
	 *  difficulties with reverse seeking from lEOT.
	 *  With the Ditto Max I get the following cunning behavior:
	 *
	 *  When at EOT, the drive will fail to read the sector
	 *  addresses for "small" seek distances. More precise: I need
	 *  to reverse skip 32 to 34 segments. But then all of a
	 *  sudden the drive reads sector address, and in the correct
	 *  distance, i.e. one then gets the sector addresses of the
	 *  segment that is (roughly) located in a distance of 32 to
	 *  34 segments from EOT. 
	 *
	 *  CORRECTION: it seems that the Ditto (Max only?) drives
	 *  need a real large margin for the "skip N segments reverse"
	 *  command when possitioned at EOT. We work around by
	 *  introducing yet another "seek parameter" that is kind of
	 *  "auto-tuned" turing tape operation. Its name is
	 *  "rew_eot_margin". It start with 4 and is exponentionally
	 *  increased "4, 8, 16 ...". When reverse seeking and
	 *  positioned at EOT, the fast seek routine is called with
	 *  this many segments overshoot.
	 */
	*rew_eot_flag = 0;
	if (ftape->location.known &&
	    ((ftape->location.bot && !reverse) ||
	     (ftape->location.eot && reverse))) {
		if (!reverse) {
			/*  (slow) skip to first segment on a track
			 */
			seek_forward(ftape,
				     ftape->location.track *
				     ftape->segments_per_track, 0);
			--count;
		} else {
			/*  When seeking backwards from
				 *  end-of-tape the number of erased
				 *  gaps found seems to be higher than
				 *  expected.  Therefor the drive must
				 *  skip some more segments than
				 *  calculated, but we don't know how
				 *  many.  Thus we will prevent the
				 *  re-calculation of offset and
				 *  overshoot when seeking backwards.
				 */
			*rew_eot_flag = 1;
			count += ftape->rew_eot_margin;
		}
	}
	if (count > 4095) {
		TRACE(ft_t_noise, "skipping clipped at 4095 segment");
		count = 4095;
	}
	/* Issue this tape command first. */
	if (!reverse) {
		TRACE(ft_t_noise, "skipping %d segment(s)", count);
		TRACE_CATCH(ftape_command(ftape, nibbles == 3 ?
					  QIC_SKIP_EXTENDED_FORWARD :
					  QIC_SKIP_FORWARD),);
	} else {
		TRACE(ft_t_noise, "backing up %d segment(s)", count);
		TRACE_CATCH(ftape_command(ftape, nibbles == 3 ?
					  QIC_SKIP_EXTENDED_REVERSE :
					  QIC_SKIP_REVERSE),);
	}
	--count;	/* 0 means one gap etc. */
	for (i = 0; i < nibbles; ++i) {
		TRACE_CATCH(ftape_parameter(ftape, count & 15),);
		count /= 16;
	}
	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.rewind, &status),);
	ftape->tape_running = 0;
	TRACE_EXIT 0;
}

static int validate(ftape_info_t *ftape, int id)
{
	/* Check to see if position found is off-track as reported
	 *  once.  Because all tracks in one direction lie next to
	 *  each other, if off-track the error will be approximately
	 *  2 * ftape->segments_per_track.
	 */
	if (ftape->location.track == -1) {
		return 1; /* unforseen situation, don't generate error */
	} else {
		/* Use margin of ftape->segments_per_track on both sides
		 * because ftape needs some margin and the error we're
		 * looking for is much larger !
		 */
		int lo = (ftape->location.track - 1) * ftape->segments_per_track;
		int hi = (ftape->location.track + 2) * ftape->segments_per_track;

		return (id >= lo && id < hi);
	}
}

static int seek_forward(ftape_info_t *ftape, int segment_id, int fast)
{
	int failures = 0;
	int count;
	static const int margin = 1; /* fixed: stop this before target */
	int expected = -1;
	int target = segment_id - margin;
	int fast_seeking;
	int prev_segment = ftape->location.segment;
	int rew_eot_flag = 0;
	TRACE_FUN(ft_t_flow);

	if (!ftape->location.known) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "fatal: cannot seek from unknown location");
	}
	if (!validate(ftape, segment_id)) {
		ftape_sleep(1 * FT_SECOND);
		ftape->failure = 1;
		TRACE_ABORT(-EIO, ft_t_err,
			    "fatal: head off track (bad hardware?)");
	}
	TRACE(ft_t_noise, "from %d/%d to %d/0 - %d",
	      ftape->location.segment, ftape->location.sector,
	      segment_id, margin);
	count = target - ftape->location.segment - ftape->ffw_overshoot;
	fast_seeking = (fast &&
			count > (ftape->min_count +
				 (ftape->location.bot ? 1 : 0)));
	if (fast_seeking) {
		TRACE(ft_t_noise, "fast skipping %d segments", count);
		expected = segment_id - margin;
		fast_seek(ftape, count, 0, &rew_eot_flag);
	}
	if (!ftape->tape_running) {
		logical_forward(ftape);
	}
	while (ftape->location.segment < segment_id) {
		/*  This requires at least one sector in a (bad) segment to
		 *  have a valid and readable sector id !
		 *  It looks like this is not guaranteed, so we must try
		 *  to find a way to skip an EMPTY_SEGMENT. !!! FIXME !!!
		 */
		if (ftape_read_id(ftape) < 0 || !ftape->location.known ||
		    ft_sigtest(_DONT_BLOCK)) {
			ftape->location.known = 0;
			if (!ftape->tape_running ||
			    ++failures > FT_SECTORS_PER_SEGMENT) {
				TRACE_ABORT(-EIO, ft_t_err,
					    "read_id failed completely");
			}
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			TRACE(ft_t_flow, "read_id failed, retry (%d)",
			      failures);
			continue;
		}
		if (fast_seeking) {
			TRACE(ft_t_noise, "ended at %d/%d (%d,%d)",
			      ftape->location.segment, ftape->location.sector,
			      ftape->ffw_overshoot, rew_eot_flag);
			if (/* !rew_eot_flag && */
			    (ftape->location.segment < expected ||
			     ftape->location.segment > expected + margin)) {
				int error = ftape->location.segment - expected;
				TRACE(ft_t_noise,
				      "adjusting overshoot from %d to %d",
				      ftape->ffw_overshoot, ftape->ffw_overshoot + error);
				ftape->ffw_overshoot += error;
				/*  All overshoots have the same
				 *  direction, so it should never
				 *  become negative, but who knows.
				 */
				if (ftape->ffw_overshoot < -5 ||
				    ftape->ffw_overshoot > OVERSHOOT_LIMIT) {
					if (ftape->ffw_overshoot < 0) {
						/* keep sane value */
						ftape->ffw_overshoot = -5;
					} else {
						/* keep sane value */
						ftape->ffw_overshoot = OVERSHOOT_LIMIT;
					}
					TRACE(ft_t_noise,
					      "clipped overshoot to %d",
					      ftape->ffw_overshoot);
				}
			}
			fast_seeking = 0;
		} else if (ftape->location.known) {
			if (ftape->location.segment > prev_segment + 1) {
				TRACE(ft_t_noise,
				      "missed segment %d while skipping",
				      prev_segment + 1);
			}
			prev_segment = ftape->location.segment;
		}
	}
	if (ftape->location.segment > segment_id) {
		TRACE_ABORT(-EIO,
			    ft_t_noise, "failed: skip ended at segment %d/%d",
			    ftape->location.segment, ftape->location.sector);
	}
	TRACE_EXIT 0;
}

static int skip_reverse(ftape_info_t *ftape,
			int segment_id, int *pstatus)
{
	int failures = 0;
	static const int margin = 1;	/* stop this before target */
	int expected = 0;
	int count = 1;
	int short_seek;
	int target = segment_id - margin;
	int rew_eot_flag = 0;
	TRACE_FUN(ft_t_flow);

	if (ftape->location.known && !validate(ftape, segment_id)) {
		ftape_sleep(1 * FT_SECOND);
		ftape->failure = 1;
		TRACE_ABORT(-EIO, ft_t_err,
			    "fatal: head off track (bad hardware?)");
	}
	do {
		if (!ftape->location.known) {
			TRACE(ft_t_warn, "warning: location not known");
			SET_TRACE_LEVEL(ft_t_noise);
		}
		TRACE(ft_t_noise, "from %d/%d to %d/0 - %d",
		      ftape->location.segment, ftape->location.sector,
		      segment_id, margin);
		/*  min_rewind == 1 + overshoot_when_doing_minimum_rewind
		 *  overshoot  == overshoot_when_doing_larger_rewind
		 *  Initially min_rewind == 1 + overshoot, optimization
		 *  of both values will be done separately.
		 *  overshoot and min_rewind can be negative as both are
		 *  sums of three components:
		 *  any_overshoot == rewind_overshoot - 
		 *                   stop_overshoot   -
		 *                   start_overshoot
		 */
		if (ftape->location.segment - target - (ftape->min_rewind - 1) < 1) {
			short_seek = 1;
		} else {
			count = ftape->location.segment - target - ftape->rew_overshoot;
			short_seek = (count < 1);
		}
		if (short_seek) {
			count = 1;	/* do shortest rewind */
			expected = ftape->location.segment - ftape->min_rewind;
			if (expected/ftape->segments_per_track != ftape->location.track) {
				expected = (ftape->location.track * 
					    ftape->segments_per_track);
			}
		} else {
			expected = target;
		}
		fast_seek(ftape, count, 1, &rew_eot_flag);
		logical_forward(ftape);
		if (ftape_read_id(ftape) < 0 || !ftape->location.known ||
		    ft_sigtest(_DONT_BLOCK)) {
			if ((!ftape->tape_running && !ftape->location.known) ||
			    ++failures > FT_SECTORS_PER_SEGMENT) {
				TRACE_ABORT(-EIO, ft_t_err,
					    "read_id failed completely");
			}
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			TRACE_CATCH(ftape_report_drive_status(ftape, pstatus),);
			TRACE(ft_t_noise, "ftape_read_id failed, retry (%d)",
			      failures);
			continue;
		}
		TRACE(ft_t_noise, "ended at %d/%d (%d,%d,%d)", 
		      ftape->location.segment, ftape->location.sector,
		      ftape->min_rewind, ftape->rew_overshoot,
		      rew_eot_flag);
		if (rew_eot_flag && ftape->location.eot) {
			TRACE(ft_t_info, "Still at EOT while reverse seeking :-(");
			ftape->rew_eot_margin += ftape->min_count - 1;
		}
		if (!rew_eot_flag &&
		    (ftape->location.segment < expected ||
		     ftape->location.segment > expected + margin)) {
			int error = expected - ftape->location.segment;
			if (short_seek) {
				TRACE(ft_t_noise,
				      "adjusting min_rewind from %d to %d",
				      ftape->min_rewind, ftape->min_rewind + error);
				ftape->min_rewind += error;
				if (ftape->min_rewind < -5) {
					/* is this right ? FIXME ! */
					/* keep sane value */
					ftape->min_rewind = -5;
					TRACE(ft_t_noise, 
					      "clipped min_rewind to %d",
					      ftape->min_rewind);
				}
			} else {
				TRACE(ft_t_noise,
				      "adjusting overshoot from %d to %d",
				      ftape->rew_overshoot, ftape->rew_overshoot + error);
				ftape->rew_overshoot += error;
				if (ftape->rew_overshoot < -5 ||
				    ftape->rew_overshoot > OVERSHOOT_LIMIT) {
					if (ftape->rew_overshoot < 0) {
						/* keep sane value */
						ftape->rew_overshoot = -5;
					} else {
						/* keep sane value */
						ftape->rew_overshoot = OVERSHOOT_LIMIT;
					}
					TRACE(ft_t_noise,
					      "clipped overshoot to %d",
					      ftape->rew_overshoot);
				}
			}
		}
	} while (ftape->location.segment > segment_id);
	if (ftape->location.known) {
		TRACE(ft_t_noise, "current location: %d/%d",
		      ftape->location.segment, ftape->location.sector);
	}
	TRACE_EXIT 0;
}

static int determine_position(ftape_info_t *ftape)
{
	int retry = 0;
	int status;
	int result;
	TRACE_FUN(ft_t_flow);

	if (!ftape->tape_running) {
		/*  This should only happen if tape is stopped by isr.
		 */
		TRACE(ft_t_flow, "waiting for tape stop");
		if (ftape_ready_wait(ftape, ftape->timeout.pause, &status) < 0) {
			TRACE(ft_t_flow, "drive still running (fatal)");
			ftape->tape_running = 1;	/* ? */
		}
	} else {
		ftape_report_drive_status(ftape, &status);
	}
	if (status & QIC_STATUS_READY) {
		/*  Drive must be ready to check error state !
		 */
		TRACE(ft_t_flow, "drive is ready");
		if (status & QIC_STATUS_ERROR) {
			unsigned int error;
			qic117_cmd_t command;

			/*  Report and clear error state, try to continue.
			 */
			TRACE(ft_t_flow, "error status set");
			ftape_report_error(ftape, &error, &command, 1);
			ftape_ready_wait(ftape, ftape->timeout.reset, &status);
			ftape->tape_running = 0;	/* ? */
		}
		if (check_bot_eot(ftape, status)) {
			if (ftape->location.bot) {
				if ((status & QIC_STATUS_READY) == 0) {
					/* tape moving away from
					 * bot/eot, let's see if we
					 * can catch up with the first
					 * segment on this track.
					 */
				} else {
					TRACE(ft_t_flow,
					      "start tape from logical bot");
					logical_forward(ftape);	/* start moving */
				}
			} else {
				if ((status & QIC_STATUS_READY) == 0) {
					TRACE(ft_t_noise, "waiting for logical end of track");
					result = ftape_ready_wait(ftape,
								  ftape->timeout.reset, &status);
					/* error handling needed ? */
				} else {
					TRACE(ft_t_noise,
					      "tape at logical end of track");
				}
			}
		} else {
			TRACE(ft_t_flow, "start tape");
			logical_forward(ftape);	/* start moving */
			ftape->location.known = 0;	/* not cleared by logical forward ! */
		}
	}
	/* tape should be moving now, start reading id's
	 */
	while (!ftape->location.known &&
	       retry++ < FT_SECTORS_PER_SEGMENT &&
	       (result = ftape_read_id(ftape)) < 0) {

		TRACE(ft_t_flow, "location unknown");

		/* exit on signal
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);

		/*  read-id somehow failed, tape may
		 *  have reached end or some other
		 *  error happened.
		 */
		TRACE(ft_t_flow, "read-id failed");
		TRACE_CATCH(ftape_report_drive_status(ftape, &status),);
		TRACE(ft_t_err, "ftape_report_drive_status: 0x%02x", status);
		if (status & QIC_STATUS_READY) {
			ftape->tape_running = 0;
			TRACE(ft_t_noise, "tape stopped for unknown reason! "
			      "status = 0x%02x", status);
			if (status & QIC_STATUS_ERROR ||
			    !check_bot_eot(ftape, status)) {
				/* oops, tape stopped but not at end!
				 */
				TRACE_EXIT -EIO;
			}
		}
	}
	TRACE(ft_t_flow,
	      "tape is positioned at segment %d", ftape->location.segment);
	TRACE_EXIT ftape->location.known ? 0 : -EIO;
}

/*      Get the tape running and position it just before the
 *      requested segment.
 *      Seek tape-track and reposition as needed.
 */
int ftape_start_tape(ftape_info_t *ftape,
		     int segment_id, int short_start)
{
	int track = segment_id / ftape->segments_per_track;
	int result = -EIO;
	int status;
	int retry;
	TRACE_FUN(ft_t_flow);

	/* If short_start != 0, seek into wanted segment instead of
	 * into previous.
	 *
	 * This allows error recovery if a part of the segment is bad
	 * (erased) causing the tape drive to generate an index pulse
	 * thus causing a no-data error before the requested sector is
	 * reached.
	 */
	ftape->tape_running = 0;
	TRACE(ft_t_noise, "target segment: %d/%d%s", segment_id, short_start,
		ftape->fdc->buffer_head->retry > 0 ? " retry" : "");
	if (ftape->fdc->buffer_head->retry > 0) {	/* this is a retry */
		/* ftape->last_segment holds the segment position
		 * where this function was called for the last time.
		 * Clearly, we should use this distance as a weight to
		 * measure the "density" of overrun errors.
		 *
		 * We should decrease the data rate BEFORE the maximum
		 * number of retries is exhausted, i.e. repeated
		 * overrun errors in the same segment should lead to a
		 * reduced data rate before FT_SOFT_RETRIES is reached.
		 */
		int dist        = segment_id - ftape->last_segment;
		int error_count = (ftape->history.overrun_errors 
				   - ftape->overrun_count_offset);
		int tolerance   = (dist >> 3) + 3;

		if ((int)ftape->history.overrun_errors < ftape->overrun_count_offset) {
			ftape->overrun_count_offset =
				ftape->history.overrun_errors;
		} else if (dist < 0 || dist > 50) {
			ftape->overrun_count_offset =
				ftape->history.overrun_errors;
		} else if (error_count >= tolerance) {
			TRACE(ft_t_any, "\n"
			      "segment  : %d\n"
			      "last     : %d\n"
			      "history  : %d\n"
			      "offset   : %d\n"
			      "tolerance: %d\n",
			      segment_id, ftape->last_segment,
			      ftape->history.overrun_errors,
			      ftape->overrun_count_offset,
			      tolerance);


			if (ftape_increase_threshold(ftape) >= 0) {
				--(ftape->fdc->buffer_head->retry);
				ftape->overrun_count_offset =
					ftape->history.overrun_errors;
				TRACE(ft_t_warn, "increased threshold because "
				      "of excessive overrun errors (%d)",
				      ftape->fdc->buffer_head->segment_id);
			} else if ((ftape->qic_std == QIC_3020 &&
				    ftape->data_rate > 1000) ||
				   (ftape->qic_std == DITTO_MAX &&
				    ftape->data_rate > 2000) ||
				   (ftape->qic_std != QIC_3020 &&
				    ftape->qic_std != DITTO_MAX &&
				    ftape->data_rate > 500)) {
				   
				if (ftape_half_data_rate(ftape) >= 0) {
					TRACE(ft_t_warn,
					      "reduced datarate because of "
					      "excessive overrun errors (%d)",
				      ftape->fdc->buffer_head->segment_id);
					--(ftape->fdc->buffer_head->retry);
				}

				ftape->bad_bus_timing = 1;
				ftape->overrun_count_offset =
					ftape->history.overrun_errors;
			}
		}
	}
	ftape->last_segment = segment_id;
	if (ftape->location.track != track ||
	    (ftape->might_be_off_track && ftape->fdc->buffer_head->retry== 0)) {
		/* current track unknown or not equal to destination
		 */
		ftape_ready_wait(ftape, ftape->timeout.seek, &status);
		ftape_seek_head_to_track(ftape, track);
		/* ftape->overrun_count_offset = ftape->history.overrun_errors; */
	}
	result = -EIO;
	retry = 0;
	while (result < 0     &&
	       retry++ <= ft_soft_retries   &&
	       !ftape->failure &&
	       !ft_sigtest(_DONT_BLOCK)) {
		
		if (retry && ftape->start_offset < 5) {
			ftape->start_offset ++;
		}
		/*  Check if we are able to catch the requested
		 *  segment in time.
		 */
		if ((ftape->location.known ||
		     (determine_position(ftape) == 0)) &&
		    ftape->location.segment >=
		    (segment_id -
		     ((ftape->tape_running || ftape->location.bot)
		      ? 0 : ftape->start_offset))) {
			/*  Too far ahead (in or past target segment).
			 */
			if (ftape->tape_running) {
				if ((result = ftape_stop_tape(ftape, &status)) < 0) {
					TRACE(ft_t_err,
					      "stop tape failed with code %d",
					      result);
					break;
				}
				TRACE(ft_t_noise, "tape stopped");
				ftape->tape_running = 0;
			}
			TRACE(ft_t_noise, "repositioning");
			++(ftape->history.rewinds);
			if (segment_id % ftape->segments_per_track < ftape->start_offset){
				TRACE(ft_t_noise, "end of track condition\n"
				      "segment_id        : %d\n"
				      "segments_per_track: %d\n"
				      "start_offset      : %d",
				      segment_id, ftape->segments_per_track, 
				      ftape->start_offset);
				      
				/*  If seeking to first segments on
				 *  track better do a complete rewind
				 *  to logical begin of track to get a
				 *  more steady tape motion.  
				 */
				result = ftape_command_wait(ftape,
					(ftape->location.track & 1)
					? QIC_PHYSICAL_FORWARD
					: QIC_PHYSICAL_REVERSE,
					ftape->timeout.rewind, &status);
				check_bot_eot(ftape, status);	/* update location */
			} else {
				result= skip_reverse(ftape, segment_id - ftape->start_offset, &status);
			}
		}
		if (!ftape->location.known) {
			TRACE(ft_t_bug, "panic: location not known");
			result = -EIO;
			continue; /* while() will check for failure */
		}
		TRACE(ft_t_noise, "current segment: %d/%d",
		      ftape->location.segment, ftape->location.sector);
		/*  We're on the right track somewhere before the
		 *  wanted segment.  Start tape movement if needed and
		 *  skip to just before or inside the requested
		 *  segment. Keep tape running.  
		 */
		result = 0;
		if (ftape->location.segment < 
		    (segment_id - ((ftape->tape_running || ftape->location.bot)
				   ? 0 : ftape->start_offset))) {
			if (short_start) {
				result = seek_forward(ftape, segment_id, retry <= (ft_soft_retries / 2));
			} else {
				result = seek_forward(ftape, segment_id - 1, retry <= (ft_soft_retries / 2));
			}
		}
		if (result == 0 &&
		    ftape->location.segment !=
		    (segment_id - (short_start ? 0 : 1))) {
			result = -EIO;
		}
	}
	if (result < 0) {
		TRACE(ft_t_err, "failed to reposition");
	} else {
		ftape->runner_status = running;
	}
	TRACE_EXIT result;
}
