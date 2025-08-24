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
ccflags-y += -DTHE_FTAPE_MAINTAINER=\"ftape-maintainer@kernel.org\"
# ccflags-y += -DCONFIG_FT_PROC_FS  # Disabled proc support for now
ccflags-y += -DCONFIG_FTAPE_MODULE
ccflags-y += -DCONFIG_FT_INTERNAL_MODULE  
ccflags-y += -DCONFIG_ZFTAPE_MODULE
# Parallel port support enabled
ccflags-y += -DCONFIG_FT_PARPORT
ccflags-y += -DCONFIG_FT_TRAKKER
ccflags-y += -DCONFIG_FT_BPCK
ccflags-y += -DFT_SOFT_RETRIES=6

# Disable some problematic features for now
ccflags-y += -DCONFIG_FT_NO_TRACE_AT_ALL

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