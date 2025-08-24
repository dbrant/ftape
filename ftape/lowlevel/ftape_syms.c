/*
 *      Copyright (C) 1996-1998 Claus-Justus Heine.

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
 * $RCSfile: ftape_syms.c,v $
 * $Revision: 1.15 $
 * $Date: 1998/12/18 22:02:17 $
 *
 *      This file contains the the symbols that the ftape low level
 *      part of the QIC-40/80/3010/3020 floppy-tape driver "ftape"
 *      exports to it's high level clients
 */


#include <linux/module.h>
#define __NO_VERSION__

#include <linux/ftape.h>
#include "ftape-tracing.h"
#include "ftape-init.h"
#include "fdc-io.h"
#include "fdc-isr.h"
#include "ftape-read.h"
#include "ftape-write.h"
#include "ftape-io.h"
#include "ftape-ctl.h"
#include "ftape-rw.h"
#include "ftape-ecc.h"
#include "ftape-bsm.h"
#include "ftape-buffer.h"
#include "ftape-format.h"

/* bad sector handling from ftape-bsm.c */
EXPORT_SYMBOL_GPL(ftape_get_bad_sector_entry);
EXPORT_SYMBOL_GPL(ftape_find_end_of_bsm_list);
/* from ftape-rw.c */
EXPORT_SYMBOL_GPL(ftape_set_state);
/* from ftape-ctl.c */
EXPORT_SYMBOL_GPL(ftape_seek_to_bot);
EXPORT_SYMBOL_GPL(ftape_seek_to_eot);
EXPORT_SYMBOL_GPL(ftape_abort_operation);
EXPORT_SYMBOL_GPL(ftape_get_status);
EXPORT_SYMBOL_GPL(ftape_enable);
EXPORT_SYMBOL_GPL(ftape_disable);
EXPORT_SYMBOL_GPL(ftape_destroy);
EXPORT_SYMBOL_GPL(ftape_calibrate_data_rate);
EXPORT_SYMBOL_GPL(ftape_get_drive_status);
/* from ftape-io.c */
EXPORT_SYMBOL_GPL(ftape_reset_drive);
EXPORT_SYMBOL_GPL(ftape_command);
EXPORT_SYMBOL_GPL(ftape_parameter);
EXPORT_SYMBOL_GPL(ftape_ready_wait);
EXPORT_SYMBOL_GPL(ftape_report_operation);
EXPORT_SYMBOL_GPL(ftape_report_error);
EXPORT_SYMBOL_GPL(ftape_door_lock);
EXPORT_SYMBOL_GPL(ftape_door_open);
EXPORT_SYMBOL_GPL(ftape_set_partition);
/* from ftape-read.c */
EXPORT_SYMBOL_GPL(ftape_ecc_correct);
EXPORT_SYMBOL_GPL(ftape_read_segment);
EXPORT_SYMBOL_GPL(ftape_zap_read_buffers);
EXPORT_SYMBOL_GPL(ftape_read_header_segment);
EXPORT_SYMBOL_GPL(ftape_decode_header_segment);
/* from ftape-write.c */
EXPORT_SYMBOL_GPL(ftape_write_segment);
EXPORT_SYMBOL_GPL(ftape_loop_until_writes_done);
EXPORT_SYMBOL_GPL(ftape_hard_error_recovery);
/* from fdc-io.c */
EXPORT_SYMBOL_GPL(fdc_infos);
EXPORT_SYMBOL_GPL(fdc_register);
EXPORT_SYMBOL_GPL(fdc_unregister);
EXPORT_SYMBOL_GPL(fdc_disable_irq);
EXPORT_SYMBOL_GPL(fdc_enable_irq);
/* from ftape-buffer.h */
EXPORT_SYMBOL_GPL(ftape_vmalloc);
EXPORT_SYMBOL_GPL(ftape_vfree);
EXPORT_SYMBOL_GPL(ftape_kmalloc);
EXPORT_SYMBOL_GPL(ftape_kfree);
EXPORT_SYMBOL_GPL(fdc_set_nr_buffers);
EXPORT_SYMBOL_GPL(fdc_get_deblock_buffer);
EXPORT_SYMBOL_GPL(fdc_put_deblock_buffer);
/* from ftape-format.h */
EXPORT_SYMBOL_GPL(ftape_format_track);
EXPORT_SYMBOL_GPL(ftape_format_status);
EXPORT_SYMBOL_GPL(ftape_verify_segment);
/* from ftape-ecc.c */
EXPORT_SYMBOL_GPL(ftape_ecc_set_segment_parity);
EXPORT_SYMBOL_GPL(ftape_ecc_correct_data);
/* from tracing.c */
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
EXPORT_SYMBOL_GPL(ftape_trace_call);
EXPORT_SYMBOL_GPL(ftape_trace_exit);
EXPORT_SYMBOL_GPL(ftape_trace_log);
EXPORT_SYMBOL_GPL(ftape_tracings);
EXPORT_SYMBOL_GPL(ftape_function_nest_levels);
#endif

