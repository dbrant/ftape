/*
 *      Copyright (C) 2000 Claus-Justus Heine

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
 *      This file contains code for auto-detection of ISA PnP floppy
 *      disk controllers. Only the Iomega Ditto EZ controller is
 *      supported for now
 *
 */


#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>

#include <linux/isapnp.h>
#include <linux/ftape.h>

#define FDC_TRACING
#include "../lowlevel/ftape-tracing.h"

#include "../lowlevel/fdc-io.h"

#include "fdc-isapnp.h"

/* The data to pass to isapnp_find_device() and isapnp_find_card().
 * BTW, shouldn't this be an "__u16" instead of "unsigned short"?
 */
static unsigned short known_vendors[] = {
	ISAPNP_VENDOR('I', 'O', 'M'),
};

static unsigned short known_devices[] = {
	ISAPNP_DEVICE(0x0040),
};

static unsigned short known_functions[] = {
	ISAPNP_FUNCTION(0x0040),
};

static unsigned short ft_fdc_pnp_ven[4] = {
#if CONFIG_FT_PNP_FDC_0
	-1,
#else
	-2, 
#endif
#if CONFIG_FT_PNP_FDC_1
	-1,
#else
	-2, 
#endif
#if CONFIG_FT_PNP_FDC_2
	-1,
#else
	-2, 
#endif
#if CONFIG_FT_PNP_FDC_3
	-1,
#else
	-2, 
#endif
};

static unsigned short ft_fdc_pnp_dev[4] = {
	-2, -2, -2, -2
};

static unsigned short ft_fdc_pnp_fct[4] = {
	-2, -2, -2, -2
};

#ifdef MODULE
static struct pci_dev *pnp_devices[4] = { NULL, };
static char *ft_fdc_pnp_vendor[4] = { NULL, };

#define FT_MOD_PARM(var,type,desc) \
	MODULE_PARM(var,type); MODULE_PARM_DESC(var,desc)
FT_MOD_PARM(ft_fdc_pnp_vendor, "1-4s", "PnP device vendor (three letters)");
FT_MOD_PARM(ft_fdc_pnp_dev, "1-4h", "PnP device number");
FT_MOD_PARM(ft_fdc_pnp_fct, "1-4h", "PnP function number");
#endif

static int find_isapnp_fdc(fdc_info_t *fdc,
				  unsigned short ven,
				  unsigned short dev,
				  unsigned short fct)
{
	struct pci_bus *card;
	struct pci_dev *device;
	TRACE_FUN(ft_t_flow);

	if ((card = isapnp_find_card(ven, dev, NULL)) == NULL) {
		TRACE(ft_t_err, "Cannot find PnP card");
		TRACE_EXIT -ENXIO; /* not found */
	}
	if ((device = isapnp_find_dev(card, ven, fct, NULL)) == NULL) {
		TRACE(ft_t_err, "Cannot find PnP device");
		TRACE_EXIT -ENXIO; /* not found */
	}
	TRACE(ft_t_info, "%s detected",
	      card->name[0] ? card->name : "PnP fdc");
	if (device->prepare(device) < 0) {
		TRACE(ft_t_err, "Cannot prepare PnP device");
		TRACE_EXIT -EAGAIN;
	}
	if (!(device->resource[0].flags & IORESOURCE_IO)) {
		TRACE(ft_t_err, "I/O resource seems not to be an I/O port");
		TRACE_EXIT -ENXIO; /* not found */
	}
	if (!(device->dma_resource[0].flags & IORESOURCE_DMA)) {
		TRACE(ft_t_err, "DMA resource seems not to be an DMA channel");
		TRACE_EXIT -ENXIO; /* not found */
	}
	if (!(device->irq_resource[0].flags & IORESOURCE_IRQ)) {
		TRACE(ft_t_err, "IRQ resource seems not to be an IRQ line");
		TRACE_EXIT -ENXIO; /* not found */
	}
	if (device->ro &&
	    (fdc->dma != -1 ||
	     fdc->irq != -1 ||
	     fdc->sra != (__u16)-1)) {
		TRACE(ft_t_err, "Can't modify read-only PnP resources");
		TRACE_EXIT -EIO; /* was read-only */
	}
	if (fdc->sra != (__u16)-1) {
		isapnp_resource_change(&device->resource[0], fdc->sra, 8);
	}
	if (fdc->dma != -1) {
		isapnp_resource_change(&device->dma_resource[0], fdc->dma, 1);
	}
	if (fdc->irq != -1) {
		isapnp_resource_change(&device->irq_resource[0], fdc->irq, 1);
	}
#if 0
	device->deactivate(device);
#endif
	TRACE_CATCH(device->activate(device),);

	/* check whether we got the thing relocated to desired places ... */
	if (fdc->sra != (__u16)-1 && fdc->sra != device->resource[0].start) {
		TRACE(ft_t_err,
		      "Could not relocate I/O port to 0x%04x\n", fdc->sra);
		device->deactivate(device);
		TRACE_EXIT -EIO;
	}
	if (fdc->dma != -1 && fdc->dma != device->dma_resource[0].start) {
		TRACE(ft_t_err,
		      "Could not relocate dma channel to %d\n", fdc->dma);
		device->deactivate(device);
		TRACE_EXIT -EIO;
	}
	if (fdc->irq != -1 && fdc->irq != device->irq_resource[0].start) {
		TRACE(ft_t_err,
		      "Could not relocate IRQ to %d", fdc->irq);
		device->deactivate(device);
		TRACE_EXIT -EIO;
	}
	fdc->sra  = device->resource[0].start;
	fdc->srb  = fdc->sra + 1;
	fdc->dor  = fdc->sra + 2;
	fdc->tdr  = fdc->sra + 3;
	fdc->msr  = fdc->dsr = fdc->sra + 4;
	fdc->fifo = fdc->sra + 5;
	fdc->dor2 = fdc->sra + 6; /* should be unused in most cases */
	fdc->dir  = fdc->ccr = fdc->sra + 7;
	fdc->dma  = device->dma_resource[0].start;
	fdc->irq  = device->irq_resource[0].start;
#ifdef MODULE
	pnp_devices[fdc->unit] = device;
#endif
	TRACE(ft_t_info, "ISAPnP reports '%s' at i/o %#x, irq %d, dma %d",
	      card->name[0] ? card->name : "PnP fdc",
	      fdc->sra, fdc->irq, fdc->dma);

	TRACE_EXIT 0;
}

/* try to find and possibly configure a PnP device. If user specifies
 * irqs, dma and I/O base as command line argument, then we try to tell
 * the isapnp layer to configure the device accordingly.
 */
int fdc_int_isapnp_init(fdc_info_t *fdc)
{
	int i;
	int sel = fdc->unit;
	TRACE_FUN(ft_t_flow);

#ifdef MODULE
	/* first translate string module parameter */
	if (ft_fdc_pnp_vendor[sel] != NULL) {
		if (strcmp(ft_fdc_pnp_vendor[sel], "a") == 0) {
			ft_fdc_pnp_ven[sel] = -1;
		} else if (strcmp(ft_fdc_pnp_vendor[sel], "n") == 0 ||
			   strlen(ft_fdc_pnp_vendor[sel]) != 3) {
			ft_fdc_pnp_ven[sel] = -2;
		} else {
			ft_fdc_pnp_ven[sel] =
				ISAPNP_VENDOR(ft_fdc_pnp_vendor[sel][0],
					      ft_fdc_pnp_vendor[sel][1],
					      ft_fdc_pnp_vendor[sel][2]);
		}
	}
#endif
	if (ft_fdc_pnp_ven[sel] == (unsigned short)-2) {
		TRACE(ft_t_info, "ISAPnP disabled for device %d", sel);
		TRACE_EXIT 0; /* nothing to do */
	}
	if (ft_fdc_pnp_ven[sel] == (unsigned short)-1) {
		/* attempt to figure out ourselves */
		for (i = 0; i < NR_ITEMS(known_vendors); i++) {
			if (find_isapnp_fdc(fdc,
					    known_vendors[i],
					    known_devices[i],
					    known_functions[i]) >= 0) {
				TRACE_EXIT 0;
			}
		}
		TRACE(ft_t_info, "No ISAPnP device found");
		TRACE_EXIT -EIO; /* no success */
	}
	TRACE_CATCH(find_isapnp_fdc(fdc,
				    ft_fdc_pnp_ven[sel],
				    ft_fdc_pnp_dev[sel],
				    ft_fdc_pnp_fct[sel]),);
	TRACE_EXIT 0;
}

#ifdef MODULE
void fdc_int_isapnp_disable(void)
{
	int sel;
	TRACE_FUN(ft_t_flow);
	
	for (sel = 0; sel < 4; sel++) {
		if (pnp_devices[sel]) {
			TRACE(ft_t_info, "Disabling ISA PnP device");
			pnp_devices[sel]->deactivate(pnp_devices[sel]);
			pnp_devices[sel] = NULL;
		}
	}
	TRACE_EXIT;
}
#endif

#ifndef MODULE
/* "str" is supposed to be of the form
 * VENxxxxyyyy,VENxxxxyyyy,VENxxxxyyyy,VENxxxxyyyy,pnpdev
 *
 * wher VEN is either a vendor id (e.g. IOM) and xxxx is the hexadecimal
 * device id (for the Ditto EZ this would be 0040). yyyy is the hexadecimal
 * device function (for boards with more than one device)
 *
 * The position corresponds to the ftape device number. Instead of
 * "VENxxx" either (literal) "a" or "n" are accepted, where "n"
 * disables searching of PnP cards for this device, and "a" means
 * "auto", i.e. search for all know PnP devices and assign the first
 * one found. In case a single number > 0 is used, it is treated as
 * offset into the array of known cards, i.e. a "1" would search for
 * the Ditto EZ controller.
 *
 * Retzurn
 *
 */
int fdc_int_isapnp_setup(char *str)
{
	size_t offset = strlen(str) - strlen("pnpdev");
	TRACE_FUN(ft_t_flow);

	if (strcmp(str + offset, "pnpdev") == 0) {
		/* this one is for us, now break into substrings */
		char *cur  = str;
		char *last = strchr(cur, ',');
		char *endptr;
		int   i    = 0;
		int   dev;
		unsigned long pnpdevfct;
		unsigned short devw, fctw;

		while (last && i <= FTAPE_SEL_D) {
			dev = simple_strtol(cur, &endptr, 0);
			TRACE(ft_t_noise, "Got dev %d", dev);
			if (cur != endptr) {
				if (dev < 0) {
					ft_fdc_pnp_ven[i] = dev;
				} else if (dev < NR_ITEMS(known_vendors)) {
					ft_fdc_pnp_ven[i] = known_vendors[dev];
					ft_fdc_pnp_dev[i] = known_devices[dev];
					ft_fdc_pnp_fct[i] = known_functions[dev];
				} else {
					ft_fdc_pnp_ven[i] = -2; /* disable */
				}
				i++;
			} else {
				/* use first three letters to create
				 * PnP vendor, next four digits to create
				 * PnP device, last four digits to create
				 * PnP function
				 */
				if (last - cur != 11) {
					TRACE(ft_t_err,
					      "Botched ftape option %s", str);
					TRACE_EXIT -EINVAL;
				}
				ft_fdc_pnp_ven[i] = 
					ISAPNP_VENDOR(cur[0], cur[1], cur[2]);
				pnpdevfct = simple_strtoul(cur + 3, NULL, 16);
				devw = (pnpdevfct >> 16) & 0xffff;
				ft_fdc_pnp_dev[i] = ISAPNP_DEVICE(devw);
				fctw = pnpdevfct & 0xffff;
				ft_fdc_pnp_fct[i] = ISAPNP_FUNCTION(fctw);
				TRACE(ft_t_noise,
				      "ven: %c%c%c, dev: 0x%04x, fct: 0%04x"
				      "ven/dev/fct = 0x%04x/0x%04x/0x%04x",
				      cur[0], cur[1], cur[2], devw, fctw,
				      ft_fdc_pnp_ven[i],
				      ft_fdc_pnp_dev[i],
				      ft_fdc_pnp_fct[i]);
				i ++;
			}
			cur = last + 1;
			last = strchr(cur, ',');
		}
		if (last) {
			TRACE(ft_t_err, "Too many parameters in string %s\n",
			      str);
			TRACE_EXIT -EINVAL;
		} else {
			TRACE_EXIT 0;
		}
	}
	TRACE_EXIT 1;
}
#endif
