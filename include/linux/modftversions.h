#ifndef _FTAPE_MODVERSIONS_H
#define _FTAPE_MODVERSIONS_H
#include <linux/version.h>
#if LINUX_VERSION_CODE <= ((1<<16)+(2<<8)+13)
/* #include <linux/config.h> - not needed in modern kernels */
#include <linux/module.h>
#endif
#include <linux/modules/ftape_syms.ver>
#include <linux/modules/zftape_syms.ver>
#include <linux/modversions.h>
#endif
