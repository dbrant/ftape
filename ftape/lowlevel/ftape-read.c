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
 * $RCSfile: ftape-read.c,v $
 * $Revision: 1.29 $
 * $Date: 2000/07/23 20:30:26 $
 *
 *      This file contains the reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/segment.h>

#include <linux/ftape.h>
#include <linux/qic117.h>

#include "ftape-tracing.h"
#include "ftape-ecc.h"
#include "ftape-read.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-write.h"
#include "ftape-bsm.h"
#include "ftape-buffer.h"

/*      Global vars.
 */

extern int ft_ignore_ecc_err;

/*      Local vars.
 */

/* shouldn't this be calle "init_buffers()" or something?
 */
void ftape_zap_read_buffers(ftape_info_t *ftape)
{
	fdc_zap_buffers(ftape->fdc);
	if (ftape->fdc->ops && ftape->fdc->ops->terminate_dma) {
		(void)ftape->fdc->ops->terminate_dma(ftape->fdc, no_error);
	}
	ftape_reset_buffer(ftape);
}

static SectorMap convert_sector_map(ftape_info_t *ftape,
				    buffer_struct * buff)
{
	int i = 0;
	SectorMap bad_map = ftape_get_bad_sector_entry(ftape, buff->segment_id);
	SectorMap src_map = buff->soft_error_map | buff->hard_error_map;
	SectorMap dst_map = 0;
	TRACE_FUN(ft_t_any);

	if (bad_map || src_map) {
		TRACE(ft_t_flow, "bad_map = 0x%08lx", (long) bad_map);
		TRACE(ft_t_flow, "src_map = 0x%08lx", (long) src_map);
	}
	while (bad_map) {
		while ((bad_map & 1) == 0) {
			if (src_map & 1) {
				dst_map |= (1 << i);
			}
			src_map >>= 1;
			bad_map >>= 1;
			++i;
		}
		/* (bad_map & 1) == 1 */
		src_map >>= 1;
		bad_map >>= 1;
	}
	if (src_map) {
		dst_map |= (src_map << i);
	}
	if (dst_map) {
		TRACE(ft_t_flow, "dst_map = 0x%08lx", (long) dst_map);
	}
	TRACE_EXIT dst_map;
}

int ftape_ecc_correct(ftape_info_t *ftape, buffer_struct *buff)
{
	int result;
	struct memory_segment mseg;
	SectorMap read_bad;
	TRACE_FUN(ft_t_any);

	mseg.read_bad   = convert_sector_map(ftape, buff);
	mseg.marked_bad = 0;	/* not used... */
	mseg.blocks     = buff->bytes / FT_SECTOR_SIZE;
	mseg.data       = buff->virtual;

	/*    If there are no data sectors we can skip this segment.
	 */
	if (mseg.blocks <= 3) {
		TRACE(ft_t_noise, "empty segment");
		TRACE_EXIT 0;
	}

	read_bad = mseg.read_bad;
	ftape->history.crc_errors += count_ones(read_bad);
	
	result = ftape_ecc_correct_data(&mseg);

	if (read_bad != 0 || mseg.corrected != 0) {
		ft_trace_t level;
#if 0
		if ((read_bad ^ mseg.corrected) & mseg.corrected) {
			level = ft_t_info;
		} else {
			level = ft_t_noise;
		}
#else
		level = ft_t_info;
#endif
		TRACE(level, "crc error map: 0x%08lx",
		      (unsigned long)read_bad);
		TRACE(level, "corrected map: 0x%08lx",
		      (unsigned long)mseg.corrected);
		ftape->history.corrected += count_ones(mseg.corrected);
	}



	if (ft_ignore_ecc_err) {
		if (result == ECC_CORRECTED || result == ECC_OK) {

		} else {
			TRACE(ft_t_info, ">>> Ignoring ECC failure at segment: %d", buff->segment_id);
		}
		TRACE_EXIT (mseg.blocks - 3) * FT_SECTOR_SIZE;
	}



	if (result == ECC_CORRECTED || result == ECC_OK) {
		if (result == ECC_CORRECTED) {
			TRACE(ft_t_info, "ecc corrected segment: %d",
			      buff->segment_id);
		}
		if ((read_bad ^ mseg.corrected) & mseg.corrected) {
			/* sectors corrected without crc errors set */
			ftape->history.crc_failures++;
		}
		TRACE_EXIT (mseg.blocks - 3) * FT_SECTOR_SIZE;
	} else {
		ftape->history.ecc_failures++;
		TRACE(ft_t_err, "ecc failure on segment %d", buff->segment_id);
		TRACE_EXIT -EAGAIN;
	}
}

/*      Read given segment into buffer at address.
 *
 *      The low level FDC drivers will assign a memory region to
 *      *address which will contain the data.
 *
 *      *address is guaranteed to point to a valid region if return
 *      value >= 0
 */
int ftape_read_segment(ftape_info_t *ftape,
		       int segment_id,
		       __u8 **address, 
		       ft_read_mode_t read_mode)
{
	int result = 0;
	int retry  = 0;
	int bytes_read = 0;
	int read_done  = 0;
	TRACE_FUN(ft_t_flow);

	ftape->history.used |= 1;
	*address = NULL;
	TRACE(ft_t_data_flow, "segment_id = %d", segment_id);
	if (ftape->driver_state != reading) {
		TRACE(ft_t_noise, "calling ftape_abort_operation");
		TRACE_CATCH(ftape_abort_operation(ftape),);
		ftape_set_state(ftape, reading);
	}
	for(;;) {
		/*  Allow escape from this loop on signal !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/*  Search all full buffers for the first matching the
		 *  wanted segment.  Clear other buffers on the fly.
		 */
		while (!read_done && ftape->TAIL->status == done) {
			/*  Allow escape from this loop on signal !
			 */
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			if (ftape->TAIL->segment_id == segment_id) {
				/*  If out buffer is already full,
				 *  return its contents.  
				 */
				TRACE(ft_t_flow, "found segment in cache: %d",
				      segment_id);
				if (ftape->TAIL->deleted) {
					/*  Return a value that
					 *  read_header_segment
					 *  understands.  As this
					 *  should only occur when
					 *  searching for the header
					 *  segments it shouldn't be
					 *  misinterpreted elsewhere.
					 */
					TRACE_EXIT -ENODATA;
				}
				result =
					ftape->fdc->ops->correct_and_copy(
						ftape->fdc,
						ftape->TAIL,
						address);
				TRACE(ft_t_flow, "segment contains (bytes): %d",
				      result);
				if (result < 0) {
					if (result != -EAGAIN) {
						TRACE_EXIT result;
					}
					/* keep read_done == 0, will
					 * trigger
					 * ftape_abort_operation
					 * because reading wrong
					 * segment.
					 */
					TRACE(ft_t_err, "ecc failed, retry");
					++retry;
				} else {
					read_done = 1;
					bytes_read = result;
				}
			} else {
				TRACE(ft_t_flow,"zapping segment in cache: %d",
				      ftape->TAIL->segment_id);
			}
			ftape->TAIL->status = waiting;
			ftape->TAIL = ftape->TAIL->next;
		}
		if (!read_done && ftape->TAIL->status == reading) {
			if (ftape->TAIL->segment_id == segment_id) {
				switch(ftape_wait_segment(ftape, reading)) {
				case 0:
					break;
				case -EINTR:
					TRACE_ABORT(-EINTR, ft_t_warn,
						    "interrupted by "
						    "non-blockable signal");
					break;
				default:
					TRACE(ft_t_noise,
					      "wait_segment failed");
					ftape_abort_operation(ftape);
					ftape_set_state(ftape, reading);
					break;
				}
			} else {
				/*  We're reading the wrong segment,
				 *  stop runner.
				 */
				TRACE(ft_t_noise, "reading wrong segment");
				ftape_abort_operation(ftape);
				ftape_set_state(ftape, reading);
			}
		}
		/*    should runner stop ? FIXME: if this fails, we
		 *    release the deblock buffer.
		 */
		if (ftape->runner_status == aborting) {

			switch(ftape->HEAD->status) {
			case error:
				ftape->history.defects += 
					count_ones(ftape->HEAD->hard_error_map);
			case reading:
				ftape->HEAD->status = waiting;
				break;
			default:
				break;
			}
			TRACE_CATCH(ftape_dumb_stop(ftape),
				    fdc_put_deblock_buffer(ftape->fdc, address));
		} else {
			/*  If just passed last segment on tape: wait
			 *  for BOT or EOT mark. Sets ft_runner_status to
			 *  idle if at lEOT and successful 
			 */
			TRACE_CATCH(ftape_handle_logical_eot(ftape),
				    fdc_put_deblock_buffer(ftape->fdc, address));
		}
		/*    If we got a segment: quit, or else retry up to limit.
		 *
		 *    If segment to read is empty, do not start runner for it,
		 *    but wait for next read call.
		 */
		if (read_done) { /* no special case for empty segments */
			TRACE_EXIT bytes_read;
		}
		if (retry > FT_RETRIES_ON_ECC_ERROR) {
			ftape->history.defects ++;
			/* why no data? This is a r/w error, hence an I/O error
			 */
			TRACE_ABORT(-EIO /* NODATA */, ft_t_err,
				    "too many retries on ecc failure");
		}
		/*    Now at least one buffer is empty !
		 *    Restart runner & tape if needed.
		 */
		TRACE(ft_t_flow, "buffer[].status, [head]: %d, [tail]: %d",
		      ftape->HEAD->status,
		      ftape->TAIL->status);
		if (ftape->TAIL->status == waiting) {

			ftape_setup_new_segment(ftape, ftape->HEAD, segment_id, -1);
			if (read_mode == FT_RD_SINGLE) {
				/* disable read-ahead */
				ftape->HEAD->next_segment = 0;
			}
			ftape_calc_next_cluster(ftape->HEAD);
			if (ftape->runner_status == idle) {
				result = ftape_start_tape(ftape, segment_id,
							  ftape->HEAD->short_start);
				if (result < 0) {
					TRACE_ABORT(result, ft_t_err, "Error: "
						    "segment %d unreachable",
						    segment_id);
				}
			}
			ftape->HEAD->status = reading;
			fdc_setup_read_write(ftape->fdc, ftape->HEAD, FDC_READ);
		}
	}
	/* not reached */
	TRACE_EXIT -EIO;
}

/* Read and decode the header segment. "address" must point to 29k
 * area (virtual memory)
 */
int ftape_read_header_segment(ftape_info_t *ftape, __u8 *address)
{
	int result;
	int header_segment;
	int first_failed = 0;
	int status;
	__u8 *hseg = NULL;
	TRACE_FUN(ft_t_flow);

	ftape->used_header_segment = -1;
	TRACE_CATCH(ftape_report_drive_status(ftape, &status),);
	if (!(status & QIC_STATUS_REFERENCED)) {
		TRACE(ft_t_warn, "Cartridge is not formatted");
		TRACE_EXIT -EIO;
	}
	TRACE(ft_t_flow, "reading...");
	/*  We're looking for the first header segment.  A header
	 *  segment cannot contain bad sectors, therefor at the tape
	 *  start, segments with bad sectors are (according to
	 *  QIC-40/80) written with deleted data marks and must be
	 *  skipped.
	 *
	 *  "result == 0" means there was a segment containing deleted
	 *  data marks
	 */
	memset(address, '\0', (FT_SECTORS_PER_SEGMENT - 3) * FT_SECTOR_SIZE); 
#define HEADER_SEGMENT_BOUNDARY 68  /* why not 42? */
	for (header_segment = 0;
	     header_segment < HEADER_SEGMENT_BOUNDARY;
	     header_segment ++) {
		result = ftape_read_segment(ftape, header_segment, &hseg,
					    FT_RD_SINGLE);
		if (ftape->TAIL->deleted /* result == 0 */ ) { /* deleted data was hit */
			continue;
		}
		if (result < 0) {
			if (!first_failed) { /* try backup */
				TRACE(ft_t_err,
				      "header segment damaged, trying backup");
				first_failed = 1;
				/* force read of next (backup) segment */
				continue;
			} else { /* bail out */
				break;
			}
		}
		/* result > 0, try to decode header segment */
		if (hseg == NULL) { /* should not happen */
			result = -EIO;
			TRACE(ft_t_bug, "No deblock buffer");
			break;
		}
		if ((result = ftape_abort_operation(ftape)) < 0) {
			TRACE(ft_t_err, "ftape_abort_operation() failed");
			break;
		}
		memcpy(address, hseg, FT_SEGMENT_SIZE);
		result = ftape_decode_header_segment(ftape, address);
		if (result < 0) {
			if (!first_failed) { /* try backup */
				TRACE(ft_t_err,
				      "header segment damaged, trying backup");
				first_failed = 1;
				/* force read of next (backup) segment */
				continue;
			} else { /* bail out */
				break;
			}
		}
		ftape->used_header_segment = header_segment; /* got it */
		break;
	}
	if (result < 0 || header_segment >= HEADER_SEGMENT_BOUNDARY) {
		TRACE(ft_t_err, "no readable header segment found");
	}
	if (hseg != NULL) {
		TRACE_CATCH(fdc_put_deblock_buffer(ftape->fdc, &hseg),);
	}
 	TRACE_EXIT result;
}

int ftape_decode_header_segment(ftape_info_t *ftape, __u8 *address)
{
	unsigned int should_be_max_floppy_side;
	unsigned int max_floppy_side;
	unsigned int max_floppy_track;
	unsigned int max_floppy_sector;
	unsigned int new_tape_len;
	TRACE_FUN(ft_t_flow);

	if (GET4(address, FT_SIGNATURE) == FT_D2G_MAGIC) {
		/* Ditto 2GB header segment. They encrypt the bad sector map.
		 * We decrypt it and store them in normal format.
		 * I hope this is correct.
		 */
		int i;
		TRACE(ft_t_warn,
		      "Found Ditto 2GB tape, "
		      "trying to decrypt bad sector map");
		for (i=256; i < 29 * FT_SECTOR_SIZE; i++) {
			address[i] = ~(address[i] - (i&0xff));
		}
		PUT4(address, 0,FT_HSEG_MAGIC);
	} else if (GET4(address, FT_SIGNATURE) != FT_HSEG_MAGIC) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "wrong signature in header segment");
	}
	ftape->format_code = (ft_format_type) address[FT_FMT_CODE];
	if (ftape->format_code != fmt_big) {
		ftape->header_segment_1   = GET2(address, FT_HSEG_1);
		ftape->header_segment_2   = GET2(address, FT_HSEG_2);
		ftape->first_data_segment = GET2(address, FT_FRST_SEG);
		ftape->last_data_segment  = GET2(address, FT_LAST_SEG);
	} else {
		ftape->header_segment_1   = GET4(address, FT_6_HSEG_1);
		ftape->header_segment_2   = GET4(address, FT_6_HSEG_2);
		ftape->first_data_segment = GET4(address, FT_6_FRST_SEG);
		ftape->last_data_segment  = GET4(address, FT_6_LAST_SEG);
	}
	TRACE(ft_t_noise, "first data segment: %d", ftape->first_data_segment);
	TRACE(ft_t_noise, "last  data segment: %d", ftape->last_data_segment);
	TRACE(ft_t_noise, "header segments are %d and %d",
	      ftape->header_segment_1, ftape->header_segment_2);

	/*    Verify tape parameters...
	 *    QIC-40/80 spec:                 tape_parameters:
	 *
	 *    segments-per-track              segments_per_track
	 *    tracks-per-cartridge            tracks_per_tape
	 *    max-floppy-side                 (segments_per_track *
	 *                                    tracks_per_tape - 1) /
	 *                                    ftape_segments_per_head
	 *    max-floppy-track                ftape_segments_per_head /
	 *                                    ftape_segments_per_cylinder - 1
	 *    max-floppy-sector               ftape_segments_per_cylinder *
	 *                                    FT_SECTORS_PER_SEGMENT
	 */
	ftape->segments_per_track = GET2(address, FT_SPT);
	ftape->tracks_per_tape    = address[FT_TPC];
	/*
	 *  Begin of Ditto-Max hack.
	 */
	if (ftape->tracks_per_tape > 50 &&
	    ftape->drive_type.flags & DITTO_MAX_EXT) {
		ftape->qic_std = DITTO_MAX;
	}
	/*
	 *  End of Ditto Max hack.
	 */
	max_floppy_side       = address[FT_FHM];
	max_floppy_track      = address[FT_FTM];
	max_floppy_sector     = address[FT_FSM];
	TRACE(ft_t_noise, "(fmt/spt/tpc/fhm/ftm/fsm) = %d/%d/%d/%d/%d/%d",
	      ftape->format_code, ftape->segments_per_track, ftape->tracks_per_tape,
	      max_floppy_side, max_floppy_track, max_floppy_sector);
	new_tape_len = ftape->tape_len;
	switch (ftape->format_code) {
	case fmt_425ft:
		new_tape_len = 425;
		break;
	case fmt_normal:
		if (ftape->tape_len == 0) {	/* otherwise 307 ft */
			new_tape_len = 205;
		}
		break;
	case fmt_1100ft:
		new_tape_len = 1100;
		break;
	case fmt_big:
	case fmt_var:
	{
		int segments_per_1000_feet = 1;		/* non-zero default for switch */
		switch (ftape->qic_std) {
		case QIC_UNKNOWN:
		case QIC_40:
			segments_per_1000_feet = 332;
			break;
		case QIC_80:
			segments_per_1000_feet = 488;
			break;
		case QIC_3010:
			segments_per_1000_feet = 730;
			break;
		case QIC_3020:
			segments_per_1000_feet = 1430;
			break;
		case DITTO_MAX:
			/*  The following is a guess. According to
			 *  Iomega a 1250 ft tape has min. 2427 spt. 
			 */
			segments_per_1000_feet = 1940;
			break;
		}
		new_tape_len = (1000 * ftape->segments_per_track +
				(segments_per_1000_feet - 1)) / segments_per_1000_feet;
		break;
	}
	default:
		TRACE_ABORT(-EIO, ft_t_err,
			    "unknown tape format, please report !");
	}
	if (new_tape_len != ftape->tape_len) {
		ftape->tape_len = new_tape_len;
		TRACE(ft_t_info, "calculated tape length is %d ft",
		      ftape->tape_len);
		ftape_calc_timeouts(ftape,
				    ftape->qic_std,
				    ftape->data_rate,
				    ftape->tape_len);
	}
	if (ftape->segments_per_track == 0 && ftape->tracks_per_tape == 0 &&
	    max_floppy_side == 0 && max_floppy_track == 0 &&
	    max_floppy_sector == 0) {
		/*  QIC-40 Rev E and earlier has no values in the header.
		 */
		ftape->segments_per_track = 68;
		ftape->tracks_per_tape = 20;
		max_floppy_side = 1;
		max_floppy_track = 169;
		max_floppy_sector = 128;
		if (ftape->last_data_segment == 2799)
		{
			/* set for Xenix (80meg - even if a DC2120 tape) */
		    ftape->segments_per_track = 100;
		    ftape->tracks_per_tape = 28;
		    max_floppy_side = 4;
		    max_floppy_track = 149;
		    max_floppy_sector = 128;
		    TRACE(ft_t_info, "Setting for a Xenix tape");
		}
	}
	/*  This test will compensate for the wrong parameter on tapes
	 *  formatted by Conner software.
	 */
	if (ftape->segments_per_track == 150 &&
	    ftape->tracks_per_tape == 28 &&
	    max_floppy_side == 7 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous CONNER bug: max_floppy_side off by one !");
		max_floppy_side = 6;
	}
	/*  These tests will compensate for the wrong parameter on tapes
	 *  formatted by ComByte Windows software.
	 *
	 *  First, for 205 foot tapes
	 */
	if (ftape->segments_per_track == 100 &&
	    ftape->tracks_per_tape == 28 &&
	    max_floppy_side == 9 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the ComByte bug: max_floppy_side incorrect!");
		max_floppy_side = 4;
	}
	/* Next, for 307 foot tapes. */
	if (ftape->segments_per_track == 150 &&
	    ftape->tracks_per_tape == 28 &&
	    max_floppy_side == 9 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the ComByte bug: max_floppy_side incorrect!");
		max_floppy_side = 6;
	}
	/*  This test will compensate for the wrong parameter on tapes
	 *  formatted by Colorado Windows software.
	 */
	if (ftape->segments_per_track == 150 &&
	    ftape->tracks_per_tape == 28 &&
	    max_floppy_side == 6 &&
	    max_floppy_track == 150 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous Colorado bug: max_floppy_track off by one !");
		max_floppy_track = 149;
	}
	/* another max_floppy_track bug. Seems to affect some OS/2
	 * software. Reported by Mike Hardy <hardymi@earthlink.net>
	 */
	if (ftape->segments_per_track == 150 &&
	    ftape->tracks_per_tape == 28 &&
	    max_floppy_side == 6 &&
	    max_floppy_track == 254 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous OS/2 bug: max_floppy_track off by 105 !");
		max_floppy_track = 149;
	}
	/*  This test will compensate for some bug reported by Dima
	 *  Brodsky.  Seems to be a Colorado bug, either. (freebee
	 *  Imation tape shipped together with Colorado T3000
	 */
	if ((ftape->format_code == fmt_var || ftape->format_code == fmt_big) &&
	    ftape->tracks_per_tape == 50 &&
	    max_floppy_side == 54 &&
	    max_floppy_track == 255 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous ??? bug: max_floppy_track off by one !");
		max_floppy_track = 254;
	}

	/*  This rather special test will compensate a wrong field in
	 *  some Imation tape cartridge. Reported by
	 *  Nick.Holloway@alfie.demon.co.uk
	 */
	if (ftape->format_code        == 6 &&
	    ftape->segments_per_track == 1414 &&
	    ftape->tracks_per_tape    == 62   &&
	    max_floppy_side           == 2    && /* !!!! */ 
	    max_floppy_track          == 254  &&
	    max_floppy_sector         == 128) {
TRACE(ft_t_info, "One of the rare Imation bugs: max_floppy_side is totally wrong");
		max_floppy_side = 85;
	}

#if 0
	/*  steve@rex.dircon.co.uk.  Yet another bug in Colorado
 	 *  tapes?! This time, the max floppy side is wrong by one: it
 	 *  says 55 but should be 54.  Another case of freebie Imation
 	 *  tape shipped together with Colorado T3000 perhaps?
	 *
	 *  In principle, this formatting bug is already caught by the
	 *  more general test below. Therefore it is "#ifdefed" 0
	 */
 	if ((ft_format_code == fmt_var || ft_format_code == fmt_big) &&
	    ft_tracks_per_tape == 50 &&
	    max_floppy_side == 55 &&
	    max_floppy_track == 254 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "another Colorado/T3000/imation tape bug: max_floppy_side off by one!");
 		max_floppy_side  = 54;
 	}
#endif
	ftape->segments_per_head = ((max_floppy_sector/FT_SECTORS_PER_SEGMENT) *
				    (max_floppy_track + 1));
	should_be_max_floppy_side = ((ftape->segments_per_track *
				      ftape->tracks_per_tape -1) / 
				     ftape->segments_per_head );
	if ((max_floppy_side - should_be_max_floppy_side) == 1) {
		TRACE(ft_t_info, "max_floppy_side off by one");
		max_floppy_side --;		
	}

	/*
	 *    Verify drive_configuration with tape parameters
	 */
	if (ftape->segments_per_head == 0     ||
	    ftape->segments_per_cylinder == 0 ||
	    ((ftape->segments_per_track * ftape->tracks_per_tape - 1) / ftape->segments_per_head
	     != max_floppy_side) ||
	    (ftape->segments_per_head / ftape->segments_per_cylinder - 1 != max_floppy_track) ||
	    (ftape->segments_per_cylinder * FT_SECTORS_PER_SEGMENT != max_floppy_sector)
#ifdef TESTING
	    || ((ftape->format_code == fmt_var || ftape->format_code == fmt_big) && 
		(max_floppy_track != 254 || max_floppy_sector != 128))
#endif
	   ) {
		TRACE(ft_t_err,"Tape parameters inconsistency, please report");
		TRACE(ft_t_err, "reported = %d/%d/%d/%d/%d/%d",
		      ftape->format_code,
		      ftape->segments_per_track,
		      ftape->tracks_per_tape,
		      max_floppy_side,
		      max_floppy_track,
		      max_floppy_sector);
		TRACE(ft_t_err, "required = %d/%d/%d/%d/%d/%d",
		      ftape->format_code,
		      ftape->segments_per_track,
		      ftape->tracks_per_tape,
		      ((ftape->segments_per_track * ftape->tracks_per_tape -1) / 
		       ftape->segments_per_head ),
		      (ftape->segments_per_head / 
		       ftape->segments_per_cylinder - 1 ),
		      (ftape->segments_per_cylinder * FT_SECTORS_PER_SEGMENT));
		TRACE_EXIT -EIO;
	}
	ftape_extract_bad_sector_map(ftape, address);
	/* ftape_put_bad_sector_entry(ftape, 4, 0); */
#if TESTING
	ftape_put_bad_sector_entry(ftape, 2, EMPTY_SEGMENT);
	ftape_put_bad_sector_entry(ftape, 3, EMPTY_SEGMENT);
	ftape_put_bad_sector_entry(ftape, 4, EMPTY_SEGMENT);
	ftape->first_data_segment = 5;
	PUT4(address, FT_6_FRST_SEG, ftape->first_data_segment);
#endif
 	TRACE_EXIT 0;
}
