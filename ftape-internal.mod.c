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



static const char ____versions[]
__used __section("__versions") =
	"\x14\x00\x00\x00\x3b\x4a\x51\xc1"
	"free_irq\0\0\0\0"
	"\x18\x00\x00\x00\x63\x3b\x05\x44"
	"try_module_get\0\0"
	"\x18\x00\x00\x00\x0f\x6e\xa2\x12"
	"param_array_ops\0"
	"\x18\x00\x00\x00\x0a\x39\x21\xb1"
	"probe_irq_on\0\0\0\0"
	"\x14\x00\x00\x00\x87\x09\xec\xfc"
	"enable_irq\0\0"
	"\x14\x00\x00\x00\xeb\xd0\x02\x43"
	"free_pages\0\0"
	"\x18\x00\x00\x00\x8c\x89\xd4\xcb"
	"fortify_panic\0\0\0"
	"\x1c\x00\x00\x00\xc2\xc7\x35\x10"
	"__release_region\0\0\0\0"
	"\x18\x00\x00\x00\x34\x4a\xf1\x21"
	"fdc_register\0\0\0\0"
	"\x14\x00\x00\x00\xbb\x6d\xfb\xbd"
	"__fentry__\0\0"
	"\x24\x00\x00\x00\x97\x70\x48\x65"
	"__x86_indirect_thunk_rax\0\0\0\0"
	"\x10\x00\x00\x00\x7e\x3a\x2c\x12"
	"_printk\0"
	"\x1c\x00\x00\x00\xcb\xf6\xfd\xf0"
	"__stack_chk_fail\0\0\0\0"
	"\x1c\x00\x00\x00\xee\xb5\x5c\x6a"
	"__get_free_pages\0\0\0\0"
	"\x18\x00\x00\x00\x21\x04\x60\xab"
	"probe_irq_off\0\0\0"
	"\x1c\x00\x00\x00\x00\x73\xd4\x7e"
	"ftape_ecc_correct\0\0\0"
	"\x14\x00\x00\x00\x78\x96\x77\xa1"
	"module_put\0\0"
	"\x28\x00\x00\x00\xb3\x1c\xa2\x87"
	"__ubsan_handle_out_of_bounds\0\0\0\0"
	"\x1c\x00\x00\x00\x5e\xd7\xd8\x7c"
	"page_offset_base\0\0\0\0"
	"\x20\x00\x00\x00\x8e\x83\xd5\x92"
	"request_threaded_irq\0\0\0\0"
	"\x14\x00\x00\x00\xb0\x28\x9d\x4c"
	"phys_base\0\0\0"
	"\x18\x00\x00\x00\x33\x8a\x58\x24"
	"fdc_unregister\0\0"
	"\x18\x00\x00\x00\x92\x6c\xdf\xdb"
	"ioport_resource\0"
	"\x1c\x00\x00\x00\xca\x39\x82\x5b"
	"__x86_return_thunk\0\0"
	"\x10\x00\x00\x00\x95\x68\x6d\xcf"
	"pv_ops\0\0"
	"\x14\x00\x00\x00\xe4\xa3\x54\x70"
	"request_dma\0"
	"\x18\x00\x00\x00\xd6\xdf\xe3\xea"
	"__const_udelay\0\0"
	"\x28\x00\x00\x00\x4f\x8f\xd6\x4e"
	"ftape_ecc_set_segment_parity\0\0\0\0"
	"\x14\x00\x00\x00\xd4\x43\xb2\x72"
	"free_dma\0\0\0\0"
	"\x18\x00\x00\x00\x4e\xc6\x24\xd8"
	"param_ops_int\0\0\0"
	"\x14\x00\x00\x00\xe6\x10\xec\xd4"
	"BUG_func\0\0\0\0"
	"\x1c\x00\x00\x00\x08\x16\xbd\x85"
	"__request_region\0\0\0\0"
	"\x14\x00\x00\x00\x6f\xca\xe4\x3c"
	"disable_irq\0"
	"\x18\x00\x00\x00\x76\xf2\x0f\x5e"
	"module_layout\0\0\0"
	"\x00\x00\x00\x00\x00\x00\x00\x00";

MODULE_INFO(depends, "ftape-core");


MODULE_INFO(srcversion, "AD0B1E06FAC6E36C37C217F");
