/*
 *      Copyright (C) 1997-1998 Claus-Justus Heine

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
 * $RCSfile: zftape_syms.c,v $
 * $Revision: 1.12 $
 * $Date: 2000/07/06 14:42:21 $
 *
 *      This file contains the the symbols that the zftape frontend to 
 *      the ftape floppy tape driver exports 
 */		 

#include <linux/config.h>
#include <linux/module.h>
#define __NO_VERSION__

#include <linux/zftape.h>

#include "zftape-init.h"
#include "zftape-read.h"
#include "zftape-buffers.h"
#include "zftape-ctl.h"

#ifdef CONFIG_ZFT_COMPRESSOR_MODULE
/* zftape-init.c */
EXPORT_SYMBOL_GPL(zft_cmpr_register);
EXPORT_SYMBOL_GPL(zft_cmpr_unregister);
#endif
/* zftape-read.c */
EXPORT_SYMBOL_GPL(zft_fetch_segment);
/* zftape-buffers.c */
EXPORT_SYMBOL_GPL(zft_vmalloc_once);
EXPORT_SYMBOL_GPL(zft_vmalloc_always);
