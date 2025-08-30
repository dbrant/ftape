/*
 *      Copyright (C) 1993-1995 Bas Laarhoven,
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
 * $RCSfile: ftape-write.c,v $
 * $Revision: 1.21 $
 * $Date: 1999/03/17 11:19:35 $
 *
 *      This file contains the writing code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/segment.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "ftape-tracing.h"
#include "ftape-write.h"
#include "ftape-read.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-ecc.h"
#include "ftape-bsm.h"
#include "fdc-isr.h"

/*      Global vars.
 */

/*      Local vars.
 */
void ftape_zap_write_buffers(ftape_info_t *ftape)
{
	buffer_struct *pos = ftape->fdc->buffers;
	TRACE_FUN(ft_t_flow);

	if (pos == NULL) {
		TRACE_EXIT;
	}

	do {
		pos->status = done;
		pos = pos->next;
	} while (pos != ftape->fdc->buffers);

	if (ftape->fdc->ops && ftape->fdc->ops->terminate_dma) {
		(void)ftape->fdc->ops->terminate_dma(ftape->fdc, no_error);
	}
	ftape_reset_buffer(ftape);
	TRACE_EXIT;
}

int ftape_start_writing(ftape_info_t *ftape, const ft_write_mode_t mode)
{
	int segment_id = ftape->HEAD->segment_id;
	int result;
	buffer_state_enum wanted_state = (mode == FT_WR_DELETE
					  ? deleting
					  : writing);
	TRACE_FUN(ft_t_flow);

	if (ftape->driver_state != wanted_state) {
		TRACE(ft_t_warn, "wanted stated: %d, driver state: %d",
		      wanted_state, ftape->driver_state);
		TRACE_EXIT 0;
	}
	if (ftape->HEAD->status != waiting) {
		TRACE(ft_t_warn, "head status: %d", ftape->HEAD->status);
		TRACE_EXIT 0;
	}
	ftape_setup_new_segment(ftape, ftape->HEAD, segment_id, 1);
	if (mode == FT_WR_SINGLE) {
		/* stop tape instead of pause */
		ftape->HEAD->next_segment = 0;
	}
	ftape_calc_next_cluster(ftape->HEAD); /* prepare */
	ftape->HEAD->status = ftape->driver_state; /* either writing or deleting */
	if (ftape->runner_status == idle) {
		TRACE(ft_t_noise,
		      "starting runner for segment %d", segment_id);

		TRACE_CATCH(ftape_start_tape(ftape,
					     segment_id, ftape->HEAD->short_start),);
	} else {
		TRACE(ft_t_noise, "runner not idle, not starting tape");
	}
	/* go */
	result = fdc_setup_read_write(ftape->fdc, ftape->HEAD, (mode == FT_WR_DELETE
					     ? FDC_WRITE_DELETED : FDC_WRITE));
	ftape_set_state(ftape, wanted_state); /* should not be necessary */
	TRACE_EXIT result;
}

/*  Wait until all data is actually written to tape.
 *  
 *  There is a problem: when the tape runs into logical EOT, then this
 *  failes. We need to restart the runner in this case.
 */
int ftape_loop_until_writes_done(ftape_info_t *ftape)
{
	TRACE_FUN(ft_t_flow);

	for (;;) {
		fdc_disable_irq(ftape->fdc);
		if ((ftape->driver_state != writing &&
		     ftape->driver_state != deleting) ||
		    ftape->HEAD->status == done) {
			TRACE(ft_t_noise,
			      "done. driver state: %d, HEAD->status: %d",
			      ftape->driver_state, ftape->HEAD->status);
			fdc_enable_irq(ftape->fdc);
			break;
		}
		fdc_enable_irq(ftape->fdc);
		TRACE_CATCH(ftape_handle_logical_eot(ftape),
			    ftape->last_write_failed = 1);
		/* restart the tape if necessary */
		if (ftape->runner_status == idle) {
			TRACE(ft_t_noise, "runner is idle, restarting");
			if (ftape->driver_state == deleting) {
				TRACE_CATCH(ftape_start_writing(ftape,
								FT_WR_DELETE),
					    ftape->last_write_failed = 1);
			} else {
				TRACE_CATCH(ftape_start_writing(ftape,
								FT_WR_MULTI),
					    ftape->last_write_failed = 1);
			}
		}
		fdc_disable_irq(ftape->fdc);
		/* fdc_interrupt_wait() will enable interrupts again in ANY
		 * case.
		 */
		if (ftape->HEAD->status == ftape->driver_state) {
			TRACE_CATCH(fdc_interrupt_wait(ftape->fdc,
						       5 * FT_SECOND),
				    ftape->last_write_failed = 1);
		} else {
			fdc_enable_irq(ftape->fdc);
		}
		if (ftape->HEAD->status == error) {
			TRACE(ft_t_warn, "error status set");
			if (ftape->runner_status == aborting) {
				ftape_dumb_stop(ftape);
			}
			if (ftape->runner_status != idle) {
				TRACE_ABORT(-EIO, ft_t_err,
					    "unexpected state: "
					    "ftape->runner_status != idle");
			}
			if (ftape->HEAD->hard_error_map != 0) {
				/*  Implement hard write error recovery here
				 */

				/* Let's do it!  We simply report the
				 * hard error to the caller, which
				 * then should do the right thing,
				 * i.e. call
				 * "ftape_hard_error_recovery() and
				 * check whether there is enough space
				 * left on tape to continue
				 */
				TRACE_ABORT(-EIO, ft_t_info,
					    "hard error in segment %d",
					    ftape->HEAD->segment_id);
			}
			/* retry this one */
			ftape->HEAD->status = waiting;
			ftape_start_writing(ftape,
					    ftape->driver_state == deleting
					    ? FT_WR_DELETE : FT_WR_MULTI);
		}
		TRACE(ft_t_noise, "looping until writes done");
		/* Allow escape from loop when signaled !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}
	ftape_set_state(ftape, waiting);
	TRACE_EXIT 0;
}

/*      Write given segment from buffer at address to tape.
 *      *address will be set to NULL by copy_and_gen_ecc() if successful.
 */
static int write_segment(ftape_info_t *ftape,
			 int segment_id,
			 __u8 **address,
			 ft_write_mode_t write_mode)
{
	int bytes_written = 0;
	buffer_state_enum wanted_state = (write_mode == FT_WR_DELETE
					  ? deleting : writing);
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "segment_id = %d", segment_id);
	if (ftape->driver_state != wanted_state) {
		if (ftape->driver_state == deleting ||
		    wanted_state == deleting) {
			/*
			 *  We don't cope with hard errors in this case
			 */
			TRACE_CATCH(ftape_loop_until_writes_done(ftape),);
		}
		TRACE(ft_t_noise, "calling ftape_abort_operation");
		TRACE_CATCH(ftape_abort_operation(ftape),);
		ftape_zap_write_buffers(ftape);
		ftape_set_state(ftape, wanted_state);
	}
	/*    if all buffers full we'll have to wait...
	 */
	if (ftape_wait_segment(ftape, wanted_state) < 0) {
		TRACE_CATCH(ftape_dumb_stop(ftape),);
	}		
		
	switch(ftape->TAIL->status) {
	case done:
		ftape->history.defects += count_ones(ftape->TAIL->hard_error_map);
		break;
	case waiting:
		/* this could happen with multiple EMPTY_SEGMENTs, but
		 * shouldn't happen any more as we re-start the runner even
		 * with an empty segment.
		 */
		TRACE(ft_t_warn, "TAIL->status == waiting");
		TRACE(ft_t_warn, "HEAD: %p, TAIL: %p",
		      ftape->HEAD, ftape->TAIL);
		TRACE(ft_t_warn, "runner_status: %d", ftape->runner_status);
		      
		      
		bytes_written = -EAGAIN;
		break;
#if 0 || defined(HACK)
	/* TODO: it seems that at least the Ditto Max sometimes has
	 * problems and reports overrun errors which occur after (???) 
	 * all data has been transferred. In that case weird things
	 * happen. So maybe introduce a "fatal_error" state which make
	 * write_segment() retry the entire segment after
	 * re-programming the FDC
	 */
	case fatal_error:
		TRACE_CATCH(ftape_dumb_stop(ftape),);
		TRACE(ft_t_info,
		      "Resetting FDC and reprogramming FIFO threshold");
		fdc_reset(ftape->fdc);	
		fdc_fifo_threshold(ftape->fdc, ftape->fdc->threshold,
				   NULL, NULL, NULL);
		ftape->TAIL->status = waiting;
		bytes_written = -EAGAIN;
		break;
#endif
	case error:
		/*  setup for a retry
		 */
		TRACE(ft_t_noise, "TAIL->status == error");
		if (ftape->TAIL->hard_error_map != 0) {
			TRACE(ft_t_warn, 
			      "warning: %d hard error(s) in written segment",
			      count_ones(ftape->TAIL->hard_error_map));
			TRACE(ft_t_noise, "hard_error_map = 0x%08lx", 
			      (long)ftape->TAIL->hard_error_map);
			/*  Implement hard write error recovery here
			 */
			/* Let's do it!  We simply report the hard
			 * error to the caller, which then should do
			 * the right thing, i.e. call
			 * "ftape_hard_error_recovery() and check
			 * whether there is enough space left on tape
			 * to continue
			 */
			TRACE(ft_t_info, "hard error in segment %d",
			      ftape->HEAD->segment_id);
			bytes_written = -EIO;
			/* keep the error state */
		} else {
			ftape->TAIL->status = waiting;
			bytes_written = -EAGAIN; /* force retry */
		}
		break;
	default:
	{
		TRACE(ft_t_err, "runner: %d", ftape->runner_status);
		TRACE(ft_t_err, "head: %d", ftape->HEAD->status);
#if 0
		TRACE_ABORT(-EIO, ft_t_err,
			    "wait for empty segment failed, tail status: %d",
			    ftape->TAIL->status);
#else
		TRACE(ft_t_err,
		      "wait for empty segment failed, tail status: %d",
		      ftape->TAIL->status);
		bytes_written = -EAGAIN;
		ftape->TAIL->status = waiting;
		break;
#endif
	}
	}
	/*    should runner stop ?
	 */
	if (ftape->runner_status == aborting) {
		if (ftape->HEAD->status == wanted_state) {
			ftape->HEAD->status = done; /* ???? */
		}
		/*  don't call abort_operation(), we don't want to zap
		 *  the dma buffers
		 */
		TRACE_CATCH(ftape_dumb_stop(ftape),);
	} else {
		/*  If just passed last segment on tape: wait for BOT
		 *  or EOT mark. Sets ft_runner_status to idle if at lEOT
		 *  and successful 
		 */
		TRACE_CATCH(ftape_handle_logical_eot(ftape),);
	}
	if (ftape->TAIL->status == done) {
		/* now at least one buffer is empty, fill it with our
		 * data.  skip bad sectors and generate ecc.
		 * copy_and_gen_ecc return nr of bytes written, range
		 * 0..29 Kb inclusive!  
		 *
		 * Empty segments are handled inside copy_and_gen_ecc()
		 */
		TRACE_CATCH(bytes_written =
			    ftape->fdc->ops->copy_and_gen_ecc(ftape->fdc,
							      ftape->TAIL,
							      address,
			      ftape_get_bad_sector_entry(ftape, segment_id)),);
		ftape->TAIL->segment_id = segment_id;
		ftape->TAIL->status = waiting;
		TRACE(ft_t_any, "filled buffer %p", ftape->TAIL);
		ftape->TAIL = ftape->TAIL->next;
	}
	/*  Start tape only if all buffers full or flush mode.
	 *  This will give higher probability of streaming.
	 */
	if (ftape->runner_status != running && 
	    ((ftape->TAIL->status == waiting &&
	      ftape->HEAD == ftape->TAIL) ||
	     write_mode != FT_WR_ASYNC)) {
		TRACE(ft_t_any, "Starting runner because:\n"
		      "not running: %s\n"
		      "tail waiting: %s\n"
		      "head %p == tail %p\n"
		      "write_mode %d != FT_WR_ASYNC: %s",
		      ftape->runner_status != running ? "yes":"no",
		      ftape->TAIL->status == waiting ? "yes" : "no",
		      ftape->HEAD, ftape->TAIL,
		      write_mode,
		      write_mode != FT_WR_ASYNC ? "yes" : "no");
		TRACE_CATCH(ftape_start_writing(ftape, write_mode),);
	}
	TRACE_EXIT bytes_written;
}

/*  Write as much as fits from buffer to the given segment on tape
 *  and handle retries.
 *  Return the number of bytes written (>= 0), or:
 *      -EIO          write failed
 *      -EINTR        interrupted by signal
 *      -ENOSPC       device full
 *
 *  *buffer will be set to NULL by copy_and_gen_ecc() if successful
 */
int ftape_write_segment(ftape_info_t *ftape,
			int segment_id,
			__u8 **buffer, 
			ft_write_mode_t flush)
{
	int retry = 0;
	int result;
	TRACE_FUN(ft_t_flow);

	ftape->history.used |= 2;
	if (segment_id >= ftape->tracks_per_tape * ftape->segments_per_track) {
		/* tape full */
		TRACE_ABORT(-ENOSPC, ft_t_err,
			    "invalid segment id: %d (max %d)", 
			    segment_id, 
			    ftape->tracks_per_tape *
			    ftape->segments_per_track -1);
	}
	for (;;) {
		if ((result = write_segment(ftape,
					    segment_id, buffer, flush)) >= 0) {
			if (result == 0) { /* empty segment */
				TRACE(ft_t_noise,
				      "empty segment, nothing written");
			}
			TRACE_EXIT result;
		}
		if (result == -EAGAIN) {
			if (++retry > 100) { /* give up */
				TRACE_ABORT(-EIO, ft_t_err,
				      "write failed, >100 retries in segment");
			}
			TRACE(ft_t_warn, "write error, retry %d (%d)",
			      retry, ftape->TAIL->segment_id);
		} else {
			TRACE_ABORT(result, ft_t_err,
				    "write_segment failed, error: %d", result);
		}
		/* Allow escape from loop when signaled !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}
}

/* This is called after a hard write error. The data that is still
 * contained in the DMA buffers is shifted towards BOT. There will be
 * some data remaining (at most 29kb) which is copied to *address (the
 * designated deblock buffer).
 *
 * Ok. There might be (worst case) fdc->nr_buffers buffers filled with
 * data. The deblock buffer already is filled with new data.
 *
 * We need some backing store to recover, i.e. an additional 32kb
 * deblock buffer. Sorry, but it has to be. 
 *
 * fdc->buffer_head->segment_id is the failing segment
 * fdc->buffer_head->next->segment is the next segment, not yet processed.
 *
 * A segment contains data not yet written to tape if buff->status == waiting
 *
 * The trick is to set up the buffer state machine as if nothing has happened
 *
 * We map the failing sector(s) as bad, then shift the data towards EOT
 * 
 * When this routine has completed, the status of the head buffer is
 * "waiting" (was "error"), and all buffers that previously had status
 * "waiting" still are waiting. "ftape-TAIL" still points to the first
 * available buffer or is identical to ftape-HEAD.
 *
 * The buffers are set up correctly, with valid ECC.
 * 
 * "scratch" holds the remaining bytes.
 *
 * BEFORE this routine is called, the caller should make a backup of
 * the currently active deblock buffer, as the deblock_buffer pointer
 * will be moved around, and its contents will be destroyed.
 *   
 * "scratch" has to point to a buffer of FT_SEGMENT_SIZE (i.e. 29k)
 * After completion "scratch" contains the bytes that are left over
 * 
 * The return value is the number of bytes left over in "scratch"
 *
 * We need to update the history here as we eliminate the hard error ...
 */
int ftape_hard_error_recovery(ftape_info_t *ftape, void *scratch)
{
	buffer_struct *head = ftape->HEAD, *pos;
	SectorMap bsm = ftape_get_bad_sector_entry(ftape, head->segment_id);
	int seg_size, old_size = (FT_SEGMENT_SIZE -
				  count_ones(bsm) * FT_SECTOR_SIZE);
	fdc_info_t *fdc = ftape->fdc;
	__u8 *deblock_buffer;
	int cnt;
	TRACE_FUN(ft_t_flow);

	if (fdc->ops->read_buffer == NULL) {
		TRACE_EXIT -EIO;
	}

	/* sanity check. There shouldn't be more than one hard write
	 * error at a given write_segment attempt
	 */
	if (count_ones(head->hard_error_map) != 1) {
		TRACE_EXIT -EIO;
	}

	/*  update the history.
	 */
	ftape->history.defects ++;

	/*  put new bad sector entry, calculate new space.
	 */
	bsm |= head->hard_error_map;
	ftape_put_bad_sector_entry(ftape, head->segment_id, bsm);
	/*  First handle the failing segment.
	 */
	seg_size = old_size - FT_SECTOR_SIZE;

	/*  Get the old data.
	 */
	fdc->ops->read_buffer(fdc, head, &deblock_buffer);
	memcpy(scratch, deblock_buffer + seg_size, FT_SECTOR_SIZE);
	/* will set deblock_buffer to NULL */
	fdc->ops->copy_and_gen_ecc(fdc, head, &deblock_buffer, bsm);
	head->status = waiting;

	for (pos = head->next;
	     pos != head && pos->status == waiting;
	     pos = pos->next) {
		/*  deblock_buffer now points to the old data
		 */
		fdc->ops->read_buffer(fdc, pos, &deblock_buffer);
		/*  get the new segment size
		 */
		bsm = ftape_get_bad_sector_entry(ftape, pos->segment_id);
		seg_size = (FT_SEGMENT_SIZE -
			    count_ones(bsm) * FT_SECTOR_SIZE);

		/* shift got be bigger than seg_size
		 */
		cnt = (seg_size <= 0) ? 0 : FT_SECTOR_SIZE;

		if (seg_size <= 0) {
			continue;
		}
		memmove(deblock_buffer + FT_SECTOR_SIZE, deblock_buffer,
			seg_size);
		memcpy(deblock_buffer, scratch, FT_SECTOR_SIZE);
		memcpy(scratch, deblock_buffer + seg_size, FT_SECTOR_SIZE);
		/* will set deblock_buffer to NULL */
		fdc->ops->copy_and_gen_ecc(fdc, pos, &deblock_buffer, bsm);
	}

	ftape->old_segment_id = -1; /* don't retry, but initialize buffer
				     * with new data
				     */
	TRACE_EXIT FT_SECTOR_SIZE;
}
