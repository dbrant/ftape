/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                    1996-2000 Claus-Justus Heine,
 *		      1997      Jochen Hoenicke.

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
 *      This file contains the low level code that communicates
 *      with the internal floppy disc controller.
 *
 */

#include <linux/module.h>
#include <linux/version.h>

#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include <linux/ftape.h>

#define FDC_TRACING
#include "../lowlevel/ftape-tracing.h"

#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/fdc-isr.h"
#include "fdc-internal.h"
#include "fdc-isapnp.h"
#include "fc-10.h"

/*      Global vars.
 */
char fdc_int_internal_src[] __initdata = "$RCSfile: fdc-internal.c,v $";
char fdc_int_internal_rev[] __initdata = "$Revision: 1.40 $";
char fdc_int_internal_dat[] __initdata = "$Date: 2000/07/25 10:36:40 $";

#ifdef fdc
#error foo
#endif
#undef fdc /* to catch this #define pit-fall 
	    * This driver should be re-entrant
	    */

/* the following are module/kernel parameters. The CONFIG_FT_FDC_BASE
 * and CONFIG_FT_FDC_IRQ parameters are shared by the parport modules.
 * So what. It is generally an idiotic idea to share an ftape device
 * node between internal, unremovable FDCs and external, parport FDCs
 *
 * We could make this __initdata() as internal FDCs doesn't change
 * during the uptime of the system. However, if used with PnP cards it
 * would be nice to be able to check again after the system is
 * running.
 */

static int ft_fdc_base[4] __initdata = {
	CONFIG_FT_FDC_BASE_0,
	CONFIG_FT_FDC_BASE_1,
	CONFIG_FT_FDC_BASE_2,
	CONFIG_FT_FDC_BASE_3 };

static int ft_fdc_irq[4] __initdata = {
	CONFIG_FT_FDC_IRQ_0,
	CONFIG_FT_FDC_IRQ_1,
	CONFIG_FT_FDC_IRQ_2,
	CONFIG_FT_FDC_IRQ_3 };

static int ft_fdc_dma[4] __initdata = {
	CONFIG_FT_FDC_DMA_0,
	CONFIG_FT_FDC_DMA_1,
	CONFIG_FT_FDC_DMA_2,
	CONFIG_FT_FDC_DMA_3,
};

static int ft_fdc_fc10[4] __initdata = {
	CONFIG_FT_FC10_0,
	CONFIG_FT_FC10_1,
	CONFIG_FT_FC10_2,
	CONFIG_FT_FC10_3
};

static int ft_fdc_mach2[4] __initdata = {
	CONFIG_FT_MACH2_0,
	CONFIG_FT_MACH2_1,
	CONFIG_FT_MACH2_2,
	CONFIG_FT_MACH2_3
};

unsigned int ft_fdc_threshold[4] __initdata = {
	CONFIG_FT_FDC_THRESHOLD_0,
	CONFIG_FT_FDC_THRESHOLD_1,
	CONFIG_FT_FDC_THRESHOLD_2,
	CONFIG_FT_FDC_THRESHOLD_3
};

unsigned int ft_fdc_rate_limit[4] __initdata = {
	CONFIG_FT_FDC_MAX_RATE_0,
	CONFIG_FT_FDC_MAX_RATE_1,
	CONFIG_FT_FDC_MAX_RATE_2,
	CONFIG_FT_FDC_MAX_RATE_3
};

static const char ftape_id[] = "ftape";

static fdc_int_t fdc_int[4];

static inline void fdc_int_setup_dma(fdc_info_t *fdc,
				     char mode, 
				     unsigned long addr, unsigned int count)
{
	/* Program the DMA controller.
	 */
	disable_dma(fdc->dma);
	clear_dma_ff(fdc->dma);
	set_dma_mode(fdc->dma, mode);
	set_dma_addr(fdc->dma, addr);
	set_dma_count(fdc->dma, count);
#ifdef GCC_2_4_5_BUG
	/*  This seemingly stupid construction confuses the gcc-2.4.5
	 *  code generator enough to create correct code.
	 */
	if (1) {
		int i;
		
		for (i = 0; i < 1; ++i) {
			ftape_udelay(1);
		}
	}
#endif
	enable_dma(fdc->dma);
}

static unsigned int fdc_int_terminate_dma(fdc_info_t *fdc,
					  ft_error_cause_t cause)
{
	unsigned int dma_residue;

#if 0
	/*  Using less preferred order of disable_dma and
	 *  get_dma_residue because this seems to fail on at least one
	 *  system if reversed!
	 */
	
	dma_residue = get_dma_residue(fdc->dma);
	disable_dma(fdc->dma);
#else
	clear_dma_ff(fdc->dma);
	disable_dma(fdc->dma);
	dma_residue = get_dma_residue(fdc->dma);
#endif

	/* data crc error can only be generated after the data has
	 * been transferred! Except when using the Ditto EZ
	 * controller! Surprise! What is this?!
	 */
	if (fdc->type != DITTOEZ && cause == data_crc_error) {
		dma_residue += FT_SECTOR_SIZE;
	}
	return dma_residue;
}

/*  DMA'able memory allocation stuff.
 */

#define ORDER (15 - PAGE_SHIFT) 
#if (PAGE_SIZE << ORDER) != FT_BUFF_SIZE
#error incorrect FT_BUFF_SIZE ?
#endif

static int fdc_int_alloc(fdc_info_t *fdc, buffer_struct *buffer)
{
	fdc_int_t *fdc_int = (fdc_int_t *)fdc->data;
	void * addr;

	if (!buffer && (!fdc_int || fdc_int->deblock_buffer)) {
		/* deblock_buffer already allocated */
		return -EINVAL;
	}
	addr = (void *) __get_dma_pages(GFP_KERNEL, ORDER);
	if (!addr) {
		return -ENOMEM;
	}
	fdc->used_dma_memory += FT_BUFF_SIZE;
	if (fdc->used_dma_memory > fdc->peak_dma_memory) {
		fdc->peak_dma_memory = fdc->used_dma_memory;
	}
	if (buffer) {
		buffer->virtual     = addr;
		buffer->dma_address = virt_to_phys(addr);
	} else {
		fdc_int->deblock_buffer = addr;
	}
	return 0;
}

static void fdc_int_free(fdc_info_t *fdc, buffer_struct *buffer)
{
	fdc_int_t *fdc_int = (fdc_int_t *)fdc->data;
	void *addr = NULL;
	TRACE_FUN(ft_t_any);
	
	if (buffer) {
		addr = buffer->virtual;
		buffer->virtual = NULL;
	} else if (fdc_int != NULL) {
		addr = fdc_int->deblock_buffer;
		fdc_int->deblock_buffer = NULL;
		if (fdc_int->locked) {
			module_put(THIS_MODULE);
		}
	}
	if (addr != NULL) {
		free_pages((unsigned long) addr, ORDER);	
		fdc->used_dma_memory -= FT_BUFF_SIZE;
	}
	TRACE_EXIT;
}

static void *fdc_int_get_deblock_buffer(fdc_info_t *fdc)
{
	fdc_int_t *fdc_int = (fdc_int_t *)fdc->data;
	TRACE_FUN(ft_t_any);

	if (!fdc_int->locked) {
		if (!try_module_get(THIS_MODULE)) {
			TRACE_EXIT NULL;
		}
		fdc_int->locked = 1;
	}
	TRACE_EXIT fdc_int->deblock_buffer;
}

static int fdc_int_put_deblock_buffer(fdc_info_t *fdc, __u8 **buffer)
{
	fdc_int_t *fdc_int = (fdc_int_t *)fdc->data;
	TRACE_FUN(ft_t_any);

	if (*buffer != fdc_int->deblock_buffer) {
		TRACE(ft_t_bug,
		      "Must be called with fdc_deblock_buffer %p (was %p)",
		      fdc_int->deblock_buffer, *buffer);
		TRACE_EXIT -EINVAL;
	}
	if (fdc_int->locked) {
		fdc_int->locked = 0;
		module_put(THIS_MODULE);
	}
	*buffer = NULL;
	TRACE_EXIT 0;
}

/* When using same dma channel or irq as standard fdc, we need to
 * disable the dma-gate on the std fdc. This couldn't be done in the
 * floppy driver as some laptops are using the dma-gate to enter a low
 * power or even suspended state :-(
 *
 * We must make sure that the floppy driver won't enable the dma gate
 * again, therefore we claim the floppy DOR here, so that any
 * check_region will fail.
 */

static int fdc_int_request_regions(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_flow);

	if (fdc->dor2 != 0xffff) {
		if (!request_region(fdc->sra, 8, "fdc (ft)")) {
#ifndef BROKEN_FLOPPY_DRIVER
			TRACE_EXIT -EBUSY;
#else
			TRACE(ft_t_warn,
			      "address 0x%03x occupied (by floppy driver?), "
			      "using it anyway", fdc->sra);
#endif
		}
	} else {
		if (!request_region(fdc->sra, 6, "fdc (ft)") || 
		    !request_region(fdc->dir, 1, "fdc DIR (ft)")) {
#ifndef BROKEN_FLOPPY_DRIVER
			if (request_region(fdc->sra, 6, "fdc (ft)"))
				release_region(fdc->sra, 6);
			TRACE_EXIT -EBUSY;
#else
			TRACE(ft_t_warn,
			      "address 0x%03x occupied (by floppy driver?), "
			      "using it anyway", fdc->sra);
#endif
		}
	}
	if (fdc->sra != 0x3f0 && (fdc->dma == 2 || fdc->irq == 6)) {
		if (!request_region(0x3f0, 6, "standard fdc (ft)") ||
		    !request_region(0x3f0 + 7, 1, "standard fdc DIR (ft)")) {
#ifndef BROKEN_FLOPPY_DRIVER
			if (request_region(0x3f0, 6, "standard fdc (ft)"))
				release_region(0x3f0, 6);
			/* Also cleanup previous regions */
			if (fdc->dor2 != 0xffff) {
				release_region(fdc->sra, 8);
			} else {
				release_region(fdc->sra, 6);
				release_region(fdc->dir, 1);
			}
			TRACE_EXIT -EBUSY;
#else
			TRACE(ft_t_warn,
			      "address 0x%03x occupied (by floppy driver?), "
			      "using it anyway", 0x3f0);
#endif
		}
	}
	TRACE_EXIT 0;
}

static void fdc_int_release_regions(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_flow);

	if (fdc->dor2 != 0xffff) {
		release_region(fdc->sra, 8);
	} else {
		release_region(fdc->sra, 6);
		release_region(fdc->dir, 1);
	}
	if (fdc->sra != 0x3f0 && (fdc->dma == 2 || fdc->irq == 6)) {
		release_region(0x3f0, 6);
		release_region(0x3f0 + 7, 1);
	}
	TRACE_EXIT;
}

#define GLOBAL_TRACING
#include "../lowlevel/ftape-real-tracing.h"

static irqreturn_t ftape_interrupt(int irq, void *dev_id)
{
	fdc_info_t *fdc = (fdc_info_t *)dev_id;
	TRACE_FUN(ft_t_fdc_dma);

	if (!fdc) {
		TRACE(ft_t_bug, "BUG: Spurious interrupt (no fdc data)");
		return IRQ_NONE;
	}
	if (fdc->magic != FT_FDC_MAGIC) {
		TRACE(ft_t_bug, "BUG: Magic number mismatch (0x%08x/0x%08x), "
		      "prepare for Armageddon", FT_FDC_MAGIC, fdc->magic);
		return IRQ_NONE;
	}
	if (fdc->irq != irq) {
		TRACE(ft_t_bug, "BUG: Wrong IRQ number (%d/%d)", irq, fdc->irq);
		return IRQ_NONE;
	}

	if (fdc->hook) {
		void (*handler) (fdc_info_t *fdc) = fdc->hook;
		fdc->hook = NULL;
		handler(fdc);
		return IRQ_HANDLED;
	} else {
		TRACE(ft_t_bug, "Unexpected ftape interrupt");
		return IRQ_NONE;
	}
}

#define FDC_TRACING
#include "../lowlevel/ftape-real-tracing.h"

static volatile int fdc_int_got_irq = 0;

/*  grab handler. Used to determine whether we really have found the
 *  FDC.
 */
static void fdc_int_grab_handler(fdc_info_t *fdc)
{
	fdc_int_got_irq ++;
}

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

static void __init fdc_int_cause_reset(fdc_info_t *fdc)
{
	const __u8 fdc_ctl = fdc->unit | FDC_DMA_MODE;

	udelay(FT_RQM_DELAY);
	if (fdc->dor2 != 0xffff && fdc->type != fc10) {  /* Mountain MACH-2 */
		outb_p(fdc_ctl, fdc->dor);
		outb_p(fdc_ctl, fdc->dor2);
	} else {
		outb_p(fdc_ctl, fdc->dor);
	}
	udelay(10);
	if (fdc->dor2 != 0xffff && fdc->type != fc10) {  /* Mountain MACH-2 */
		outb_p(fdc_ctl | FDC_RESET_NOT, fdc->dor);
		outb_p(fdc_ctl | FDC_RESET_NOT, fdc->dor2);
	} else {
		outb_p(fdc_ctl | FDC_RESET_NOT, fdc->dor);
	}
	udelay(10);
}

static int __init fdc_int_probe_irq(fdc_info_t *fdc)
{
	int irq;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(fdc_int_request_regions(fdc),);
	irq = probe_irq_on();

	fdc_int_cause_reset(fdc);
	
	irq = probe_irq_off(irq);

	if (irq <= 0) {
		TRACE(ft_t_err,"%s irq found", 
		      irq < 0 ? "multiple" : "no");
		irq = -1;
	}

	fdc_int_release_regions(fdc);
	TRACE_EXIT irq;
}

#undef FT_TRACE_ATTR
#define FT_TRACE_ATTR /**/

static int fdc_int_grab(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_flow);

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;
	fdc->hook = fdc_int_grab_handler;
	TRACE_CATCH(fdc_int_request_regions(fdc),
		    module_put(THIS_MODULE));
	/*  Get fast interrupt handler.
	 */
#if LINUX_VERSION_CODE >= KERNEL_VER(1,3,70)
	if (request_irq(fdc->irq, ftape_interrupt,
			0, ftape_id, fdc)) {
		fdc_int_release_regions(fdc);
		module_put(THIS_MODULE); 
		TRACE_ABORT(-EBUSY, ft_t_bug,
			    "Unable to grab IRQ%d for ftape driver",
			    fdc->irq);
	}
#else
	if (request_irq(fdc->irq, ftape_interrupt, SA_INTERRUPT,
			ftape_id)) {
		fdc_int_release_regions(fdc);
		module_put(THIS_MODULE);
		TRACE_ABORT(-EBUSY, ft_t_bug,
			    "Unable to grab IRQ%d for ftape driver",
			    fdc->irq);
	}
#endif
	if (request_dma(fdc->dma, ftape_id)) {
#if LINUX_VERSION_CODE >= KERNEL_VER(1,3,70)
		free_irq(fdc->irq, fdc);
#else
		free_irq(fdc->irq);
#endif
		fdc_int_release_regions(fdc);
		module_put(THIS_MODULE);
		TRACE_ABORT(-EBUSY, ft_t_bug,
			    "Unable to grab DMA%d for ftape driver",
			    fdc->dma);		
	}
	if (fdc->sra != 0x3f0 && (fdc->dma == 2 || fdc->irq == 6)) {
		/* Using same dma channel or irq as standard fdc, need
		 * to disable the dma-gate on the std fdc. This
		 * couldn't be done in the floppy driver as some
		 * laptops are using the dma-gate to enter a low power
		 * or even suspended state :-(
		 */
		outb_p(FDC_RESET_NOT, 0x3f2);
		TRACE(ft_t_noise, "DMA-gate on standard fdc disabled");
	}
	TRACE_EXIT 0;
}

static int fdc_int_release(fdc_info_t *fdc)
{
	TRACE_FUN(ft_t_flow);

	disable_dma(fdc->dma);	/* just in case... */
	free_dma(fdc->dma);
#if LINUX_VERSION_CODE >= KERNEL_VER(1,3,70)
	free_irq(fdc->irq, fdc);
#else
	free_irq(fdc->irq);
#endif
	if (fdc->sra != 0x3f0 && (fdc->dma == 2 || fdc->irq == 6)) {
		/* Using same dma channel as standard fdc, need to
		 * disable the dma-gate on the std fdc. This couldn't
		 * be done in the floppy driver as some laptops are
		 * using the dma-gate to enter a low power or even
		 * suspended state :-(
		 */
		outb_p(FDC_RESET_NOT | FDC_DMA_MODE, 0x3f2);
		TRACE(ft_t_noise, "DMA-gate on standard fdc enabled again");
	}
	fdc_int_release_regions(fdc);
	module_put(THIS_MODULE);
	TRACE_EXIT 0;
}

static void fdc_int_outp (fdc_info_t *fdc,
			  unsigned char value, unsigned short reg)
{
	outb_p(value, reg);
}

static void fdc_int_out (fdc_info_t *fdc,
			 unsigned char value, unsigned short reg)
{
	outb(value, reg);
}

static unsigned char fdc_int_inp (fdc_info_t *fdc,
				  unsigned short reg)
{
	return inb_p(reg);
}

static unsigned char fdc_int_in (fdc_info_t *fdc,
				 unsigned short reg)
{
	return inb(reg);
}

static void fdc_int_disable_irq(fdc_info_t *fdc) 
{
	disable_irq(fdc->irq);
}

static void fdc_int_enable_irq(fdc_info_t *fdc) 
{
	enable_irq(fdc->irq);
}

static int fdc_int_write_buffer(fdc_info_t *fdc,
				buffer_struct *buffer, __u8 **source)
{
	fdc_int_t *fdc_int = (fdc_int_t *)fdc->data;
	void *tmp = *source;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(fdc_int_put_deblock_buffer(fdc, source),);

	/* Instead of copying the data twice, we just flip pointers.
	 * Of course, the calling function must be very careful not to
	 * write any more to "source" after this flip.
	 */
	fdc_int->deblock_buffer = buffer->virtual;
	buffer->virtual         = tmp;
	buffer->dma_address     = virt_to_phys(buffer->virtual);
	TRACE_EXIT 0;
}

static int fdc_int_copy_and_gen_ecc(fdc_info_t *fdc,
				    buffer_struct *buffer,
				    __u8 **source,
				    SectorMap bad_sector_map)
{
	struct memory_segment mseg;
	int bads = count_ones(bad_sector_map);
	TRACE_FUN(ft_t_flow);

	if (bads > 0) {
		TRACE(ft_t_noise, "bad sectors in map: %d", bads);
	}
	
	TRACE_CATCH(fdc_int_write_buffer(fdc, buffer, source),);

	if (bads + 3 >= FT_SECTORS_PER_SEGMENT) {
		TRACE(ft_t_noise, "empty segment");
		TRACE_EXIT 0;  /* skip entire segment */
	} else {
		mseg.blocks = FT_SECTORS_PER_SEGMENT - bads;
		mseg.data = buffer->virtual;
		TRACE_CATCH(ftape_ecc_set_segment_parity(&mseg),
			    /* will not happen unless ECC code is
			     * broken source already has been set to
			     * NULL if this happesn
			     */
			    *source = buffer->virtual;
			    buffer->virtual = fdc_int->deblock_buffer;
			    fdc_int->deblock_buffer = *source;
			    buffer->dma_address = virt_to_phys(buffer->virtual);
			    (void)fdc_int_get_deblock_buffer(fdc));
		buffer->bytes = mseg.blocks * FT_SECTOR_SIZE;
		TRACE_EXIT (mseg.blocks - 3) * FT_SECTOR_SIZE;
	}
}

static int fdc_int_read_buffer(fdc_info_t *fdc,
			       buffer_struct *buff, __u8 **destination)
{
	fdc_int_t *fdc_int = (fdc_int_t *)fdc->data;
	void *tmp;

	tmp                     = buff->virtual;
	buff->virtual           = fdc_int->deblock_buffer;
	fdc_int->deblock_buffer = tmp;
	*destination            = fdc_int_get_deblock_buffer(fdc);
	buff->dma_address       = virt_to_phys(buff->virtual);
	return 0;
}

static int fdc_int_correct_and_copy(fdc_info_t *fdc,
				    buffer_struct *buff, __u8 **destination)
{
	int result;

	result = ftape_ecc_correct(fdc->ftape, buff);

	if (result >= 0) {
		(void)fdc_int_read_buffer(fdc, buff, destination);
	} else {
		*destination = NULL;
	}
	return result;
}

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

static void __init fdc_int_config(fdc_info_t *fdc)
{
	int sel = fdc->unit;
	TRACE_FUN(ft_t_flow);

	/* fill in configuration parameters */
	fdc->sra        = (__u16)ft_fdc_base[sel];
	fdc->irq        = (unsigned int)ft_fdc_irq[sel];
	fdc->dma        = (unsigned int)ft_fdc_dma[sel];
	fdc->threshold  = ft_fdc_threshold[sel];
	fdc->rate_limit = ft_fdc_rate_limit[sel];	
	switch (fdc->rate_limit) {
	case 250:
	case 500:
	case 1000:
	case 2000:
	case 3000:
	case 4000:
		break;
	default:
		TRACE(ft_t_warn, "\n"
		      KERN_INFO "Configuration error, wrong rate limit (%d)."
		      KERN_INFO "Falling back to %d Kbps", 
		      fdc->rate_limit, 4000);
		fdc->rate_limit = 4000;
		break;
	}
	if (ft_fdc_fc10[sel]) {
		if (fdc->sra == 0xffff) fdc->sra = 0x180;
		if (fdc->irq == -1) fdc->irq = 9;
		if (fdc->dma == -1) fdc->dma  = 3;
	} else if (ft_fdc_mach2[sel]) {
		if (fdc->sra == 0xffff) fdc->sra = 0x1e0;
		if (fdc->irq == -1) fdc->irq = 6;
		if (fdc->dma == -1) fdc->dma = 2;
	}
	fdc->srb  = fdc->sra + 1;
	fdc->dor  = fdc->sra + 2;
	fdc->tdr  = fdc->sra + 3;
	fdc->msr  = fdc->dsr = fdc->sra + 4;
	fdc->fifo = fdc->sra + 5;
	fdc->dir  = fdc->ccr = fdc->sra + 7;
	fdc->dor2 = (ft_fdc_mach2[sel]) ? fdc->sra + 6 : 0xffff;
	
	/* three buffers are enough. We don't make this a module
	 * parameter
	 */
	fdc->buffers_needed = CONFIG_FT_NR_BUFFERS;

	TRACE_EXIT;
}

static int __init fdc_int_detect(fdc_info_t *fdc)
{
	int sel = fdc->unit;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_info, "called with count %d", sel);
	fdc_int_config(fdc);
	memset(&fdc_int[sel], 0, sizeof(fdc_int[sel]));
	fdc->data           = &fdc_int[sel];
	if (ft_fdc_fc10[sel]) {
		int fc_type;

		fdc->dor2 = fdc->sra + 6;
		TRACE_CATCH(fdc_int_request_regions(fdc),);
		fc_type = fc10_enable(fdc);
		fdc_int_release_regions(fdc);
		if (!fc_type) {
			TRACE_ABORT(-ENXIO, ft_t_warn,
				    "FC-10/20 controller not found");
		}
		TRACE(ft_t_warn, "FC-%c0 controller found", '0' + fc_type);
		fdc->type = fc10;
	}
#if defined(CONFIG_ISAPNP) || \
    (defined(CONFIG_ISAPNP_MODULE) && defined(CONFIG_FT_INTERNAL_MODULE))
	else {
		TRACE_CATCH(fdc_int_isapnp_init(fdc),);
	}
#endif	
	/* FIXME: autoprobe default parameters.
	 */
	if (fdc->sra == 0xffff) {
		TRACE(ft_t_err, "Need the I/O port base address of the FDC");
		TRACE_EXIT -ENXIO;
	}
	if (fdc->irq == -1) {
		fdc->irq = fdc_int_probe_irq(fdc);
		if (fdc->irq == -1) {
			TRACE_EXIT -ENXIO;
		}
		/* the grab/release pair tests whether we can get all resources
		 */
		TRACE_CATCH(fdc_int_grab(fdc),);
		TRACE_CATCH(fdc_int_release(fdc),);
	}
	/* back to not probing for the hardware. We gained nothing if
	 * we would really probe for the FDC here, e.g. by resetting
	 * it. Ftape will fail later when we really try to access the
	 * hardware. AND: in case we are compiled into the kernel we
	 * can handle PnP cards this way. The FC-10/FC-20 isn't PnP,
	 * so we loose nothing when its initialization code is in the
	 * .init section.
	 */
	else {
		TRACE_CATCH(fdc_int_grab(fdc),);
		TRACE_CATCH(fdc_int_release(fdc),);
	}

	TRACE(ft_t_warn, "fdc[%d] base: 0x%04x, irq: %d, dma: %d",
	      sel, fdc->sra, fdc->irq, fdc->dma);

	TRACE_EXIT 0;
}

#ifdef CONFIG_FT_INTERNAL
static int fdc_int_dummy_detect(fdc_info_t *fdc)
{
	return -ENXIO;
}
#endif

struct fdc_operations fdc_internal_ops = {
	{ NULL, NULL },
	"ftape-internal",
	"driver for internal FDC controllers (FC-10/FC-20/82078/Ditto EZ)",

	fdc_int_detect,
	
	fdc_int_grab,
	fdc_int_release,
		
	fdc_int_out,
	fdc_int_outp,
	fdc_int_in,
	fdc_int_inp,
		
	fdc_int_disable_irq,
	fdc_int_enable_irq,
		
	fdc_int_setup_dma,
	fdc_int_terminate_dma,
		
	fdc_int_alloc,
	fdc_int_free,
	fdc_int_get_deblock_buffer,
	fdc_int_put_deblock_buffer,
		
	fdc_int_copy_and_gen_ecc,
	fdc_int_correct_and_copy,
	fdc_int_write_buffer,
	fdc_int_read_buffer,
};

#ifdef FT_TRACE_ATTR
# undef FT_TRACE_ATTR
#endif
#define FT_TRACE_ATTR __initlocaldata

int __init fdc_internal_register(void)
{
	int result;

	printk(__FILE__": %s @ 0x%p.\n", __func__, fdc_internal_register);

	/* try to register ...
	 */
	if ((result = fdc_register(&fdc_internal_ops)) < 0) {
		return result;
	}
#ifdef CONFIG_FT_INTERNAL
	/* No need to probe again.
	 */
	fdc_internal_ops.detect = fdc_int_dummy_detect;
#endif
	return 0;
}

#ifdef MODULE
#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,18)
#define FT_MOD_PARM(var,type,desc) \
	module_param_array(var, int, NULL, 0644); MODULE_PARM_DESC(var,desc)

FT_MOD_PARM(ft_fdc_base,       "1-4i", "I/O port base addresses");
FT_MOD_PARM(ft_fdc_irq,        "1-4i", "IRQ lines");
FT_MOD_PARM(ft_fdc_dma,        "1-4i", "DMA channels (not always needed)");
FT_MOD_PARM(ft_fdc_threshold,  "1-4i", "FDC FIFO thresholds");
FT_MOD_PARM(ft_fdc_rate_limit, "1-4i", "FDC data transfer rate limits");
FT_MOD_PARM(ft_fdc_fc10,       "1-4i", 
	    "If non-zero, probe for a Colorado FC-10/FC-20 controller.");
FT_MOD_PARM(ft_fdc_mach2,      "1-4i",
	    "If non-zero, treat FDC as a Mountain MACH-2 controller.");

MODULE_LICENSE("GPL");

MODULE_AUTHOR(
	"(c) 1997-2000 Claus-Justus Heine <heine@instmath.rwth-aachen.de>");
MODULE_DESCRIPTION(
	"Ftape interface to the internal fdc.");
#endif


/* Called by modules package when installing the driver
 */
int init_module(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VER(1,1,85)
# if LINUX_VERSION_CODE < KERNEL_VER(2,1,18)
	register_symtab(0); /* remove global ftape symbols */
# else
	/* EXPORT_NO_SYMBOLS is deprecated - no global exports by default */
# endif
#endif
	return fdc_internal_register();
}

/* Called by modules package when removing the driver 
 */
void cleanup_module(void)
{
#if defined(CONFIG_ISAPNP) || defined(CONFIG_ISAPNP_MODULE)
	fdc_int_isapnp_disable();
#endif
	fdc_unregister(&fdc_internal_ops);
        printk(KERN_INFO "fdc_internal successfully unloaded.\n");
}
#endif /* MODULE */

#ifndef MODULE

#include "../setup/ftape-setup.h"

static ftape_setup_t config_params[] __initdata = {
#ifdef CONFIG_ISAPNP
	{ "base",      ft_fdc_base,       -1, 0xffff, 1 },
	{ "dma",       ft_fdc_dma,        -1,      3, 0 },
#else
	{ "base",      ft_fdc_base,      0x0, 0xffff, 1 },
	{ "dma",       ft_fdc_dma,         0,      3, 0 },
#endif
	{ "irq",       ft_fdc_irq,        -1,     15, 1 },
        { "thr",       ft_fdc_threshold,   1,     16, 1 },
	{ "rate",      ft_fdc_rate_limit,  0,   4000, 1 },
	{ "fc10",      ft_fdc_fc10,        0,      1, 0 },
	{ "mach2",     ft_fdc_mach2,       0,      1, 0 },
	{ NULL, }
};

int __init ftape_internal_setup(char *str)
{
	static __initlocaldata int ints[6] = { 0, };
#ifdef CONFIG_ISAPNP
	int result;

	result = fdc_int_isapnp_setup(str);
	if (result <= 0) {
		return result;
	}
#endif
	str = get_options(str, ARRAY_SIZE(ints), ints);

	return ftape_setup_parse(str, ints, config_params);
}

#endif
