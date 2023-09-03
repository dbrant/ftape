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

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>

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

#if LINUX_VERSION_CODE >= KERNEL_VER(2,1,18)
# define FT_KSYM(sym) EXPORT_SYMBOL(##sym);
#else
# define FT_KSYM(sym) X(##sym),
#endif

#if LINUX_VERSION_CODE < KERNEL_VER(2,1,18)
struct symbol_table ftape_symbol_table = {
#include <linux/symtab_begin.h>
#endif
/* bad sector handling from ftape-bsm.c */
FT_KSYM(ftape_get_bad_sector_entry)
FT_KSYM(ftape_find_end_of_bsm_list)
/* from ftape-rw.c */
FT_KSYM(ftape_set_state)
/* from ftape-ctl.c */
FT_KSYM(ftape_seek_to_bot)
FT_KSYM(ftape_seek_to_eot)
FT_KSYM(ftape_abort_operation)
FT_KSYM(ftape_get_status)
FT_KSYM(ftape_enable)
FT_KSYM(ftape_disable)
FT_KSYM(ftape_destroy)
FT_KSYM(ftape_calibrate_data_rate)
FT_KSYM(ftape_get_drive_status)
/* from ftape-io.c */
FT_KSYM(ftape_reset_drive)
FT_KSYM(ftape_command)
FT_KSYM(ftape_parameter)
FT_KSYM(ftape_ready_wait)
FT_KSYM(ftape_report_operation)
FT_KSYM(ftape_report_error)
FT_KSYM(ftape_door_lock)
FT_KSYM(ftape_door_open)
FT_KSYM(ftape_set_partition)
/* from ftape-read.c */
FT_KSYM(ftape_ecc_correct)
FT_KSYM(ftape_read_segment)
FT_KSYM(ftape_zap_read_buffers)
FT_KSYM(ftape_read_header_segment)
FT_KSYM(ftape_decode_header_segment)
/* from ftape-write.c */
FT_KSYM(ftape_write_segment)
FT_KSYM(ftape_loop_until_writes_done)
FT_KSYM(ftape_hard_error_recovery)
/* from fdc-io.c */
FT_KSYM(fdc_infos)
FT_KSYM(fdc_register)
FT_KSYM(fdc_unregister)
FT_KSYM(fdc_disable_irq)
FT_KSYM(fdc_enable_irq)
/* from ftape-buffer.h */
FT_KSYM(ftape_vmalloc)
FT_KSYM(ftape_vfree)
FT_KSYM(ftape_kmalloc)
FT_KSYM(ftape_kfree)
FT_KSYM(fdc_set_nr_buffers)
FT_KSYM(fdc_get_deblock_buffer)
FT_KSYM(fdc_put_deblock_buffer)
/* from ftape-format.h */
FT_KSYM(ftape_format_track)
FT_KSYM(ftape_format_status)
FT_KSYM(ftape_verify_segment)
/* from ftape-ecc.c */
FT_KSYM(ftape_ecc_set_segment_parity)
FT_KSYM(ftape_ecc_correct_data)
/* from tracing.c */
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
FT_KSYM(ftape_trace_call)
FT_KSYM(ftape_trace_exit)
FT_KSYM(ftape_trace_log)
FT_KSYM(ftape_tracings)
FT_KSYM(ftape_function_nest_levels)
#endif
/* end of ksym table */
#if LINUX_VERSION_CODE < KERNEL_VER(2,1,18)
#include <linux/symtab_end.h>
};
#endif
