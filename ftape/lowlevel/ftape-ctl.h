#ifndef _FTAPE_CTL_H
#define _FTAPE_CTL_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-ctl.h,v $
 * $Revision: 1.13 $
 * $Date: 1999/02/07 00:25:15 $
 *
 *      This file contains the non-standard IOCTL related definitions
 *      for the QIC-40/80/3010/3020 floppy-tape driver "ftape" for
 *      Linux.
 */

#include <linux/ioctl.h>
#include <linux/mtio.h>
#include <linux/ftape-vendors.h>

#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-io.h"
#include <linux/ftape-header-segment.h>

#define FT_HISTORY_READ  1
#define FT_HISTORY_WROTE 2

typedef struct {
	unsigned int used:2;		/* any reading or writing done */
	/* isr statistics */
	unsigned int id_am_errors;	/* id address mark not found */
	unsigned int id_crc_errors;	/* crc error in id address mark */
	unsigned int data_am_errors;	/* data address mark not found */
	unsigned int data_crc_errors;	/* crc error in data field */
	unsigned int overrun_errors;	/* fdc access timing problem */
	unsigned int no_data_errors;	/* sector not found */
	unsigned int retries;	/* number of tape retries */
	/* ecc statistics */
	unsigned int crc_errors;	/* crc error in data */
	unsigned int crc_failures;	/* bad data without crc error */
	unsigned int ecc_failures;	/* failed to correct */
	unsigned int corrected;	/* total sectors corrected */
	/* general statistics */
	unsigned int rewinds;	/* number of tape rewinds */
	unsigned int defects;	/* bad sectors due to media defects */
} ft_history_t;

typedef struct ftape_info {
	vendor_struct drive_type;
	fdc_info_t *fdc; /* fdc config info   */	
	enum runner_status_enum runner_status;
	/*
	 * various flags
	 */
	unsigned int new_tape:1;
	unsigned int no_tape:1;
	unsigned int write_protected:1;
	unsigned int formatted:1;
	unsigned int init_drive_needed:1;
	unsigned int failure:1;
	unsigned int active:1;
	volatile unsigned int tape_running:1;
	unsigned int might_be_off_track:1;
	unsigned int diagnostic_mode:1;
	unsigned int set_rate_supported:1;
	unsigned int bad_bus_timing:1;
	/*************/
	ft_format_type format_code;
	unsigned int drive_sel;
	qic_std_t qic_std;
	unsigned int data_rate;
	unsigned int tape_len;
	buffer_state_enum driver_state;
	volatile qic117_cmd_t current_command;
	unsigned int segments_per_track;
	int segments_per_head;
	int segments_per_cylinder;
	unsigned int tracks_per_tape;
	unsigned int drive_max_rate;
	int header_segment_1;
	int header_segment_2;
	int used_header_segment;
	int first_data_segment;
	int last_data_segment;
	ft_drive_status last_status;
	ft_drive_error  last_error;
	int qic_cmd_level;
	/* needed for the retry stuff in setup_segment()
	 */
	int old_segment_id;
	buffer_state_enum old_driver_state;
	/*
	 * cope with overrun errors
	 */
	int overrun_count_offset;
	int last_segment; /* last segment id that start_tape() was called */
	/*
	 * fast seeking
	 */
	int ffw_overshoot;
	int min_count;
	int rew_overshoot;
	int min_rewind;
	int rew_eot_margin;
	/*
	 * for ftape_start_tape()
	 */
	/* number of segments passing the head between starting the tape
	 * and being able to access the first sector.
	 */
	int start_offset;
	/*
	 * low level location
	 */
	location_record location;
	/*
	 * timeouts for pause, reset etc.
	 */
	ft_timeout_table timeout;
	/*
	 * bad sector map handling
	 */
	__u8 *bad_sector_map;
	SectorCount *bsm_hash_ptr;
	int last_reference;
	SectorMap map;
	/*
	 * from ftape-write.c
	 */
	int last_write_failed;
	/*
	 *  first segment of the new buffer. From ftape-format.c
	 */
	int switch_segment;
	/*
	 * miscinfo
	 */
	ft_history_t history; /* history */
	/*
	 *  stuff to get the trace stuff going
	 */
	ft_trace_t *tracing;
	atomic_t *function_nest_level;
} ftape_info_t;

/*      Global vars.
 */

extern ftape_info_t *ftapes[4];

/*
 *      ftape-ctl.c defined global functions.
 */
extern ftape_info_t *ftape_get_status(int sel);
extern void ftape_set_status(const ftape_info_t *ftape, int sel);
extern int  ftape_seek_to_eot(ftape_info_t *ftape);
extern int  ftape_seek_to_bot(ftape_info_t *ftape);
extern int ftape_abort_operation(ftape_info_t *ftape);
extern void ftape_calc_timeouts(ftape_info_t *ftape,
				qic_std_t qic_std,
				unsigned int data_rate,
				unsigned int tape_len);
extern int ftape_calibrate_data_rate(ftape_info_t *ftape,
				     qic_std_t qic_std);
extern int  ftape_destroy(int sel);
extern int  ftape_enable(int drive_selection);
extern void ftape_disable(int drive_selection);
extern void ftape_init_driver(ftape_info_t *ftape, int sel);
extern int  ftape_get_drive_status(ftape_info_t *ftape);

#endif



