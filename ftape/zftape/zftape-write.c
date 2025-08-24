/*
 *      Copyright (C) 1996-1998 Claus Heine

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
 * $RCSfile: zftape-write.c,v $
 * $Revision: 1.21 $
 * $Date: 2000/07/20 11:17:16 $
 *
 *      This file contains the writing code
 *      for the QIC-117 floppy-tape driver for Linux.
 */


#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mm.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#define ZFTAPE_TRACING
#include "zftape-init.h"
#include "zftape-eof.h"
#include "zftape-ctl.h"
#include "zftape-write.h"
#include "zftape-read.h"
#include "zftape-rw.h"
#include "zftape-vtbl.h"
#include "zftape-buffers.h"
#include "zftape-inline.h"

/*      Global vars.
 */

/*      Local vars.
 */


static int zft_write_header_segments(zftape_info_t *zftape, __u8* buffer)
{
	ftape_info_t *ftape = zftape->ftape;
	int header_1_ok = 0;
	int header_2_ok = 0;
	unsigned int time_stamp;
	TRACE_FUN(ft_t_noise);
	
	TRACE_CATCH(ftape_abort_operation(ftape),);
	ftape_seek_to_bot(ftape);    /* prevents extra rewind */
	if (GET4(buffer, 0) != FT_HSEG_MAGIC) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "wrong header signature found, aborting");
	} 
	/*   Be optimistic: */
	PUT4(buffer, FT_SEG_CNT,
	     zftape->written_segments + GET4(buffer, FT_SEG_CNT) + 2);
	if ((time_stamp = zft_get_time()) != 0) {
		PUT4(buffer, FT_WR_DATE, time_stamp);
		if (zftape->label_changed) {
			PUT4(buffer, FT_LABEL_DATE, time_stamp);
		}
	}
	TRACE(ft_t_noise,
	      "writing first header segment %d", ftape->header_segment_1);
	header_1_ok = zft_verify_write_segments(zftape,
						ftape->header_segment_1,
						buffer, FT_SEGMENT_SIZE) >= 0;
	TRACE(ft_t_noise,
	      "writing second header segment %d", ftape->header_segment_2);
	header_2_ok = zft_verify_write_segments(zftape,
						ftape->header_segment_2, 
						buffer, FT_SEGMENT_SIZE) >= 0;
	if (!header_1_ok) {
		TRACE(ft_t_warn, "Warning: "
		      "update of first header segment failed");
	}
	if (!header_2_ok) {
		TRACE(ft_t_warn, "Warning: "
		      "update of second header segment failed");
	}
	if (!header_1_ok && !header_2_ok) {
		TRACE_ABORT(-EIO, ft_t_err, "Error: "
		      "update of both header segments failed.");
	}
	TRACE_EXIT 0;
}

int zft_update_header_segments(zftape_info_t *zftape)
{
	ftape_info_t *ftape = zftape->ftape;
	TRACE_FUN(ft_t_noise);
	
	/*  must NOT use zft_write_protected, as it also includes the
	 *  file access mode. But we also want to update when soft
	 *  write protection is enabled (O_RDONLY)
	 */
	if (ftape->write_protected || zft_vtbl_soft_ro(zftape)) {
		TRACE_ABORT(0, ft_t_noise, "Tape set read-only: no update");
	} 
	if (!zftape->header_read) {
		TRACE_ABORT(0, ft_t_noise, "Nothing to update");
	}
	if (!zftape->header_changed) {
		zftape->header_changed = zftape->written_segments > 0;
	}
	if (!zftape->header_changed && !zftape->volume_table_changed) {
		TRACE_ABORT(0, ft_t_noise, "Nothing to update");
	}
	TRACE(ft_t_noise, "Updating header segments");
	if (ftape->driver_state == writing) {
		TRACE_CATCH(ftape_loop_until_writes_done(ftape),);
	}
	TRACE_CATCH(ftape_abort_operation(ftape),);

	if (zftape->header_changed) {
		TRACE_CATCH(zft_write_header_segments(zftape, zftape->hseg_buf),);
	}
	if (zftape->vtbl_read && zftape->volume_table_changed) {
		TRACE_CATCH(zft_update_volume_table(zftape, ftape->first_data_segment),);
	}
	zftape->header_changed =
		zftape->volume_table_changed = 
		zftape->label_changed        =
		zftape->written_segments     = 0;
	TRACE_CATCH(ftape_abort_operation(ftape),);
	ftape_seek_to_bot(ftape);
	TRACE_EXIT 0;
}

/* hard write error recovery on a sector by sector basis implies that we
 * have to shift the data physically towards EOT on the tape. This gives us
 * some overshoot sectors which are stored in this list.
 */

struct hard_error {
	__u8 space[FT_SECTOR_SIZE];
	struct list_head node;
};

void zft_zap_write_buffers(zftape_info_t *zftape)
{
	TRACE_FUN(ft_t_flow);

	/* this catches the case were next == prev == NULL
	 */
	if (zftape->hard_errors.next == NULL ||
	    zftape->hard_errors.prev == NULL) {
		INIT_LIST_HEAD(&zftape->hard_errors);
		TRACE_EXIT;
	}
	for (;;) {
		struct list_head *tmp = zftape->hard_errors.prev;
		struct hard_error *herror;

		if (tmp == &zftape->hard_errors)
			break;
		list_del(tmp);
		zftape->hard_error_cnt --;
		herror = list_entry(tmp, struct hard_error, node);
		ftape_kfree(FTAPE_SEL(zftape->unit),
			    &herror,
			    sizeof(struct hard_error));
	}
	if (zftape->hard_error_cnt) {
		TRACE(ft_t_warn, "hard error count: %d",
		      zftape->hard_error_cnt);
	}
	TRACE_EXIT;
}

static int empty_hard_error_list(zftape_info_t *zftape,
				 __u8 *dst_buf, int seg_sz,
				 zft_position *pos)
{
	int space_left = seg_sz - pos->seg_byte_pos;
	TRACE_FUN(ft_t_flow);
	
	for (;;) {
		struct list_head *tmp = zftape->hard_errors.next;
		struct hard_error *herror;
		
		if (tmp == &zftape->hard_errors) {
			/* no hard error data pending */
			break; 
		}
		TRACE(ft_t_warn, "Trying to handle hard error");
		if (space_left % FT_SECTOR_SIZE != 0) {
			TRACE_ABORT(-EIO, ft_t_bug,
				    "space_left %d should be a multiple of %d",
				    space_left, FT_SECTOR_SIZE);
		}
		if (space_left < FT_SECTOR_SIZE) {
			TRACE_EXIT 0; /* Nothing, use next segment */
		}
		if (pos->tape_pos + FT_SECTOR_SIZE > zftape->capacity) {
			/* forget the remaining hard errors.
			 */
			zft_zap_write_buffers(zftape);
			TRACE_EXIT -ENOSPC; /* no luck */
		}
		list_del(tmp);
		zftape->hard_error_cnt --;
		herror = list_entry(tmp, struct hard_error, node);
		memcpy(dst_buf + pos->seg_byte_pos, herror->space,
		       FT_SECTOR_SIZE);
		ftape_kfree(FTAPE_SEL(zftape->unit), &herror, sizeof(*herror));
		pos->seg_byte_pos += FT_SECTOR_SIZE;
		space_left -= FT_SECTOR_SIZE;
	};
	TRACE_EXIT 0;
}

/*  This function writes the segment at pos->seg_pos and handles hard write
 *  errors. NOT for use with the header segments.
 *
 *  It assumes that zftape->deblock_buffer already points to a valid buffer
 *  (and contains data)
 *
 *  Note: in the unlikely case that HEAD == TAIL the segment size could 
 *  have changed after calling this function ...
 *
 *  The zftape->hard_errors list will contain at most as many sectors
 *  as fit into a single segment, typically 29.  This statement holds
 *  as we first flush the hard error list to the deblock buffer before
 *  copying new user data, so if there N sectors left over from hard
 *  write errors, and the next segment will exhibit 29 new hard write
 *  errors, then first the hard write error list is decreased by
 *  min(29,N) and the hard write error list will at worst increase by
 *  29-N (if N <= 29). If N > 29, then the size of the hard error list
 *  remains unchanged in the worst case. Or in summary: if there are
 *  more "over-shoot" sectors in the hard write error list than there
 *  are free sectors in the next segment, then the hard write error
 *  list will at worst stay at the same size.
 */
static int zftape_write_segment(zftape_info_t *zftape, int seg_pos, int seg_sz)
{
	ftape_info_t *ftape = zftape->ftape;
	void *backup = NULL;
	struct hard_error *scratch = NULL;
	int result;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_flow, "seg. no: %d", seg_pos);
	for (;;) { /* until no more hard errors */
		if ((result = ftape_write_segment(ftape,
						  seg_pos,
						  &zftape->deblock_buf,
						  FT_WR_ASYNC)) >= 0) {
			zftape->written_segments ++;
			break;
		}
		if (result != -EIO ||
		    ftape->HEAD->status != error ||
		    ftape->HEAD->hard_error_map == 0) {
			break;
		}
		TRACE(ft_t_warn,
		      "Hard error writing segment, trying to recover");
		/* hard write error, try to recover
		 *
		 * First we need to save the data contained in the
		 * deblock buffer
		 */
		if (zft_vmalloc_once(zftape, &backup, FT_SEGMENT_SIZE)  < 0) {
			break;
		}
		/* save the deblock buffer
		 */
		memcpy(backup, zftape->deblock_buf, seg_sz);
		scratch = ftape_kmalloc(FTAPE_SEL(zftape->unit),
					sizeof(struct hard_error), 1);
		/* move the data around.
		 */
		if (ftape_hard_error_recovery(ftape, scratch->space) < 0) {
			break;
		}
		zftape->capacity -= FT_SECTOR_SIZE; /* 1kb less */
		zftape->deblock_buf = fdc_get_deblock_buffer(ftape->fdc);

		if (seg_sz > 0) {
			memcpy(zftape->deblock_buf, scratch->space,
			       FT_SECTOR_SIZE);
			memcpy(zftape->deblock_buf + FT_SECTOR_SIZE, backup,
			       seg_sz - FT_SECTOR_SIZE);
			memcpy(scratch->space,
			       backup + seg_sz - FT_SECTOR_SIZE,
			       FT_SECTOR_SIZE);
		}

		list_add( &scratch->node, zftape->hard_errors.prev);
		zftape->hard_error_cnt ++;
		scratch = NULL;
	}
	/*  N.B.: these function take care of the case where ptr == NULL
	 */
	ftape_kfree(FTAPE_SEL(zftape->unit), &scratch, sizeof(*scratch));
	ftape_vfree(FTAPE_SEL(zftape->unit), &backup, FT_SEGMENT_SIZE);
	TRACE_EXIT result;
}

/* Loop until writes or done or until hard write
 * error. zft_flush_buffers() will then do the right thing, i.e. write
 * the shifted data to a new segment or simply discard it (if it was
 * simply the zero-padding at the end of the last segment)
 */
static int zftape_loop_until_writes_done(zftape_info_t *zftape)
{
	ftape_info_t *ftape = zftape->ftape;
	struct hard_error *scratch = NULL;
	int result;
	TRACE_FUN(ft_t_flow);
	
	if ((result = ftape_loop_until_writes_done(ftape)) == 0) {
		TRACE_EXIT 0;
	}
	
	if (result != -EIO ||
	    ftape->HEAD->status != error ||
	    ftape->HEAD->hard_error_map == 0) {
		/* that's really bad. What to to with
		 * zft_tape_pos?
		 */
		TRACE_EXIT result;
	}
	TRACE(ft_t_warn, "hard error while flushing write buffers");
	/* Ok, this is a hard write error. This time, we don't
	 * need to save the deblock buffer, as it already has been
	 * enqueued in the dma buffer list.
	 */
	scratch = ftape_kmalloc(FTAPE_SEL(zftape->unit),
				sizeof(struct hard_error), 1);
	/* move the data around.
	 */
	if (ftape_hard_error_recovery(ftape, scratch->space) < 0) {
		ftape_kfree(FTAPE_SEL(zftape->unit),
			    &scratch, sizeof(*scratch));
		TRACE_EXIT -EIO;
	}
	zftape->capacity -= FT_SECTOR_SIZE; /* 1kb less */
	list_add(&scratch->node, zftape->hard_errors.prev);
	zftape->hard_error_cnt ++;
	TRACE_EXIT 0;
}

/* decide whether we can forget about the data shifted towards EOT by
 * a hard write error. Return -EAGAIN if write_segment() should be be
 * called again. If the shifted data still fits into the current
 * segment, simply return 0, the caller will use loop_until_writes_done()
 * again.
 */
static int flush_handle_hard_errors(zftape_info_t *zftape, 
				    zft_position *pos,
				    int *pad_cnt, int old_seg_size)
{
	int space_left;
	int this_segs_size;
	TRACE_FUN(ft_t_flow);

	/* The current segment still is pos->seg_pos. But it's
	 * size might have changed ...
	 *
	 * The linear data position doesn't change in case of
	 * hard errors
	 */
	this_segs_size = zft_get_seg_sz(zftape, pos->seg_pos);
	space_left = (this_segs_size -
		      pos->seg_byte_pos -
		      zftape->hard_error_cnt * FT_SECTOR_SIZE);
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "hard_error_cnt: %d\n"
	      KERN_INFO "space_left    : %d\n"
	      KERN_INFO "seg_size      : %d\n"
	      KERN_INFO "seg_byte_pos  : %d\n"
	      KERN_INFO "pad_cnt       : %d",
	      zftape->hard_error_cnt, space_left, this_segs_size,
	      pos->seg_byte_pos, *pad_cnt);  
	if (space_left >= -(*pad_cnt)) {
		pos->seg_byte_pos += 
			((zftape->hard_error_cnt * FT_SECTOR_SIZE) - 
			 (old_seg_size - this_segs_size));
		/* forget the remaining hard errors.
		 */
		zft_zap_write_buffers(zftape);
		/*  Don't forget to adjust pad_cnt ...
		 */
		*pad_cnt = this_segs_size - pos->seg_byte_pos;
		/* no need to call write_segment() again. The data 
		 * already has been moved to the cyclic buffer list.
		 */
		TRACE_EXIT 0; /* okay to continue loop_until_writes_done() */
	}
	/* need to write another segment. First flush the hard
	 * error list to the deblock buffer
	 */
	zftape->deblock_buf = fdc_get_deblock_buffer(zftape->ftape->fdc);
#if 1 || defined(PARANOID)
	if (zftape->deblock_buf == NULL) {
		TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
	}
#endif
	pos->seg_pos ++;
	pos->seg_byte_pos = 0;
	this_segs_size = zft_get_seg_sz(zftape, pos->seg_pos);
	empty_hard_error_list(zftape,
			      zftape->deblock_buf, this_segs_size, pos);
	TRACE_EXIT -EAGAIN;
}

/* Flush the write buffer(s) to tape.
 *
 * We have to be careful to treat hard write errors correctly.
 * The remainder of the segment is always filled with zeroes.
 * This function DOES NOT write file marks. This is done by zft_close()
 * zft_def_idle_state().
 *
 * NOTE: we NEVER put more data into the deblock buffer than there is
 * space in the corresponding tape segment.
 *
 * Also, the hard error list already has been flushed out to the deblock
 * buffer in case there was space left on the tape.
 *
 * Therefor we have the following layout:
 *
 * a) either there are no pending hard errors in the hard error
 *    list. The we only have to take care of new hard errors that
 *    loop_until_writes_done() will report to us.
 *
 * b) or the hard error list isn't empty. But then there is no space
 *    left on the device. Nevertheless we need to copy with new hard
 *    errors.  But a new hard error will -- of course -- lead to data
 *    loss as the data is shifted towards EOT over EOT and is lost and
 *    forgotten
 */

int zft_flush_buffers(zftape_info_t *zftape)
{
	ftape_info_t *ftape = zftape->ftape;
	int result;
	int pad_cnt;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_data_flow,
	      "entered, ftape_state = %d", ftape->driver_state);
	if (ftape->driver_state != writing && !zftape->need_flush) {
		TRACE_ABORT(0, ft_t_noise, "no need for flush");
	}
	zftape->io_state = zft_idle; /*  triggers some initializations for the
				      *  read and write routines 
				      */
	if (zftape->last_write_failed) {
		ftape_abort_operation(ftape);
		TRACE_EXIT -EIO;
	}
	TRACE(ft_t_noise, "flushing write buffers");

	pad_cnt = 0;

	for (;;) {
		int old_seg_size, this_segs_size;

		old_seg_size = this_segs_size =
			zft_get_seg_sz(zftape, zftape->pos.seg_pos);
		if (zftape->pos.seg_byte_pos != this_segs_size) {
			zftape->pos.seg_byte_pos -= pad_cnt;
			pad_cnt = this_segs_size - zftape->pos.seg_byte_pos;
		}

		/* If there is any data not written to tape yet,
		 * append zero's up to the end of the segment.
		 */
		TRACE(ft_t_noise, "Position:\n"
		      KERN_INFO "seg_pos  : %d\n"
 		      KERN_INFO "byte pos : %d",
		      zftape->pos.seg_pos,
		      zftape->pos.seg_byte_pos);

		if (zftape->pos.seg_byte_pos > 0) {

			if (zftape->deblock_buf == NULL) {
				TRACE_ABORT(-EIO, ft_t_bug,
					    "No deblock buffer");
			}

			memset(zftape->deblock_buf + zftape->pos.seg_byte_pos,
			       0, this_segs_size - zftape->pos.seg_byte_pos);

			result = zftape_write_segment(zftape,
						      zftape->pos.seg_pos,
						      this_segs_size);
			if (result < 0) {
				goto error_out;
			}
		}
		/* now loop until the writes are actually done. This possibly
		 * will give us one additional bad sector ...
		 */
		do {
			result = zftape_loop_until_writes_done(zftape);
			if (result < 0) {
				goto error_out;
			}
			if (zftape->hard_error_cnt == 0) {
				/* no more pending hard errors. Adjust
				 * the position record and break the
				 * loop!
				 */
				goto out_flushed;
			}
			result = flush_handle_hard_errors(zftape, 
							  &zftape->pos,
							  &pad_cnt,
							  old_seg_size);
			if (result < 0 && result != -EAGAIN) {
				goto error_out;
			}
			FT_SIGNAL_EXIT(_DONT_BLOCK);
		} while (result != -EAGAIN);
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}; /* for(;;) */
 out_flushed:
	if (zftape->pos.seg_byte_pos == 0) {
		zftape->pos.seg_pos --;
		zftape->pos.seg_byte_pos = zft_get_seg_sz(zftape,
							  zftape->pos.seg_pos);
	}
	TRACE(ft_t_any, "zft_seg_pos: %d, zft_seg_byte_pos: %d",
	      zftape->pos.seg_pos, zftape->pos.seg_byte_pos);
	zftape->last_write_failed  =
		zftape->need_flush = 0;
	TRACE_EXIT 0;
 error_out:
	TRACE(ft_t_err, "flush buffers failed");
	zftape->pos.tape_pos -= zftape->pos.seg_byte_pos;
	zftape->pos.seg_byte_pos = 0;
	zftape->last_write_failed = 1;
	TRACE_EXIT result;
	
}

static int check_write_access(zftape_info_t *zftape,
			      int req_len,
			      const zft_volinfo **volume,
			      zft_position *pos,
			      const unsigned int blk_sz)
{
	int result;
	TRACE_FUN(ft_t_flow);

	if ((req_len % zftape->blk_sz) != 0) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "write-count %d must be multiple of block-size %d",
			    req_len, blk_sz);
	}
	if (zftape->io_state == zft_writing) {
		/*  all other error conditions have been checked earlier
		 */
		TRACE_EXIT 0;
	}
	zftape->io_state = zft_idle;
	/*  If we haven't read the header segment yet, do it now.
	 *  This will verify the configuration, get the bad sector
	 *  table and read the volume table segment 
	 */
	TRACE_CATCH(zft_check_write_access(zftape, pos),);
	/*  fine. Now the tape is either at BOT or at EOD,
	 *  Write start of volume now
	 */
	TRACE_CATCH(zft_open_volume(zftape, pos, blk_sz),);
	*volume = zft_volume_by_segpos(zftape, pos->seg_pos);
	DUMP_VOLINFO(ft_t_noise, "", *volume);
	zftape->just_before_eof = 0;
	/* now merge with old data if neccessary */
	if (!zftape->qic_mode && pos->seg_byte_pos != 0){
		result = zft_fetch_segment(zftape,
					   pos->seg_pos,
					   &zftape->deblock_buf,
					   FT_RD_SINGLE);
		if (result < 0) {
			if (result == -EINTR || result == -ENOSPC) {
				TRACE_EXIT result;
			}
			TRACE(ft_t_noise, 
			      "ftape_read_segment() result: %d.\n"
			      KERN_INFO "This might be normal when using "
			      "a newly formatted tape", result);
			memset(zftape->deblock_buf, '\0', pos->seg_byte_pos);
		}
	}
	zftape->deblock_segment = -1; /* invalidate cache */
	zftape->io_state = zft_writing;
	TRACE_EXIT 0;
}

static int fill_deblock_buf(zftape_info_t *zftape,
			    __u8 *dst_buf, const int seg_sz,
			    zft_position *pos, const zft_volinfo *volume,
			    const char *usr_buf, const int req_len)
{
	int cnt = 0;
	int space_left;
	TRACE_FUN(ft_t_flow);

	if (seg_sz == 0) {
		TRACE_ABORT(0, ft_t_data_flow, "empty segment");
	}
	TRACE(ft_t_data_flow, "\n"
	      KERN_INFO "remaining req_len: %d\n"
	      KERN_INFO "          buf_pos: %d", 
	      req_len, pos->seg_byte_pos);

	/* First handle the hard write error recovery ...
	 */
	TRACE_CATCH(empty_hard_error_list(zftape, dst_buf, seg_sz, pos),);

	if (pos->tape_pos + volume->blk_sz > zftape->capacity) {
		TRACE_EXIT -ENOSPC;
	}
	/*  now handle the "regular" data.
	 */
	space_left = seg_sz - pos->seg_byte_pos;
	cnt = req_len < space_left ? req_len : space_left;
	if (copy_from_user(dst_buf + pos->seg_byte_pos, usr_buf, cnt) != 0) {
		TRACE_EXIT -EFAULT;
	}
	pos->volume_pos   += cnt;
	pos->seg_byte_pos += cnt;
	pos->tape_pos     += cnt;
	TRACE(ft_t_data_flow, "\n"
	      KERN_INFO "removed from user-buffer : %d bytes.\n"
	      KERN_INFO "zft_tape_pos             : " LL_X " bytes.",
	      cnt, LL(pos->tape_pos));
	TRACE_EXIT cnt;
}


/*  called by the kernel-interface routine "zft_write()"
 */
int _zft_write(zftape_info_t *zftape, const char* buff, int req_len)
{
	int result = 0;
	int written = 0;
	int write_cnt;
	int seg_sz;
	TRACE_FUN(ft_t_flow);
	
	zftape->resid         = req_len;	
	zftape->last_write_failed = 1; /* reset to 0 when successful */
	/* check if write is allowed 
	 */
	TRACE_CATCH(check_write_access(zftape,
				       req_len,
				       &zftape->volume,
				       &zftape->pos,
				       zftape->blk_sz),);
	while (req_len > 0 || !list_empty(&zftape->hard_errors)) {
		if (zftape->pos.seg_pos == zftape->ftape->first_data_segment) {
			zftape->vtbl_read = 0;
		}
		seg_sz = zft_get_seg_sz(zftape, zftape->pos.seg_pos);
		if ((zftape->deblock_buf = fdc_get_deblock_buffer(zftape->ftape->fdc)) == NULL) {
			TRACE_ABORT(-EIO, ft_t_bug, "No deblock buffer");
		}
		if ((write_cnt = fill_deblock_buf(zftape,
						  zftape->deblock_buf,
						  seg_sz,
						  &zftape->pos,
						  zftape->volume,
						  buff,
						  req_len)) < 0) {
			zftape->resid -= written;
			if (write_cnt == -ENOSPC) {
				/* leave the remainder to flush_buffers()
				 */
				TRACE(ft_t_info, "No space left on device");
				zftape->last_write_failed = 0;
				if (!zftape->need_flush) {
					zftape->need_flush = written > 0;
				}
				TRACE_EXIT written > 0 ? written : -ENOSPC;
			} else {
				TRACE_EXIT result;
			}
		}
		/* Allow us to escape from this loop with a signal !
		 * N.B.: Leave this at THIS PLACE, in between
		 * fill_deblock_buffer() and zftape_write_segment().
		 * fill_deblock_buffer() also handles write hard
		 * errors, while zftape_write_segment() might produce
		 * new hard errors. So better abort after handling old
		 * hard errors without producing new ones ...
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		if (zftape->pos.seg_byte_pos == seg_sz) {
			TRACE_CATCH(zftape_write_segment(zftape,
							 zftape->pos.seg_pos,
							 seg_sz),
				    zftape->resid -= written);
			zftape->pos.seg_byte_pos =  0;
			++(zftape->pos.seg_pos);
		}
		written += write_cnt;
		buff    += write_cnt;
		req_len -= write_cnt;
	} /* while (req_len > 0) */
	TRACE(ft_t_data_flow, "remaining in blocking buffer: %d",
	       zftape->pos.seg_byte_pos);
	TRACE(ft_t_data_flow, "just written bytes: %d", written);
	zftape->last_write_failed = 0;
	zftape->resid -= written;
	zftape->need_flush = zftape->need_flush || written > 0;
	TRACE_EXIT written;               /* bytes written */
}
