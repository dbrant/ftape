#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

KSYMTAB_FUNC(ftape_get_bad_sector_entry, "_gpl", "");
KSYMTAB_FUNC(ftape_find_end_of_bsm_list, "_gpl", "");
KSYMTAB_FUNC(ftape_set_state, "_gpl", "");
KSYMTAB_FUNC(ftape_seek_to_bot, "_gpl", "");
KSYMTAB_FUNC(ftape_seek_to_eot, "_gpl", "");
KSYMTAB_FUNC(ftape_abort_operation, "_gpl", "");
KSYMTAB_FUNC(ftape_get_status, "_gpl", "");
KSYMTAB_FUNC(ftape_enable, "_gpl", "");
KSYMTAB_FUNC(ftape_disable, "_gpl", "");
KSYMTAB_FUNC(ftape_destroy, "_gpl", "");
KSYMTAB_FUNC(ftape_calibrate_data_rate, "_gpl", "");
KSYMTAB_FUNC(ftape_get_drive_status, "_gpl", "");
KSYMTAB_FUNC(ftape_reset_drive, "_gpl", "");
KSYMTAB_FUNC(ftape_command, "_gpl", "");
KSYMTAB_FUNC(ftape_parameter, "_gpl", "");
KSYMTAB_FUNC(ftape_ready_wait, "_gpl", "");
KSYMTAB_FUNC(ftape_report_operation, "_gpl", "");
KSYMTAB_FUNC(ftape_report_error, "_gpl", "");
KSYMTAB_FUNC(ftape_door_lock, "_gpl", "");
KSYMTAB_FUNC(ftape_door_open, "_gpl", "");
KSYMTAB_FUNC(ftape_set_partition, "_gpl", "");
KSYMTAB_FUNC(ftape_ecc_correct, "_gpl", "");
KSYMTAB_FUNC(ftape_read_segment, "_gpl", "");
KSYMTAB_FUNC(ftape_zap_read_buffers, "_gpl", "");
KSYMTAB_FUNC(ftape_read_header_segment, "_gpl", "");
KSYMTAB_FUNC(ftape_decode_header_segment, "_gpl", "");
KSYMTAB_FUNC(ftape_write_segment, "_gpl", "");
KSYMTAB_FUNC(ftape_loop_until_writes_done, "_gpl", "");
KSYMTAB_FUNC(ftape_hard_error_recovery, "_gpl", "");
KSYMTAB_DATA(fdc_infos, "_gpl", "");
KSYMTAB_FUNC(fdc_register, "_gpl", "");
KSYMTAB_FUNC(fdc_unregister, "_gpl", "");
KSYMTAB_FUNC(fdc_disable_irq, "_gpl", "");
KSYMTAB_FUNC(fdc_enable_irq, "_gpl", "");
KSYMTAB_FUNC(ftape_vmalloc, "_gpl", "");
KSYMTAB_FUNC(ftape_vfree, "_gpl", "");
KSYMTAB_FUNC(ftape_kmalloc, "_gpl", "");
KSYMTAB_FUNC(ftape_kfree, "_gpl", "");
KSYMTAB_FUNC(fdc_set_nr_buffers, "_gpl", "");
KSYMTAB_FUNC(fdc_get_deblock_buffer, "_gpl", "");
KSYMTAB_FUNC(fdc_put_deblock_buffer, "_gpl", "");
KSYMTAB_FUNC(ftape_format_track, "_gpl", "");
KSYMTAB_FUNC(ftape_format_status, "_gpl", "");
KSYMTAB_FUNC(ftape_verify_segment, "_gpl", "");
KSYMTAB_FUNC(ftape_ecc_set_segment_parity, "_gpl", "");
KSYMTAB_FUNC(ftape_ecc_correct_data, "_gpl", "");
KSYMTAB_FUNC(ftape_trace_call, "_gpl", "");
KSYMTAB_FUNC(ftape_trace_exit, "_gpl", "");
KSYMTAB_FUNC(ftape_trace_log, "_gpl", "");
KSYMTAB_DATA(ftape_tracings, "_gpl", "");
KSYMTAB_DATA(ftape_function_nest_levels, "_gpl", "");
KSYMTAB_DATA(ft_ignore_ecc_err, "_gpl", "");
KSYMTAB_DATA(ft_soft_retries, "_gpl", "");

SYMBOL_CRC(ftape_get_bad_sector_entry, 0x0863210d, "_gpl");
SYMBOL_CRC(ftape_find_end_of_bsm_list, 0x46738687, "_gpl");
SYMBOL_CRC(ftape_set_state, 0xc7800b31, "_gpl");
SYMBOL_CRC(ftape_seek_to_bot, 0xd2f82945, "_gpl");
SYMBOL_CRC(ftape_seek_to_eot, 0xe5034c6c, "_gpl");
SYMBOL_CRC(ftape_abort_operation, 0x037343a7, "_gpl");
SYMBOL_CRC(ftape_get_status, 0x21c6e2a2, "_gpl");
SYMBOL_CRC(ftape_enable, 0x7ec0d5d4, "_gpl");
SYMBOL_CRC(ftape_disable, 0x5eafa581, "_gpl");
SYMBOL_CRC(ftape_destroy, 0xdd0e4abf, "_gpl");
SYMBOL_CRC(ftape_calibrate_data_rate, 0x2feee8d4, "_gpl");
SYMBOL_CRC(ftape_get_drive_status, 0x5c1c888e, "_gpl");
SYMBOL_CRC(ftape_reset_drive, 0x744589dc, "_gpl");
SYMBOL_CRC(ftape_command, 0x11d9198e, "_gpl");
SYMBOL_CRC(ftape_parameter, 0x01515ac3, "_gpl");
SYMBOL_CRC(ftape_ready_wait, 0x769c2982, "_gpl");
SYMBOL_CRC(ftape_report_operation, 0x104f6e4e, "_gpl");
SYMBOL_CRC(ftape_report_error, 0x114a6027, "_gpl");
SYMBOL_CRC(ftape_door_lock, 0x53084231, "_gpl");
SYMBOL_CRC(ftape_door_open, 0x0036f036, "_gpl");
SYMBOL_CRC(ftape_set_partition, 0xa10ad991, "_gpl");
SYMBOL_CRC(ftape_ecc_correct, 0x2fb00534, "_gpl");
SYMBOL_CRC(ftape_read_segment, 0xc150c9f1, "_gpl");
SYMBOL_CRC(ftape_zap_read_buffers, 0xc7c0e00e, "_gpl");
SYMBOL_CRC(ftape_read_header_segment, 0x65af7249, "_gpl");
SYMBOL_CRC(ftape_decode_header_segment, 0xb1669c8d, "_gpl");
SYMBOL_CRC(ftape_write_segment, 0x04a82e15, "_gpl");
SYMBOL_CRC(ftape_loop_until_writes_done, 0x88aac44a, "_gpl");
SYMBOL_CRC(ftape_hard_error_recovery, 0x6676057d, "_gpl");
SYMBOL_CRC(fdc_infos, 0x85d28aa1, "_gpl");
SYMBOL_CRC(fdc_register, 0x06a987ac, "_gpl");
SYMBOL_CRC(fdc_unregister, 0xadc1a72e, "_gpl");
SYMBOL_CRC(fdc_disable_irq, 0xed88507b, "_gpl");
SYMBOL_CRC(fdc_enable_irq, 0xa7ba6147, "_gpl");
SYMBOL_CRC(ftape_vmalloc, 0xd079c2fe, "_gpl");
SYMBOL_CRC(ftape_vfree, 0x329451f6, "_gpl");
SYMBOL_CRC(ftape_kmalloc, 0x359246bf, "_gpl");
SYMBOL_CRC(ftape_kfree, 0xd24321f9, "_gpl");
SYMBOL_CRC(fdc_set_nr_buffers, 0x48e30ca1, "_gpl");
SYMBOL_CRC(fdc_get_deblock_buffer, 0xc23d8e37, "_gpl");
SYMBOL_CRC(fdc_put_deblock_buffer, 0xe72c6f4a, "_gpl");
SYMBOL_CRC(ftape_format_track, 0x356dd799, "_gpl");
SYMBOL_CRC(ftape_format_status, 0xc8058917, "_gpl");
SYMBOL_CRC(ftape_verify_segment, 0x8c8ab5dd, "_gpl");
SYMBOL_CRC(ftape_ecc_set_segment_parity, 0x4ed68f4f, "_gpl");
SYMBOL_CRC(ftape_ecc_correct_data, 0xdd95e831, "_gpl");
SYMBOL_CRC(ftape_trace_call, 0x2e84adf8, "_gpl");
SYMBOL_CRC(ftape_trace_exit, 0x31111ad1, "_gpl");
SYMBOL_CRC(ftape_trace_log, 0x1203c91a, "_gpl");
SYMBOL_CRC(ftape_tracings, 0xfad7bffb, "_gpl");
SYMBOL_CRC(ftape_function_nest_levels, 0xe55fc8ee, "_gpl");
SYMBOL_CRC(ft_ignore_ecc_err, 0xcf1e7756, "_gpl");
SYMBOL_CRC(ft_soft_retries, 0xf854f9d8, "_gpl");

static const char ____versions[]
__used __section("__versions") =
	"\x14\x00\x00\x00\xd0\x6b\x7d\x9e"
	"__udelay\0\0\0\0"
	"\x10\x00\x00\x00\xeb\x02\xe6\xb0"
	"memmove\0"
	"\x18\x00\x00\x00\x0f\x6e\xa2\x12"
	"param_array_ops\0"
	"\x10\x00\x00\x00\x38\xdf\xac\x69"
	"memcpy\0\0"
	"\x10\x00\x00\x00\xba\x0c\x7a\x03"
	"kfree\0\0\0"
	"\x14\x00\x00\x00\x35\xe4\x57\x2d"
	"pcpu_hot\0\0\0\0"
	"\x18\x00\x00\x00\x38\x22\xfb\x4a"
	"add_wait_queue\0\0"
	"\x14\x00\x00\x00\x44\x43\x96\xe2"
	"__wake_up\0\0\0"
	"\x14\x00\x00\x00\xbb\x6d\xfb\xbd"
	"__fentry__\0\0"
	"\x24\x00\x00\x00\x97\x70\x48\x65"
	"__x86_indirect_thunk_rax\0\0\0\0"
	"\x10\x00\x00\x00\x7e\x3a\x2c\x12"
	"_printk\0"
	"\x1c\x00\x00\x00\xad\x8a\xdd\x8d"
	"schedule_timeout\0\0\0\0"
	"\x1c\x00\x00\x00\xcb\xf6\xfd\xf0"
	"__stack_chk_fail\0\0\0\0"
	"\x24\x00\x00\x00\x7c\xb2\x83\x63"
	"__x86_indirect_thunk_rdx\0\0\0\0"
	"\x28\x00\x00\x00\xb3\x1c\xa2\x87"
	"__ubsan_handle_out_of_bounds\0\0\0\0"
	"\x24\x00\x00\x00\x2e\x5e\x38\x55"
	"__x86_indirect_thunk_r14\0\0\0\0"
	"\x10\x00\x00\x00\x11\x13\x92\x5a"
	"strncmp\0"
	"\x24\x00\x00\x00\xe9\xc8\x79\x1a"
	"__x86_indirect_thunk_r13\0\0\0\0"
	"\x10\x00\x00\x00\xc5\x8f\x57\xfb"
	"memset\0\0"
	"\x18\x00\x00\x00\x5e\x1d\x51\x6f"
	"param_ops_charp\0"
	"\x1c\x00\x00\x00\xca\x39\x82\x5b"
	"__x86_return_thunk\0\0"
	"\x20\x00\x00\x00\xd6\xc7\xd8\xaa"
	"default_wake_function\0\0\0"
	"\x20\x00\x00\x00\x54\xea\xa5\xd9"
	"__init_waitqueue_head\0\0\0"
	"\x10\x00\x00\x00\x5a\x25\xd5\xe2"
	"strcmp\0\0"
	"\x10\x00\x00\x00\xa6\x50\xba\x15"
	"jiffies\0"
	"\x2c\x00\x00\x00\x61\xe5\x48\xa6"
	"__ubsan_handle_shift_out_of_bounds\0\0"
	"\x10\x00\x00\x00\x97\x82\x9e\x99"
	"vfree\0\0\0"
	"\x18\x00\x00\x00\xd6\xdf\xe3\xea"
	"__const_udelay\0\0"
	"\x1c\x00\x00\x00\x88\x00\x11\x37"
	"remove_wait_queue\0\0\0"
	"\x24\x00\x00\x00\xa8\xf9\x62\x03"
	"__x86_indirect_thunk_r12\0\0\0\0"
	"\x10\x00\x00\x00\x9c\x53\x4d\x75"
	"strlen\0\0"
	"\x18\x00\x00\x00\x4e\xc6\x24\xd8"
	"param_ops_int\0\0\0"
	"\x10\x00\x00\x00\x85\xba\x9c\x34"
	"strchr\0\0"
	"\x10\x00\x00\x00\x8f\x68\xee\xd6"
	"vmalloc\0"
	"\x14\x00\x00\x00\x45\x3a\x23\xeb"
	"__kmalloc\0\0\0"
	"\x18\x00\x00\x00\x76\xf2\x0f\x5e"
	"module_layout\0\0\0"
	"\x00\x00\x00\x00\x00\x00\x00\x00";

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "B79960AFEC644E0AAF72AF4");
