#ifndef _ZFTAPE_CTL_H
#define _ZFTAPE_CTL_H

/*
 * Copyright (C) 1996-1998 Claus-Justus Heine.

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
 * $RCSfile: zftape-ctl.h,v $
 * $Revision: 1.15 $
 * $Date: 2000/07/20 11:17:16 $
 *
 *      This file contains the non-standard IOCTL related definitions
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/config.h>
#include <linux/ioctl.h>
#include <linux/mtio.h>
#include <linux/list.h>

#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"

typedef struct zftape_info {
	ftape_info_t *ftape;
	unsigned int qic_mode:1; /* use the vtbl */
	unsigned int old_ftape:1; /* prevents old ftaped tapes to be overwritten */
	unsigned int exvt_extension:1; /* force r/o */
	unsigned int vtbl_broken:1;    /* force r/o */
	unsigned int offline:1;
	unsigned int going_offline:1;
	unsigned int write_protected:1;
	unsigned int header_read:1;
	unsigned int vtbl_read:1;
	unsigned int header_changed:1;
	unsigned int volume_table_changed:1; /* for write_header_segments() */
	unsigned int bad_sector_map_changed:1;
	unsigned int just_before_eof:1;
	unsigned int qic113:1; /* conform to old specs. and old zftape */
	unsigned int force_lock:1;
	unsigned int label_changed:1;
	unsigned int last_write_failed:1;
	unsigned int need_flush:1;
	unsigned int unit;
	int deblock_segment;
	int resid;
	unsigned int file_access_mode;
	struct list_head vtbl;
	/*  We could also allocate these dynamically when extracting
	 *  the volume table sizeof(zft_volinfo) is about 32 or
	 *  something close to that
	 */
	zft_volinfo  tape_vtbl;
	zft_volinfo  eot_vtbl;
	zft_volinfo *cur_vtbl;
	unsigned int vtbl_size; /* size of the volume table segment */
	zft_position pos;
	__u8 *deblock_buf;
	__u8 *hseg_buf;
	__u8 *vtbl_buf;
	zft_status_enum io_state;
	unsigned int blk_sz;
	__s64 capacity;
	unsigned int written_segments;
	unsigned int used_memory;
	unsigned int peak_memory;
	int seg_pos;
	__s64 tape_pos;
	/*
	 * number of eof marks (entries in bad sector log) on tape.
	 */
	int nr_eof_marks;
	/*
	 * a copy of the failed sector log from the header segment.
	 */
	eof_mark_union *eof_map;
	/*
	 * a remainder of ftape-2.x
	 */
	unsigned int ftape_fmt_version;
	/*
	 * from zftape-read.c
	 */
	int buf_len_rd;
	__s64 remaining;
	int eod;
	unsigned int seg_sz;
	const zft_volinfo *volume; /* shared by _zft_read() and zft_write() */
	struct list_head hard_errors; /* the list of pending hard errors */
	int hard_error_cnt; /* how many of them */

#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes */
	unsigned int use_compression:1;
	void *cmpr_handle;
#endif

#ifndef CONFIG_FT_NO_TRACE_AT_ALL
	ft_trace_t *tracing;
	atomic_t *function_nest_level;
#endif
} zftape_info_t;

extern zftape_info_t *zftapes[4];

extern void zft_reset_position(zftape_info_t *zftape, zft_position *pos);
extern int  zft_check_write_access(zftape_info_t *zftape, zft_position *pos);
extern int  zft_def_idle_state(zftape_info_t *zftape);
extern int  zft_dirty(zftape_info_t *zftape);

/*  hooks for the VFS interface 
 */
extern int  _zft_open(unsigned int dev_minor, unsigned int access_mode);
extern int  _zft_close(zftape_info_t *zftape, unsigned int dev_minor);
extern int  _zft_ioctl(zftape_info_t *zftape,
		       unsigned int command, void *arg);
#endif
