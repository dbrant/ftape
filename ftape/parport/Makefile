#
#       Copyright (C) 1996, 1997 Claus-Justus Heine.
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
# $Revision: 1.10 $
# $Date: 1999/03/29 04:32:26 $
#
#       Makefile for parport modules for ftape.
#

#
#  This isn't used inside the kernel, only for my private development
#  version
#
ifndef TOPDIR
TOPDIR=../..
include $(TOPDIR)/MCONFIG
endif

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_OBJS   :=
O_TARGET := ftape-parport.o
M_OBJS   :=

ifeq ($(CONFIG_FT_PARPORT),y)
  O_OBJS += fdc-parport.o
endif

ifeq ($(CONFIG_FT_TRAKKER),y)
  O_OBJS += trakker.o
else
  ifeq ($(CONFIG_FT_TRAKKER),m)
    M_OBJS += trakker.o
  endif
endif

ifeq ($(CONFIG_FT_BPCK),y)
  O_OBJS += bpck-fdc.o
else
  ifeq ($(CONFIG_FT_BPCK),m)
    M_OBJS += bpck-fdc.o
  endif
endif

include $(TOPDIR)/Rules.make

ftape-makecrc: ftape-makecrc.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

bpck-fdc-crctab.h: ftape-makecrc
	./ftape-makecrc 0x1021 > $@

bpck-fdc.c: bpck-fdc-crctab.h
