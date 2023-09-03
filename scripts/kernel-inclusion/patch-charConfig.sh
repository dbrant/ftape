#! /bin/bash
#
# Patch drivers/char/Config.in
#
# Usage: sh patch-Config.sh Config.in
#
# Result will be output into file Config.in.new
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
CONFIG=${1:-Config.in}
#
# temporary files
#
TMPCONFIG=$TMPDIR/$(basename $CONFIG).$ME
#
# remove temp files on exit
#
trap exit 1 2 13 15
trap "rm -f $TMPCONFIG*" 0
#
# in principle, this is an awk, not sh script :-)
#
AWKSCRIPT='
BEGIN {
    state   = 0;
}

/CONFIG_FTAPE/ && (state == 0) {
    state = 1;
    next;
}

/if.*CONFIG_FTAPE/ && (state = 1) {
    state = 2;
    next;
}

/^fi/ && (state == 2) {
    state = 3;
    next;
}

(state == 2) {
    next;
}

/^endmenu/ && (state == 3) {
    saveend = $0;
    state = 4;
    next;
}

(state == 4) {
    print saveend;
    state == 3;
}

{
    print $0;
    next;
}
'
grep "drivers/char/ftape/Config.in" $CONFIG > /dev/null 2>&1 && exit 0
$AWK "$AWKSCRIPT"  $CONFIG > $TMPCONFIG

cat  >> $TMPCONFIG <<EOF

mainmenu_option next_comment
comment 'Floppy tape support (Ftape)'
tristate 'Ftape (QIC-80/Travan) support' CONFIG_FTAPE
if [ "\$CONFIG_FTAPE" != "n" ]; then
  source drivers/char/ftape/Config.in
fi
endmenu

endmenu
EOF
mv -f $TMPCONFIG $CONFIG.new
