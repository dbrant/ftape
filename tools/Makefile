#
# Makefile for the userspace ftape utilities
#

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
# use the driver's own headers, so that the ioctl definitions and the
# QIC-117 command/error tables always match the driver being built here.
CPPFLAGS += -I../include

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

PROGS := ftapecmd

all: $(PROGS)

ftapecmd: ftapecmd.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(PROGS) $(DESTDIR)$(BINDIR)

clean:
	rm -f $(PROGS)

.PHONY: all install clean
