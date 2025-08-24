/*
 * Copyright (C) 1997-1998 Jochen Hoenicke.

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
 * $RCSfile: trakker.h,v $
 * $Revision: 1.8 $
 * $Date: 1999/04/25 21:12:19 $
 *
 *      This file contains the low level interface between the
 *      parallel port and the Colorado Trakker.  
 *
 */

#ifndef _TRAKKER_H_
#define _TRAKKER_H_

/* #include <linux/config.h> - not needed in modern kernels */
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "../lowlevel/ftape-tracing.h"

#include "fdc-parport.h"

#define FT_TRAKKER_MAGIC 0x524b5254 /* "TRKR" */

struct trakker_struct {
	/* the following fields are used by the bpck-fdc driver as well */
	int magic;                     /* set to "TRKR" */
	ft_parinfo_t parinfo;          /* parport data */
	fdc_info_t *fdc;               /* back pointer */
	unsigned int used_buffers:4;   /* 128k or RAM */
	__u8 *buffer;                  /* pointer to the deblock buffer */
	unsigned int locked:1;         /* set when deblock buffer is in use */

	/* begin of trakker specific data */
	unsigned char dtr, ctr;

	unsigned char mode;
	unsigned char chksum;

	void (*out) (fdc_info_t *fdc, unsigned char value, unsigned short reg);
	unsigned char (*in) (fdc_info_t *fdc, unsigned short reg);
	void (*read_mem) (fdc_info_t *fdc, __u8* buffer, int count);
};

/* EPP doesn't work, it is probably not possible at all */
#define REG18_EPP     0x01
#define REG18_PS2     0x02
#define REG18_HSHAKE  0x04
#define REG18_WRITE   0x08
#define REG18_READ    0x10
#define REG18_RAM     0x20
#define REG18_DMA     0x40
#define REG18_DMAREAD 0x80

#define WRITE_BLOCK_SIZE PAGE_SIZE
#define READ_BLOCK_SIZE  PAGE_SIZE

#define REG1E_OPEN  0x43
#define REG1E_ACK   0x10
#define REG1E_CLOSE 0x47

#define CONTROL_REG_MODE PARPORT_CONTROL_SELECT
#define CONTROL_STROKE   PARPORT_CONTROL_AUTOFD


#endif
