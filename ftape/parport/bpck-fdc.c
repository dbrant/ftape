/*
 *      Copyright (C) 1998 Claus-Justus Heine.

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
 *      This file contains the low-level interface code between the
 *      parallel port and the floppy controller of the Micro Solution's
 *      Bpck parallel port floppy tape drive.
 *
 */


#include <linux/module.h>
#include <linux/version.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/mman.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/uaccess.h>
#include <asm/dma.h>

#include <linux/ftape.h>

#include "../lowlevel/ftape-tracing.h"

#include "../lowlevel/fdc-io.h"
#include "../lowlevel/fdc-isr.h"
#include "../lowlevel/ftape-init.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-buffer.h"

#include "bpck-fdc.h"
#include "bpck-fdc-regset.h"
#include "bpck-fdc-crctab.h"

/*      Global vars.
 */
char bpck_src[]  = "$RCSfile: bpck-fdc.c,v $";
char bpck_rev[]  = "$Revision: 1.31 $";
char bpck_dat[]  = "$Date: 2000/06/30 10:53:42 $";

int ecr_bits = 0x01; /* what to write to econtrol (changed to int for module_param) */

int parport_proto = ft_bpck_none; /* parport protocol (changed to int for module_param) */

/* backup register access macros. */

/* write a single byte to a register ... */
#define WR(reg, val) bpck_fdc_write_reg(bpck, reg, val)
/* write a buffer of bytes to a register ... */
#define WRB(reg, buf, sz) bpck_fdc_write_reg_vals(bpck, reg, buf, sz)
/* write some bytes to a register ... */
#define WRM(reg, ...)					\
{								\
	const __u8 _val[] = { __VA_ARGS__ };				\
	bpck_fdc_write_reg_vals(bpck, reg, _val, sizeof(_val));	\
}
/* Write a three byte value to one of the address registers.  */
#define WR3(reg, addr)						\
{								\
	__u8 addr_bytes[4];					\
	PUT4(addr_bytes, 0, addr);				\
	bpck_fdc_write_reg_vals(bpck, reg, addr_bytes, 3);	\
}
/* read a single byte from a register ... */
#define RR(reg) bpck_fdc_read_reg(bpck, reg)
/* read a 16 byte value from a register ... */
#define RR2(reg)						\
( {								\
	__u8 val_bytes[2];					\
	__u16 value;				       		\
	bpck_fdc_read_reg_vals(bpck, reg, val_bytes, 2);	\
	value = GET2(val_bytes, 0);				\
} )



int bpck_fdc_grab(fdc_info_t *fdc);
int bpck_fdc_register(void);
int bpck_fdc_unregister(void);


/* read a byte on an uni-directional (4 bit) port. "toggle" is the value
 * that distinguishes the two nibbles.
 */
static inline __u8 denibble(bpck_fdc_t *bpck)
{
	__u8 l, h;

	t2(PARPORT_CONTROL_INIT);
	l = r1();
	t2(PARPORT_CONTROL_INIT);
	h = r1();
	return j44(l, h); /* and join them ... */
}

/* Connect to the bpck tape drive. The bpck protocol disables
 * interrupts as long as we are connected.
 *
 * However, interrupt should have been disabled before calling this
 * function, for safety.
 *
 * May return -EINVAL when the bpck indicates it is programmed for the
 * wrong parport protocol.
 */
static int bpck_fdc_connect(bpck_fdc_t *bpck)
{
	__u8 res;
	TRACE_FUN(ft_t_flow);

	if (bpck->connected) {
		TRACE(ft_t_bug, "BUG: already connected");
		TRACE_EXIT -EIO;
	}
	
	w1(res = r1());
	TRACE(ft_t_any, "Status: 0x%02x", res);

	bpck->saved_ctr = r2() & ~PARPORT_CONTROL_INTEN;
	w2(PARPORT_CONTROL_INIT);
	bpck->saved_dtr = r0();
	w0(0x00);
	t2(PARPORT_CONTROL_SELECT);
	t2(PARPORT_CONTROL_SELECT);
	t2(PARPORT_CONTROL_SELECT);

	bpck->connected = 1;

	t2(PARPORT_CONTROL_AUTOFD);
	res = r1() & 0xf8;
	t2(PARPORT_CONTROL_AUTOFD);

	TRACE(ft_t_flow, "Connect response: 0x%02x", res);

	switch(res) {
	case PARPORT_STATUS_ACK | PARPORT_STATUS_BUSY:
		w2(0x00);
		if (bpck->used_proto < ft_bpck_epp8) {
			bpck->connect_proto = ft_bpck_epp8;
			TRACE(ft_t_err,
			      "Connect response indicates EPP mode, "
			      "but we wanted non-EPP mode");
			TRACE_EXIT -EINVAL; /* we have the hardware,
					     * but misconfigured
					     */
		}
		break;
	case PARPORT_STATUS_ACK:
		bpck->connect_proto = ft_bpck_spp;
		w2(PARPORT_CONTROL_INIT);
		if (bpck->used_proto >= ft_bpck_epp8) {
			bpck->connect_proto = ft_bpck_spp;
			TRACE(ft_t_err,
			      "Connect response indicates non-EPP mode, "
			      "but we wanted EPP mode");
			TRACE_EXIT -EINVAL; /* we have the hardware,
					     * but misconfigured
					     */
		}
		break;
	default:		
		w2(0x00);
		bpck->connected = 0;
		bpck->failure   = 1;
		TRACE(ft_t_noise, "Unknown connect response: 0x%02x", res);
		TRACE_EXIT -ENXIO; /* probably the parport drive isn't
				    * plugged in
				    */
	}

	res = r1() & 0xf8;
	if (res & PARPORT_STATUS_BUSY) { /* make some noise ... */
		TRACE(ft_t_flow, "Connect while IRQ is pending");
	}

	/* needed for disconnecting */
	bpck->connect_proto = bpck->used_proto;

	TRACE_EXIT 0;
}

/* enable irqs only when irq_on != 0
 */
static void bpck_fdc_disconnect(bpck_fdc_t *bpck, int irq_on)
{
	TRACE_FUN(ft_t_any);

	if (!bpck->connected) {
		TRACE(ft_t_bug, "BUG: not connected");
		TRACE_EXIT;
	}

	w0(0); 
	
	if (bpck->connect_proto >= ft_bpck_epp8) {
		w2(PARPORT_CONTROL_STROBE | PARPORT_CONTROL_SELECT);
		w2(0);
	} else {
		w2(PARPORT_CONTROL_AUTOFD);
	}
	w2(PARPORT_CONTROL_INIT | PARPORT_CONTROL_SELECT);
	
	w2(irq_on ? (bpck->saved_ctr|PARPORT_CONTROL_INTEN) : bpck->saved_ctr);
	w0(bpck->saved_dtr);
	bpck->connected = 0;
	TRACE(ft_t_data_flow, "ctr: 0x%02x", bpck->ctr);

	TRACE_EXIT;
} 

/* address a register for either read or write ... */
static inline int bpck_fdc_select_reg(bpck_fdc_t *bpck, int reg)
{
	TRACE_FUN(ft_t_flow);

	if (!bpck->connected) {
		if (bpck_fdc_connect(bpck) < 0) {
			TRACE(ft_t_noise, "bpck_fdc_connect() failed");
			TRACE_EXIT -ENXIO;
		}
	}
	if (bpck->used_proto < ft_bpck_epp8) {
		w0(reg + 1); /* probably ANY value != reg would work ... */
		w0(reg);
		t2(PARPORT_CONTROL_AUTOFD);
	} else {
		w3(reg);
	}
	TRACE_EXIT 0;
}

/* Only read a single byte, without addressing the register ... */
static inline int bpck_fdc_read_reg_value(bpck_fdc_t *bpck)
{
	__u8 val = 0xff;
	TRACE_FUN(ft_t_flow);

	switch (bpck->used_proto) {
	case ft_bpck_spp:
		e2(); /* erase bit zero */
		val = denibble(bpck);
		break;
	case ft_bpck_ps2:
		e2();
		t2(PARPORT_CONTROL_DIRECTION | PARPORT_CONTROL_INIT);
		val = r0();
		t2(PARPORT_CONTROL_DIRECTION | PARPORT_CONTROL_STROBE);
		break;
	case ft_bpck_epp8:
	case ft_bpck_epp16:
	case ft_bpck_epp32:
		t2(PARPORT_CONTROL_DIRECTION);
		val = r4();
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	default:
		break; /* make GCC happy */
	}
	TRACE(ft_t_data_flow, "-> 0x%02x", val & 0xff);
	TRACE_EXIT val;
}	

/* read a single value from register r ... */
static int bpck_fdc_read_reg(bpck_fdc_t *bpck, int r)
{
	return bpck_fdc_select_reg(bpck, r) < 0
		? -1 : bpck_fdc_read_reg_value(bpck);
}

/* read "count" many bytes from register "r" and store them in "val" ... */
static void bpck_fdc_read_reg_vals(bpck_fdc_t *bpck,
				   int r, __u8 *val, int count)
{
	int oc = count;
	__u8 *ov = val;
	TRACE_FUN(ft_t_flow);

	if (bpck_fdc_select_reg(bpck, r) < 0) {
		memset(val, -1, count); /* bail out */
		TRACE_EXIT;
	}
	switch (bpck->used_proto) {
	case ft_bpck_spp:
		while (count --) {
			e2(); /* erase bit zero */
			*val++ = denibble(bpck);
		}
		break;
	case ft_bpck_ps2:
		e2();
		while (count --) {
			t2(PARPORT_CONTROL_DIRECTION | PARPORT_CONTROL_INIT);
			*val++ = r0();
			t2(PARPORT_CONTROL_DIRECTION | PARPORT_CONTROL_STROBE);
			if (count) {
				e2();
			}
		}
		break;
	case ft_bpck_epp8:
	case ft_bpck_epp16:
	case ft_bpck_epp32:
		while (count --) {
			t2(PARPORT_CONTROL_DIRECTION);
			*val++ = r4();
			t2(PARPORT_CONTROL_DIRECTION);
		}
		break;
	default:
		break;
	}
	/* this should be optimized away by the compiler ... */
	switch (oc) {
	case 1:
		TRACE(ft_t_data_flow, "[%02x] -> 0x%02x", r, *ov & 0xff);
		break;
	case 2:
		TRACE(ft_t_data_flow, "[%02x] -> 0x%02x 0x%02x",
		      r, *ov & 0xff, ov[1] & 0xff);
		break;
	case 3:
		TRACE(ft_t_data_flow, "[%02x] -> 0x%02x 0x%02x 0x%02x",
		      r, *ov & 0xff, ov[1] & 0xff, ov[2] & 0xff);
		break;
	case 4:
		TRACE(ft_t_data_flow, "[%02x] -> 0x%02x 0x%02x 0x%02x 0x%02x",
		      r, *ov & 0xff, ov[1] & 0xff, ov[2] & 0xff, ov[3] & 0xff);
		break;
	default:
		break;
	}
	TRACE_EXIT;
}

/* write a single value without addressing the register ... */
static inline void bpck_fdc_write_reg_value(bpck_fdc_t *bpck, int val)
{
	TRACE_FUN(ft_t_flow);

	switch (bpck->used_proto) {
	case ft_bpck_spp:
	case ft_bpck_ps2:
		o2(); /* set bit zero */
		w0(val);
		t2(PARPORT_CONTROL_INIT);
		break;
	case ft_bpck_epp8:
	case ft_bpck_epp16:
	case ft_bpck_epp32:
		o2(); w4(val); r2(); r2(); e2();
		break;
	case ft_bpck_none: /* make GCC happy */
		break;
	}
	TRACE(ft_t_data_flow, "<- 0x%02x", val & 0xff);
	TRACE_EXIT;
}

/* write "val" to register "r" ... */
static int bpck_fdc_write_reg(bpck_fdc_t *bpck, int r, int val)
{
	/* no chance to return connect failure to caller */
	if (bpck_fdc_select_reg(bpck, r) < 0) {
		return -ENXIO;
	}
	bpck_fdc_write_reg_value(bpck, val);
	return 0;
}

/* write "count" many bytes to register "r" ... */
static int bpck_fdc_write_reg_vals(bpck_fdc_t *bpck,
				    int r, const __u8 *val, int count)
{
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(bpck_fdc_select_reg(bpck, r),);
	/* should be optimzed away by compiler ... */
	switch (count) {
	case 1:
		TRACE(ft_t_data_flow, "[%02x] <- 0x%02x", r, val[0] & 0xff);
		break;
	case 2:
		TRACE(ft_t_data_flow, "[%02x] <- 0x%02x 0x%02x",
		      r, val[0] & 0xff, val[1] & 0xff);
		break;
	case 3:
		TRACE(ft_t_data_flow, "[%02x] <- 0x%02x 0x%02x 0x%02x",
		      r, val[0] & 0xff, val[1] & 0xff, val[2] & 0xff);
		break;
	case 4:
		TRACE(ft_t_data_flow, "[%02x] <- 0x%02x 0x%02x 0x%02x 0x%02x",
		      r,
		      val[0] & 0xff, val[1] & 0xff,
		      val[2] & 0xff, val[3] & 0xff);
		break;
	default:
		break;
	}
	if (bpck->used_proto < ft_bpck_epp8) {
		while (count --) {
			o2(); /* set bit zero */
			w0(*val++);
			t2(PARPORT_CONTROL_INIT);
		}
	} else {
		while (count --) {
			o2(); w4(*val++); r2(); r2(); e2();
		}
	}
	TRACE_EXIT 0;
}


/* Write data to the bpck memory. The memory is accessed by addressing
 * register a0 and writing a consecutive stream of bytes. The memory
 * size is 128k, and the memory wraps around, i.e. writing beyond the
 * top bound of the memory will again result in writing to the bottom
 * of the memory region.
 */
static int bpck_fdc_write_data(bpck_fdc_t *bpck,
				const __u8 *data, int size)
{
	TRACE_FUN(ft_t_flow);

	if (bpck->used_proto < ft_bpck_epp8) {
		__u8 old_dtr;
		/* set write flag */
		TRACE_CATCH(bpck_fdc_write_reg(bpck, FT_BPCK_REG_CTRL,
					       bpck->proto_bits |
					       FT_BPCK_CTRL_WRITE),);

		TRACE_CATCH(bpck_fdc_select_reg(bpck, FT_BPCK_MEMORY),);
		o2();
		w0(old_dtr = *data++);
		t2(PARPORT_CONTROL_INIT);
		size --;
		while (size --) {
			if (old_dtr == *data) {
				/* can't write the same value twice */
				t2(PARPORT_CONTROL_INIT);
				data ++;
			} else {
				w0(old_dtr = *data++);
			}
		}
		e2();
		/* clear write flag */
		TRACE_CATCH(WR(FT_BPCK_REG_CTRL, bpck->proto_bits),);
		e2();
	} else if (bpck->used_proto <= ft_bpck_epp32) {
		TRACE_CATCH(bpck_fdc_select_reg(bpck, FT_BPCK_MEMORY),);
		o2();
#ifndef FT_BPCK_USE_WIDE_EPP_WRITES
		/* unluckily, EPP-16 and EPP-32 writes seem to corrupt
		 * the CRC engine of the bpck tape drive though the data
		 * seems to be transferred correctly.
		 *
		 * PLAY SAFE: use EPP-8 for writes, and the best
		 * available protocol for reads.
		 */
		while (size --) {
			w4(*data++);
		}
#else
		switch (bpck->used_proto) {
		case ft_bpck_epp8:
			while (size --) {
				w4(*data++);
			}
			break;
		case ft_bpck_epp16:
			while (size) {
				w4w(*((__u16 *)data)++);
				size -= 2;
			}
			break;
		case ft_bpck_epp32:
			while (size) {
				w4l(*((__u32 *)data)++);
				size -= 4;
			}
			break;
		default:
			break;
		}
#endif
		r2();
		r2();
		e2();
	}
	TRACE_EXIT 0;
}

/* pipe the bpck's memory through its CRC engine.  
 */
static int bpck_fdc_check_data(bpck_fdc_t *bpck, int count)
{
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(bpck_fdc_select_reg(bpck, FT_BPCK_MEMORY),);
	switch (bpck->used_proto) {
	case ft_bpck_spp:
		e2();
		while (count --) {
			t2(PARPORT_CONTROL_INIT);
			t2(PARPORT_CONTROL_INIT);
		}
		o2();
		break;
	case ft_bpck_ps2:
		bpck->ctr |= 0x20;
		e2();
		while (count --) {
			t2(PARPORT_CONTROL_INIT);
		}
		t2(0x21);
		break;
	case ft_bpck_epp8:
		bpck->ctr |= 0x20;
		e2();
		while (count --) {
			__u8 dummy;
			dummy = r4();
		}
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	case ft_bpck_epp16:
		bpck->ctr |= 0x20;
		e2();
		while (count) {
			(void)r4w();
			count -= 2;
		}
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	case ft_bpck_epp32:
		bpck->ctr |= 0x20;
		e2();
		while (count) {
			(void)r4l();
			count -= 4;
		}
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	default:
		break;
	}
	TRACE_EXIT 0;
}

/* Read data from the bpck memory. The memory is accessed by
 * addressing register a0 and reading a consecutive stream of
 * bytes. The memory size is 128k, and the memory wraps around,
 * i.e. reading from beyond the top bound of the memory will again
 * result in reading from the bottom of the memory region. 
 */
static int bpck_fdc_read_data(bpck_fdc_t *bpck, __u8 *data, int count)
{
	if (bpck_fdc_select_reg(bpck, FT_BPCK_MEMORY) < 0) {
		memset(data, -1, count);
		return -ENXIO;
	}
	switch (bpck->used_proto) {
	case ft_bpck_spp:
		e2();
		while (count --) {
			*data++ = denibble(bpck);
		}
		o2();
		break;
	case ft_bpck_ps2:
		bpck->ctr |= 0x20;
		e2();
		while (count --) {
			t2(PARPORT_CONTROL_INIT);
			*data++ = r0();
		}
		t2(0x21);
		break;
	case ft_bpck_epp8:
		bpck->ctr |= 0x20;
		e2();
		while (count --) {
			*data++ = r4();
		}
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	case ft_bpck_epp16:
		bpck->ctr |= 0x20;
		e2();
		while (count) {
			*(__u16 *)data = r4w(); data += 2;
			count -= 2;
		}
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	case ft_bpck_epp32:
		bpck->ctr |= 0x20;
		e2();
		while (count) {
			*(__u32 *)data = r4l(); data += 4;
			count -= 4;
		}
		t2(PARPORT_CONTROL_DIRECTION);
		break;
	default:
		break;
	}
	return 0;
}

/*  initialize the proto_bits component which is written to the control
 *  register to control the transfer mode.
 *
 *  The control register also controls the direction of dma transfers,
 *  but those DMA bits are set only during DMA transfers. The
 *  following function determines only the bits responsible for the
 *  parport protocol that the bpck tape drive uses.
 */
static __u8 bpck_fdc_decode_proto(ft_bpck_proto_t proto)
{
	__u8 proto_bits;

	switch (proto) {
	case ft_bpck_spp:
		proto_bits = FT_BPCK_CTRL_SPP;
		break;
	case ft_bpck_ps2:
		proto_bits = FT_BPCK_CTRL_PS2;
		break;
	case ft_bpck_epp8:
	case ft_bpck_epp16:
	case ft_bpck_epp32:
		proto_bits = FT_BPCK_CTRL_EPP;
		break;
	default:
		proto_bits = 0;
		break;
	}
	/* we seem to need those bits, do we? */
	proto_bits |= FT_BPCK_CTRL_0x20;  /* ??? */
	proto_bits |= FT_BPCK_CTRL_0x04;  /* ??? */
	return proto_bits;
}	

/*  Force the bpck drive to use the specified protocol for the next
 *  connect. This seems to circumvent the EPP logic to write to
 *  register 0x04 (which controls the transfer mode) in SPP resp. PS2
 *  mode.
 *
 *  The protocol written here will last for the next connect ONLY.
 */
static void bpck_fdc_force_protocol(bpck_fdc_t *bpck,
				    ft_bpck_proto_t proto)
{       
	/*  Address the register in the usual non-EPP mode ... */
	w0(FT_BPCK_REG_CTRL + 1);
	w0(FT_BPCK_REG_CTRL);

	/*  this is a little bit of magic. the 0x09 and 0x08 ARE necessary.
	 */
	w2(PARPORT_CONTROL_STROBE | PARPORT_CONTROL_SELECT);
	t2(PARPORT_CONTROL_SELECT);

	/*  write to register 0x04 ... */
	w0(bpck->proto_bits = bpck_fdc_decode_proto(proto));

	/*  terminate ... */
	t2(PARPORT_CONTROL_AUTOFD);
	w2(0);
	
	bpck->used_proto = proto; /* store the new protocol ... */
}

/* Register 0x13 (i.e. FT_BPCK_REG_TEST) provides means to test the
 * protocol in use. While it seems to be possible to program the bpck
 * tape drive to produce the wildest byte sequences with register
 * 0x13, it is intended to be a counter, it is incremented each time a
 * value is read.
 *
 * This looks a little bit more complicated than
 * drivers/block/paride/bpck.c but this way we don't need a buffer to
 * test the protocol.
 *
 * We return -EIO on error.
 */
#define TEST_LEN  256
#define PRINT_T(...) if (TRACE_LEVEL >= ft_t_noise) printk( __VA_ARGS__ )

static int bpck_fdc_test_protocol(bpck_fdc_t *bpck)
{	
	int i, e = 0;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "[0x13] ->");
	switch (bpck->used_proto) {
	case ft_bpck_spp: {
		__u8 ov, nv; /* old value, new values ... */

		w2(0x00);
		WR(FT_BPCK_REG_TEST, 0x18);
		ov = RR(FT_BPCK_REG_TEST);
		TRACE(ft_t_noise, "CTRL: 0x%02x", bpck->ctr);
		PRINT_T(KERN_INFO "0x%02x ", ov);
		for(i=1; i < TEST_LEN; i++) {
			e2();
			nv = denibble(bpck);
			if (nv != ((ov + 1) & 0xff)) {
				e ++;
			}
			ov = nv;
			if ((i & 0xf) == 0) {
				PRINT_T("\n"KERN_INFO);
				schedule();
			}
			PRINT_T("0x%02x ", nv);
		}
		PRINT_T("\n");
		break;
	}
        case ft_bpck_ps2: {
		__u8 ov, nv; /* old value, new value */

		WR(FT_BPCK_REG_TEST, 0x18);
		ov = RR(FT_BPCK_REG_TEST);		
		PRINT_T(KERN_INFO "0x%02x ", ov);
                for(i = 1; i < TEST_LEN; i++) {
			e2();
			t2(PARPORT_CONTROL_DIRECTION | PARPORT_CONTROL_INIT);
			nv = r0();
			t2(PARPORT_CONTROL_DIRECTION | PARPORT_CONTROL_STROBE);
			if (nv != ((ov + 1) & 0xff)) {
				e ++;
			}
			ov = nv;
			if ((i & 0xf) == 0) {
				PRINT_T("\n"KERN_INFO);
				schedule();
			}
			PRINT_T("0x%02x ", nv);
		}
		PRINT_T("\n");
		break;
	}
	case ft_bpck_epp8:
	case ft_bpck_epp16:
	case ft_bpck_epp32:
		/* This seems to access the test register 0x13, but
		 * looks a little bit peculiar. A little bit like the
		 * "force_proto" thing, but not quite.
		 *
		 * It works, so what!
		 */
		w0(0x14); w0(0x13);
		t2(PARPORT_CONTROL_STROBE);
		t2(PARPORT_CONTROL_SELECT);
		t2(PARPORT_CONTROL_SELECT);
		w0(0);
		t2(PARPORT_CONTROL_AUTOFD);
		t2(PARPORT_CONTROL_AUTOFD);
		t2(PARPORT_CONTROL_STROBE);

		t2(PARPORT_CONTROL_DIRECTION); /* enable reading */

		switch (bpck->used_proto) {
		case ft_bpck_epp8: {
			__u8 ov = r4();
			__u8 nv;

			PRINT_T(KERN_INFO"0x%02x ", ov);
			for (i=1; i < TEST_LEN; i++) {
				nv = r4();
				if ((__u8)(nv - ov) != 0x01 &&
				    (__u8)(nv - ov) != 0x0f) {
					e ++;
				}
				ov = nv;
				if ((i & 0xf) == 0) {
					PRINT_T("\n"KERN_INFO);
					schedule();
				}
				PRINT_T("0x%02x ", nv);
			}
			PRINT_T("\n");
			break;
		}
		case ft_bpck_epp16: {
			__u16 ov = r4w();
			__u16 nv;

			PRINT_T(KERN_INFO "0x%02x 0x%02x ",
			       ov & 0xff, (ov >> 8) & 0xff);
			for (i=1; i < TEST_LEN/2; i++) {
				nv = r4w();
				if ((__u16)(nv - ov) != 0x0202 &&
				    (__u16)(nv - ov) != 0xfefe) {
					e ++;
				}
				ov = nv;
				if ((i & 0x7) == 0) {
					PRINT_T("\n"KERN_INFO);
					schedule();
				}
				PRINT_T("0x%02x 0x%02x ",
				       nv & 0xff, (nv >> 8) & 0xff);
			}
			PRINT_T("\n");
			break;
		}
		case ft_bpck_epp32: {
			__u32 ov = r4l();
			__u32 nv;

			PRINT_T(KERN_INFO "0x%02x 0x%02x 0x%02x 0x%02x ",
			       ov & 0xff, (ov >> 8) & 0xff,
			       (ov >> 16) & 0xff, (ov >>24) & 0xff);
			for (i=1; i < TEST_LEN/4;  i++) {
				nv = r4l();
				if ((__u32)(nv - ov) != 0x04040404 &&
				    (__u32)(nv - ov) != 0xfcfcfcfc) {
					e ++;
				}
				ov = nv;
				if ((i & 0x3) == 0) {
					PRINT_T("\n"KERN_INFO);
					schedule();
				}
				PRINT_T("0x%02x 0x%02x 0x%02x 0x%02x ",
				       nv & 0xff, (nv >> 8) & 0xff,
				       (nv >> 16) & 0xff, (nv >>24) & 0xff);
			}
			PRINT_T("\n");
			break;
		}
		default:
			break; /* make GCC happy */
		}		

		t2(PARPORT_CONTROL_DIRECTION); /* disable reading */

		break;
	default:
		break;
	}
	
	TRACE_EXIT (e == 0) ? 0 : -EIO;
}

/*  stolen from drivers/block/paride/... like so many else in this file ...
 */
static int bpck_fdc_test_port(bpck_fdc_t *bpck) /* check for 8-bit port */
{	
	int	i, r, m;
	TRACE_FUN(ft_t_flow);

	w2(0x2c);   /* tristate data on bid. ports */
	i = r0();   /* read value */
	w0(255-i);  /* write something different to the parport */
	r = r0();   /* should still equal to i if bidirectional */
	w0(i);      /* restore old value */

	m = ft_bpck_none;

	TRACE(ft_t_any, "r: 0x%02x, i: 0x%02x", r, i);

	if (r == i) {
		m = ft_bpck_epp32; /* might be either EPP or PS2 port */
	} else if (r == (255 - i)) {
		m = ft_bpck_spp;
	}

	w2(0xc);   /* the upper two bits are unused, but play safe ... */
	i = r0();  /* should now be in write-only mode */
	w0(255-i); /* write something to the port */
	r = r0();  /* should still be the value just written */
	w0(i);     /* restore old value */
	TRACE(ft_t_any, "r: 0x%02x, i: 0x%02x", r, i);

	if (r != (255-i)) {
		m = ft_bpck_none; /* didn't work out */
	}

	if (m == ft_bpck_spp) {
		w2(6); w2(0xc); r = r0(); w0(0xaa); w0(r); w0(0xaa);
	}
	if (m == ft_bpck_epp32) {
		w2(0x26); w2(0xc);
	}


	TRACE_EXIT (m == ft_bpck_none) ? ft_bpck_spp : ft_bpck_epp32;
}

/* stole this one, too, from paride/bpck.c ...
 */
static void bpck_fdc_log_adapter(bpck_fdc_t *bpck)
{
	static const char *mode_string[5] = { "4-bit","8-bit","EPP-8",
					      "EPP-16","EPP-32" };
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_info,
	      "bpck floppy tape at 0x%lx, mode %d (%s), delay %d",
	      (long unsigned int)bpck->BASE,
	      bpck->used_proto,
	      mode_string[bpck->used_proto],
	      bpck->parinfo.delay);
	TRACE_EXIT;
}

/*  generate the CRC value for the data contained in "data".
 *
 *  Please have a look at "makecrc.c" in this directory for some
 *  explanations.
 */
static void bpck_fdc_generate_crc(bpck_fdc_t *bpck,
				  const __u8 *data, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		bpck->crc = (crc_tab[((bpck->crc >>8) ^ data[i]) & 0xff] 
			     ^ (bpck->crc << 8));		
	}
}

/* compare CRC register contents and reset crc value to 0xffff
 *
 * FT_BPCK_REG_CLEAR (i.e. 0x0b) is a funny thing. It expects a value
 * of 0x0b to be written to it to clear the CRC shift register
 * contents ... 
 */
static int bpck_fdc_crc_check(bpck_fdc_t *bpck)
{
	__u16 crc;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	crc = RR2(FT_BPCK_REG_CRC);
	
	if (crc != bpck->crc) {
		TRACE(ft_t_err,
		      "CRC checksum error. Driver: 0x%04x, bpck: 0x%04x",
		      bpck->crc, crc);
		result = -EIO;
	}
	TRACE(ft_t_noise, "CRC: 0x%04x/0x%04x", crc, bpck->crc);
	bpck->crc = 0xffff;
	WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);
	TRACE_EXIT result;
}

#if 0
/*  determine the size of the installed memory
 *
 *  This is a fun routine. We know that we have 128k of RAM
 *
 *  Also, it is damn slow. But works. At least I know that the memory
 *  address stuff works correctly.
 */
static int bpck_fdc_memory_size(bpck_fdc_t *bpck)
{
	__u32 address;
	__u8  data[32];
	__u16 crc0;
	int i;
	TRACE_FUN(ft_t_flow);

	for (i = 0; i < sizeof(data)/2; i++) {
		data[2*i]     = 0x00;
		data[2*i + 1] = 0xff;
	}
	WR3(FT_BPCK_REG_MEMADDR, 0x000000);
	WR3(FT_BPCK_REG_DMAADDR, 0x000000);
	WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);
	
	bpck_fdc_write_data(bpck, data, sizeof(data));
	bpck_fdc_generate_crc(bpck, data, sizeof(data));	
		
	crc0 = bpck->crc;

	TRACE_CATCH(bpck_fdc_crc_check(bpck),);

	for (i = 0; i < sizeof(data)/2; i++) {
		data[2*i]     = 0xff;
		data[2*i + 1] = 0x00;
	}

	for (i = 1; i < (32768*8/sizeof(data)); i++) {

		schedule();
		
		/* initialize memory address register and disable dma.
		 */
		address = i * sizeof(data);
		WR3(FT_BPCK_REG_MEMADDR, address);
		/* WR3(FT_BPCK_REG_DMA_ADDR, 0x000000); */
		WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);
	
		bpck_fdc_write_data(bpck, data, sizeof(data));
		bpck_fdc_generate_crc(bpck, data, sizeof(data));

		TRACE_CATCH(bpck_fdc_crc_check(bpck),);

		/* now check whether we still get the same CRC when
		 * reading at address 0x000000
		 */

		WR3(FT_BPCK_REG_MEMADDR, 0x000000);
		WR3(FT_BPCK_REG_DMAADDR, 0x000000);		
		WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);
		
		bpck_fdc_check_data(bpck, sizeof(data));
		
		bpck->crc = crc0;

		if (bpck_fdc_crc_check(bpck) < 0) {
			TRACE(ft_t_info, "Found %d bytes of installed memory",
			      i * sizeof(data));
			TRACE_EXIT i*sizeof(data);
		}
	}
	TRACE_EXIT 0;
}
#endif

/*  Check memory and CRC checksum facilities.  */
static int bpck_fdc_memory_test(bpck_fdc_t *bpck)
{
	__u16 data[16];
	__u16 pattern;
	__u16 ocrc;
	int i, j;
	TRACE_FUN(ft_t_flow);

	for (i = 0; i < 16; i++) {

		schedule();

		pattern = 0xff00 << i;
		pattern |= 0xff00 >> (16 - i);
		for (j = 0; j < 16; j ++) {
			data[j]   = pattern;
		}

		/* initialize memory address register and disable dma.
		 */
		WR3(FT_BPCK_REG_MEMADDR, 0x000000);
		WR3(FT_BPCK_REG_DMAADDR, 0x000000);
		WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);

		TRACE(ft_t_noise, "Writing pattern 0x%04x", pattern);

		(void)bpck_fdc_write_data(bpck, (__u8 *)data, 32);
		bpck_fdc_generate_crc(bpck, (__u8 *)data, 32);
		
		ocrc = bpck->crc;
		
		TRACE_CATCH(bpck_fdc_crc_check(bpck),);

		WR3(FT_BPCK_REG_MEMADDR, 0x000000);
		WR3(FT_BPCK_REG_DMAADDR, 0x000000);
		WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);
		
		TRACE(ft_t_noise, "Reading it back");

		(void)bpck_fdc_read_data(bpck, (__u8 *)data, 32);
		bpck_fdc_generate_crc(bpck, (__u8 *)data, 32);

		if (data[0] != pattern) {
			TRACE(ft_t_err, "Expected pattern 0x%04x, got 0x%04x",
			      pattern, data[0]);
			TRACE_EXIT -EIO;
		}

		TRACE_CATCH(bpck_fdc_crc_check(bpck),);

		WR3(FT_BPCK_REG_MEMADDR, 0x000000);
		WR3(FT_BPCK_REG_DMAADDR, 0x000000);
		WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);

		TRACE(ft_t_noise, "Reading without transfer");

		(void)bpck_fdc_check_data(bpck, 32);

		bpck->crc = ocrc;
			
		TRACE_CATCH(bpck_fdc_crc_check(bpck),);

	}
	TRACE_EXIT 0;
}

static void bpck_fdc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	fdc_info_t *fdc = (fdc_info_t *)dev_id;
	bpck_fdc_t *bpck;
	static int moan = 0;
	int i;
	TRACE_FUN(ft_t_flow);     

	if (fdc == NULL) {
		TRACE(ft_t_bug, "BUG: Spurious interrupt (no fdc data)");
		goto err_out;
	}
	if (fdc->magic != FT_FDC_MAGIC) {
		TRACE(ft_t_bug, "BUG: Magic number mismatch (0x%08x/0x%08x), "
		      "prepare for Armageddon", FT_FDC_MAGIC, fdc->magic);
		goto err_out;
	}
	if (fdc->irq != irq) {
		TRACE(ft_t_warn, "BUG?: Wrong IRQ number: got %d, expected %d", irq, fdc->irq);
		// goto err_out;
	}
	if ((bpck = (bpck_fdc_t *)fdc->data) == NULL) {
		TRACE(ft_t_bug,
		      "BUG: no bpck data allocated for bpck driver");
		goto err_out;
	}
	if (bpck->magic != FT_BPCK_MAGIC) {
		TRACE(ft_t_bug, "BUG: Magic number mismatch (0x%08x/0x%08x), "
		      "prepare for Armageddon", FT_BPCK_MAGIC, bpck->magic);
		goto err_out;
	}

	if (bpck->connected && !bpck->polling) {
		TRACE(ft_t_warn, "BUG: Already connected");
		goto err_out;
	}
	if (!bpck->initialized) {
		TRACE(ft_t_noise, "parport grab interrupt");
		goto err_out;
	}
	if (!fdc->active) {
		/* I get lots of interrupts when my printer is
		 * switched off, but connected through the obispo
		 * driver. Not that pathologic.
		 */
		if (moan ++ < 10) {
			TRACE(ft_t_noise, "Spurious or grab interrupt");
		}
		goto err_out;
	}
	for (i = 0;;i++) {
		void (*handler) (fdc_info_t *data) = fdc->hook;
		__u8 reg0;
		__u8 regf;

		if (!fdc->hook) {
			/* probably this will never happen. */
			static int count = 0;
			if (++count < 3) {
				TRACE(ft_t_err, "Unexpected ftape interrupt");
			}
			goto err_out;
		}

		reg0 = RR(FT_BPCK_REG_STAT);
		if (reg0 == 0xff) {
			TRACE(ft_t_err, "Probably not connected 0x%02x",
			      r1());
			w2(bpck->saved_ctr);
			goto err_out;
		}
		if (!(reg0 & FT_BPCK_STAT_IRQ)) {
			goto out;
		}
		regf = RR(FT_BPCK_REG_IRQ);
		if (regf == 0xff) {
			TRACE(ft_t_err, "Probably not connected 0x%02x",
			      r1());
			w2(bpck->saved_ctr);
			goto err_out;
		}
		if (!(regf & FT_BPCK_IRQ_IRQ)) {
			goto out;
		}
		fdc->hook = NULL;		
		handler(fdc); /* this should be fdc_isr() in
			       * most cases
			       * grab_grab_handler() at
			       * probe time
			       */
	} 
 out:
	bpck_fdc_disconnect (bpck, 1);
 err_out:
	TRACE_EXIT;
}

static int bpck_fdc_query_proto(bpck_fdc_t *bpck)
{
	__u8 res;
	TRACE_FUN(ft_t_flow);
	
	w1(r1());
	/* save initial values */
	bpck->saved_ctr = r2();
	w2(PARPORT_CONTROL_INIT);
	bpck->saved_dtr = r0();

	w0(0xff);
	w0(0x00);
	t2(PARPORT_CONTROL_SELECT);
	t2(PARPORT_CONTROL_SELECT);
	t2(PARPORT_CONTROL_SELECT);

	t2(PARPORT_CONTROL_AUTOFD);
	res = r1() & 0xf8;
	t2(PARPORT_CONTROL_AUTOFD);
	if (r1() & PARPORT_STATUS_BUSY) { /* make some noise ... */
		TRACE(ft_t_flow, "Connect while IRQ is pending");
	}
	
	switch (res) {
	case PARPORT_STATUS_ACK:
		t2(PARPORT_CONTROL_SELECT);
		t2(PARPORT_CONTROL_SELECT);
		printk(KERN_INFO "Detected possible drive in SPP mode!");
		bpck->used_proto = ft_bpck_spp; /* could also be PS2 */
		break;
	case PARPORT_STATUS_ACK | PARPORT_STATUS_BUSY:
		t2(PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT);
		t2(PARPORT_CONTROL_SELECT | PARPORT_CONTROL_INIT);
		printk(KERN_INFO "Detected possible drive in EPP mode!");
		bpck->used_proto = ft_bpck_epp8; /* could also be better */
		break;
	default:
		bpck->used_proto = ft_bpck_none; /* failure, not connected */
		TRACE(ft_t_err, "Drive likely not connected: parport reports status 0x%02x, expected 0xc0 or 0x40", res);
		printk(KERN_ERR "Drive not connected, or unable to detect or communicate.");
		break;
	}

	/* restore old values */
	w2(bpck->saved_ctr);
	w0(bpck->saved_dtr);
	 
	TRACE_EXIT (bpck->used_proto != ft_bpck_none) ? 0 : -ENXIO;
}


static int bpck_fdc_probe_irq(bpck_fdc_t *bpck)
{
	int irq = bpck->IRQ;
	TRACE_FUN(ft_t_flow);

#ifndef USE_PARPORT
	irq = probe_irq_on();
#endif

	/* initialize IRQ register to known state */
	WR (FT_BPCK_REG_IRQ,  0x00);

	/* I don`t know WHY the following sequence causes an
	 * interrupt, but it works. Register 0x06 isn't an IRQ enable
	 * register. It is used to read the EEPROM, too (if there is
	 * an EEPROM installed, which is not the case with modern
	 * floppy tape devices made by MS ... )
	 */

	WR (FT_BPCK_REG_0x06, 0x08);
	WR (FT_BPCK_REG_0x06, 0x00);
	WR (FT_BPCK_REG_0x06, 0x80);
	WR (FT_BPCK_REG_CTRL, 0x04);

	w2(bpck->ctr | PARPORT_CONTROL_INTEN);
	WR (FT_BPCK_REG_CTRL, 0x00); /* this triggers the interrupt */
	udelay(10); /* 2 seems to be sufficient. Play safe */
	WR (FT_BPCK_REG_CTRL, 0x04); /* this triggers the interrupt */
	w2(bpck->ctr & ~PARPORT_CONTROL_INTEN);

#ifndef USE_PARPORT
	irq = probe_irq_off(irq);

	if (irq <= 0) {
		TRACE(ft_t_err,"%s irq found", 
		      irq < 0 ? "multiple" : "no");
		irq = -1;
	}
#endif

	TRACE_EXIT irq;
}

/*  Send some magic reset (?) or init sequence to the bpck drive
 *
 *  Don't know. In any case, this is the major detection routine that
 *  gets us more or less defined results to decide whether we are
 *  connected and whether is the correct device, and not a bpck CDROM
 *  or something else. 
 */
static int bpck_fdc_detect_bpck(bpck_fdc_t *bpck)
{
	__u8 b;
	__u8 regf;
#ifdef USE_PARPORT
	__u8 ectrl = 0x00;
#endif
	int result = 0;
	TRACE_FUN(ft_t_flow);

#ifdef USE_PARPORT
# ifdef parport_read_econtrol
	/* HACK HACK HACK. However, the parport initialization
	 * routines disable the PS2 protocol with my particular
	 * chipset.
	 */
	TRACE(ft_t_flow, "Econtrol: 0x%02x",
	      ectrl = parport_read_econtrol(bpck->parinfo.dev->port));

	parport_write_econtrol(bpck->parinfo.dev->port,
			       (ectrl & 0x1f) | (ecr_bits << 5));
# else
	TRACE(ft_t_info, "Econtrol: 0x%02x",
	      ectrl = inb(bpck->parinfo.dev->port->base_hi + 0x02));
	outb((ectrl & 0x1f) | (ecr_bits << 5),
	     bpck->parinfo.dev->port->base_hi + 0x02);
	TRACE(ft_t_info, "Econtrol: 0x%02x",
	      ectrl = inb(bpck->parinfo.dev->port->base_hi + 0x02));
# endif
#endif

	WR (FT_BPCK_REG_CTRL, 0x00);
	WR (FT_BPCK_REG_0x05, 0x24);
	WRM(FT_BPCK_REG_PROTO, 0x00, 0x24, 0x00);
	b = RR(FT_BPCK_REG_CLEAR);
#ifdef TESTING
	TRACE(ft_t_warn,
	      "Please notify "THE_FTAPE_MAINTAINER": reg 0x0b = 0x%02x. "
	      "Thank you for using ftape", b);
#endif
	WRM(FT_BPCK_REG_PROTO, 0x00, 0x24, 0x00, 0x01);
	WR (FT_BPCK_REG_0x1a, 0x01);

	if (bpck->fdc->irq == -1) {
		bpck->fdc->irq = bpck_fdc_probe_irq(bpck);
		if (bpck->fdc->irq == -1) {
			result = -ENXIO;
			goto out;
		}
	}

	WR (FT_BPCK_REG_0x06, 0x00);
	WR (FT_BPCK_REG_CTRL, 0x00);

	TRACE_CATCH(bpck_fdc_select_reg(bpck, FT_BPCK_REG_IRQ),);
	bpck_fdc_write_reg_value(bpck, 0x18);
	regf = bpck_fdc_read_reg_value(bpck);
	if ((regf & 0x11) != 0x11 && (regf & 0x11) != 0x10) {
		TRACE(ft_t_err, "Expected 0x11 or 0x10, got 0x%02x",
		      regf & 0x11);
		result = -ENXIO;
		goto out;
	}
	TRACE_CATCH(bpck_fdc_select_reg(bpck, FT_BPCK_REG_IRQ),);
	bpck_fdc_write_reg_value(bpck, 0x10);
	regf = bpck_fdc_read_reg_value(bpck);
	if ((regf & 0x11) != 0x10) {
		TRACE(ft_t_err, "Expected 0x10, got 0x%02x",
		      regf & 0x11);
	}

	/* end of magic
	 */
	
	/*  ready, bpck was found. 
	 */
	
 out:
	TRACE_EXIT result;
}

/* Switch to a new protocol using register 0x24. (???)  Try to test
 * it with register 0x13 and switch back to the old mode in case of
 * problems.
 *
 * Return -ENXIO if neither works.
 *
 * TODO: check which of all that magic below is really needed. 
 */
static int bpck_fdc_switch_proto(bpck_fdc_t *bpck,
				 ft_bpck_proto_t new_proto)
{
	ft_bpck_proto_t old_proto = bpck->used_proto;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	/* why 0xa4 ???
	 */
	WRM(FT_BPCK_REG_PROTO,
	    bpck->proto_bits = bpck_fdc_decode_proto(new_proto), 0xa4);	
	bpck->used_proto = new_proto; /* hopefully */

	TRACE(ft_t_noise, "Trying to switch to proto %d", new_proto);

	bpck_fdc_disconnect(bpck, 0);
	if (bpck_fdc_connect(bpck) < 0) {
		goto protocol_failure;
	}
	WR(FT_BPCK_REG_0x07, 0x01); /* maybe 0x05 for non EPP. Find out */
	
	bpck->parinfo.delay = 0;

	do {
		if (bpck_fdc_test_protocol(bpck) == 0) {
			if (bpck->parinfo.delay != 0) {
				TRACE(ft_t_info, "Need delay %d for proto %d",
				      bpck->parinfo.delay, new_proto);
			}
			TRACE(ft_t_noise, "Success");
			goto out; /* got it */
		}
	} while (++(bpck->parinfo.delay) < 10); /* brute force */

 protocol_failure:
	bpck_fdc_force_protocol(bpck, old_proto);
	bpck_fdc_disconnect(bpck, 0);
	TRACE_CATCH(bpck_fdc_connect(bpck),);
	WR (FT_BPCK_REG_0x05, 0x24); /* magic */
	WR (FT_BPCK_REG_0x07, 0x05); /* maybe 0x01 for EPP */
	w2(PARPORT_CONTROL_STROBE | PARPORT_CONTROL_INIT);
	result = -EINVAL; /* return -EINVAL to caller to make it retry */
 out:
	WR (FT_BPCK_REG_CTRL, bpck->proto_bits);
	WR3(FT_BPCK_REG_MEMADDR, 0x000000);
	WR3(FT_BPCK_REG_DMAADDR, 0x000000);
	WR (FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);

	TRACE_EXIT result;
}

/* This is called by the ftape fdc routines before trying to access
 * the FDC. Detection is done elsewhere, post-detection initialization
 * and claiming of resources should be done here.
 */
int bpck_fdc_grab(fdc_info_t *fdc)
{
	bpck_fdc_t *bpck = fdc->data;
	TRACE_FUN(ft_t_flow);

	if (bpck->failure) {
		TRACE_EXIT -ENXIO;
	}
	fdc->hook = NULL;

	/*  allocate I/O regions and irq first.
	 */
	TRACE_CATCH(ft_parport_claim(fdc, &bpck->parinfo), );

	if (bpck->initialized) {
		/* normal fdc grab, after bpck had been detected
		 */

		/* But: nevertheless check whether it is still
		 * connected!
		 */
		disable_irq(bpck->IRQ);
		TRACE_CATCH(bpck_fdc_connect(bpck),
			    ft_parport_release(fdc, &bpck->parinfo);
			    enable_irq(bpck->IRQ););

		WR (FT_BPCK_REG_CTRL, bpck->proto_bits); /* play safe */

		w2(bpck->ctr | PARPORT_CONTROL_INTEN); /* enable interrupts */
		WR(FT_BPCK_REG_IRQ, (FT_BPCK_IRQ_SOFTEN |
				      FT_BPCK_IRQ_HARDEN));
		
		bpck_fdc_disconnect (bpck, 1); /* disconnect with IRQs on */
		enable_irq(bpck->IRQ);
	}

	TRACE_EXIT 0;
}

/* This is called by the higher level fdc routines after releasing
 * the tape drive.
 */
static int bpck_fdc_release(fdc_info_t *fdc)
{
	bpck_fdc_t *bpck = fdc->data;
	TRACE_FUN(ft_t_flow);
	
	if (bpck->initialized) {

		if (!bpck->connected) {
			(void)bpck_fdc_connect(bpck);
		}
		/* disconnect with irqs off
		 *
		 * But don't disable the IRQ in the IRQ controller
		 * Alan's brain damaged IRQ accounting stuff is just
		 * too bad.
		 */
		bpck_fdc_disconnect(bpck, 0);
	}

	ft_parport_release(fdc, &bpck->parinfo);
	TRACE_EXIT 0;
}

/* not an __initfunc() as the parallel port device can be removed.
 */
static int bpck_fdc_probe(fdc_info_t *fdc)
{
	bpck_fdc_t *bpck = fdc->data;
	ft_bpck_proto_t max, best;
	int result;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_info,
	      "Bpck parallel port tape drive interface for " FTAPE_VERSION);

	bpck->used_proto = ft_bpck_spp;
	bpck->crc        = 0xffff; /* initialize crc register */
	bpck->fdc        = fdc;    /* restore pointer */

	TRACE_CATCH(bpck_fdc_grab(fdc),);	

	max = bpck_fdc_test_port(bpck); /* check for 8-bit port */
	
#if 0
	bpck_fdc_test_something(bpck, 6, PARPORT_CONTROL_SELECT);
	bpck_fdc_test_something(bpck, 5, PARPORT_CONTROL_AUTOFD);
	bpck_fdc_test_something(bpck, 6, PARPORT_CONTROL_AUTOFD);
#endif

	/*  Before testing for the optimal protocol, we first check
	 *  for bpck's idea of the protocol and then reset the tape
	 *  drive state machine. Afterwards we try to switch to the
	 *  best protocol.
	 */
	if ((result = bpck_fdc_query_proto(bpck)) < 0) {
		goto out;
	}

	/*  Use the SPP protocol for the initialization phase
	 */
	if ((result = bpck_fdc_connect(bpck)) == -ENXIO) {
		goto out;
	}
	if (bpck->used_proto != ft_bpck_spp ||
	    result == -EINVAL) {
		bpck_fdc_force_protocol(bpck, ft_bpck_spp);
		bpck_fdc_disconnect(bpck, 0);
		if ((result = bpck_fdc_connect(bpck)) < 0) {
			goto out;
		}
	}

	/*  Do the detection magic.
	 */
	if ((result = bpck_fdc_detect_bpck(bpck)) < 0) {
		goto out;
	}

	/*  register 0x13 is used to test whether the protocol is
	 *  working. If bpck is configured correctly, then register
	 *  0x13 returns a sequence of bytes, steadily incremented and
	 *  wrapping around at 0xff
	 */

	for (best = ft_bpck_spp; best <= max; best ++) {
		if (bpck_fdc_switch_proto(bpck, best) >= 0) {
			bpck->best_proto = best;
		}
	}

	if (parport_proto != ft_bpck_none) {
		bpck->best_proto = parport_proto;
	}

	if ((result = bpck_fdc_switch_proto(bpck, bpck->best_proto)) < 0) {
		goto out;
	}

	bpck_fdc_log_adapter(bpck);

	result = bpck_fdc_memory_test(bpck);

#if 0
	(void)bpck_fdc_memory_size(bpck);
#endif

	WR3(FT_BPCK_REG_MEMADDR, 0x000000);
	WR3(FT_BPCK_REG_DMAADDR, 0x000000);
	WR(FT_BPCK_REG_CLEAR, FT_BPCK_CLEAR_CLEAR);


	bpck_fdc_disconnect(bpck, 0);

 out:
	bpck_fdc_release(fdc);
	/* initialize IRQ after releasing fdc */
	if (bpck->IRQ == -1 && fdc->irq != -1) {
		TRACE(ft_t_info, "Setting bpck irq to %d", fdc->irq);
		bpck->IRQ = fdc->irq;
	}
	TRACE_EXIT result;
}

/* not an __initfunc() as the parallel port device can be removed.
 *
 * This is called by the higher level fdc routines to determine
 * whether the hardware is present.
 */
static int bpck_fdc_detect(fdc_info_t *fdc)
{
	int sel = fdc->unit;
	bpck_fdc_t *bpck;
	TRACE_FUN(ft_t_flow);

	bpck = ftape_kmalloc(fdc->unit, sizeof(*bpck), 1);
	if (!bpck) {
		TRACE_EXIT -ENOMEM;
	}
	bpck->magic       = FT_BPCK_MAGIC;
	fdc->data         = bpck;
	bpck->fdc         = fdc;
	bpck->connected   = 0;
	bpck->initialized = 0;
	bpck->failure     = 0;
	TRACE(ft_t_noise, "called with ftape id %d", sel);

	bpck->parinfo.handler = bpck_fdc_interrupt;
	bpck->parinfo.probe   = bpck_fdc_probe;
	bpck->parinfo.id      = "bpck fdc (ftape)";

	TRACE_CATCH(ft_parport_probe(fdc, &bpck->parinfo),
		    ftape_kfree(fdc->unit, &fdc->data, sizeof(*bpck));
		    TRACE(ft_t_err,
			  "can't find bpck interface for ftape id %d", sel));

	fdc->dma = -1;

	fdc->sra  = 0;
	fdc->srb  = 1;
	fdc->dor  = 2;
	fdc->tdr  = 3;
	fdc->msr  = fdc->dsr = 4;
	fdc->fifo = 5;
	fdc->dir  = 7;
	fdc->ccr  = 7;
	fdc->dor2 = 0xffff;

	fdc->buffers_needed = 4;

	fdc->hook = NULL;

	bpck->initialized = 1;

	TRACE_EXIT 0;
}

/* the write register hook for the high level fdc routines ... */
static void bpck_fdc_out(fdc_info_t *fdc,
			 unsigned char value, unsigned short r)
{
	bpck_fdc_t *bpck = (bpck_fdc_t *)fdc->data;

	WR(FT_BPCK_FDC_REGSET + r, value);
}

/* the read register hook for the high level fdc routines ... */
static __u8 bpck_fdc_in(fdc_info_t *fdc, unsigned short reg)
{
	bpck_fdc_t *bpck = (bpck_fdc_t *)fdc->data;

	return RR(FT_BPCK_FDC_REGSET + reg);
}

/* disable interrupts. As the bpck doesn't produce interrupts as long
 * as we are connected, we simply connect in case we are disconnected.
 * Otherwise we disable the irq here and tell the parport not to
 * generate IRQs. Both is also done in bpck_fdc_connect()
 */
static void bpck_fdc_disable_irq(fdc_info_t *fdc)
{
	bpck_fdc_t *bpck = (bpck_fdc_t *)fdc->data;
	TRACE_FUN(ft_t_any);

	if (bpck->polling) {
		TRACE(ft_t_noise, "Called during software polling");
		TRACE_EXIT;
	}

	/* first disable the interrupt 
	 */
	if (bpck->IRQ != -1) {
		disable_irq(bpck->IRQ);
	}

	/* Then either connect, if disconnected, or simply disable the
	 * interrupts in the parport chip
	 */
	if (!bpck->connected) {
		bpck_fdc_connect(bpck);
	} else {
		w2(bpck->ctr & ~PARPORT_CONTROL_INTEN);
	}

	TRACE_EXIT;
}

/* Yes, we do need this, we DO NEED software polling. The bpck logic
 * doesn't generate an interrupt while we are connected.
 */
static inline void bpck_fdc_poll_interrupt(bpck_fdc_t *bpck)
{
	__u8 stat = RR(FT_BPCK_REG_STAT);
	TRACE_FUN(ft_t_any);
	
	if (stat != 0xff && (stat & FT_BPCK_STAT_IRQ)) {
		bpck->polling = 1;
		bpck_fdc_interrupt(bpck->IRQ, bpck->fdc, NULL);
		bpck->polling = 0;
		TRACE(ft_t_noise, "Ran irq handler");
	}
	TRACE_EXIT;
}

/* The parport irq is still disabled, which means that we are sure
 * that bpck_interrupt() won't be re-entered by spurious interrupts.
 *
 * However, when we enter here, fdc->irq_level is 0. Therefore,
 * bpck_interrupt() might again call a fdc_disable_irq() /
 * fdc_enable_irq() pair. This doesn't cause too much harm, as long as
 * there won't be another interrupt ...
 */
static void bpck_fdc_enable_irq(fdc_info_t *fdc)
{
	bpck_fdc_t *bpck = (bpck_fdc_t *)fdc->data;
	TRACE_FUN(ft_t_any);

	if (bpck->polling) {
		TRACE(ft_t_noise, "Called during software polling");
		TRACE_EXIT;
	}
	bpck_fdc_poll_interrupt(bpck);
	if (bpck->connected) { /* the isr likes to disconnect itself */
		bpck_fdc_disconnect(bpck, 1);
	} else {
		w2(bpck->ctr | PARPORT_CONTROL_INTEN);
	}
	if (bpck->IRQ != -1) {
		enable_irq(bpck->IRQ);
	}
	TRACE_EXIT;
}

/* straight forward ... */
static void bpck_fdc_setup_dma(fdc_info_t *fdc,
			       char mode, unsigned long addr, 
			       unsigned int count)
{
	bpck_fdc_t *bpck = fdc->data;

	WR3(FT_BPCK_REG_DMAADDR, addr);
	WR3(FT_BPCK_REG_DMASIZE, count - 1);
	if (mode == DMA_MODE_READ) {
		bpck->proto_bits |= FT_BPCK_CTRL_DMA_READ;
	} else {
		bpck->proto_bits |= FT_BPCK_CTRL_DMA_WRITE;
	}
	WR(FT_BPCK_REG_CTRL, bpck->proto_bits);
}

static unsigned int bpck_fdc_terminate_dma(fdc_info_t *fdc,
					   ft_error_cause_t cause)
{		
	bpck_fdc_t *bpck = fdc->data;

	bpck->proto_bits &= ~(FT_BPCK_CTRL_DMA_WRITE | FT_BPCK_CTRL_DMA_READ);
       	WR(FT_BPCK_REG_CTRL, bpck->proto_bits);
	return (unsigned int)(-1); /* no dma residue available. sad. */
}

/* allocate the single deblock buffer when called with buff == NULL,
 * init the buffer structure buff is pointing to otherwise.
 */
static int bpck_fdc_alloc(fdc_info_t *fdc, buffer_struct *buff)
{
	bpck_fdc_t *bpck = fdc->data;
	int i;
	TRACE_FUN(ft_t_flow);

	if (buff == NULL) {
		if (bpck->buffer) {
			TRACE_EXIT -EINVAL;
		}
		TRACE_CATCH(ftape_vmalloc(fdc->unit, &bpck->buffer,
					  FT_BUFF_SIZE),);
		TRACE_EXIT 0;
	}
	for (i=0; i < 4; i++) {
		if (!(bpck->used_buffers & (1<<i))) {
			buff->virtual = bpck->buffer;
			bpck->used_buffers |= (1<<i);
			buff->dma_address = i * FT_BUFF_SIZE;
			TRACE(ft_t_noise, "dma: %p, virtual: %p",
			      (void *)((unsigned long)(i * FT_BUFF_SIZE)),
			      buff->virtual);
			TRACE_EXIT 0;
		}
	}
	TRACE_EXIT -ENOMEM;
}

/* if buff == NULL, free all pending resources, including the parport.
 * mark the dma area of "buff" as unused.
 */
static void bpck_fdc_free(fdc_info_t *fdc, buffer_struct *buff)
{
	bpck_fdc_t *bpck = fdc->data;
	
	if (buff != NULL) {
		int i = buff->dma_address / FT_BUFF_SIZE;

		bpck->used_buffers &= ~(1<<i);
	} else if (bpck && bpck->buffer) {
		ftape_vfree(fdc->unit, &bpck->buffer, FT_BUFF_SIZE);
		ft_parport_destroy(fdc, &bpck->parinfo);
		ftape_kfree(fdc->unit, &bpck, sizeof(*bpck));
		fdc->data = NULL;
	}
}

/* return the currently active deblock_buffer. This is always
 * bpck->buffer for us, but is more complicated for the internal FDC
 * driver.
 */
static void *bpck_fdc_get_deblock_buffer(fdc_info_t *fdc)
{
	bpck_fdc_t *bpck = fdc->data;

	if (!bpck->locked) {
		bpck->locked = 1;
	}
	return (void *)bpck->buffer;
}

static int bpck_fdc_put_deblock_buffer(fdc_info_t *fdc, __u8 **buffer)
{
	bpck_fdc_t *bpck = fdc->data;
	TRACE_FUN(ft_t_any);

	if (*buffer != bpck->buffer) {
		TRACE(ft_t_bug,
		      "Must be called with fdc_deblock_buffer %p (was %p)",
		      bpck->buffer, buffer);
		TRACE_EXIT -EINVAL;
	}
	if (bpck->locked) {
		bpck->locked = 0;
	}
	*buffer = NULL;
	TRACE_EXIT 0;
}

/* poll this often for interrutps. Maybe increase to 1024 */
#define BPCK_FDC_POLL_SIZE 512

/* Copy the contents of buff->virtual to the memory of the parport
 * drive.
 *
 * We compute the CRC checksum after the data transfer and compare it
 * with bpck's idea of the shift register. Return an error if they
 * don't match. Poll for interrupts between BPCK_FDC_POLL_SIZE bytes.
 */
static int bpck_fdc_write(fdc_info_t *fdc, buffer_struct *buff, int size)
{
	bpck_fdc_t *bpck = fdc->data;
	int i;
	TRACE_FUN(ft_t_flow);

	if (fdc->isr_active) TRACE(ft_t_bug, "BIG FAT BUG");

	TRACE(ft_t_flow, "virtual: %p, size: %d, dma: %p",
	      buff->virtual, buff->bytes, (void *)buff->dma_address);

	TRACE_CATCH(WR3(FT_BPCK_REG_MEMADDR, buff->dma_address),);

	for (i = 0; i < size; i += BPCK_FDC_POLL_SIZE) {
		bpck_fdc_poll_interrupt(bpck);
		TRACE_CATCH(bpck_fdc_write_data(bpck,
						buff->virtual + i,
						BPCK_FDC_POLL_SIZE),);
	}
	bpck_fdc_generate_crc(bpck, buff->virtual, size);
	TRACE_CATCH(bpck_fdc_crc_check(bpck),);
	TRACE_EXIT 0;
}

/* This is for the purpose of formatting. Hopefully the ECC
 * coprocessor stays inactive. We retry a single time on CRC error.
 *
 * Return -EIO if the retry fails, too.
 */
static int bpck_fdc_write_buffer(fdc_info_t *fdc,
				 buffer_struct *buffer, __u8 **dummy)
{
	int retry = 0;
	TRACE_FUN(ft_t_any);
	
	TRACE_CATCH(bpck_fdc_put_deblock_buffer(fdc, dummy),);

	buffer->bytes = FT_SECTORS_PER_SEGMENT * FT_SECTOR_SIZE;
 again:
	if (bpck_fdc_write(fdc, buffer, buffer->bytes) < 0) {
		if (retry ++ < 1) goto again;
		*dummy = bpck_fdc_get_deblock_buffer(fdc);
		TRACE_EXIT -EIO;
	}
	TRACE_EXIT buffer->bytes;
}

/* main data-write hook for ftape. Copy data, program ECC coprocessor
 * to generate syndromes. Doesn't increase performance too much, but
 * is nice, anyway.
 */
static int bpck_fdc_copy_and_gen_ecc(fdc_info_t *fdc,
				     buffer_struct *buffer,
				     __u8 **dummy,
				     SectorMap bad_sector_map)
{
	bpck_fdc_t *bpck = fdc->data;
	int retry = 0;
	int size, blocks, bads = count_ones(bad_sector_map);
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(bpck_fdc_put_deblock_buffer(fdc, dummy),);
	
	if (bads > 0) {
		TRACE(ft_t_noise, "bad sectors in map: %d", bads);
	}

	blocks = FT_SECTORS_PER_SEGMENT - bads;
	
	if (blocks < FT_ECC_SECTORS) {
		TRACE(ft_t_noise, "empty segment");
		TRACE_EXIT 0; /* skip entire segment */
	}

	blocks        = FT_SECTORS_PER_SEGMENT - bads;
	buffer->bytes = blocks * FT_SECTOR_SIZE;
	size          = (blocks - FT_ECC_SECTORS) * FT_SECTOR_SIZE;

	/*  write data to bpck memory, with retry on CRC error
	 */
 again:
	if (bpck_fdc_write(fdc, buffer, size) < 0) {
		if (retry ++ < 1) goto again;
		*dummy = bpck_fdc_get_deblock_buffer(fdc);
		TRACE_EXIT -EIO;
	}
	
	/* Program the ECC coprocessor. Hopefully. Yup. It does.
	 */
	WRM(FT_BPCK_REG_ECC,
	    FT_BPCK_ECC_GEN | FT_BPCK_ECC_SIZE(buffer->bytes),
	    FT_BPCK_ECC_START(buffer->dma_address));
	bpck_fdc_disconnect (bpck, 1); /* enable interrupts */
	TRACE_EXIT size;
}

/* This hook is called by the hard write error recovery machine to
 * rearrange the data still cached in the buffer queue.
 */
static ft_bpck_error_t bpck_fdc_read(fdc_info_t *fdc, buffer_struct *buff)
{
	bpck_fdc_t *bpck = fdc->data;
	int i;
	unsigned int size;
	ft_bpck_error_t result = ft_bpck_no_error;
	TRACE_FUN(ft_t_flow);

	WR(FT_BPCK_REG_CTRL, bpck->proto_bits);

	WR3(FT_BPCK_REG_MEMADDR, buff->dma_address);

	/* Program the ECC coprocessor.
	 */
	WRM(FT_BPCK_REG_ECC,
	    FT_BPCK_ECC_CHECK | FT_BPCK_ECC_SIZE(buff->bytes),
	    FT_BPCK_ECC_START(buff->dma_address));
	
	/* first try to transfer without ECC information
	 */
	size = buff->bytes - FT_ECC_SECTORS * FT_SECTOR_SIZE;

	for (i = 0; i < size; i += BPCK_FDC_POLL_SIZE) {
		bpck_fdc_poll_interrupt(bpck);
		bpck_fdc_read_data(bpck,
				   buff->virtual + i, BPCK_FDC_POLL_SIZE);
	}

	/* check whether we need the ECC syndromes as well
	 */
	if (RR(FT_BPCK_REG_STAT) & FT_BPCK_STAT_ECC_ERROR) {
		for (; i < buff->bytes; i += BPCK_FDC_POLL_SIZE) {
			bpck_fdc_poll_interrupt(bpck);
			bpck_fdc_read_data(bpck,
					   buff->virtual + i,
					   BPCK_FDC_POLL_SIZE);
		}
		result = ft_bpck_ecc_error;
	}

	/* generate CRC
	 */
	bpck_fdc_generate_crc(bpck, buff->virtual, i);
	if (bpck_fdc_crc_check(bpck) < 0) {
		result = ft_bpck_crc_error;
	}
	TRACE_EXIT result;
}

static int bpck_fdc_read_buffer(fdc_info_t *fdc, buffer_struct *buff, __u8 **destination)
{
	int retry = 0;
	TRACE_FUN(ft_t_any);

 again:
	switch (bpck_fdc_read(fdc, buff)) {
	case ft_bpck_no_error:
	case ft_bpck_ecc_error:
		break;
	case ft_bpck_crc_error: 
		if (retry ++ < 1) {
			goto again; /* ugly, but short */
		}
		*destination = NULL;
		TRACE_EXIT -EIO;
	}
	*destination = bpck_fdc_get_deblock_buffer(fdc);
	TRACE_EXIT 0;
}

/* main data read routine.
 *
 * TODO: determine how to program the ECC coprocessor to automatically
 * correct the data (is this possible at all?)
 */
static int bpck_fdc_correct_and_copy(fdc_info_t *fdc,
				     buffer_struct *buff, __u8 **destination)
{
	int retry = 0;
	bpck_fdc_t *bpck = fdc->data;
	TRACE_FUN(ft_t_any);


	if (buff->bytes <= 3 * FT_SECTOR_SIZE) {
		*destination = bpck_fdc_get_deblock_buffer(fdc);
		TRACE(ft_t_noise, "empty segment");
		TRACE_EXIT 0;
	}
	
	TRACE(ft_t_noise, "virtual: %p, size: %d, dma: %p",
	      buff->virtual, buff->bytes, (void *)buff->dma_address);

	TRACE(ft_t_flow, "virtual: %p, size: %d, dma: %p",
	      buff->virtual, buff->bytes, (void *)buff->dma_address);
	
 again:
	switch (bpck_fdc_read(fdc, buff)) {
	case ft_bpck_no_error:
		bpck_fdc_disconnect(bpck, 1);
		break;
	case ft_bpck_ecc_error:
		bpck_fdc_disconnect(bpck, 1);
		TRACE_CATCH(ftape_ecc_correct(fdc->ftape, buff),
			    *destination = NULL);
		break;
	case ft_bpck_crc_error: 
		if (retry ++ < 1) {
			goto again; /* ugly, but short */
		}
		*destination = NULL;
		TRACE_EXIT -EIO;
	}

	*destination = bpck_fdc_get_deblock_buffer(fdc);
	TRACE_EXIT buff->bytes - (3 * FT_SECTOR_SIZE);
}

/* This is the interface to the stock ftape driver
 */
static fdc_operations bpck_fdc_ops = {
	{ NULL, NULL },                 /* list node */
	"bpck-fdc",                     /* id, should be unique */
	"Micro Solutions \"Backpack\" parallel port floppy tape drives",

	bpck_fdc_detect,                /* hardware detection */

	bpck_fdc_grab,                  /* resource grabbing */
	bpck_fdc_release,               /* releasing of resources */
	
	bpck_fdc_out,			/* outp */
	bpck_fdc_out,			/* out  */
	bpck_fdc_in,			/* inp  */
	bpck_fdc_in,			/* in   */
	
	bpck_fdc_disable_irq,           /* unconditionally disable irqs */
	bpck_fdc_enable_irq,            /* unconditionally enable irqs  */
	
	bpck_fdc_setup_dma,             /* setup data transfer to/from FDC */
	bpck_fdc_terminate_dma,         /* terminate/disale dma */
	
	bpck_fdc_alloc,                 /* allocate buffers */
	bpck_fdc_free,                  /* release buffers */
	bpck_fdc_get_deblock_buffer,    /* return deblock buffer's address */
	bpck_fdc_put_deblock_buffer,    /* unlock module again */
	
	bpck_fdc_copy_and_gen_ecc,      /* write data with ECC generation */
	bpck_fdc_correct_and_copy,      /* read data with ECC correction */
	bpck_fdc_write_buffer,          /* write data without ECC */
	bpck_fdc_read_buffer,           /* read data without ECC */
};

/* Initialization
 */
int bpck_fdc_register(void)
{
	TRACE_FUN(ft_t_flow);

	printk(__FILE__ ": %s @ 0x%p\n", __func__, bpck_fdc_register);

	TRACE_CATCH(fdc_register(&bpck_fdc_ops),);

	TRACE_EXIT 0;
}

#ifdef MODULE

int bpck_fdc_unregister(void)
{

	fdc_unregister(&bpck_fdc_ops);

	return 0;
}


FT_MOD_PARM_INT(ecr_bits, "What to write to the econtrol ECR reg.");
FT_MOD_PARM_INT(parport_proto, "Parport protocol.");

MODULE_LICENSE("GPL");

MODULE_AUTHOR(
  "(c) 1998 Claus-Justus Heine");
MODULE_DESCRIPTION("Ftape-interface for Bpck parallel port floppy tape");
/* EXPORT_NO_SYMBOLS - deprecated, no longer needed */

/* Called by modules package when installing the driver
 */
int init_module(void)
{
	int result;

	/* EXPORT_NO_SYMBOLS - deprecated, no longer needed */

	result = bpck_fdc_register();
	return result;
}

/* Called by modules package when removing the driver 
 */
void cleanup_module(void)
{
	TRACE_FUN(ft_t_flow);
	bpck_fdc_unregister();
        printk(KERN_INFO "bpck module successfully unloaded.");
	TRACE_EXIT;
}

#endif /* MODULE */

#ifndef MODULE

#include "../setup/ftape-setup.h"

static ftape_setup_t config_params[] = {
#ifndef USE_PARPORT
	{ "base",      ft_fdc_base,      0x0, 0xffff, 1 },
	{ "irq" ,      ft_fdc_irq,        -1,     15, 1 },
#endif
	{ "pp",        ft_fdc_parport,    -2,      3, 1 },
        { "thr",       ft_fdc_threshold,   1,     16, 1 },
	{ "rate",      ft_fdc_rate_limit,  0,   4000, 1 },
	{ NULL, }
};

int bpck_fdc_setup(char *str)
{
	static int ints[6] = { 0, };

	str = get_options(str, ARRAY_SIZE(ints), ints);

	return ftape_setup_parse(str, ints, config_params);
}

#endif
