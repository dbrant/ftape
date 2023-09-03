#ifndef _ZFTAPE_BUFFERS_H
#define _ZFTAPE_BUFFERS_H

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
 * $RCSfile: zftape-buffers.h,v $
 * $Revision: 1.6 $
 * $Date: 1998/12/18 22:39:04 $
 *
 *   memory allocation routines.
 *
 */

struct zftape_info; /* forward */

/* we do not allocate all of the really large buffer memory before
 * someone tries to open the device. ftape_open() may fail with
 * -ENOMEM, but that's better having 200k of vmalloced memory which
 * cannot be swapped out.
 */

extern void  zft_memory_stats(struct zftape_info *zftape);
extern int   zft_vmalloc_once(struct zftape_info *zftape, void *new, size_t size);
extern int   zft_vcalloc_once(struct zftape_info *zftape, void *new, size_t size);
extern int   zft_vmalloc_always(struct zftape_info *zftape, void *new, size_t size);
extern int   zft_vcalloc_always(struct zftape_info *zftape, void *new, size_t size);

/* called by cleanup_module() 
 */
extern void zft_uninit_mem(struct zftape_info *zftape);

#endif







