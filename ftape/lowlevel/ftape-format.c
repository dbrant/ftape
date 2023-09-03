/*
 * Copyright (C) 1997-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-format.c,v $
 * $Revision: 1.16 $
 * $Date: 1999/02/26 13:04:48 $
 *
 *      This file contains the code to support formatting of floppy
 *      tape cartridges with the QIC-40/80/3010/3020 floppy-tape
 *      driver "ftape" for Linux.
 */
 
#include <linux/string.h>
#include <linux/errno.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "ftape-tracing.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-ecc.h"
#include "ftape-bsm.h"
#include "ftape-buffer.h"
#include "ftape-format.h"

/*
 *  at most 256 segments fit into one 32 kb buffer.  Even TR-1 cartridges have
 *  more than this many segments per track, so better be careful.
 *
 *  buffer_struct *buff: buffer to store the formatting coordinates in
 *  int  start: starting segment for this buffer.
 *  int    spt: segments per track
 *
 *  Note: segment ids are relative to the start of the track here.
 */


static int fill_format_buffer(ftape_info_t *ftape,
			      buffer_struct *buff,
			      int start, int todo)
{
	__u8 *deblock_buffer = fdc_get_deblock_buffer(ftape->fdc);
	ft_format_segment *data = (ft_format_segment *)deblock_buffer;
	int i, seg;

	for (seg = start; seg < start + todo; seg ++) {
		data->sectors[0].cyl  = ((seg % ftape->segments_per_head)
					 / ftape->segments_per_cylinder);
		data->sectors[0].head = seg / ftape->segments_per_head;
		data->sectors[0].sect = ((seg % ftape->segments_per_cylinder)
					 * FT_SECTORS_PER_SEGMENT + 1);
		data->sectors[0].size = 3;
		for (i = 1; i < FT_SECTORS_PER_SEGMENT; i++) {
			memcpy(&data->sectors[i], &data->sectors[0], 4);
			data->sectors[i].sect += i;
		}
		data ++;
	}
	return ftape->fdc->ops->write_buffer(ftape->fdc, buff,
					     &deblock_buffer);
}

static int setup_format_buffer(ftape_info_t *ftape,
			       buffer_struct *buff,
			       int start, int spt, int track)
{
	int to_do = spt - start;
	TRACE_FUN(ft_t_flow);

	if (to_do > FT_FMT_SEGS_PER_BUF) {
		to_do = FT_FMT_SEGS_PER_BUF;
	}
	TRACE_CATCH(fill_format_buffer(ftape, buff,
				       track * spt + start, to_do),);

	buff->ptr          = buff->dma_address;
	buff->remaining    = to_do * FT_SECTORS_PER_SEGMENT; /* # sectors */
	buff->bytes        = buff->remaining * 4; /* need 4 bytes per sector */
	buff->segment_id   = start;
	buff->next_segment = start + to_do;
	if (buff->next_segment >= spt) {
		buff->next_segment = 0; /* 0 means: stop runner */
	}
	buff->status       = waiting; /* tells the isr that it can use
				       * this buffer
				       */
	TRACE_EXIT 0;
}


/*
 *  start formatting a new track.
 */
int ftape_format_track(ftape_info_t *ftape, const unsigned int track)
{
	int status;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(ftape_ready_wait(ftape, ftape->timeout.pause, &status),);
	if (track & 1) {
		if (!(status & QIC_STATUS_AT_EOT)) {
			TRACE_CATCH(ftape_seek_to_eot(ftape),);
		}
	} else {
		if (!(status & QIC_STATUS_AT_BOT)) {
			TRACE_CATCH(ftape_seek_to_bot(ftape),);
		}
	}
	ftape_abort_operation(ftape); /* this sets ft_head = ft_tail = 0 */
	ftape_set_state(ftape, formatting);

	TRACE(ft_t_noise,
	      "Formatting track %d, logical: from segment %d to %d",
	      track, track * ftape->segments_per_track, 
	      (track + 1) * ftape->segments_per_track - 1);
	
	/*
	 *  initialize the buffer switching protocol for this track
	 */
	ftape->switch_segment = 0;
	do {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		setup_format_buffer(ftape, ftape->TAIL, ftape->switch_segment,
				    ftape->segments_per_track, track);
		ftape->switch_segment = ftape->TAIL->next_segment;
	} while ((ftape->switch_segment != 0) &&
		 ((ftape->TAIL = ftape->TAIL->next) != ftape->HEAD));
	/* go */
	ftape->HEAD->status = formatting;
	TRACE_CATCH(ftape_seek_head_to_track(ftape, track),);
	TRACE_CATCH(ftape_command(ftape, QIC_LOGICAL_FORWARD),);
	TRACE_CATCH(fdc_setup_formatting(ftape->fdc, ftape->HEAD),);
	TRACE_EXIT 0;
}

/*   return segment id of segment currently being formatted and do the
 *   buffer switching stuff.
 */
int ftape_format_status(ftape_info_t *ftape, unsigned int *segment_id)
{
	int result;
	TRACE_FUN(ft_t_flow);

	while (ftape->switch_segment != 0 && ftape->HEAD != ftape->TAIL) {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/*  need more buffers, first wait for empty buffer
		 */
		TRACE_CATCH(ftape_wait_segment(ftape, formatting),);

		setup_format_buffer(ftape, ftape->TAIL, ftape->switch_segment,
				    ftape->segments_per_track,
				    ftape->location.track);

		ftape->switch_segment = ftape->TAIL->next_segment;
		if (ftape->switch_segment != 0) {
			ftape->TAIL = ftape->TAIL->next;
		}
	}
	/*    should runner stop ?
	 */
	if (ftape->runner_status == aborting || ftape->runner_status == do_abort) {
		TRACE(ft_t_warn, "Error formatting segment %d", ftape->HEAD->segment_id);
		(void)ftape_abort_operation(ftape);
		TRACE_EXIT (ftape->HEAD->status != error) ? -EAGAIN : -EIO;
	}
	/* don't care if the timer expires, this is just kind of a
	 * "select" operation that lets the calling process sleep
	 * until something has happened
	 */
	fdc_disable_irq(ftape->fdc);
	if (fdc_interrupt_wait(ftape->fdc, 5 * FT_SECOND) < 0) {
		TRACE(ft_t_noise, "End of track %d at segment %d",
		      ftape->location.track, ftape->HEAD->segment_id);
		result = 1;  /* end of track, unlock module */
	} else {
		result = 0;
	}
	*segment_id = ftape->HEAD->segment_id;

	/* Internally we start counting segment ids from the start of
	 * each track when formatting, but externally we keep them
	 * relative to the start of the tape:
	 */
	*segment_id += ftape->location.track * ftape->segments_per_track;
	TRACE_EXIT result;
}

/* The segment id is relative to the start of the tape */
int ftape_verify_segment(ftape_info_t *ftape,
			 const unsigned int segment_id, SectorMap *bsm)
{
	int result;
	int verify_done = 0;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Verifying segment %d", segment_id);

	if (ftape->driver_state != verifying) {
		TRACE(ft_t_noise, "calling ftape_abort_operation");
		if (ftape_abort_operation(ftape) < 0) {
			TRACE(ft_t_err, "ftape_abort_operation failed");
			TRACE_EXIT -EIO;
		}
	}
	*bsm = 0x00000000;
	ftape_set_state(ftape, verifying);
	for (;;) {
		/* Allow escape from this loop on signal */
		FT_SIGNAL_EXIT(_DONT_BLOCK);

		/* Search all full buffers for the first matching the
		 * wanted segment.  Clear other buffers on the fly.
		 */
		while (!verify_done && ftape->TAIL->status == done) {
			/*
			 *  Allow escape from this loop on signal !
			 */
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			if (ftape->TAIL->segment_id == segment_id) {
				/*  If out buffer is already full,
				 *  return its contents.  
				 */
				TRACE(ft_t_flow, "found segment in cache: %d",
				      segment_id);
				if ((ftape->TAIL->soft_error_map |
				     ftape->TAIL->hard_error_map) != 0) {
					*bsm = (ftape->TAIL->soft_error_map |
						ftape->TAIL->hard_error_map);
					TRACE(ft_t_info,"bsm[%d] = 0x%08lx",
					      segment_id,
					      (unsigned long)
					      (*bsm & EMPTY_SEGMENT));
				}
				verify_done = 1;
			} else {
				TRACE(ft_t_flow,"zapping segment in cache: %d",
				      ftape->TAIL->segment_id);
			}
			ftape->TAIL->status = waiting;
			ftape->TAIL = ftape->TAIL->next;
		}
		if (!verify_done && ftape->TAIL->status == verifying) {
			if (ftape->TAIL->segment_id == segment_id) {
				switch(ftape_wait_segment(ftape, verifying)) {
				case 0:
					break;
				case -EINTR:
					TRACE_ABORT(-EINTR, ft_t_warn,
						    "interrupted by "
						    "non-blockable signal");
					break;
				default:
					ftape_abort_operation(ftape);
					ftape_set_state(ftape, verifying);
					/* be picky */
					TRACE_ABORT(-EIO, ft_t_warn,
						    "wait_segment failed");
				}
			} else {
				/*  We're reading the wrong segment,
				 *  stop runner.
				 */
				TRACE(ft_t_noise, "verifying wrong segment");
				ftape_abort_operation(ftape);
				ftape_set_state(ftape, verifying);
			}
		}
		/*    should runner stop ?
		 */
		if (ftape->runner_status == aborting) {
			if (ftape->HEAD->status == error ||
			    ftape->HEAD->status == verifying) {
				/* no data or overrun error */
				ftape->HEAD->status = waiting;
			}
			TRACE_CATCH(ftape_dumb_stop(ftape),);
		} else {
			/*  If just passed last segment on tape: wait
			 *  for BOT or EOT mark. Sets ft_runner_status to
			 *  idle if at lEOT and successful 
			 */
			TRACE_CATCH(ftape_handle_logical_eot(ftape),);
		}
		if (verify_done) {
			TRACE_EXIT 0;
		}
		/*    Now at least one buffer is idle!
		 *    Restart runner & tape if needed.
		 */
		/*  We could optimize the following a little bit. We know that 
		 *  the bad sector map is empty.
		 */
		if (ftape->TAIL->status == waiting) {

			ftape_setup_new_segment(ftape, ftape->HEAD, segment_id, -1);
			ftape_calc_next_cluster(ftape->HEAD);
			if (ftape->runner_status == idle) {
				result = ftape_start_tape(ftape, segment_id,
							  ftape->HEAD->short_start);
				switch(result) {
				case 0:
					break;
				case -ETIME:
				case -EINTR:
					TRACE_ABORT(result, ft_t_err, "Error: "
						    "segment %d unreachable",
						    segment_id);
					break;
				default:
					*bsm = EMPTY_SEGMENT;
					TRACE(ft_t_info,"bsm[%d] = 0x%08lx",
					      segment_id,
					      (unsigned long)
					      (*bsm & EMPTY_SEGMENT));
					TRACE_EXIT 0;
					break;
				}
			}
			ftape->HEAD->status = verifying;
			fdc_setup_read_write(ftape->fdc, ftape->HEAD, FDC_VERIFY);
		}
	}
	/* not reached */
	TRACE_EXIT -EIO;
}
