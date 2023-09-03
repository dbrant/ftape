#! /bin/bash
#
# Patch Configure.help. Instead of providing a patch file for each
# Kernel version, we do it this way.
#
# Usage: sh patch-confhelp.sh Configure.help Configure.help.ftape
#
# Result will be output into file Configure.help.new
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
CONFHELP=${1:-Configure.help}
#
# let this depend on the kernel version.
#
FTAPEHELP=${2:-Configure.help.ftape}
#
# tmporary Configure.help, prepared Configure.ftape.help, localwords
#
TMPHELP=$TMPDIR/Configure.help.$ME
TMPFTAPE=$TMPDIR/Configure.help.ftape.$ME
TMPLOCALWORDS=$TMPDIR/Localwords.ftape.$ME
#
# remove temp files on exit
#
trap exit 1 2 13 15
trap "rm -f $TMPHELP* $TMPFTAPE* $TMPLOCALWORDS*; exit 1" 0
#
# first ftape line in Configure.help. We insert the new ftape
# Configure.help at this point
#
FTAPE_LINE=($(grep -E --line-number 'CONFIG_(FT|ZFT)' $CONFHELP|cut -d ':' -f 1))
FTAPE_LINE=$(( $FTAPE_LINE - 2 ))
#
# remove all FTAPE related items.
#
REMOVEAWKSCRIPT='
BEGIN {
  state = -1;
}

$1 ~ /CONFIG_(FT|ZFT)/ && (state == 0) {
  state = 1;
  next;
}

$1 !~ /CONFIG_(FT|ZFT)/ && $1 ~ /CONFIG_/ && (state == 1) {
  print save;
  save = $0;
  state = 0;
  next;
}

(state == 1) {
  save = $0;
  next;
}

#
# hack for first line.
#
(state == -1) {
  save = $0;
  state = 0;
  next;
}

(state == 0) {
  print save;
  save = $0;
  next;
}

END {
  print save;
}
'
$AWK "$REMOVEAWKSCRIPT"  $CONFHELP > $TMPHELP
#
# remove Localwords and "-*-Text-*-" mode
#
grep -v "Yeah, Emacs" $FTAPEHELP | grep -v "LocalWords:" > $TMPFTAPE
#
# extrace LocalWord lines
#
grep "LocalWords:" $FTAPEHELP | \
  sed -e 's/^[ ]*LocalWords:/# LocalWords:/g' > $TMPLOCALWORDS
#
# Insert new help text
#
sed "${FTAPE_LINE[0]} r $TMPFTAPE" $TMPHELP > $TMPHELP.new
#
# add new LocalWords: to make ispell happy
#
cat $TMPHELP.new $TMPLOCALWORDS > $CONFHELP.new


