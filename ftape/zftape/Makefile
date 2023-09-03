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
# $Revision: 1.9 $
# $Date: 2000/07/06 14:42:20 $
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

O_TARGET := zftape.o
O_OBJS   := zftape-rw.o zftape-ctl.o zftape-read.o \
	    zftape-write.o zftape-vtbl.o zftape-eof.o \
	    zftape-init.o zftape-buffers.o

ifeq ($(CONFIG_ZFT_COMPRESSOR),m)
OX_OBJS  += zftape_syms.o
endif

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make

