#
#       Copyright (C) 1996-1998 Claus-Justus Heine.
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
# $RCSfile: Makefile,v $
# $Revision: 1.36 $
# $Date: 2000/07/03 09:08:04 $
#
#      Makefile for the QIC-40/80/3010/3020 floppy-tape driver for
#      Linux.
#
#
# The configuration section is now in the MCONFIG file.
#
TOPDIR=.
#
# all other configuration is in $(TOPDIR)/MCONFIG:
#
include $(TOPDIR)/MCONFIG

SUBDIRS = ftape

default: all

$(SUBDIRS):
	make -C $@ all

all::
	+for i in $(SUBDIRS) ; do make -C $$i $@ ; done

install uninstall tags::
	+for i in $(SUBDIRS) ; do make -C $$i NODEP=true $@ ; done

install::
	/sbin/depmod -a
	$(SHELL) ./scripts/MAKEDEV.ftape

uninstall::
	-rmdir $(DOCDIR)
	/sbin/depmod -a

clean realclean:
	+for i in $(SUBDIRS) ; do make -C $$i NODEP=true $@ ; done

distclean: realclean
	find . -name \*~ -exec rm -f \{\} \;
	-rm -f ftape/parport/ftape-makecrc
	-rm -f ftape/parport/bpck-fdc-crctab.h
	find . -name TAGS -exec rm -f \{\} \;
	-rm -f include/linux/autoconf.h*

tags:: TAGS

TAGS:
	here=`pwd` ; etags -i $$here/ftape/TAGS

.PHONY: all doc $(SUBDIRS) install uninstall \
	clean realclean distclean tags
