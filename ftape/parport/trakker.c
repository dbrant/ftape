/*
 *      Copyright (C) 1997-1998 Jochen Hoenicke.

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
 *      parallel port and the floppy controller of the Colorado
 *      Trakker.
 *
 */


#include <linux/module.h>
#include <linux/version.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/segment.h>

#include <linux/ftape.h>
#include <linux/init.h>

#define FDC_TRACING
#include "../lowlevel/ftape-tracing.h"

#include "../lowlevel/fdc-io.h"
#include "../lowlevel/fdc-isr.h"
#include "../lowlevel/ftape-init.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-buffer.h"

#undef fdc
#include "trakker.h"

#include "fdc-parport.h"

/*      Global vars.
 */
char trakker_src[] __initdata = "$RCSfile: trakker.c,v $";
char trakker_rev[] __initdata = "$Revision: 1.20 $";
char trakker_dat[] __initdata = "$Date: 2000/07/06 14:58:17 $";

/* If you have problems with this driver try to define some of these */
#undef	FULL_HANDSHAKE
#undef	DONT_USE_PS2

#define MAX_DELAY 3
#define MIN_DELAY 0

static __inline__ int min_(int x, int y)
{
        return (x<y)?x:y;
}

/*
 * the interface to the parport
 */
#define r_dtr()         ft_r_dtr(trakker->parinfo)
#define r_str()         ft_r_str(trakker->parinfo)
#define w_dtr(y)        ft_w_dtr(trakker->parinfo, y)
#define w_ctr(y)        ft_w_ctr(trakker->parinfo, y)

/*
 * low level input/output
 */

/*
 * The Colorado Trakker supports two handshake modes:
 *   - full handshake  
 *        send data_byte, toggle control
 *   - fast mode 
 *        if data_byte different send data_byte else toggle control
 */

#ifndef FULL_HANDSHAKE
static __inline__ void w_fast(struct trakker_struct *trakker, int value) 
{
	if (value != trakker->dtr) {
		w_dtr(trakker->dtr = value);
	} else {
		w_ctr(trakker->ctr ^= CONTROL_STROKE);
	}
}

static void trakker_outb_fast(fdc_info_t *fdc,
			      unsigned char value, unsigned short reg)
{
	struct trakker_struct *trakker = fdc->data;
#ifdef TESTING
	TRACE_FUN(ft_t_any);
#endif
	if (!in_interrupt() && !fdc->irq_level++) {
		disable_irq(trakker->IRQ);
	}
	w_fast(trakker, reg);
	w_fast(trakker, value);
	trakker->chksum ^= reg^value;
	if (!in_interrupt() && !--fdc->irq_level) {
		enable_irq(trakker->IRQ);
	}

#ifdef TESTING
	TRACE(ft_t_data_flow, "out(%x,%x)",reg,value);
	TRACE_EXIT;
#endif
}

static unsigned char trakker_inb_fast(fdc_info_t *fdc, unsigned short reg)
{
	struct trakker_struct *trakker = fdc->data;
	unsigned char value;
#ifdef TESTING
	TRACE_FUN(ft_t_any);
	if (!fdc->irq_level && !in_interrupt()) {
		TRACE(ft_t_bug, "(bug) missing fdc_disable_irq()");
	}
#endif
	w_fast(trakker, reg|0x80);
	value = r_str() & 0xf0;
	w_dtr(trakker->dtr = reg);
	value |= r_str() >> 4;
	value ^= 0x88;
	trakker->chksum ^= reg^value^0x80;

#ifdef TESTING
	TRACE(ft_t_data_flow, "in(%x)=%x",reg,value);
	TRACE_EXIT value;
#else
	return value;
#endif
}
#endif /* ! FULL_HANDSHAKE */

static void trakker_outb_hshake(fdc_info_t *fdc, 
				unsigned char value, unsigned short reg)
{
	struct trakker_struct *trakker = fdc->data;
#ifdef TESTING
	TRACE_FUN(ft_t_any);
#endif
	if (!in_interrupt() && !fdc->irq_level++)
		disable_irq(trakker->IRQ);

	w_dtr(reg);
	w_ctr(trakker->ctr ^ CONTROL_STROKE);
	w_dtr(trakker->dtr = value);
	w_ctr(trakker->ctr);
	trakker->chksum ^= reg^value;

	if (!in_interrupt() && !--fdc->irq_level)
		enable_irq(trakker->IRQ);

#ifdef TESTING
	TRACE(ft_t_data_flow,"outh(%x,%x)",reg,value);
	TRACE_EXIT;
#endif
}

static unsigned char trakker_inb_hshake(fdc_info_t *fdc, unsigned short reg) 
{
	struct trakker_struct *trakker = fdc->data;
	unsigned char value;
#ifdef TESTING
	TRACE_FUN(ft_t_any);
	if (!fdc->irq_level && !in_interrupt()) {
		TRACE(ft_t_bug, "(bug) missing fdc_disable_irq()");
	}
#endif

	w_dtr(reg|0x80);
	w_ctr(trakker->ctr ^= CONTROL_STROKE);
	value  = r_str() & 0xf0;
	w_dtr(trakker->dtr = reg);
	value |= r_str() >> 4;
	value ^= 0x88;
	trakker->chksum ^= reg^value^0x80;

#ifdef TESTING
	TRACE(ft_t_data_flow,"inh(%x)=%x",reg,value);
	TRACE_EXIT value;
#else
	return value;
#endif
}

static void trakker_disable_irq(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;

	disable_irq(trakker->IRQ);
	trakker->out(fdc, 0x0, 0x19);
}

static void trakker_enable_irq(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;

	trakker->out(fdc, 0xf, 0x19);
	enable_irq(trakker->IRQ);
}

static void write_mem(fdc_info_t *fdc,
		      __u8* buffer, int count) 
{
	struct trakker_struct *trakker = fdc->data;
	int check=0;
	int dtr, ctr;
	unsigned char value;
	TRACE_FUN (ft_t_any);

	/* Hack: This doesn't work if the first byte is equal to
	 * initial dtr.  So put something different on dtr.  0x1d is
	 * not existing.
	 */
	trakker->out(fdc, ~*buffer, 0x1d);
	dtr = trakker->dtr;
	ctr = trakker->ctr & ~CONTROL_REG_MODE;
	w_ctr(ctr);

	if (!(trakker->mode & REG18_HSHAKE)) {
		while (count--) {
			value = *buffer++;
			check ^= value;
			if (value != dtr)
				w_dtr(dtr=value);
			else
				w_ctr(ctr^=CONTROL_STROKE);
		}
	} else {
		while (count--) {
			value = *buffer++;
			check ^= value;
			w_dtr(value);
			w_ctr(ctr^=CONTROL_STROKE);
		}
	}
	trakker->dtr = dtr;
	trakker->ctr = ctr | CONTROL_REG_MODE;
	w_ctr(trakker->ctr);
	if (check ^= trakker->in(fdc, 0x1b))
		TRACE(ft_t_err, "checksum error (off by 0x%02x)", check);
	TRACE_EXIT;
}

#ifndef DONT_USE_PS2
static void read_mem_ps2(fdc_info_t *fdc, __u8* buffer, int count)
{
	struct trakker_struct *trakker = fdc->data;
	int check = 0, ctr;
	TRACE_FUN(ft_t_any);

	ctr = (trakker->ctr & ~CONTROL_REG_MODE) | PARPORT_CONTROL_DIRECTION;
	w_ctr(ctr);
	while (count--) {
		w_ctr(ctr^=CONTROL_STROKE);
		*buffer = r_dtr();
		check ^= *buffer++;
	}
	trakker->ctr = (ctr & ~PARPORT_CONTROL_DIRECTION) | CONTROL_REG_MODE;
	w_ctr(trakker->ctr);

	check ^= trakker_inb_hshake(fdc, 0x1b);

	/* XXX Weird, I get checksum errors of this form.
	 * Until I know why, I ignore them.
	 */
	if (check && check != 0xff)
		TRACE(ft_t_err, "checksum error");
	TRACE_EXIT;
}
#endif /* DONT_USE_PS2 */

static void read_mem_spp(fdc_info_t *fdc,
			 __u8* buffer, int count)
{
	struct trakker_struct *trakker = fdc->data;
	int check = 0;
	TRACE_FUN(ft_t_any);

	w_ctr(trakker->ctr & ~CONTROL_REG_MODE);
	while (count--) {
		unsigned char value;

		w_dtr(trakker->dtr^0x80);
		value  = r_str() & 0xf0;
		w_dtr(trakker->dtr);
		value |= r_str() >> 4;
		value ^= 0x88;
		check ^= value;
		*buffer++ = value;
	}
	w_ctr(trakker->ctr);
	if (check != trakker_inb_hshake(fdc, 0x1b))
		TRACE(ft_t_err, "checksum error");
	TRACE_EXIT;
}

static int trakker_checksum(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	TRACE_FUN(ft_t_any);

	/* this checking is tricky */
	trakker->in(fdc, 0x1c);
	if (!trakker->chksum) {
		TRACE_EXIT 0;
	}
	
	/* We have a checksum error.
	 * Clear checksum, print message and return an error
	 */
	TRACE(ft_t_err, "checksum error (off by %2x)", trakker->chksum);
	trakker->chksum = 0;
	TRACE_EXIT -EIO;
}

static void trakker_write(fdc_info_t *fdc, buffer_struct *buff)
{
	struct trakker_struct *trakker = fdc->data;
	unsigned long dest = (unsigned long) buff->dma_address;
	__u8* source = buff->virtual;
	int size = buff->bytes;
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_fdc_dma, "offset 0x%lx", dest << 1);
	trakker->out(fdc, (dest << 1) & 0xff, 0x10);
	trakker->out(fdc, (dest >> 7) & 0xff, 0x11);
	trakker->out(fdc, (dest >>15) & 0xff, 0x12);
		
	if ((trakker->mode & REG18_HSHAKE)) {
		trakker->out(fdc, trakker->mode | REG18_WRITE, 0x18);
	}
	while (size) {
		int count = min_(size, WRITE_BLOCK_SIZE);

		fdc_disable_irq(fdc);

		write_mem(fdc, source, count);
		trakker_checksum(fdc);

		fdc_enable_irq(fdc);

		source += count;
		size -= count;
	}
	TRACE_EXIT;
}

void trakker_read(fdc_info_t *fdc, buffer_struct *buff)
{
	struct trakker_struct *trakker = fdc->data;
	__u8* dest = buff->virtual;
	unsigned long source = buff->dma_address;
	int size = buff->bytes;
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_fdc_dma, "offset 0x%lx", source << 1);

	trakker->out(fdc, (source << 1) & 0xff, 0x10);
	trakker->out(fdc, (source >> 7) & 0xff, 0x11);
	trakker->out(fdc, (source >>15) & 0xff, 0x12);
	trakker->out(fdc, trakker->mode |= REG18_READ, 0x18);
	while (size) {
		int count = min_(size, READ_BLOCK_SIZE);

		fdc_disable_irq(fdc);

		trakker->read_mem(fdc, dest, count);
		trakker_checksum(fdc);

		fdc_enable_irq(fdc);

		dest += count;
		size -= count;
	}
	trakker->out(fdc, trakker->mode &= ~REG18_READ, 0x18);
	TRACE_EXIT;
}

#define GLOBAL_TRACING
#include "../lowlevel/ftape-real-tracing.h"

static void trakker_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	fdc_info_t *fdc = (fdc_info_t *)dev_id;
	struct trakker_struct *trakker;
	static int interrupt_active = 0;
	TRACE_FUN(ft_t_any);

	if (!fdc) {
		TRACE(ft_t_bug, "BUG: Spurious interrupt (no fdc data)");
		goto err_out;
	}
	if (fdc->magic != FT_FDC_MAGIC) {
		TRACE(ft_t_bug, "BUG: Magic number mismatch (0x%08x/0x%08x), "
		      "prepare for Armageddon", FT_FDC_MAGIC, fdc->magic);
		goto err_out;
	}
	if (fdc->irq != irq) {
		TRACE(ft_t_bug, "BUG: Wrong IRQ number (%d/%d)", irq, fdc->irq);
		goto err_out;
	}

	if ((trakker = (struct trakker_struct *)fdc->data) == NULL) {
		TRACE(ft_t_bug, "BUG: "
		      "no trakker data allocated for trakker driver");
		goto err_out;
	}
	if (trakker->magic != FT_TRAKKER_MAGIC) {
		TRACE(ft_t_bug, "BUG: Magic number mismatch (0x%08x/0x%08x), "
		      "prepare for Armageddon",
		      FT_TRAKKER_MAGIC, trakker->magic);
		goto err_out;
	}

	if (!(r_str() & PARPORT_STATUS_ACK)) {
		/* The ACK line is used for data input and interrupt 
		 * signal.  While data is transferred the irq is disabled
		 * but the irq controller remembers the change and 
		 * issues an interrupt as soon as they are enabled again.
		 */
		goto err_out;
	}
	if (interrupt_active) {
		TRACE(ft_t_warn, "interrupt while another was pending.");
		goto err_out;
	}
	interrupt_active=1;

	if (fdc->hook) {
		/* The enable_irq/disable_irq calls are done by the
		 * fast_IRQ_interrupt functions.  We do the remaining
		 * part here.
		 * out(1, 0x1a) acknowledge this interrupt, so that
		 * the ACK line is cleared again.  This can only be
		 * done after the interrupt has been completely handled.
		 */
		void (*handler) (fdc_info_t *fdc) = fdc->hook;
		fdc->hook = NULL;
		trakker->out(fdc, 0x0, 0x19);
		handler(fdc);
		trakker->out(fdc, 1, 0x1a);
		trakker->out(fdc, 0xf, 0x19);
	} else {
		static int count = 0;
		/* Ack interrupt
		 */
		trakker->out(fdc, 4, 0x1a);
		if (trakker->in(fdc, 0x1e) & REG1E_ACK)
			trakker->out(fdc, REG1E_OPEN, 0x1e);

		if (++count < 3)
			TRACE(ft_t_err, "Unexpected ftape interrupt");
		if ((r_str() & PARPORT_STATUS_ACK)) {
			TRACE(ft_t_err, "can't clear it, trying harder: %x",
			      trakker->in(fdc, 0x1e));
			trakker->out(fdc, REG1E_OPEN | REG1E_ACK, 0x1e);
			if ((r_str() & PARPORT_STATUS_ACK)) {
				TRACE(ft_t_bug, 
				      "disabling interrupt to prevent hangup"
				      ", check that trakker is connected");
				w_ctr(trakker->ctr &= ~PARPORT_CONTROL_INTEN);
			}
		}
	}
	interrupt_active = 0;
 err_out:
	TRACE_EXIT;
}

static const char trakker_id[] = "trakker (ftape)";

static void trakker_grab_handler(fdc_info_t *fdc) 
{
	struct trakker_struct *trakker = fdc->data;
	TRACE_FUN(ft_t_any);
	trakker->out(fdc, REG1E_OPEN, 0x1e);
	TRACE_EXIT;
}

#define FDC_TRACING
#include "../lowlevel/ftape-real-tracing.h"
			
static const char * const trakker_init_sequence = 
	"\x04\xbf\x04\x3f\x7f\x06\xff\xbf\xff\x04\x0c\x7f\x0e\xff"
	"\x06\x7f\x04\x3f\xbf\x06\x04\x0c\xff\x04\x06\xbf\x04\x0c\x00";

int trakker_grab(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	const char * seq = trakker_init_sequence;
	TRACE_FUN(ft_t_any);

	MOD_INC_USE_COUNT;
	fdc->hook = NULL;
	
	/*  allocate I/O regions and irq first.
	 */
	TRACE_CATCH(ft_parport_claim(fdc, &trakker->parinfo),
		    MOD_DEC_USE_COUNT);

	while (*seq) {
		if (*seq & 1) {
			w_dtr(*seq);
		} else {
			w_ctr(*seq);
		}
		seq++;
	}
	trakker->ctr = 0x0c;
	trakker->mode &= (REG18_EPP | REG18_PS2 | REG18_HSHAKE | REG18_RAM);

	trakker_outb_hshake(fdc, trakker->mode, 0x18);
	trakker->out(fdc, REG1E_OPEN, 0x1e);
	fdc_disable_irq(fdc);

	/* Clear checksums
	 */
	trakker->in(fdc, 0x1b);
	trakker->in(fdc, 0x1c);
	trakker->chksum = 0;

	w_ctr(trakker->ctr |= PARPORT_CONTROL_INTEN);
	trakker_checksum(fdc);

	/* Clear possible pending interrupt?
	 */
	trakker->out(fdc, 4, 0x1a);

	fdc_enable_irq(fdc);

	fdc->hook = trakker_grab_handler;
	trakker->out(fdc, REG1E_OPEN | REG1E_ACK, 0x1e);

	if (fdc->hook == trakker_grab_handler) {
		fdc->hook = NULL;
		TRACE(ft_t_warn, "Check IRQ settings.");
	}

	TRACE_EXIT 0;
}

int trakker_release(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	TRACE_FUN(ft_t_any);

	trakker_disable_irq(fdc);

	w_ctr(trakker->ctr &= ~PARPORT_CONTROL_INTEN); /* disable IRQ */
	trakker->out(fdc, REG18_HSHAKE, 0x18);
	trakker_outb_hshake(fdc, REG1E_CLOSE, 0x1e);

	ft_parport_release(fdc, &trakker->parinfo);

	/* Turn IRQs on again, disable_irq/enable_irq must match */
	enable_irq(trakker->IRQ);

	MOD_DEC_USE_COUNT;
	TRACE_EXIT 0;
}

static void trakker_setup_dma(fdc_info_t *fdc,
			      char mode, unsigned long addr, 
			      unsigned int count)
{
	struct trakker_struct *trakker = fdc->data;
	unsigned long a = (unsigned long) addr;

	trakker->mode &= ~REG18_DMA;
	if (mode == DMA_MODE_READ) {
		trakker->mode |=  REG18_DMAREAD;
	} else {
		trakker->mode &= ~REG18_DMAREAD;
	}
	trakker->out(fdc, trakker->mode, 0x18);
	trakker->out(fdc, (a   << 1) & 0xff, 0x15);
	trakker->out(fdc, (a   >> 7) & 0xff, 0x16);
	trakker->out(fdc, (a   >>15) & 0xff, 0x17);
	trakker->out(fdc, (count   ) & 0xff, 0x13);
	trakker->out(fdc, (count>>8) & 0xff, 0x14);
	trakker->mode |= REG18_DMA;
	trakker->out(fdc, trakker->mode, 0x18);
}

static unsigned int trakker_terminate_dma(fdc_info_t *fdc,
					  ft_error_cause_t cause)
{
	struct trakker_struct *trakker = fdc->data;
	unsigned int count;

	fdc_disable_irq(fdc);
	
	count  = trakker->in(fdc, 0x13);
	count += trakker->in(fdc, 0x14) << 8;

	trakker->mode &= ~REG18_DMA;
	trakker->out(fdc, trakker->mode, 0x18);

	fdc_enable_irq(fdc);

	return (count + FT_SECTOR_SIZE - 1) / FT_SECTOR_SIZE;
}

static int trakker_alloc(fdc_info_t *fdc, buffer_struct *buff)
{
	struct trakker_struct *trakker = fdc->data;
	int i;

	if (buff == NULL) {
		if (trakker->buffer) {
			return -EINVAL;
		}
		if (ftape_vmalloc(fdc->unit,
				  &trakker->buffer, FT_BUFF_SIZE) < 0) {
			return -ENOMEM;
		}
		return 0;
	}
	for (i=0; i < 4; i++) {
		if (!(trakker->used_buffers & (1<<i))) {
			trakker->used_buffers |= (1<<i);
			buff->dma_address = i * FT_BUFF_SIZE;
			buff->virtual = trakker->buffer;
			return 0;
		}
	}
	return -ENOMEM;
}

static void trakker_free(fdc_info_t *fdc, buffer_struct *buff)
{
	struct trakker_struct *trakker = fdc->data;
	TRACE_FUN(ft_t_flow);

	if (buff != NULL) {
		int i = buff->dma_address / FT_BUFF_SIZE;
		trakker->used_buffers &= ~(1<<i);
	} else if (trakker && trakker->buffer) {
		ftape_vfree(fdc->unit, &trakker->buffer, FT_BUFF_SIZE);
		ft_parport_destroy(fdc, &trakker->parinfo);
		ftape_kfree(FTAPE_SEL(fdc->unit), &fdc->data,sizeof(*trakker));
		fdc->data = NULL;
	}
	TRACE_EXIT;
}

static void *trakker_get_deblock_buffer(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;

	if (!trakker->locked) {
		MOD_INC_USE_COUNT;
		trakker->locked = 1;
	}
	return (void *)trakker->buffer;
}

static int trakker_put_deblock_buffer(fdc_info_t *fdc, __u8 **buffer)
{
	struct trakker_struct *trakker = fdc->data;
	TRACE_FUN(ft_t_any);

	if (*buffer != trakker->buffer) {
		TRACE(ft_t_bug,
		      "Must be called with fdc_deblock_buffer %p (was %p)",
		      trakker->buffer, buffer);
		TRACE_EXIT -EINVAL;
	}
	if (trakker->locked) {
		trakker->locked = 0;
		MOD_DEC_USE_COUNT;
	}
	*buffer = NULL;
	TRACE_EXIT 0;
}

static int trakker_write_buffer(fdc_info_t *fdc, buffer_struct *buffer, __u8 **source)
{
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(trakker_put_deblock_buffer(fdc, source),);
	
	buffer->bytes = FT_SECTORS_PER_SEGMENT * FT_SECTOR_SIZE;
	trakker_write(fdc, buffer);
	TRACE_EXIT buffer->bytes;
}

static int trakker_copy_and_gen_ecc(fdc_info_t *fdc,
				    buffer_struct *buffer,
				    __u8 **source,
				    SectorMap bad_sector_map)
{
	struct memory_segment mseg;
	int bads = count_ones(bad_sector_map);
	TRACE_FUN(ft_t_any);

	TRACE_CATCH(trakker_put_deblock_buffer(fdc, source),);

	if (bads > 0) {
		TRACE(ft_t_noise, "bad sectors in map: %d", bads);
	}
	
	if (bads + 3 >= FT_SECTORS_PER_SEGMENT) {
		TRACE(ft_t_noise, "empty segment");
		TRACE_EXIT 0;      /* nothing written */
	}

	mseg.blocks = FT_SECTORS_PER_SEGMENT - bads;
	mseg.data = buffer->virtual;
	TRACE_CATCH(ftape_ecc_set_segment_parity(&mseg),
		    *source = trakker_get_deblock_buffer(fdc));
	buffer->bytes = mseg.blocks * FT_SECTOR_SIZE;
	trakker_write(fdc, buffer);
	TRACE_EXIT (mseg.blocks - 3) * FT_SECTOR_SIZE;
}

static int trakker_read_buffer(fdc_info_t *fdc, buffer_struct *buff, __u8 **destination)
{
	trakker_read(fdc, buff);
	*destination = trakker_get_deblock_buffer(fdc);
	return 0;
}

static int trakker_correct_and_copy(fdc_info_t *fdc,
				    buffer_struct *buff, __u8 **destination)
{
	int result;

	trakker_read(fdc, buff);

	if ((result = ftape_ecc_correct(fdc->ftape, buff)) >= 0) {
		*destination = trakker_get_deblock_buffer(fdc);
	} else {
		*destination = NULL;
	}
	return result;
}

static int trakker_regtest(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	int i, err=0;
	TRACE_FUN(ft_t_any);

	for (i=0; i < 16 && !err; i++) {
		/* register 0x11 should be safe to write to. */
		trakker->out(fdc, i^0xff,0x11);
		trakker->out(fdc, i     ,0x11);
		err |= (i != trakker->in(fdc, 0x11));
		trakker->out(fdc, i^0xf0,0x11);
		trakker->out(fdc, i^0x0f,0x11);
		err |= ((i^0x0f) != trakker->in(fdc, 0x11));
		trakker->in(fdc, 0x1c);	/* read checksum */
		err |= (trakker->chksum != 0);
	}

	TRACE_EXIT err?-EIO:0;
}

static int trakker_memtest(fdc_info_t *fdc, int pages, int size)
{
	int i, j, random, err=0;
	buffer_struct buffer;
	TRACE_FUN(ft_t_any);

	buffer.virtual = NULL;
	TRACE_CATCH(ftape_vmalloc(fdc->unit, &buffer.virtual, FT_BUFF_SIZE),);
	
	buffer.bytes = size;

	for (j=0; j< pages; j++){
		random = j;
		for (i=0; i< size; i++) {
			/* borrowed from libc */
			random = ((random * 1103515245)+12345) & 0x7fffffff;
			buffer.virtual[i] = (random>>16) & 0xff;
		}
		buffer.dma_address = j * FT_BUFF_SIZE;
		trakker_write(fdc, &buffer);
	}

	for (j=0; j< pages; j++){
		buffer.dma_address = j * FT_BUFF_SIZE;
		trakker_read(fdc, &buffer);

		random = j;
		for (i=0; i< size; i++) {
			/* borrowed from libc */
			random = ((random * 1103515245)+12345) & 0x7fffffff;
			if (buffer.virtual[i] != ((random>>16) & 0xff)) {
#ifdef TESTING
				TRACE(ft_t_err,
				      "%d-%04x: %2x != %2x",j,i,
				      random>>16 & 0xff, buffer.virtual[i]);
				if (err++ > 10) {
					break;
				}
#else
				err = 1;
				break;
#endif
			}
		}
	}
	ftape_vfree(fdc->unit, &buffer.virtual, FT_BUFF_SIZE);
	TRACE_EXIT (err > 0)?-EIO:0;
}

static void trakker_speedtest(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	int err=0;
	TRACE_FUN(ft_t_any);

	/*
	 * decrease the delay, until an error happens
	 */
	while(!err && trakker->parinfo.delay > MIN_DELAY) {
		trakker->parinfo.delay--;
		TRACE(ft_t_data_flow, "trakker delay := %d",
		      trakker->parinfo.delay);
		err = (trakker_regtest(fdc) || trakker_memtest(fdc, 1, 287));
	}
	if (err) { 
		trakker->parinfo.delay++;
		trakker->in(fdc, 0x1c);
		trakker->chksum =0;
	}

	if (trakker_regtest(fdc) || trakker_memtest(fdc, 4, FT_BUFF_SIZE))
		TRACE(ft_t_err,"shouldn't happen");
	TRACE(ft_t_warn,"delay: %d%s%s", trakker->parinfo.delay,
	      (trakker->mode & REG18_PS2)?" PS2":"",
	      (trakker->mode & REG18_HSHAKE)?" full handshake":"");
	TRACE_EXIT;
}

#if 0
#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

static int __init trakker_probe_irq(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	int irqs;

	TRACE_FUN(ft_t_any);

	irqs = probe_irq_on();
	w_ctr(trakker->ctr |= PARPORT_CONTROL_INTEN);
	trakker->out(fdc, REG1E_OPEN | REG1E_ACK, 0x1e);
	trakker->out(fdc, REG1E_OPEN, 0x1e);
	w_ctr(trakker->ctr &= ~PARPORT_CONTROL_INTEN);
	irqs = probe_irq_off(irqs);
	if (irqs <= 0) {
		TRACE(ft_t_err,"%s irq found", 
		      irqs < 0 ? "multiple" : "no");
		irqs = -1;
	}

	TRACE_EXIT irqs;
}

#undef FT_TRACE_ATTR
#define FT_TRACE_ATTR /**/

#endif

static int trakker_probe(fdc_info_t *fdc)
{
	struct trakker_struct *trakker = fdc->data;
	const char *seq;
	int val;
	int phase = 0;
	int result = -ENXIO;
	TRACE_FUN(ft_t_flow);

	/*
	 * Initialize the trakker struct
	 */

	trakker->parinfo.delay = MAX_DELAY;
	trakker->used_buffers  = 0;

	trakker->mode = REG18_RAM | REG18_HSHAKE;
	trakker->out = trakker_outb_hshake;
	trakker->in  = trakker_inb_hshake;
	trakker->read_mem = read_mem_spp;

	fdc->irq_level++;

	/*
	 * Now grab the parport, prepare the drive
	 */

#ifdef USE_PARPORT
	TRACE_CATCH(parport_claim_or_block(trakker->parinfo.dev),)
#else
	if (check_region(trakker->BASE, 3) < 0) {
		TRACE_ABORT(-EBUSY, ft_t_bug, 
			    "Unable to grab address 0x%x for ftape driver", 
			    trakker->BASE);
	}
	request_region(trakker->BASE, 3, trakker->parinfo.id);
#endif /* USE_PARPORT */

	for (;;) {
		TRACE(ft_t_flow, "Initialising: %d", phase);
		seq = trakker_init_sequence;
		while (*seq) {
			if (*seq & 1) {
				w_dtr(*seq);
			} else {
				w_ctr(*seq);
			}
			seq++;
		}
		trakker->ctr = 0x0c;
		
		trakker_outb_hshake(fdc, trakker->mode, 0x18);
		trakker->out(fdc, REG1E_OPEN, 0x1e);
		trakker->out(fdc, 0x0, 0x19);
		
		
		/* Clear checksums
		 */
		trakker->in(fdc, 0x1b);
		trakker->in(fdc, 0x1c);
		trakker->chksum = 0;
		trakker_checksum(fdc);

		/* Clear possible pending interrupt?
		 */
		trakker->out(fdc, 4, 0x1a);
		
		/*
		 * This is a very simple detection
		 */
		
		val = trakker->in(fdc, 0x1e);
		trakker->out(fdc, val^2, 0x1e);
		if (trakker->in(fdc, 0x1e) != (val^2)) {
			/* Try again, maybe the trakker wasn't cleanly
			 * released.  But only try again on the first
			 * run.
			 */
			TRACE(ft_t_flow, "Init-Sequence failed: %d", phase);
			if (phase++ < 1)
				goto next_try;
			break;
		}
		trakker->out(fdc, val, 0x1e);
		trakker_checksum(fdc);
		
		/* Now test if register and memory transfer works. */
		if (trakker_regtest(fdc) < 0) {
			/* Try again, maybe the trakker wasn't cleanly
			 * released.  But only try again on the first
			 * run.  
			 */
			if (phase++ < 1)
				goto next_try;
			break;
		}
		
		TRACE(ft_t_flow, "testing no handshake: %d", phase);
		if (phase <= 1) {
			phase = 2;
#ifndef FULL_HANDSHAKE
			/* Phase 1:  Test no handshake mode */
			trakker->mode &= ~REG18_HSHAKE;
			trakker->out(fdc, trakker->mode, 0x18);
			trakker->out = trakker_outb_fast;
			trakker->in = trakker_inb_fast;
			if (trakker_regtest(fdc) < 0) {
				trakker->mode |= REG18_HSHAKE;
				trakker->out(fdc, trakker->mode, 0x18);
				trakker->out = trakker_outb_hshake;
				trakker->in = trakker_inb_hshake;

				TRACE(ft_t_flow, "no handshake failed: %d", phase);
				/* Restart again, to recover from any
				 * possible error 
				 */
				goto next_try;
			}
#endif
		}

		if (trakker_memtest(fdc, 1, 8192) < 0) {
			TRACE(ft_t_err, "memory test failed.");
#if 1
			break;
#endif
		}

		TRACE(ft_t_flow, "testing ps2: %d", phase);
#ifndef DONT_USE_PS2
#ifdef USE_PARPORT
		if (phase == 2 &&
		    (trakker->parinfo.dev->port->modes & PARPORT_MODE_PCPS2))
#else
		if (phase == 2)
#endif
		{
			phase++;
			/* Phase 2:  Test PS2 mode */
			trakker->out(fdc, trakker->mode |= REG18_PS2, 0x18);
			trakker->read_mem = read_mem_ps2;
			if (trakker_memtest(fdc, 1, 287) < 0) {
				trakker->mode &= ~REG18_PS2;
				trakker->out(fdc, trakker->mode, 0x18);
				trakker->read_mem = read_mem_spp;
				
				TRACE(ft_t_flow, "ps2 failed: %d", phase);
				/* Restart again, to recover from any
				 * possible error 
				 */
				goto next_try;
			}
		}
#endif
		
		/* Now probe the best speed.  This shouldn't fail.
		 */
		trakker_speedtest(fdc);

#ifndef USE_PARPORT
		/* cH: AFAIK the parport subsystems does IRQ probing itself.
		 * JH: Yes, but only for EPP capable ports. 
		 */
		/* Last but not least, probe the irq if not given.  If
		 * we can't find any, we give up.
		 */
		if (trakker->IRQ < 0) {
			int irq;

			irq = probe_irq_on();
			w_ctr(trakker->ctr |= PARPORT_CONTROL_INTEN);
			trakker->out(fdc, REG1E_OPEN | REG1E_ACK, 0x1e);
			trakker->out(fdc, REG1E_OPEN, 0x1e);
			w_ctr(trakker->ctr &= ~PARPORT_CONTROL_INTEN);
			irq = probe_irq_off(irq);

			if (irq <= 0) {
				TRACE(ft_t_err,"%s irq found", 
				      irq < 0 ? "multiple" : "no");
				break;
			}
#if 0
#ifdef USE_PARPORT
			/* XXX This is magic.  I couldn't find a clean
			 * way to set the interrupt with parport.
			 */
			request_irq(irq, trakker_interrupt, SA_INTERRUPT,
				    trakker_id, trakker->parinfo.dev->private);
#endif
#endif
			fdc->irq = trakker->IRQ = irq;
		}
#endif
		if (trakker->IRQ > 0) {
			/* Set result to tell that everything is okay. */
			result = 0;
		}
		break;

	next_try:
		trakker->out(fdc, REG18_HSHAKE, 0x18);
		trakker_outb_hshake(fdc, REG1E_CLOSE, 0x1e);
	}

	trakker->out(fdc, REG18_HSHAKE, 0x18);
	trakker_outb_hshake(fdc, REG1E_CLOSE, 0x1e);

	fdc->irq_level--;

#ifdef USE_PARPORT	
	parport_release(trakker->parinfo.dev);
#else
	release_region(trakker->BASE, 3);
#endif
	TRACE_EXIT result;
}

static int trakker_detect(fdc_info_t *fdc)
{
	int sel = fdc->unit;
	struct trakker_struct *trakker;
	TRACE_FUN(ft_t_flow);

	trakker = ftape_kmalloc(FTAPE_SEL(fdc->unit), sizeof(*trakker), 1);
	if (!trakker) {
		TRACE_EXIT -ENOMEM;
	}
	memset(trakker, 0, sizeof(*trakker));
	trakker->magic = FT_TRAKKER_MAGIC;
	fdc->data = trakker;

	trakker->parinfo.handler = trakker_interrupt;
	trakker->parinfo.probe   = trakker_probe;
	trakker->parinfo.id      = trakker_id;

	TRACE_CATCH(ft_parport_probe(fdc, &trakker->parinfo),
		    ftape_kfree(FTAPE_SEL(fdc->unit),&fdc->data,
				sizeof(*trakker));
		    TRACE(ft_t_err,
			  "can't find trakker interface for ftape id %d",
			  sel));

	fdc->ops->out = fdc->ops->outp = trakker->out;
	fdc->ops->in  = fdc->ops->inp  = trakker->in;

	fdc->dma  = -1;
	fdc->sra  = 0;
	fdc->srb  = 1;
	fdc->dor  = 2;
	fdc->tdr  = 3;
	fdc->msr  = fdc->dsr = 4;
	fdc->fifo = 5;
	fdc->dir  = 7;
	fdc->ccr  = 7;
	fdc->dor2 = 0xffff;
	fdc->threshold  = 16;
	fdc->rate_limit = 2000;
	fdc->buffers_needed = 4;

	fdc->hook = NULL;

	TRACE_EXIT 0;
}

static fdc_operations trakker_ops = {
	{ NULL, NULL },
	"trakker",
	"Colorado \"Trakker\" parallel port tape drives",

	trakker_detect,

	trakker_grab,
	trakker_release,
		
	NULL,			/* outp - filled in later */
	NULL,			/* out  - filled in later */
	NULL,			/* inp  - filled in later */
	NULL,			/* in   - filled in later */
	
	trakker_disable_irq,
	trakker_enable_irq,
	
	trakker_setup_dma,
	trakker_terminate_dma,
	
	trakker_alloc,
	trakker_free,
	trakker_get_deblock_buffer,
	trakker_put_deblock_buffer,
		
	trakker_copy_and_gen_ecc,
	trakker_correct_and_copy,
	trakker_write_buffer,
	trakker_read_buffer,
};

/* Initialization
 */
#define GLOBAL_TRACING
#include "../lowlevel/ftape-real-tracing.h"

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

int __init trakker_register(void)
{
	TRACE_FUN(ft_t_flow);

	printk(__FILE__ ": "__func__" @ 0x%p\n", trakker_register);

	TRACE_CATCH (fdc_register(&trakker_ops),);

	TRACE_EXIT 0;
}

#undef FT_TRACE_ATTR
#define FT_TRACE_ATTR /**/

#ifdef MODULE

int trakker_unregister(void)
{
	fdc_unregister(&trakker_ops);
	return 0;
}

EXPORT_NO_SYMBOLS;

MODULE_LICENSE("GPL");

MODULE_AUTHOR(
  "(c) 1997 Jochen Hoenicke (jochen.hoenicke@informatik.uni-oldenburg.de)");
MODULE_DESCRIPTION("Ftape-interface for HP Colorado Trakker");

/* Called by modules package when installing the driver
 */
int init_module(void)
{
	return trakker_register();
}

/* Called by modules package when removing the driver 
 */
void cleanup_module(void)
{
	TRACE_FUN(ft_t_flow);
	trakker_unregister();
	printk(KERN_INFO "trakker successfully unloaded.\n");
	TRACE_EXIT;
}

#endif /* MODULE */

#ifndef MODULE

#include "../setup/ftape-setup.h"

static ftape_setup_t config_params[] __initdata = {
#ifndef USE_PARPORT
	{ "base",      ft_fdc_base,      0x0, 0xffff, 1 },
	{ "irq" ,      ft_fdc_irq,        -1,     15, 1 },
#endif
	{ "pp",        ft_fdc_parport,    -2,      3, 1 },
#if 0
/* trakker.o hardwires the threshold to 16 */
        { "thr",       ft_fdc_threshold,   1,     16, 1 },
#endif
	{ "rate",      ft_fdc_rate_limit,  0,   4000, 1 },
	{ NULL, }
};

int __init trakker_setup(char *str)
{
	static __initlocaldata int ints[6] = { 0, };

	str = get_options(str, ARRAY_SIZE(ints), ints);

	return ftape_setup_parse(str, ints, config_params);
}

#endif
