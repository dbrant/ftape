#! /bin/bash
#
# Patch include/linux/mtio.h. Instead of providing a patch file for each
# Kernel version, we do it this way.
#
# Usage: sh patch-mtio.sh mtio.c ftape-4.x/include/linux/mtio.h
#
# Result will be output into file mtio.c.new
#
: ${AWK=awk}
: ${TMPDIR=/var/tmp}
#
# for temporary files
#
ME=$$
#
# Source files
#
MTIO=${1:-mtio.h}
FTMTIO=${2:-ftape-4.x/include/linux/mtio.h}
#
# temporary files
#
TMPMTIO=$TMPDIR/$(basename $MTIO).$ME
#
# remove temp files on exit
#
trap exit 1 2 13 15
trap "rm -f $TMPMTIO*; exit 1" 0
#
# in principle, this is an awk, not sh script :-)
#
AWKSCRIPT='
BEGIN {
    count   = 0;
    hook    = 0;
    discard = 0;
}

{
    save[count++] = $0;
}

$1 ~ /^@@/ {
    if (!discard) for (i = 0; i < count - 1; i ++) {
	print save[i];
    }
    save[0] = save[count - 1];
    count = 1;
    hook = 1;
    discard = 0;
    next;
}

/^\+.*daddr_t/ && (hook == 1) {
# discard this hook
    discard = 1;
    next;
}

/#define.* MT_ST/ && (hook == 1) {
# discard this hook
    discard = 1;
    next;
}


END {
    if (!discard) for (i = 0; i < count - 1; i ++) {
	print save[i];
    }
}
'
diff -u $MTIO $FTMTIO | $AWK "$AWKSCRIPT" > $TMPMTIO.diff
patch $MTIO $TMPMTIO.diff > /dev/null 2>&1 

