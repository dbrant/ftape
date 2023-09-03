#! /bin/bash
#
# Patch Documentation/00-INDEX
#
# Usage: sh patch-00-INDEX.sh 00-INDEX
#
# Result will be output into file 00-INDEX.new
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
INDEX=${1:-00-INDEX}
#
# temporary files
#
TMPINDEX=$TMPDIR/$(basename $INDEX).$ME
#
# remove temp files on exit
#
trap exit 1 2 13 15
trap "rm -f $TMPINDEX*; exit 1" 0
#
# in principle, this is an awk, not sh script :-)
#
AWKSCRIPT='
BEGIN {
    state   = 0;
}

$1 ~ /00-INDEX/ {
    state = 1;
    print $0;
    next;
}

$1 ~ /ftape/ && (state == 1) {
    state = 2;
    next;
}
$1 ~ /^-$/ && (state == 2) {
    state = 1;
    next;
}

$1 !~ /^-$/ && ($1 > "ftape/") && (state == 1) {
    printf("ftape/\n");
    printf("\t- directory with information on the Linux floppy tape driver.\n");
    print $0;
    state = 3;
    next;
}

{
    print $0;
    next;
}

'

$AWK "$AWKSCRIPT"  $INDEX > $TMPINDEX
mv $TMPINDEX $INDEX.new
