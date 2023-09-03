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
# $Revision: 1.4 $
# $Date: 1998/12/18 22:38:38 $
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

O_OBJS   := ftape-setup.o

O_TARGET := setup.o

include $(TOPDIR)/Rules.make
