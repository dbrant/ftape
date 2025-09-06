#
# Standalone out-of-tree Makefile for ftape driver
#

# Kernel build directory
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Module objects
obj-m := ftape-core.o ftape-internal.o zftape.o ftape-parport.o ftape-trakker.o ftape-bpck.o

# Core ftape objects
ftape-core-objs := ftape/lowlevel/ftape-init.o \
		   ftape/lowlevel/ftape-calibr.o \
		   ftape/lowlevel/ftape-ctl.o \
		   ftape/lowlevel/ftape-io.o \
		   ftape/lowlevel/ftape-read.o \
		   ftape/lowlevel/ftape-rw.o \
		   ftape/lowlevel/ftape-write.o \
		   ftape/lowlevel/ftape-buffer.o \
		   ftape/lowlevel/ftape-ecc.o \
		   ftape/lowlevel/ftape-bsm.o \
		   ftape/lowlevel/ftape-format.o \
		   ftape/lowlevel/ftape-tracing.o \
		   ftape/lowlevel/fdc-io.o \
		   ftape/lowlevel/fdc-isr.o \
		   ftape/lowlevel/ftape-proc.o \
		   ftape/lowlevel/ftape_syms.o \
		   ftape/setup/ftape-setup.o

# Internal FDC objects
ftape-internal-objs := ftape/internal/fdc-internal.o \
		       ftape/internal/fc-10.o

# Zftape VFS interface
zftape-objs := ftape/zftape/zftape-init.o \
	       ftape/zftape/zftape-read.o \
	       ftape/zftape/zftape-write.o \
	       ftape/zftape/zftape-ctl.o \
	       ftape/zftape/zftape-buffers.o \
	       ftape/zftape/zftape-rw.o \
	       ftape/zftape/zftape-vtbl.o \
	       ftape/zftape/zftape-eof.o \
	       ftape/zftape/zftape_syms.o

# Parallel port interface
ftape-parport-objs := ftape/parport/fdc-parport.o

# Trakker parallel port support  
ftape-trakker-objs := ftape/parport/trakker.o

# BackPack parallel port support
ftape-bpck-objs := ftape/parport/bpck-fdc.o

# Include our local headers
ccflags-y := -I$(src)/include

ccflags-y += -DTHE_FTAPE_MAINTAINER=\"me@dmitrybrant.com\"

# Loudly insist that we're building as a module. This is a holdover from
# when this driver could be built into the kernel, which I suppose it still could.
ccflags-y += -DMODULE
ccflags-y += -DCONFIG_MODULES
ccflags-y += -DCONFIG_FTAPE_MODULE
ccflags-y += -DCONFIG_FT_INTERNAL_MODULE  
ccflags-y += -DCONFIG_ZFTAPE_MODULE

# Enable proc-fs stuff (TODO)
ccflags-y += -DCONFIG_PROC_FS
#ccflags-y += -DCONFIG_FT_PROC_FS

# Parallel port support enabled
ccflags-y += -DCONFIG_FT_PARPORT
ccflags-y += -DCONFIG_FT_TRAKKER
ccflags-y += -DCONFIG_FT_BPCK

# Compile with hardcoded defaults for FDC 0.
# (but can/should be configured via module parameters)
ccflags-y += -DCONFIG_FT_STD_FDC_0
ccflags-y += -DCONFIG_FT_AUTO_0


# Optionally set the default block size. This must be a multiple of 1024,
# since 1024 is the actual block size at the hardware level.
# This is hardcoded to 10240 if not set here, because 10240 is the default
# record size of gnu tar.
# ccflags-y += -DCONFIG_ZFT_DFLT_BLK_SZ=10240

# Set the default number of retries for soft errors. This is also configurable
# with the "ft_soft_retries" module parameter.
ccflags-y += -DFT_SOFT_RETRIES=3

# The code has quite a few switch cases that intentionally fall through,
# so ignore this particular warning.
ccflags-y += -Wimplicit-fallthrough=1


all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c .*.cmd *.markers *.order *.symvers
	find . -name "*.o" -delete
	find . -name ".*.cmd" -delete

install: all
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

help:
	@echo "Available targets:"
	@echo "  all      - Build all modules"
	@echo "  clean    - Clean build files"
	@echo "  install  - Install modules"
	@echo "  help     - Show this help"

.PHONY: all clean install help
