#ifndef _BPCK_FDC_H_
#define _BPCK_FDC_H_

/*
 * Copyright (C) 1998 Claus-Justus Heine

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
 * $RCSfile: bpck-fdc.h,v $
 * $Revision: 1.8 $
 * $Date: 1998/12/18 22:38:19 $
 *
 *      Definitions and declarations for the bpck parallel port floppy
 *      tape drive
 *
 */

#include "../lowlevel/fdc-io.h"

#undef fdc

#include "fdc-parport.h"

/* type definitions
 */

/* The parallel port protocol to use. EPP-16 and EPP-32 seem not to
 * be usable for writing with the bpck fdc
 */
typedef enum {
	ft_bpck_none = -1,
	ft_bpck_spp = 0,
	ft_bpck_ps2,
	ft_bpck_epp8,
	ft_bpck_epp16,
	ft_bpck_epp32,
} ft_bpck_proto_t;

/* return values of some data transfer functions.
 */
typedef enum {
	ft_bpck_crc_error = -1,
	ft_bpck_no_error  =  0,
	ft_bpck_ecc_error =  1,
} ft_bpck_error_t;

#define FT_BPCK_MAGIC 0x4b435042 /* BPCK */

typedef struct bpck_fdc {

	/* the following fields are used by the trakker fdc driver as well */
	int magic;                     /* set to "BPCK" */
	ft_parinfo_t parinfo;          /* parport data */
	fdc_info_t *fdc;               /* back pointer */
	unsigned int used_buffers:4;   /* 128k or RAM */
	__u8 *buffer;                  /* pointer to the deblock buffer */
	unsigned int locked:1;         /* set when deblock buffer is in use */

	/* begin of bpck fdc specific data */
	ft_bpck_proto_t best_proto;    /* what we aim at */
	ft_bpck_proto_t used_proto;    /* what we got */
	ft_bpck_proto_t connect_proto; /* which proto we connected with */
	__u8 proto_bits;
	__u8 ctr;
	__u8 saved_dtr;
	__u8 saved_ctr;
	__u16 crc;                     /* CRC shift register */
	unsigned int initialized:1;    /* after initialization has completed */
	unsigned int connected:1;      /* set when connected */
	unsigned int polling:1;        /* set during interrutp polling */
	unsigned int failure:1;        /* general hardware failure */
} bpck_fdc_t;

/* low level input/output All the ft_... macros are defined in
 * fdc-parport.h and mask the presence of the parport_... macros ...
 */

#define w0(byte)                ft_w_dtr(bpck->parinfo, byte)
#define r0()                    ft_r_dtr(bpck->parinfo)
#define w1(byte)                ft_w_str(bpck->parinfo, byte)
#define r1()                    ft_r_str(bpck->parinfo)
#define w3(byte)                ft_epp_w_adr(bpck->parinfo, byte)
#define w4(byte)                ft_epp_w_dtr(bpck->parinfo, byte)
#define r4()                    ft_epp_r_dtr(bpck->parinfo)
#define w4w(w)     		ft_epp_w_dtr_w(bpck->parinfo, w)
#define r4w()         		ft_epp_r_dtr_w(bpck->parinfo)
#define w4l(l)     		ft_epp_w_dtr_l(bpck->parinfo, l)
#define r4l()         		ft_epp_r_dtr_l(bpck->parinfo)

#define PC			bpck->ctr
#define r2()			(bpck->ctr=ft_r_ctr(bpck->parinfo))
#define w2(b)  		        {ft_w_ctr(bpck->parinfo, b); bpck->ctr = b;}
#define t2(pat)   		w2(bpck->ctr ^ (pat))
#define e2()			w2(bpck->ctr & ~PARPORT_CONTROL_STROBE) 
#define o2()			w2(bpck->ctr | PARPORT_CONTROL_STROBE)

#define j44(l,h)     (((l>>3)&0x7)|((l>>4)&0x8)|((h<<1)&0x70)|(h&0x80))

#endif /*  _BPCK_FDC_H_ */
