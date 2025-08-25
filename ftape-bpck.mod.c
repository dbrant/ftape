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
	"\x14\x00\x00\x00\xd0\x6b\x7d\x9e"
	"__udelay\0\0\0\0"
	"\x18\x00\x00\x00\x0f\x6e\xa2\x12"
	"param_array_ops\0"
	"\x14\x00\x00\x00\xf9\x21\x43\xd2"
	"ftape_kfree\0"
	"\x14\x00\x00\x00\x87\x09\xec\xfc"
	"enable_irq\0\0"
	"\x14\x00\x00\x00\xf6\x51\x94\x32"
	"ftape_vfree\0"
	"\x18\x00\x00\x00\xfe\xc2\x79\xd0"
	"ftape_vmalloc\0\0\0"
	"\x18\x00\x00\x00\xac\x87\xa9\x06"
	"fdc_register\0\0\0\0"
	"\x14\x00\x00\x00\xbb\x6d\xfb\xbd"
	"__fentry__\0\0"
	"\x1c\x00\x00\x00\xd1\x1a\x11\x31"
	"ftape_trace_exit\0\0\0\0"
	"\x24\x00\x00\x00\x97\x70\x48\x65"
	"__x86_indirect_thunk_rax\0\0\0\0"
	"\x10\x00\x00\x00\x7e\x3a\x2c\x12"
	"_printk\0"
	"\x14\x00\x00\x00\x51\x0e\x00\x01"
	"schedule\0\0\0\0"
	"\x24\x00\x00\x00\xbd\x3c\xad\x18"
	"parport_register_dev_model\0\0"
	"\x1c\x00\x00\x00\xcb\xf6\xfd\xf0"
	"__stack_chk_fail\0\0\0\0"
	"\x20\x00\x00\x00\x56\x46\xc3\x35"
	"parport_claim_or_block\0\0"
	"\x1c\x00\x00\x00\x34\x05\xb0\x2f"
	"ftape_ecc_correct\0\0\0"
	"\x28\x00\x00\x00\xb3\x1c\xa2\x87"
	"__ubsan_handle_out_of_bounds\0\0\0\0"
	"\x18\x00\x00\x00\x54\x3b\x51\x71"
	"parport_release\0"
	"\x10\x00\x00\x00\x11\x13\x92\x5a"
	"strncmp\0"
	"\x1c\x00\x00\x00\xf8\xad\x84\x2e"
	"ftape_trace_call\0\0\0\0"
	"\x24\x00\x00\x00\xe9\xc8\x79\x1a"
	"__x86_indirect_thunk_r13\0\0\0\0"
	"\x18\x00\x00\x00\xbf\x46\x92\x35"
	"ftape_kmalloc\0\0\0"
	"\x24\x00\x00\x00\x5c\x33\xa1\x2c"
	"parport_unregister_device\0\0\0"
	"\x1c\x00\x00\x00\x1f\x8a\xef\x46"
	"parport_find_number\0"
	"\x18\x00\x00\x00\x2e\xa7\xc1\xad"
	"fdc_unregister\0\0"
	"\x10\x00\x00\x00\xc5\x8f\x57\xfb"
	"memset\0\0"
	"\x1c\x00\x00\x00\xca\x39\x82\x5b"
	"__x86_return_thunk\0\0"
	"\x10\x00\x00\x00\xfd\xf9\x3f\x3c"
	"sprintf\0"
	"\x2c\x00\x00\x00\x61\xe5\x48\xa6"
	"__ubsan_handle_shift_out_of_bounds\0\0"
	"\x18\x00\x00\x00\xd6\xdf\xe3\xea"
	"__const_udelay\0\0"
	"\x1c\x00\x00\x00\xca\x7c\x70\xee"
	"parport_put_port\0\0\0\0"
	"\x18\x00\x00\x00\xfb\xbf\xd7\xfa"
	"ftape_tracings\0\0"
	"\x18\x00\x00\x00\x4e\xc6\x24\xd8"
	"param_ops_int\0\0\0"
	"\x24\x00\x00\x00\xee\xc8\x5f\xe5"
	"ftape_function_nest_levels\0\0"
	"\x14\x00\x00\x00\x6f\xca\xe4\x3c"
	"disable_irq\0"
	"\x18\x00\x00\x00\x1a\xc9\x03\x12"
	"ftape_trace_log\0"
	"\x18\x00\x00\x00\x76\xf2\x0f\x5e"
	"module_layout\0\0\0"
	"\x00\x00\x00\x00\x00\x00\x00\x00";

MODULE_INFO(depends, "ftape-core,parport");


MODULE_INFO(srcversion, "5EDE8A9161AADA8A7B7758E");
