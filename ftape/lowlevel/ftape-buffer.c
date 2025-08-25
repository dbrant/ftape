/*
 *      Copyright (C) 1997, 1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-buffer.c,v $
 * $Revision: 1.18 $
 * $Date: 1999/09/11 14:59:35 $
 *
 *  This file contains the allocator/dealloctor for ftape's dynamic dma
 *  buffer.
 */

#include <asm/segment.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <linux/ftape.h>

#include <linux/vmalloc.h>

#define SEL_TRACING
#include "ftape-tracing.h"

#include "fdc-io.h"
#include "ftape-rw.h"
#include "ftape-read.h"
#include "ftape-ctl.h"
#include "ftape-buffer.h"

/*
 *  first some standard allocation routines to keep track of
 *  memory consumption.
 */

/*
 *  memory stats for vmalloc
 */

unsigned int ft_v_used_memory[5];
unsigned int ft_v_peak_memory[5];


int ftape_vmalloc(int sel, void *new, size_t size)
{
	TRACE_FUN(ft_t_flow);

	if (*(void **)new != NULL || size == 0) {
		TRACE_EXIT 0;
	}
	if ((*(void **)new = vmalloc(size)) == NULL) {
		TRACE(ft_t_warn, "Geeh! No more memory :-(");
		TRACE_EXIT -ENOMEM;
	}
	ft_v_used_memory[sel] += size;
	if (ft_v_peak_memory[sel] < ft_v_used_memory[sel]) {
		ft_v_peak_memory[sel] = ft_v_used_memory[sel];
	}
	TRACE_ABORT(0, ft_t_noise,
		    "allocated buffer @ %p, %ld bytes", *(void **)new, size);
}
void ftape_vfree(int sel, void *old, size_t size)
{
	TRACE_FUN(ft_t_flow);

	if (*(void **)old) {
		vfree(*(void **)old);
		ft_v_used_memory[sel] -= size;
		TRACE(ft_t_noise, "released buffer @ %p, %ld bytes",
		      *(void **)old, size);
		*(void **)old = NULL;
	}
	TRACE_EXIT;
}

/*
 *  memory stats for kmalloc
 */

unsigned int ft_k_used_memory[5];
unsigned int ft_k_peak_memory[5];

void *ftape_kmalloc(int sel, size_t size, int retry)
{
	void *new;
	TRACE_FUN(ft_t_flow);

	while ((new = kmalloc(size, GFP_KERNEL)) == NULL && retry) {
		set_current_state(TASK_INTERRUPTIBLE);
		(void)schedule_timeout(HZ/10);
	}
	if (new == NULL) {
		TRACE_EXIT NULL;
	}
	memset(new, 0, size);
	ft_k_used_memory[sel] += size;
	if (ft_k_peak_memory[sel] < ft_k_used_memory[sel]) {
		ft_k_peak_memory[sel] = ft_k_used_memory[sel];
	}
	TRACE_ABORT(new, ft_t_noise,
		    "allocated buffer @ %p, %ld bytes", new, size);
}
void ftape_kfree(int sel, void *old, size_t size)
{
	TRACE_FUN(ft_t_flow);

	if (*(void **)old) {
		kfree(*(void **)old);
		ft_k_used_memory[sel] -= size;
		TRACE(ft_t_noise, "released buffer @ %p, %ld bytes",
		      *(void **)old, size);
		*(void **)old = NULL;
	}
	TRACE_EXIT;
}

#define FDC_TRACING
#include "ftape-real-tracing.h"

static int add_one_buffer(fdc_info_t *fdc)
{
	buffer_struct *new;
	TRACE_FUN(ft_t_flow);
	
	if (fdc->nr_buffers >= FT_MAX_NR_BUFFERS) {
		TRACE_EXIT -EINVAL;
	}
	new = ftape_kmalloc(fdc->unit, sizeof(buffer_struct), 1);
	if (new == NULL) {
		TRACE_EXIT -EAGAIN;
	}
	if (fdc->ops->alloc) {
		TRACE_CATCH(fdc->ops->alloc(fdc, new),
			    ftape_kfree(fdc->unit,
					&new, sizeof(buffer_struct)));
	}
	fdc->nr_buffers ++;
	if (fdc->buffers == NULL) {
		HEAD = TAIL = fdc->buffers = new->next = new;
	} else {
		new->next = fdc->buffers->next;
		fdc->buffers->next = new;
	}
	TRACE(ft_t_info, "buffer nr #%d @ %p, dma area @ %p",
	      fdc->nr_buffers, new, new->virtual);
	TRACE_EXIT 0;
}

static void del_one_buffer(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_flow);

	if (fdc->nr_buffers > 0 && fdc->buffers != NULL) {
		buffer_struct *old = fdc->buffers->next;

		TRACE(ft_t_info, "releasing buffer nr #%d @ %p, dma area @ %p",
		      fdc->nr_buffers, old, old->virtual);
		(fdc->nr_buffers) --;
		if (old != old->next) {
			fdc->buffers->next = old->next;
		} else {
			HEAD = TAIL = fdc->buffers = NULL;
			if (fdc->nr_buffers != 0) {
				TRACE(ft_t_bug, "nr_buffers should be zero");
			}
		}
		if (fdc->ops->free) {
			fdc->ops->free(fdc, old);
		}
		ftape_kfree(fdc->unit, &old, sizeof(buffer_struct));
	}
	TRACE_EXIT;
}

int fdc_set_nr_buffers(fdc_info_t *fdc, int cnt)
{
	int delta;
	TRACE_FUN(ft_t_flow);

	if (!fdc || !fdc->ops || !fdc->ops->alloc || !fdc->ops->free) {
		TRACE_EXIT -ENXIO;
	}
	delta = cnt - fdc->nr_buffers;
	if (fdc->nr_buffers == 0 && cnt) {
		/*  First step allocation: the low level info module should
		 *  use this to allocate additional memory.
		 */
		TRACE_CATCH(fdc->ops->alloc(fdc, NULL),);
	}
	if (delta > 0) {
		while (delta--) {
			TRACE_CATCH(add_one_buffer(fdc),);
		}
	} else if (delta < 0) {
		while (delta++) {
			del_one_buffer(fdc);
		}
	}
	if (fdc->nr_buffers == 0) {
		fdc->ops->free(fdc, NULL);
	}
	fdc_zap_buffers(fdc);
	TRACE_EXIT 0;
}

void *fdc_get_deblock_buffer(fdc_info_t *fdc)
{
	if (fdc->ops && fdc->ops->get_deblock_buffer) {
		return fdc->ops->get_deblock_buffer(fdc);
	}
	return NULL;
}

int fdc_put_deblock_buffer(fdc_info_t *fdc, __u8 **buffer)
{
	if (fdc->ops && fdc->ops->put_deblock_buffer) {
		return fdc->ops->put_deblock_buffer(fdc, buffer);
	}
	return -EINVAL;
}

void fdc_zap_buffers(fdc_info_t *fdc)
{
	buffer_struct *pos = fdc->buffers;

	if (pos == NULL) {
		return;
	}
	do {
		pos->status = waiting;
		pos->bytes = 0;
		pos->skip  = 0;
		pos->retry = 0;
		pos = pos->next;
	} while (pos != fdc->buffers);
}

