#
#       Copyright (C) 1996-1997 Claus-Justus Heine.
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
# $Revision: 1.4 $
# $Date: 2000/06/23 12:09:41 $
#
#      Makefile for the QIC-40/80/3010/3020 zftape interface VFS to
#      ftape
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

O_TARGET := ftape-internal.o
O_OBJS   := fdc-internal.o fc-10.o

#
# can't use ISA PnP stuff if compiled into the kernel but isapnp compiled
# as module, unless we would create a separate configuration module which
# is loaded on demand ...
#
ifeq ($(CONFIG_FT_INTERNAL),m)
  ifneq ($(CONFIG_ISAPNP),)
    O_OBJS += fdc-isapnp.o
  endif
else
  ifeq ($(CONFIG_ISAPNP),y)
    O_OBJS += fdc-isapnp.o
  endif
endif

ifeq ($(CONFIG_FT_INTERNAL),m)
  M_OBJS = $(O_TARGET)
endif

include $(TOPDIR)/Rules.make

