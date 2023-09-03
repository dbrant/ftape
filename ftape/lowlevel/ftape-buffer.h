#ifndef _FTAPE_BUFFER_H
#define _FTAPE_BUFFER_H

/*
 *      Copyright (C) 1997-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-buffer.h,v $
 * $Revision: 1.11 $
 * $Date: 1998/12/18 22:02:16 $
 *
 *  This file contains the allocator/dealloctor for ftape's dynamic dma
 *  buffer.
 */

#include "../lowlevel/fdc-io.h"

extern unsigned int ft_v_used_memory[5];
extern unsigned int ft_v_peak_memory[5];
extern unsigned int ft_k_used_memory[5];
extern unsigned int ft_k_peak_memory[5];

extern int ftape_vmalloc(int sel, void *new, size_t size);
extern void ftape_vfree(int sel, void *old, size_t size);
extern void *ftape_kmalloc(int sel, size_t size, int retry);
extern void ftape_kfree(int sel, void *old, size_t size);
extern int  fdc_set_nr_buffers(fdc_info_t *info, int cnt);
extern void *fdc_get_deblock_buffer(fdc_info_t *fdc);
extern int   fdc_put_deblock_buffer(fdc_info_t *fdc, __u8 **buffer);
extern void fdc_zap_buffers(fdc_info_t *fdc);

#endif
