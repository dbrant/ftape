###############################################################################
#
#       Copyright (C) 1997 Claus Heine.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
#
# $RCSfile: Rules.make,v $
# $Revision: 1.16 $
# $Date: 2000/07/20 10:24:47 $
#
#     Makefile rules for the QIC-40/80/3010/3020 ftape driver
#
#     This is a modified version of that Rules.make file that is
#     distributed with the kernel, hacked to work with the separate
#     ftape source tree and to support different kernel versions.
#
###############################################################################

#
# False targets.
#

.PHONY: all install uninstall clean realclean dummy

#
# Special variables which should not be exported
#
unexport EXTRA_ASFLAGS
unexport EXTRA_CFLAGS
unexport EXTRA_LDFLAGS
unexport EXTRA_ARFLAGS
unexport SUBDIRS
unexport SUB_DIRS
unexport ALL_SUB_DIRS
unexport MOD_SUB_DIRS
unexport O_TARGET
unexport O_OBJS
unexport L_OBJS
unexport M_OBJS
# intermediate objects that form part of a module
unexport MI_OBJS
unexport ALL_MOBJS
# objects that export symbol tables
unexport OX_OBJS
unexport LX_OBJS
unexport SYMTAB_OBJS

%.o: %.s $(TOPDIR)/MCONFIG $(LINUX_LOCATION)/.config \
		$(TOPDIR)/include/linux/autoconf.h \
		$(TOPDIR)/Makefile Makefile
	$(AS) $(ASFLAGS) $(EXTRA_CFLAGS) -o $@ $<

%.o: %.c $(TOPDIR)/MCONFIG $(LINUX_LOCATION)/.config \
		$(TOPDIR)/include/linux/autoconf.h \
		$(TOPDIR)/Makefile Makefile
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -c -o $@ $<

%.s: %.c $(TOPDIR)/MCONFIG $(LINUX_LOCATION)/.config \
		$(TOPDIR)/include/linux/autoconf.h \
		$(TOPDIR)/Makefile Makefile
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -S $< -o $@

.%.d: %.c $(TOPDIR)/MCONFIG $(LINUX_LOCATION)/.config \
		$(TOPDIR)/include/linux/autoconf.h \
		$(TOPDIR)/Makefile Makefile
	-@set -e ; \
	$(CC) -M $(CPPFLAGS) $< | sed 's/\($*\.o\):/\1 $@:/g' > $@

ifdef SMP

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c -o $*.o $<

else

.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

endif


#
# Get things started.
#
first_rule all:: sub_dirs modules
	$(MAKE) all_targets

#
#
#
all_targets: $(O_TARGET) $(PROGS)

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
ALL_O = $(OX_OBJS) $(O_OBJS)
$(O_TARGET): $(ALL_O) $(LINUX_LOCATION)/include/linux/config.h
	rm -f $@
ifneq "$(strip $(ALL_O))" ""
	$(LD) $(EXTRA_LDFLAGS) -r -o $@ $(ALL_O)
else
	$(AR) rcs $@
endif
endif # O_TARGET

#
# A rule to make subdirectories
#
sub_dirs: dummy
ifdef SUB_DIRS
	set -e; for i in $(SUB_DIRS); do $(MAKE) -C $$i; done
endif

#
# A rule to do nothing
#
dummy:

#
# This sets version suffixes on exported symbols
# Uses SYMTAB_OBJS
# Separate the object into "normal" objects and "exporting" objects
# Exporting objects are: all objects that define symbol tables
#

#
# A rule to make modules
#
ALL_MOBJS = $(MX_OBJS) $(M_OBJS)
modules: versions $(ALL_MOBJS) dummy
ifdef MOD_SUB_DIRS
	set -e; for i in $(MOD_SUB_DIRS); do $(MAKE) -C $$i modules; done
endif
ifneq "$(strip $(ALL_MOBJS))" ""
	for i in $(ALL_MOBJS); do ln -f $$(pwd)/$$i $(TOPDIR)/modules/ ; done
endif

versions:: $(TOPDIR)/include/linux/modftversions_.h 
ifdef MOD_SUB_DIRS
	for i in $(MOD_SUB_DIRS); \
	do \
	  $(MAKE) -C $$i NODEP=true versions; \
	done
endif

install:: modules
ifneq "$(strip $(ALL_MOBJS))" ""
	$(INSTALL) -m 0755 -d $(MODULESDIR)
	for i in $(ALL_MOBJS); \
	do \
	  $(INSTALL) -m 0644 $$(pwd)/$$i $(MODULESDIR); \
	done
endif

uninstall::
ifneq "$(strip $(ALL_MOBJS))" ""
	for i in $(ALL_MOBJS); \
	do \
	  rm -f $(MODULESDIR)/$$i; \
	done
endif

install uninstall::
ifdef MOD_SUB_DIRS
	for i in $(MOD_SUB_DIRS); \
	do \
	  $(MAKE) -C $$i NODEP=true $@; \
	done
endif

$(TOPDIR)/include/linux/autoconf.h:	$(TOPDIR)/MCONFIG \
					$(LINUX_LOCATION)/.config
	@echo "#ifndef _FTAPE_AUTOCONF_H_" > $@.new
	@echo "#define _FTAPE_AUTOCONF_H_" >> $@.new
	@echo -e "\n/* automatically created by make -- do NOT edit */\n" >> $@.new
	@echo -e "\n#include_next <linux/autoconf.h>\n" >> $@.new
	@rm -f $@.tmp.c
	@touch $@.tmp.c
	@gcc -E -dM $(CONFIG_OPT) $@.tmp.c | \
		 grep -E '(CONFIG_|MAINTAINER|BROKEN_FLOPPY)' | \
		 awk '{ printf("#undef %s\n%s\n", $$2, $$0); }'  >> $@.new
	@rm -f $@.tmp.c
	@echo "#endif /* _FTAPE_AUTOCONF_H_ */" >> $@.new
	@if ! test -f $@ ; then \
		mv $@.new $@ ; \
	else \
		cmp $@.new $@ > /dev/null || mv -f $@.new $@ ; \
	fi

SYMTAB_OBJS = $(OX_OBJS)

ifdef CONFIG_MODVERSIONS

ifneq "$(strip $(SYMTAB_OBJS))" ""

MODINCL = $(TOPDIR)/include/linux/modules

#
#	Differ 1 and 2Gig kernels to avoid module misload errors
#

ifdef CONFIG_2GB
	genksyms_smp_prefix := -p 2gig_
endif

$(MODINCL)/%.ver: %.c
	@if $(GENKSYMS) -k $(KERNELRELEASE) $(genksyms_smp_prefix) < /dev/null > /dev/null 2>&1; then \
	  echo  "$(CC) $(GKSFLAGS) -E -D__GENKSYMS__ $<" \
		"| $(GENKSYMS) -k $(KERNELRELEASE) $(genksyms_smp_prefix)" \
		"> $@.tmp; mv $@.tmp $@"; \
	  $(CC) $(GKSFLAGS) -E -D__GENKSYMS__ $< \
	  | $(GENKSYMS) -k $(KERNELRELEASE) $(genksyms_smp_prefix) > $@.tmp; \
	  mv $@.tmp $@; \
	else \
	  echo  "rm -f $@.tmp; $(CC) $(GKSFLAGS) -E -D__GENKSYMS__ $<" \
		"| $(GENKSYMS) -k 2.4.21 > $@ 2> /dev/null; rm -f $@.tmp"; \
	  $(CC) $(GKSFLAGS) -E -D__GENKSYMS__ $< \
	  | $(GENKSYMS) -k 2.4.21 > $@ 2> /dev/null; \
	fi

$(addprefix $(MODINCL)/,$(SYMTAB_OBJS:.o=.ver)): $(TOPDIR)/MCONFIG $(LINUX_LOCATION)/include/linux/autoconf.h $(TOPDIR)/include/linux/autoconf.h

$(TOPDIR)/include/linux/modftversions_.h: $(addprefix $(MODINCL)/,$(SYMTAB_OBJS:.o=.ver))
	@echo updating $(TOPDIR)/include/linux/modftversions_.h
	@(echo "#ifndef _FTAPE_MODVERSIONS_H"; \
	  echo "#define _FTAPE_MODVERSIONS_H"; \
	  echo "#include <linux/version.h>"; \
	  echo "#if LINUX_VERSION_CODE <= ((1<<16)+(2<<8)+13)"; \
	  echo "#include <linux/config.h>"; \
	  echo "#include <linux/module.h>"; \
	  echo "#endif" ; \
	  cd $(TOPDIR)/include/linux/modules; \
	  for f in *.ver; do \
	    if [ -f $$f ]; then echo "#include <linux/modules/$${f}>"; fi; \
	  done; \
	  echo "#include <linux/modversions.h>"; \
	  echo "#endif"; \
	) > $@

realclean::
	-rm -f $(addprefix $(MODINCL)/,$(SYMTAB_OBJS:.o=.ver))

else # SYMTAB_OBJS

$(TOPDIR)/include/linux/modftversions_.h:
	> $@

endif # SYMTAB_OBJS 

$(M_OBJS): $(TOPDIR)/include/linux/modftversions_.h
ifdef MAKING_MODULES
$(O_OBJS): $(TOPDIR)/include/linux/modftversions_.h
endif

else

$(TOPDIR)/include/linux/modftversions_.h:
	> $@

endif # CONFIG_MODVERSIONS

ifneq "$(strip $(SYMTAB_OBJS))" ""
$(SYMTAB_OBJS): $(TOPDIR)/include/linux/modftversions_.h $(SYMTAB_OBJS:.o=.c)
	$(CC) $(CFLAGS) -DEXPORT_SYMTAB -c $(@:.o=.c)

$(SYMTAB_OBJS:%.o=.%.d): $(SYMTAB_OBJS:%.o=%.c) \
			 $(TOPDIR)/include/linux/modftversions_.h \
			 $(TOPDIR)/MCONFIG
	-@set -e ; $(CC) -M $(CPPFLAGS) -DEXPORT_SYMTAB $< | \
	sed 's/\($*\.o\):/\1 $@:/g' > $@

endif

clean-no-recursion:
	- rm -f *.o *.s core $(PROGS)

clean:: clean-no-recursion
ifdef ALL_SUB_DIRS
	set -e; for i in $(ALL_SUB_DIRS); do 	\
	$(MAKE) -C $$i NODEP=true $@ ; 		\
	done
endif

realclean:: clean-no-recursion
	- rm -f .*.d
	- rm -f $(TOPDIR)/include/linux/modftversions_.h
ifdef ALL_SUB_DIRS
	set -e; for i in $(ALL_SUB_DIRS); do 	\
	$(MAKE) -C $$i NODEP=true $@ ; 		\
	done
endif

ALL_SRC = $(ALL_MOBJS:%.o=%.c) $(O_OBJS:%.o=%.c) $(OX_OBJS:%.o=%.c) 

tags: TAGS

tags-recursive:
	list='$(ALL_SUB_DIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) tags); \
	done

TAGS: tags-recursive $(wildcard *.c) $(wildcard *.h)
	tags=; \
	here=`pwd`; \
	list='$(ALL_SUB_DIRS)'; for subdir in $$list; do \
	  test -f $$subdir/TAGS && tags="$$tags -i $$here/$$subdir/TAGS"; \
	done; \
	list='$(wildcard *.c) $(wildcard *.h)'; \
	unique=`for i in $$list; do echo $$i; done | \
	  awk '    { files[$$0] = 1; } \
	       END { for (i in files) print i; }'`; \
	test -z "$(ETAGS_ARGS)$$unique$(LISP)$$tags" \
	  || (cd $$here && etags $(ETAGS_ARGS) $$tags  $$unique $(LISP) -o $$here/TAGS)

ifneq "$(strip $(ALL_SRC))" ""
ifneq ($(NODEP),true)
-include $(ALL_SRC:%.c=.%.d)
endif
endif
