/*
 *      Copyright (C) 1995-1998 Claus-Justus Heine.

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
 * $RCSfile: zftape-buffers.c,v $
 * $Revision: 1.14 $
 * $Date: 2000/07/12 17:20:28 $
 *
 *      This file contains the dynamic buffer allocation routines 
 *      of zftape
 */

#include <linux/errno.h>
#include <linux/malloc.h>
#include <asm/segment.h>

#include <linux/zftape.h>

#define ZFTAPE_TRACING
#include "zftape-init.h"
#include "zftape-eof.h"
#include "zftape-ctl.h"
#include "zftape-write.h"
#include "zftape-read.h"
#include "zftape-rw.h"
#include "zftape-vtbl.h"

/*  global variables
 */

/*  local varibales
 */

int zft_vcalloc_once(zftape_info_t *zftape,
		     void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(zft_vmalloc_once(zftape, new, size),);
	memset(*(void **)new, '\0', size);
	TRACE_EXIT 0;
}
int zft_vmalloc_once(zftape_info_t *zftape,
		     void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);
	if (*(void **)new) {
		TRACE_EXIT 0;
	}
	if (ftape_vmalloc(FTAPE_SEL(zftape->unit), new, size) < 0) {
		TRACE(ft_t_warn, "Geeh! No more memory :-(");
		TRACE_EXIT -ENOMEM;
	}
	TRACE_EXIT 0;
}
int zft_vcalloc_always(zftape_info_t *zftape,
		       void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(zft_vmalloc_always(zftape, new, size),);
	memset(*(void **)new, '\0', size);
	TRACE_EXIT 0;
}
int zft_vmalloc_always(zftape_info_t *zftape,
		       void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);

	ftape_vfree(FTAPE_SEL(zftape->unit), new, size);
	TRACE_EXIT ftape_vmalloc(FTAPE_SEL(zftape->unit), new, size);
}

/* there are some more buffers that are allocated on demand.
 * cleanup_module() calles this function to be sure to have released
 * them 
 */
void zft_uninit_mem(zftape_info_t *zftape)
{
	TRACE_FUN(ft_t_flow);

	ftape_vfree(FTAPE_SEL(zftape->unit), &zftape->hseg_buf, FT_SEGMENT_SIZE);
	zft_release_deblock_buffer(zftape, &zftape->deblock_buf);
	ftape_vfree(FTAPE_SEL(zftape->unit), &zftape->vtbl_buf, FT_SEGMENT_SIZE);

	zft_free_vtbl(zftape);
# if defined(CONFIG_ZFT_COMPRESSOR) || defined(CONFIG_ZFT_COMPRESSOR_MODULE)
	/* read-only compatibility with pre-ftape-4.x compressed volumes
	 */
	if (zft_cmpr_lock(zftape, 0 /* don't load */) == 0) {
		(*zft_cmpr_ops->cleanup)(zftape->cmpr_handle);
		/* unlock it again */
		(*zft_cmpr_ops->reset)(zftape->cmpr_handle); 
	}
# endif
	zft_zap_write_buffers(zftape);
	TRACE_EXIT;
}
