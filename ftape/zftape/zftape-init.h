#ifndef _ZFTAPE_INIT_H
#define _ZFTAPE_INIT_H

/*
 * Copyright (C) 1996-1998 Claus Heine.

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
 * $RCSfile: zftape-init.h,v $
 * $Revision: 1.11 $
 * $Date: 2000/07/06 14:42:21 $
 *
 * This file contains definitions and macro for the vfs 
 * interface defined by zftape
 *
 */

#include <linux/ftape-header-segment.h>

#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-bsm.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-buffer.h"
#include "../lowlevel/ftape-format.h"

#if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
/* Only kept for compatibility with old zftape versions.  */

#include "zftape-rw.h"
#include "zftape-vtbl.h"

struct zftape_info; /* forward */

struct zft_cmpr_ops {
	int (*read)(void *handle,
		    int *read_cnt, __u8 *dst_buf, const int req_len,
		    const __u8 *src_buf, const int seg_sz,
		    const zft_position *pos, const zft_volinfo *volume);
	int (*seek)(void *handle,
		    unsigned int new_block_pos,
		    zft_position *pos, const zft_volinfo *volume,
		    __u8 **buffer);
	void* (*lock)   (struct zftape_info *zftape);
	void  (*reset)  (void *handle);
	void  (*cleanup)(void *handle);
};

extern struct zft_cmpr_ops *zft_cmpr_ops;

/* zftape-init.c defined global functions.
 */
extern int                  zft_cmpr_register(struct zft_cmpr_ops *new_ops);
extern struct zft_cmpr_ops *zft_cmpr_unregister(void);
extern int                  zft_cmpr_lock(struct zftape_info *zftape, 
					  int try_to_load);
#endif /* CONFIG_ZFT_COMPRESSOR[_MODULE] */

#ifdef MODULE

extern int  init_module(void);
extern void cleanup_module(void);

#endif

#endif


