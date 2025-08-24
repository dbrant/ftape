#ifndef _FDC_PARPORT_H_
#define _FDC_PARPORT_H_

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
 * $RCSfile: fdc-parport.h,v $
 * $Revision: 1.23 $
 * $Date: 2000/06/23 12:16:52 $
 *
 *      Module parameter parsing and hardware probing for parallel
 *      floppy tape drives supported by ftape.
 *
 */

/* #include <linux/config.h> - not needed in modern kernels */
#include <linux/delay.h>

#if (defined(CONFIG_PNP_PARPORT) || defined(CONFIG_PNP_PARPORT_MODULE) || \
     defined(CONFIG_PARPORT) || defined(CONFIG_PARPORT_MODULE))
#define USE_PARPORT
#endif

#if defined(MODULE) && LINUX_VERSION_CODE >= KERNEL_VER(2,1,18)
# define FT_MOD_PARM(var,type,desc) \
	MODULE_PARM(var,type); MODULE_PARM_DESC(var,desc)
#else
# define FT_MOD_PARM(var,type,desc) /**/
#endif

#ifdef USE_PARPORT
# if LINUX_VERSION_CODE < KERNEL_VER(2,1,47)
#  if LINUX_VERSION_CODE < KERNEL_VER(2,1,0)
#   define port bus
#  endif
#  define pardevice ppd
#  define PARPORT_MODE_PCPS2 PARPORT_MODE_PS2
# endif
# if LINUX_VERSION_CODE > KERNEL_VER(2,3,0)
#  define PARPORT_MODE_PCPS2 PARPORT_MODE_TRISTATE
# endif
#endif

#ifdef USE_PARPORT
# include <linux/parport.h>
#else

# define PARPORT_CONTROL_STROBE    0x1
# define PARPORT_CONTROL_AUTOFD    0x2
# define PARPORT_CONTROL_INIT      0x4
# define PARPORT_CONTROL_SELECT    0x8

# define PARPORT_STATUS_ERROR      0x8
# define PARPORT_STATUS_SELECT     0x10
# define PARPORT_STATUS_PAPEROUT   0x20
# define PARPORT_STATUS_ACK        0x40
# define PARPORT_STATUS_BUSY       0x80

#endif /* USE_PARPORT */

#ifndef PARPORT_CONTROL_INTEN
# define PARPORT_CONTROL_INTEN     0x10
#endif
#ifndef PARPORT_CONTROL_DIRECTION
# define PARPORT_CONTROL_DIRECTION 0x20
#endif

typedef struct ft_parinfo {
#ifdef USE_PARPORT
	struct pardevice *dev;
# define BASE parinfo.dev->port->base
# define IRQ  parinfo.dev->port->irq
#else
	int base;  /* io base           */
	int size;  /* size of io region */
	int irq;   /* interrupt channel */
# define BASE parinfo.base
# define IRQ  parinfo.irq
#endif
	int delay; /* delay for parport output */	
	void (*handler)(int, void *, struct pt_regs *);
	int  (*probe)(fdc_info_t *fdc);
	const char *id;
} ft_parinfo_t;

/*  lowlevel input/ouput routines.
 */
#define ft_p(p) ((p).delay ? udelay((p).delay) : 0)

#ifdef USE_PARPORT

# if LINUX_VERSION_CODE < KERNEL_VER(2, 3, 0)

#  define ft_w_dtr(p,b) ({parport_write_data((p).dev->port, b); ft_p(p);})
#  define ft_r_dtr(p)   (ft_p(p), parport_read_data((p).dev->port))
#  define ft_w_str(p,b) ({parport_write_status((p).dev->port, b); ft_p(p);})
#  define ft_r_str(p)   (ft_p(p), parport_read_status((p).dev->port))
#  define ft_w_ctr(p,b) ({parport_write_control((p).dev->port, b);ft_p(p);})
#  define ft_r_ctr(p)   (ft_p(p), parport_read_control((p).dev->port))

#  define ft_epp_w_adr(p,b) \
	({parport_epp_write_addr((p).dev->port,b);ft_p(p);})
#  define ft_epp_r_adr(p)   (ft_p(p), parport_epp_read_addr((p).dev->port))
#  define ft_epp_w_dtr(p, b) \
	({parport_epp_write_data((p).dev->port, b); ft_p(p);})
#  define ft_epp_r_dtr(p)   (ft_p(p), parport_epp_read_data((p).dev->port))

# else

#  define out_p(p,offs,byte) ({outb(byte, (p).dev->port->base+offs); ft_p(p);})
#  define in_p(p,offs)       (ft_p(p), inb((p).dev->port->base+offs) & 0xff)

#  define ft_w_dtr(p, b)     out_p((p), 0, b) 
#  define ft_r_dtr(p)        in_p((p), 0) 
#  define ft_w_str(p, b)     out_p((p), 1, b)
#  define ft_r_str(p)        in_p((p), 1)
#  define ft_w_ctr(p, b)     out_p((p), 2, b)
#  define ft_r_ctr(p)        in_p((p), 2)
#  define ft_epp_w_adr(p, b) out_p((p), 3, b)
#  define ft_epp_r_adr(p)    in_p((p), 3)
#  define ft_epp_w_dtr(p, b) out_p((p), 4, b)
#  define ft_epp_r_dtr(p)    in_p((p), 4)

# endif

/* multi byte IO never supported by the parport module.
 */
# define ft_epp_w_dtr_w(p,w) ({outw(w, (p).dev->port->base + 4); ft_p(p);})
# define ft_epp_r_dtr_w(p)   (ft_p(p), inw((p).dev->port->base + 4)&0xffff)
# define ft_epp_w_dtr_l(p,l) ({outl(l, (p).dev->port->base + 4); ft_p(p);})
# define ft_epp_r_dtr_l(p)   (ft_p(p), inl((p).dev->port->base + 4)&0xffffffff)

#else

#define out_p(p,offs,byte)	outb(byte, (p).base + offs); ft_p(p);
#define in_p(p,offs)		(ft_p(p), inb((p).base + offs))

# define ft_w_dtr(p,b)          ({out_p(p, 0, b);})
# define ft_r_dtr(p)            (in_p(p, 0) & 0xff)
# define ft_w_str(p,b)          ({out_p(p, 1, b);})
# define ft_r_str(p)            (in_p(p, 1) & 0xff)
# define ft_w_ctr(p,b)          ({out_p(p, 2, b);})
# define ft_r_ctr(p)            (in_p(p, 2) & 0xff)
# define ft_epp_w_adr(p,b)      ({out_p(p, 3, b);})
# define ft_epp_r_adr(p)        (in_p(p, 3) & 0xff)
# define ft_epp_w_dtr(p,b)      ({out_p(p, 4, b);})
# define ft_epp_r_dtr(p)        (in_p(p, 4) & 0xff)
# define ft_epp_w_dtr_w(p,w)    ({outw(w, (p).base + 4); ft_p(p);})
# define ft_epp_r_dtr_w(p)      (ft_p(p), inw((p).base + 4)&0xffff)
# define ft_epp_w_dtr_l(p,l)    ({outl(l, (p).base + 4); ft_p(p);})
# define ft_epp_r_dtr_l(p)      (ft_p(p), inl((p).base + 4)&0xffffffff)

#endif

/* parport numbers or bases if not USE_PARPORT
 */
static int allocated[4] = { -1, -1, -1, -1 };

#define FT_FDC_PARPORT_NONE -2
#define FT_FDC_PARPORT_AUTO -1

/* do the ugly bottom half of the kernel configuration menu ...
 */
#if defined(CONFIG_FT_INT_0) || defined(CONFIG_FT_NONE_0)
# define CONFIG_FT_FDC_PARPORT_0 FT_FDC_PARPORT_NONE
#endif
#if defined(CONFIG_FT_INT_1) || defined(CONFIG_FT_NONE_1)
# define CONFIG_FT_FDC_PARPORT_1 FT_FDC_PARPORT_NONE
#endif
#if defined(CONFIG_FT_INT_2) || defined(CONFIG_FT_NONE_2)
# define CONFIG_FT_FDC_PARPORT_2 FT_FDC_PARPORT_NONE
#endif
#if defined(CONFIG_FT_INT_3) || defined(CONFIG_FT_NONE_3)
# define CONFIG_FT_FDC_PARPORT_3 FT_FDC_PARPORT_NONE
#endif

#ifndef CONFIG_FT_FDC_PARPORT_0
# define CONFIG_FT_FDC_PARPORT_0 FT_FDC_PARPORT_AUTO
#endif
#ifndef CONFIG_FT_FDC_PARPORT_1
# define CONFIG_FT_FDC_PARPORT_1 FT_FDC_PARPORT_AUTO
#endif
#ifndef CONFIG_FT_FDC_PARPORT_2
# define CONFIG_FT_FDC_PARPORT_2 FT_FDC_PARPORT_AUTO
#endif
#ifndef CONFIG_FT_FDC_PARPORT_3
# define CONFIG_FT_FDC_PARPORT_3 FT_FDC_PARPORT_AUTO
#endif

/*  module or kernel configuration parameters. The number of the
 *  parport associated with a specific ftape device.
 */
static int ft_fdc_parport[4] = {
	CONFIG_FT_FDC_PARPORT_0,
	CONFIG_FT_FDC_PARPORT_1,
	CONFIG_FT_FDC_PARPORT_2,
	CONFIG_FT_FDC_PARPORT_3
};
static unsigned int ft_fdc_threshold[4] = {
	CONFIG_FT_FDC_THRESHOLD_0,
	CONFIG_FT_FDC_THRESHOLD_1,
	CONFIG_FT_FDC_THRESHOLD_2,
	CONFIG_FT_FDC_THRESHOLD_3
};
static unsigned int ft_fdc_rate_limit[4] = {
	CONFIG_FT_FDC_MAX_RATE_0,
	CONFIG_FT_FDC_MAX_RATE_1,
	CONFIG_FT_FDC_MAX_RATE_2,
	CONFIG_FT_FDC_MAX_RATE_3
};
FT_MOD_PARM(ft_fdc_parport,    "1-4i", "parports to use");
FT_MOD_PARM(ft_fdc_threshold,  "1-4i", "FDC FIFO thresholds");
FT_MOD_PARM(ft_fdc_rate_limit, "1-4i", "FDC data transfer rate limits");

#ifndef USE_PARPORT
/*  fallback values: nothing configured.
 */
#ifndef CONFIG_FT_FDC_BASE_0
# define CONFIG_FT_FDC_BASE_0 -1
#endif
#ifndef CONFIG_FT_FDC_BASE_1
# define CONFIG_FT_FDC_BASE_1 -1
#endif
#ifndef CONFIG_FT_FDC_BASE_2
# define CONFIG_FT_FDC_BASE_2 -1
#endif
#ifndef CONFIG_FT_FDC_BASE_3
# define CONFIG_FT_FDC_BASE_3 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_0
# define CONFIG_FT_FDC_IRQ_0 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_1
# define CONFIG_FT_FDC_IRQ_1 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_2
# define CONFIG_FT_FDC_IRQ_2 -1
#endif
#ifndef CONFIG_FT_FDC_IRQ_3
# define CONFIG_FT_FDC_IRQ_3 -1
#endif
#ifndef CONFIG_FT_FDC_DMA_0
# define CONFIG_FT_FDC_DMA_0 -1
#endif

static int ft_fdc_base[4] = {
	CONFIG_FT_FDC_BASE_0,
	CONFIG_FT_FDC_BASE_1,
	CONFIG_FT_FDC_BASE_2,
	CONFIG_FT_FDC_BASE_3
};
static int ft_fdc_irq[4] = {
	CONFIG_FT_FDC_IRQ_0,
	CONFIG_FT_FDC_IRQ_1,
	CONFIG_FT_FDC_IRQ_2,
	CONFIG_FT_FDC_IRQ_3
};
FT_MOD_PARM(ft_fdc_base,  "1-4i", "I/O port base addresses");
FT_MOD_PARM(ft_fdc_irq,   "1-4i", "IRQ lines");
#endif
/*  claim the parport resources, 2.1 and non-2.1 version
 *
 */
static int ft_parport_claim(fdc_info_t *fdc, const ft_parinfo_t *parinfo)
{
	TRACE_FUN(ft_t_flow);
#ifdef USE_PARPORT
	if (parport_claim_or_block(parinfo->dev) < 0) {
		TRACE(ft_t_err, "Abort waiting for parallel port");
		TRACE_EXIT -EBUSY;
	}
#else
	if (check_region(parinfo->base, parinfo->size) < 0) {
		TRACE_ABORT(-EBUSY, ft_t_bug, 
			    "Unable to grab address 0x%04x for %s",
			    parinfo->base, fdc->driver);
	}
	request_region(parinfo->base, parinfo->size, parinfo->id);
	
	if (parinfo->irq != -1) {
#if LINUX_VERSION_CODE >= KERNEL_VER(1,3,70)
		if (request_irq(parinfo->irq, parinfo->handler, SA_INTERRUPT,
				parinfo->id, fdc))
#else
		if (request_irq(parinfo->irq, parinfo->handler, SA_INTERRUPT,
				parinfo->id))
#endif
		{
			release_region(parinfo->base, parinfo->size);
			TRACE_ABORT(-EBUSY, ft_t_bug, 
				    "Unable to grab IRQ %d for ftape driver",
				    parinfo->irq);
		}
	}
#endif
	TRACE_EXIT 0;
}


#ifndef USE_PARPORT
static int ft_parport_probe_one(fdc_info_t *fdc, ft_parinfo_t *parinfo)
{
	TRACE_FUN(ft_t_any);
	TRACE(ft_t_info, "base: 0x%04x, irq: %d",
	      parinfo->base, parinfo->irq);
	if (allocated[0] != parinfo->base &&
	    allocated[1] != parinfo->base &&
	    allocated[2] != parinfo->base &&
	    allocated[3] != parinfo->base) {
		int result;

		fdc->irq = parinfo->irq;		
		if ((result = parinfo->probe(fdc)) >= 0) {
			allocated[fdc->unit] = parinfo->base;
			TRACE_EXIT 0;
		}
		fdc->irq = -1;
		TRACE_EXIT result;
	} else {
		TRACE_EXIT -EBUSY;
	}
	TRACE_EXIT -ENXIO;
}
#endif

static int ft_parport_probe(fdc_info_t *fdc, ft_parinfo_t *parinfo)
{
#ifdef USE_PARPORT
	char buffer[16];
	struct parport *port;
	int len = 0;
	int result = -ENXIO;
	TRACE_FUN(ft_t_any);
			   
	switch(ft_fdc_parport[fdc->unit]) {
	case FT_FDC_PARPORT_NONE:
		TRACE(ft_t_err, "Device disabled during module/kernel configuration");
		TRACE_EXIT -ENXIO;
	case FT_FDC_PARPORT_AUTO:
		*buffer = '\0';
		break;
	default:
		len = sprintf(buffer, "parport%d", ft_fdc_parport[fdc->unit]);
		break;
	}
	
	for (port = parport_enumerate(); port; port = port->next) {
		if (strncmp(buffer, port->name, len) != 0) {
			continue;
		}

		parinfo->dev = parport_register_device(port, parinfo->id, 
						       NULL, NULL, 
						       parinfo->handler,
						       PARPORT_DEV_TRAN,
						       (void *)fdc);
		
		TRACE(ft_t_info, "dev: %p", parinfo->dev);
		if (parinfo->dev) {
			TRACE(ft_t_info, "irq: %d",   port->irq);
			TRACE(ft_t_info, "port: %lx", port->base);

			fdc->irq = port->irq;

			if (port->irq == -1) {
				result = -ENXIO;
			} else if (allocated[0] == port->number ||
				   allocated[1] == port->number ||
				   allocated[2] == port->number ||
				   allocated[3] == port->number) {
				result = -EBUSY;
			} else if ((result = parinfo->probe(fdc)) >= 0) {
				goto found;
			}

			parport_unregister_device(parinfo->dev);
		}
	}
	fdc->irq = -1;			
	TRACE(ft_t_err,
	      "can't find parport interface for ftape id %d", fdc->unit);
	MOD_INC_USE_COUNT; /* mark module as used */
	MOD_DEC_USE_COUNT;
	/* return -ENXIO when probing several devices, more useful
	 * return values otherwise
	 */
	TRACE_EXIT (len == 0) ? result : -ENXIO;
 found:
	allocated[fdc->unit] = port->number;

	if (ft_fdc_threshold[fdc->unit] != -1) {
		fdc->threshold = ft_fdc_threshold[fdc->unit];
	}
	/* may be overridden by the specific parport modules */
	if (ft_fdc_rate_limit[fdc->unit] != -1) {
		fdc->rate_limit = ft_fdc_rate_limit[fdc->unit];
	}
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
	
	TRACE(ft_t_info, "base: 0x%lx, irq: %d, number: %d", 
	      port->base, port->irq, port->number);
	TRACE_EXIT 0;
#else /* USE_PARPORT */
#define FT_PARPORT_NO 3
	const struct {
		__u16 base;
		int   size;
		int   irq;
	} parport_bases[FT_PARPORT_NO] = {
		{ 0x378, 8}, { 0x278, 8}, { 0x3bc, 3}};
	int i;
	int result = -ENXIO;
	TRACE_FUN(ft_t_flow);

	if (ft_fdc_parport[fdc->unit] == FT_FDC_PARPORT_NONE) {
		fdc->irq = -1;
		MOD_INC_USE_COUNT; /* mark module as used */
		MOD_DEC_USE_COUNT;
		TRACE_EXIT -ENXIO;
	}

	if (ft_fdc_threshold[fdc->unit] != -1) {
		fdc->threshold = ft_fdc_threshold[fdc->unit];
	}
	if (ft_fdc_rate_limit[fdc->unit] != -1) {
		fdc->rate_limit = ft_fdc_rate_limit[fdc->unit];
	}

	if (0 <= ft_fdc_parport[fdc->unit] &&
	    ft_fdc_parport[fdc->unit] < FT_PARPORT_NO) {
		parinfo->base = parport_bases[ft_fdc_parport[fdc->unit]].base;
		parinfo->size = parport_bases[ft_fdc_parport[fdc->unit]].size;
		parinfo->irq  = -1;
		if ((result = ft_parport_probe_one(fdc, parinfo)) >= 0) {
			TRACE_EXIT 0;
		}		
	} else if (ft_fdc_base[fdc->unit] != -1) {
		parinfo->base = ft_fdc_base[fdc->unit];
		parinfo->irq  = ft_fdc_irq[fdc->unit];
		parinfo->size = 8;
		if ((result = ft_parport_probe_one(fdc, parinfo)) >= 0) {
			TRACE_EXIT 0;
		}
	} else for (i = 0;
		    i < sizeof(parport_bases)/sizeof(parport_bases[0]); i++) {
		parinfo->base = parport_bases[i].base;
		parinfo->size = parport_bases[i].size;
		parinfo->irq  = -1;

		if (ft_parport_probe_one(fdc, parinfo) >= 0) {
			TRACE_EXIT 0;
		}
	}
	MOD_INC_USE_COUNT; /* mark module as used */
	MOD_DEC_USE_COUNT;
	TRACE_EXIT result;
#endif
}

static void ft_parport_release(fdc_info_t *fdc, ft_parinfo_t *parinfo)
{
#ifdef USE_PARPORT
	parport_release(parinfo->dev);
#else
	release_region(parinfo->base, parinfo->size);
	if (parinfo->irq != -1) {
#if LINUX_VERSION_CODE >= KERNEL_VER(1,3,70)
		free_irq(parinfo->irq, (void *)fdc);
#else
		free_irq(parinfo->irq);
#endif
	}
#endif /* USE_PARPORT */
}	

static void ft_parport_destroy(fdc_info_t *fdc, ft_parinfo_t *parinfo)
{
#ifdef USE_PARPORT
	if (parinfo->dev) {
		parport_unregister_device(parinfo->dev);
	}
#endif
	allocated[fdc->unit] = -1;	
}

#endif /* _FDC_PARPORT_H_ */
