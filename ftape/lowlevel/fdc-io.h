#ifndef _FDC_IO_H
#define _FDC_IO_H

/*
 *    Copyright (C) 1993-1996 Bas Laarhoven,
 *              (C) 1996-1998 Claus-Justus Heine,
 *              (C)      1997 Jochen Hoenicke.

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
 * $RCSfile: fdc-io.h,v $
 * $Revision: 1.26 $
 * $Date: 1999/05/29 20:35:07 $
 *
 *      This file contains the declarations for the low level
 *      functions that communicate with the floppy disk controller,
 *      for the QIC-40/80/3010/3020 floppy-tape driver "ftape" for
 *      Linux.
 *
 */

#include <linux/fdreg.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/sched.h>

#include "../lowlevel/ftape-bsm.h"
#include "../lowlevel/ftape-tracing.h"

#define FDC_SK_BIT      (0x20)
#define FDC_MT_BIT      (0x80)

#define FDC_READ        (FD_READ & ~(FDC_SK_BIT | FDC_MT_BIT))
#define FDC_WRITE       (FD_WRITE & ~FDC_MT_BIT)
#define FDC_READ_DELETED  (0x4c)
#define FDC_WRITE_DELETED (0x49)
#define FDC_VERIFY        (0x56)
#define FDC_READID      (0x4a)
#define FDC_SENSED      (0x04)
#define FDC_SENSEI      (FD_SENSEI)
#define FDC_FORMAT      (FD_FORMAT)
#define FDC_RECAL       (FD_RECALIBRATE)
#define FDC_SEEK        (FD_SEEK)
#define FDC_SPECIFY     (FD_SPECIFY)
#define FDC_RECALIBR    (FD_RECALIBRATE)
#define FDC_VERSION     (FD_VERSION)
#define FDC_PERPEND     (FD_PERPENDICULAR)
#define FDC_DUMPREGS    (FD_DUMPREGS)
#define FDC_LOCK        (FD_LOCK)
#define FDC_UNLOCK      (FD_UNLOCK)
#define FDC_CONFIGURE   (FD_CONFIGURE)
#define FDC_DRIVE_SPEC  (0x8e)	/* i82078 has this (any others?) */
#define FDC_PARTID      (0x18)	/* i82078 has this */
#define FDC_SAVE        (0x2e)	/* i82078 has this (any others?) */
#define FDC_RESTORE     (0x4e)	/* i82078 has this (any others?) */

#define FDC_STATUS_MASK (STATUS_BUSY | STATUS_DMA | STATUS_DIR | STATUS_READY)
#define FDC_DATA_READY  (STATUS_READY)
#define FDC_DATA_OUTPUT (STATUS_DIR)
#define FDC_DATA_READY_MASK (STATUS_READY | STATUS_DIR)
#define FDC_DATA_OUT_READY  (STATUS_READY | STATUS_DIR)
#define FDC_DATA_IN_READY   (STATUS_READY)
#define FDC_BUSY        (STATUS_BUSY)
#define FDC_CLK48_BIT   (0x80)
#define FDC_SEL3V_BIT   (0x40)

#define ST0_INT_MASK    (ST0_INTR)
#define FDC_INT_NORMAL  (ST0_INTR & 0x00)
#define FDC_INT_ABNORMAL (ST0_INTR & 0x40)
#define FDC_INT_INVALID (ST0_INTR & 0x80)
#define FDC_INT_READYCH (ST0_INTR & 0xC0)
#define ST0_SEEK_END    (ST0_SE)
#define ST3_TRACK_0     (ST3_TZ)

#define FDC_RESET_NOT   (0x04)
#define FDC_DMA_MODE    (0x08)
#define FDC_MOTOR_0     (0x10)
#define FDC_MOTOR_1     (0x20)

typedef enum {
	waiting = 0,
	reading,
	writing,
	formatting,
	verifying,
	deleting,
	done,
	error,
	fatal_error, /* HACK: reset FDC and retry entire segment */
} buffer_state_enum;

typedef enum {
	no_error = 0, id_am_error = 0x01, id_crc_error = 0x02,
	data_am_error = 0x04, data_crc_error = 0x08,
	no_data_error = 0x10, overrun_error = 0x20,
} ft_error_cause_t;

typedef enum {
	fdc_data_rate_250  = 2,
	fdc_data_rate_300  = 1,	/* any fdc in default configuration */
	fdc_data_rate_500  = 0,
	fdc_data_rate_1000 = 3,
	fdc_data_rate_2000 = 1,	/* i82078A-1: when using Data Rate Table #2 */
	fdc_data_rate_3000 = 0, /* Iomega DITTO-EZ special 3Mbs mode */
	fdc_data_rate_4000 = 2  /* QIC-117 rev j 4Mbs mode */
} fdc_data_rate_type;

typedef enum {
	fdc_idle          = 0,
	fdc_reading_data  = FDC_READ,
	fdc_seeking       = FDC_SEEK,
	fdc_writing_data  = FDC_WRITE,
	fdc_deleting      = FDC_WRITE_DELETED,
	fdc_reading_id    = FDC_READID,
	fdc_recalibrating = FDC_RECAL,
	fdc_formatting    = FDC_FORMAT,
	fdc_verifying     = FDC_VERIFY
} fdc_mode_enum;

#define FT_MAX_NR_BUFFERS 16 /* arbitrary value */

typedef struct buffer_struct {
	struct buffer_struct *next;
	__u8 *virtual;
        unsigned long dma_address;
        unsigned long ptr;
	volatile buffer_state_enum status;
	volatile unsigned int bytes;
	volatile unsigned int segment_id;

	/* bitmap for remainder of segment not yet handled.
	 * one bit set for each bad sector that must be skipped.
	 */
	volatile SectorMap bad_sector_map;

	/* bitmap with bad data blocks in data buffer.
	 * the errors in this map may be retried.
	 */
	volatile SectorMap soft_error_map;

	/* bitmap with bad data blocks in data buffer
	 * the errors in this map may not be retried.
	 */
	volatile SectorMap hard_error_map;

	/* retry counter for soft errors.
	 */
	volatile int retry;

	/* start offset when restarting tape. The tape should be
	 * positioned at the previous segment for overrun errors,
	 * which gives a larger setup tolerance.
	 */
	volatile int short_start;

	/* sectors to skip on retry ???
	 */
	volatile unsigned int skip;

	/* nr of data blocks in data buffer
	 */
	volatile unsigned int data_offset;

	/* offset in segment for first sector to be handled.
	 */
	volatile unsigned int sector_offset;

	/* size of cluster of good sectors to be handled.
	 */
	volatile unsigned int sector_count;

	/* size of remaining part of segment to be handled.
	 */
	volatile unsigned int remaining;

	/* points to next segment (contiguous) to be handled,
	 * or is zero if no read-ahead is allowed.
	 */
	volatile unsigned int next_segment;

	/* flag being set if deleted data was read.
	 */
	volatile int deleted;

	/* floppy coordinates of first sector in segment */
	volatile __u8 head;
	volatile __u8 cyl;
	volatile __u8 sect;

	/* gap to use when formatting */
	__u8 gap3;

} buffer_struct;

struct ftape_info; /* forward */

#define FT_FDC_MAGIC 0x43444674 /* "tFDC" */

typedef struct fdc_info {
	unsigned int magic; /* magic number, we choose this to be "tFDC"
			     * for "tape FDC" 
			     */
	const char *driver; /* passed to kerneld/kmod for auto-loading */
	void (*hook) (struct fdc_info *fdc); /* our wedge into the isr */
	enum {
		no_fdc, i8272, i82077, i82077AA, fc10,
		i82078, i82078_1, DITTOEZ
	} type;			/* FDC type */
	unsigned int irq; /* FDC irq nr */
	unsigned int dma; /* FDC dma channel nr */
	__u16 sra;	  /* Status register A (PS/2 only) */
	__u16 srb;	  /* Status register B (PS/2 only) */
	__u16 dor;	  /* Digital output register */
	__u16 tdr;	  /* Tape Drive Register (82077SL-1 &
			     82078 only) */
	__u16 msr;	  /* Main Status Register */
	__u16 dsr;	  /* Datarate Select Register (8207x only) */
	__u16 fifo;	  /* Data register / Fifo on 8207x */
	__u16 dir;	  /* Digital Input Register */
	__u16 ccr;	  /* Configuration Control Register */
	__u16 dor2;	  /* Alternate dor on MACH-2 controller,
			     also used with FC-10, meaning unknown */
	/* per fdc timeout calibration */
	unsigned int calibr_count;
	unsigned int calibr_time;
	/* per fdc tuning */
	unsigned int threshold;  /* bytes */
	unsigned int rate_limit; /* bits/sec */
	unsigned int max_rate;   /* bits/sec */
	/* signals that irq is disabled */
	int irq_level;
	/* per fdc buffer */
	unsigned int buffers_needed; /* number of buffers needed for
				      * proper operation
				      */
	unsigned int nr_buffers; /* number of buffers allocated */
	buffer_struct *buffers; /* doubly linked circular list */
	buffer_struct * volatile buffer_head;
	buffer_struct *buffer_tail;
#undef TAIL
#undef HEAD
#define TAIL fdc->buffer_tail
#define HEAD fdc->buffer_head

	/* some memory statistics */
	unsigned int used_dma_memory;
	unsigned int peak_dma_memory;

	int initialized;    /* maybe set and used to by-pass sanity checks */
	struct fdc_operations *ops;
	
	int active; /* set after ops->grab was successful */

	int unit;   /* 0, 1, 2, 3 */

	/*
	 * fdc status, local to fdc-io.c
	 */
	__u8 save_state[2];
	int perpend_mode;
	int status;
	volatile __u8 head;	/* FDC head from sector id */
	volatile __u8 cyl;	/* FDC track from sector id */
	volatile __u8 sect;	/* FDC sector from sector id */
	int data_rate;		/* data rate (Kbps) */
	int rate_code;		/* data rate code (0 == 500 Kbps) */
	int seek_rate;		/* step rate (msec) */
	int fifo_state;		/* original fifo setting - fifo enabled */
	int fifo_thr;		/* original fifo setting - threshold */
	int lock_state;		/* original lock setting - locked */
	int fifo_locked;	/* has fifo && lock set ? */
	__u8 precomp;		/* default precomp. value (nsec) */
	__u8 prec_code;		/* fdc precomp. select code */
	/*
	 * needed during formatting
	 */
	__u8 gap3; /* gap3 value */
	__u8 ffb;  /* format filler byte */
	/*
	 * global, for communication with fdc-isr etc.
	 */
	int setup_error;
	int motor;
	volatile int current_cylinder;
	volatile fdc_mode_enum mode;
	volatile int resetting;
	int in_reset;
	wait_queue_head_t wait_intr;
	/*
	 * from fdc-isr.c
	 */
	volatile int expected_stray_interrupts; /* masks stray interrupts */
	volatile int seek_result;               /* result of a seek command */
	volatile int interrupt_seen;            /* flag set by isr */
	volatile int seek_track;                /* for pause_tape() */
	volatile int isr_active;
	volatile int stop_read_ahead;
	volatile int no_data_error_count;       /* used during verify */
	volatile int hide_interrupt;            /* flag set by isr */	
	struct ftape_info *ftape;

	ft_trace_t *tracing;
	atomic_t *function_nest_level;

	void *data; /* in case the low level fdc driver needs additional per fdc data */
} fdc_info_t;

typedef struct fdc_operations {
	struct list_head node;

	const char *driver; /* used to map fdc-ops to fdc_info's */
	const char *description;

	/* detect fdc, should be used to perform basic sanity checks
	 */
	int (*detect)(fdc_info_t *info);

	/* grab/release the resources needed (e.g. ioports, irq and dma) */
	int (*grab)(fdc_info_t *info);
	int (*release)(fdc_info_t *info);

	/* In/Output a byte from/to a fdc-register */
	void (*outp) (fdc_info_t *fdc,
		      unsigned char value, unsigned short reg);
	void (*out) (fdc_info_t *fdc,
		     unsigned char value, unsigned short reg);
	unsigned char (*inp) (fdc_info_t *, unsigned short reg);
	unsigned char (*in) (fdc_info_t *, unsigned short reg);

	/* Disable and enable the irq. These function should
	 * enable/disable the IRQ UNCONDITIONALLY at the hardware
	 * level. fdc-io.c exports two funtions -- fdc_enable_irq(fdc)
	 * and fdc_disable_irq(fdc).
	 */
	void (*disable_irq)(fdc_info_t *info);
	void (*enable_irq)(fdc_info_t *info);

	/* setup the dma-controller to transfer to (mode==DMA_MODE_WRITE) 
	 * or from (mode==DMA_MODE_READ) the dma 
	 */
	void (*setup_dma)(fdc_info_t *info,
			  char mode, unsigned long addr, unsigned int count);

	/* Do things necessary to terminate the DMA transfer. If the
	 * hardware allows it, then return the number of bytes
	 * successfully transferred to the FDC.
	 *
	 * This function is required to return ~0 in case the hardware
	 * doesn't give any information about the DMA residue count.
	 */
	unsigned int (*terminate_dma)(fdc_info_t *info,
				      ft_error_cause_t cause);

	/* alloc/free a buffer. This should set buffer->dma_address
	 * and also allocte the deblock_buffer when called with buffer
	 * == NULL
	 *
	 * Note that the ftape-internal.o module doesn't use a separate
	 * deblock buffer any longer, but just flips the dma buffers :-)
	 */
	int  (*alloc)(fdc_info_t *info, struct buffer_struct *buffer);
	void (*free) (fdc_info_t *info, struct buffer_struct *buffer);

	/* Move data from the cyclic dma buffer list to a buffer where
	 * it can be copied from to the user buffer, without
	 * disturbing the DMA transfers.
	 *
	 * get_deblock_buffer(), correct_and_copy() and read_buffer()
	 * return a pointer to that area.
	 *
	 * They also lock the FDC module in memory, so that it can't
	 * be unloaded. This is necessary to guarantee that the
	 * deblock buffer points to valid data across subsequent
	 * open()/close() pairs.
	 *
	 * put_deblock_buffer(), copy_and_gen_ecc() and write_buffer()
	 * clear that module lock. The module can be unloaded then
	 * after fdc_release() has been called.
	 *
	 * Locking the module in memory and unlocking it is performed
	 * unconditionally. There is no counter that is
	 * incremented. Calling get_deblock_buffer() twice and then
	 * put_deblock_buffer() will unlock the module
	 *
	 * put_deblock_buffer(), copy_and_gen_ecc() and write_buffer()
	 * will set *source to NULL to invalidate the pointer.
	 */
	void* (*get_deblock_buffer)(fdc_info_t *info);
	int   (*put_deblock_buffer)(fdc_info_t *info, __u8 **buff);
	int (*copy_and_gen_ecc)(fdc_info_t *info,
				buffer_struct *buff, __u8 **source,
				SectorMap bsm);
	int (*correct_and_copy)(fdc_info_t *info,
				buffer_struct *buff, __u8 **dest);
	int (*write_buffer)(fdc_info_t *info,
			    buffer_struct *buff, __u8 **source);
	int (*read_buffer) (fdc_info_t *info,
			    buffer_struct *buff, __u8 **dest);

} fdc_operations;

/*
 *      fdc-io.c defined public variables
 */
extern fdc_info_t *fdc_infos[4]; /* FDC hardware configuration */
extern char *ft_fdc_driver[4]; /* low level FDC modules IDs */
extern int ft_fdc_driver_no[4];
/* module parameters */
extern unsigned int ft_fdc_threshold[4];
extern unsigned int ft_fdc_rate_limit[4];

/*
 *      fdc-io.c defined public functions
 */
extern int fdc_wait(fdc_info_t *fdc,
		    unsigned int usecs, __u8 mask, __u8 state);
extern int fdc_ready_wait(fdc_info_t *fdc, unsigned int timeout);
extern int fdc_result(fdc_info_t *fdc, __u8 * res_data, int res_len);
extern int __fdc_issue_command(fdc_info_t *fdc,
			       const __u8 * out_data, int out_count,
			       __u8 * in_data, int in_count);
extern int fdc_issue_command(fdc_info_t *fdc,
			     const __u8 * out_data, int out_count,
			     __u8 * in_data, int in_count);
extern int fdc_interrupt_wait(fdc_info_t *fdc, unsigned int time);
extern int fdc_set_seek_rate(fdc_info_t *fdc, int seek_rate);
extern int fdc_seek(fdc_info_t *fdc, int track);
extern int fdc_sense_drive_status(fdc_info_t *fdc, int *st3);
extern void fdc_motor(fdc_info_t *fdc, int motor);
extern void fdc_reset(fdc_info_t *fdc);
extern void fdc_disable(fdc_info_t *fdc);
extern int fdc_fifo_threshold(fdc_info_t *fdc, __u8 threshold,
			      int *fifo_state, int *lock_state, int *fifo_thr);
extern int fdc_sense_interrupt_status(fdc_info_t *fdc,
				      int *st0, int *current_cylinder);
extern int fdc_set_data_rate(fdc_info_t *fdc, int rate);
extern void fdc_set_write_precomp(fdc_info_t *fdc, int precomp);
extern int fdc_setup_read_write(fdc_info_t *fdc,
				buffer_struct * buff, __u8 operation);
extern int fdc_setup_formatting(fdc_info_t *fdc, buffer_struct * buff);

extern int fdc_register(fdc_operations *ops);
extern void fdc_unregister(fdc_operations *ops);
extern int fdc_init(struct ftape_info *ftape);
extern void fdc_destroy(int drive_selection);

extern void fdc_enable_irq(fdc_info_t *fdc);
extern void fdc_disable_irq(fdc_info_t *fdc);

#define fdc_command(fdc, data, len) fdc_issue_command(fdc, data, len, NULL, 0)

/*  Mmmh. Irq sharing will not work with this.
 */
extern inline fdc_info_t *ft_find_fdc_by_irq(int irq)
{
	int sel;

	for (sel = FTAPE_SEL_A; sel <= FTAPE_SEL_D; sel ++) {
		if (fdc_infos[sel] && fdc_infos[sel]->irq == irq) {
			return fdc_infos[sel];
		}
	}
	return NULL;
}

#endif
