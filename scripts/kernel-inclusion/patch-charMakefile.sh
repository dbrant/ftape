#! /bin/bash
#
# Patch init/main.c. Instead of providing a patch file for each
# Kernel version, we do it this way.
#
# Usage: sh patch-main.sh main.c
#
# Result will be output into file main.c.new
#
: ${AWK=awk}
: ${TMPDIR=/var/tmp}
#
# for temporary files
#
ME=$$
#
# Source file
#
MAKE=${1:-Makefile}
#
# temporary files
#
TMPMAKE=$TMPDIR/$(basename $MAKE).$ME
#
# remove temp files on exit
#
trap exit 1 2 13 15
trap "rm -f $TMPMAKE*; exit 1" 0
#
# in principle, this is an awk, not sh script :-)
#
AWKSCRIPT='
BEGIN {
    state   = 0;
}

/ifeq.*CONFIG_FTAPE/ && (state == 0) {
    state = 1;
    inif = 1;
    next;
}

/if(eq|def|neq)/ && (state == 1) {
    inif++;
    next;
}

/endif/ && (state == 1) {
    inif--;
    next;
}

(state == 1) && (inif == 0) {
    state = 2;
    printf("ifeq ($(CONFIG_FTAPE),y)\n");
    printf("SUB_DIRS     += ftape\n");
    printf("L_OBJS       += ftape/ftape.o\n");
    printf("MOD_SUB_DIRS += ftape\n");
    printf("else\n");
    printf("  ifeq ($(CONFIG_FTAPE),m)\n");
    printf("  MOD_SUB_DIRS += ftape\n");
    printf("  endif\n");
    printf("endif\n");
}

(state != 1) {
    print $0;
}
'
$AWK "$AWKSCRIPT"  $MAKE > $TMPMAKE
mv $TMPMAKE $MAKE.new
