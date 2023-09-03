#ifndef _FTAPE_IO_H
#define _FTAPE_IO_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1997-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape-io.h,v $
 * $Revision: 1.6 $
 * $Date: 1998/12/18 22:02:17 $
 *
 *      This file contains definitions for the glue part of the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/qic117.h>
#include <linux/ftape-vendors.h>

struct ftape_info;

typedef struct {
	unsigned int seek;
	unsigned int reset;
	unsigned int rewind;
	unsigned int head_seek;
	unsigned int stop;
	unsigned int pause;
	unsigned int dt;
	unsigned int first_time:1;
} ft_timeout_table;

typedef enum {
	prehistoric, pre_qic117c, post_qic117b, post_qic117d 
} qic_model;

typedef enum {
	QIC_UNKNOWN = -1,
	QIC_40      = QIC_TAPE_QIC40,
	QIC_80      = QIC_TAPE_QIC80,
	QIC_3010    = QIC_TAPE_QIC3010,
	QIC_3020    = QIC_TAPE_QIC3020,
	/* some artifical value ... */
	DITTO_MAX   = ((QIC_TAPE_QIC40   |
			QIC_TAPE_QIC80   |
			QIC_TAPE_QIC3010 |
			QIC_TAPE_QIC3020) << 16)
} qic_std_t;

/*
 *      ftape-io.c defined global vars.
 */
extern const struct qic117_command_table qic117_cmds[];

/*
 *      ftape-io.c defined global functions.
 */
extern void ftape_sleep(unsigned int time);
/* 
 * ftape_sleep() does a busy wait if time is less than a tick. The
 * following macro sleeps at least a tick; often a timeout is a
 * minimum value, and it doesn't hurt to sleep a bit longer. The "? :"
 * will be optimized away by the compiler if time is a constant.
 */
#define ftape_sleep_a_tick(time)						\
	ftape_sleep( (FT_SECOND / HZ) > (time) ? (FT_SECOND / HZ) : (time) )
extern void ftape_report_vendor_id(struct ftape_info *ftape,
				   unsigned int *id);
extern int  ftape_command(struct ftape_info *ftape, qic117_cmd_t command);
extern int  ftape_command_wait(struct ftape_info *ftape,
			       qic117_cmd_t command,
			       unsigned int timeout,
			       int *status);
extern int  ftape_parameter(struct ftape_info *ftape, unsigned int parameter);
extern int  ftape_parameter_wait(struct ftape_info *ftape,
				 unsigned int parameter,
				 unsigned int timeout,
				 int *status);
extern int ftape_report_operation(struct ftape_info *ftape,
				  int *status,
				  qic117_cmd_t  command,
				  int result_length);
extern int ftape_report_configuration(struct ftape_info *ftape,
				      qic_model *model,
				      unsigned int *rate,
				      qic_std_t *qic_std,
				      int *tape_len);
extern int ftape_report_drive_status(struct ftape_info *ftape, int *status);
extern int ftape_report_raw_drive_status(struct ftape_info *ftape,
					 int *status);
extern int ftape_report_status(int *status);
extern int ftape_ready_wait(struct ftape_info *ftape,
			    unsigned int timeout, int *status);
extern int ftape_seek_head_to_track(struct ftape_info *ftape,
				    unsigned int track);
extern int ftape_in_error_state(int status);
extern int ftape_set_data_rate(struct ftape_info *ftape,
			       unsigned int new_rate, qic_std_t qic_std);
extern int ftape_report_error(struct ftape_info *ftape,
			      unsigned int *error,
			      qic117_cmd_t *command,
			      int report);
extern int ftape_reset_drive(struct ftape_info *ftape);
extern int ftape_put_drive_to_sleep(struct ftape_info *ftape,
				    wake_up_types method);
extern int ftape_wakeup_drive(struct ftape_info *ftape, wake_up_types method);
extern int ftape_increase_threshold(struct ftape_info *ftape);
extern int ftape_half_data_rate(struct ftape_info *ftape);
extern int ftape_door_lock(struct ftape_info *ftape, int flag);
extern int ftape_door_open(struct ftape_info *ftape, int flag);
extern int ftape_set_partition(struct ftape_info *ftape, int partition);

#endif
