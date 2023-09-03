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
MAIN=${1:-main.c}
#
# temporary files
#
TMPMAIN=$TMPDIR/$(basename $MAIN).$ME
#
# remove temp files on exit
#
trap exit 1 2 13 15
trap "rm -f $TMPMAIN*; exit 1" 0
#
# in principle, this is an awk, not sh script :-)
#
AWKSCRIPT='
BEGIN {
    count   = 0;
    state   = 0;
    defcnt  = 0;
    declcnt = 0;
}

{
    save[count++] = $0;
}

/#ifdef CONFIG_/ && (state == 0) {
    state = 1;
    next;
}
/extern.*void.*_setup/ && (state == 1) {
    state = 2;
    next;
}
/#endif/ && (state == 2) {
    state = 0;
    declcnt = count;
    next;
}
/{.*".*=",.*_setup.*},/ && (state == 1) {
    state = 12;
    next;
}
/#endif/ && (state == 12) {
    state = 0;
    defcnt = count;
    next;
}

{
    state = 0;
}

END {
    for (i=0; i < declcnt; i++) {
	print save[i];
    }
    printf("#ifdef CONFIG_FTAPE\n");
    printf("extern void ftape_setup(char *str, int *ints);\n");
    printf("#endif CONFIG_FTAPE\n");
    for (i=declcnt; i < defcnt; i++) {
	print save[i];
    }
    printf("#ifdef CONFIG_FTAPE\n");
    printf("\t{ \"ftape=\", ftape_setup },\n");
    printf("#endif\n");
    for (i=defcnt; i < count; i++) {
	print save[i]
    }
}
'
grep CONFIG_FTAPE $MAIN > /dev/null 2>&1 && exit 0
$AWK "$AWKSCRIPT"  $MAIN > $TMPMAIN
mv $TMPMAIN $MAIN.new
