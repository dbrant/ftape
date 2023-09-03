#! /bin/bash
#
# determine kernel source path
#
LINUX_LOCATION=${1:-/dev/null}
FTAPE_SOURCES=${2:-$(dirname $0)/../../}
PATCH_SOURCES=$(dirname $0)
FTINCLUDES="ftape-header-segment.h ftape-vendors.h ftape.h qic117.h zftape.h"
MAJOR=$(grep -E '^VERSION =' $LINUX_LOCATION/Makefile|awk '{ print $3; }')
MINOR=$(grep -E '^PATCHLEVEL =' $LINUX_LOCATION/Makefile|awk '{ print $3; }')
SUB_VERSION=$(grep -E '^SUBLEVEL =' $LINUX_LOCATION/Makefile|awk '{ print $3; }')
#
#
#
#cd $(dirname $0)
#
#
#
function patch_file () {
    cmd=$1
    file=$2
    
    $cmd $file
    if test -f $file.new
    then
	mv -f $file $file.orig
	mv -f $file.new $file
    fi
}

patch_file $PATCH_SOURCES/patch-charConfig.sh \
	    $LINUX_LOCATION/drivers/char/Config.in
patch_file $PATCH_SOURCES/patch-charMakefile.sh \
	    $LINUX_LOCATION/drivers/char/Makefile
patch_file $PATCH_SOURCES/patch-00-INDEX.sh \
	    $LINUX_LOCATION/Documentation/00-INDEX
if test $MAJOR$MINOR -lt 24; then
    patch_file $PATCH_SOURCES/patch-main.sh \
		$LINUX_LOCATION/init/main.c
fi
#
# remove all old ftape Linux sources
#
rm -fr $LINUX_LOCATION/drivers/char/ftape
for inc in $FTINCLUDES
do
    rm -f $LINUX_LOCATION/include/linux/$inc
done
rm -f $LINUX_LOCATION/Documentation/ftape.txt
#
# Copy files into Documentation/ftape/
#
rm -fr $LINUX_LOCATION/Documentation/ftape
mkdir $LINUX_LOCATION/Documentation/ftape
cp -aR $PATCH_SOURCES/Documentation/* $LINUX_LOCATION/Documentation/ftape/
rm -rf $LINUX_LOCATION/Documentation/ftape/CVS/
cp $FTAPE_SOURCES/RELEASE-NOTES $LINUX_LOCATION/Documentation/ftape/
#
# patch Documentation/Configure.help
#
DESTCONFHELP=$LINUX_LOCATION/Documentation/Configure.help
SRCCONFHELP=$FTAPE_SOURCES/scripts/kernel-inclusion/v$MAJOR.$MINOR/Configure.help.ftape
$PATCH_SOURCES/patch-confhelp.sh $DESTCONFHELP $SRCCONFHELP
mv -f $DESTCONFHELP $DESTCONFHELP.orig
mv -f $DESTCONFHELP.new $DESTCONFHELP
#
# Copy files into include/linux/
#
for inc in $FTINCLUDES
do
    cp $FTAPE_SOURCES/include/linux/$inc $LINUX_LOCATION/include/linux/$inc
done
#
# patch include/linux/mtio.h
#
$PATCH_SOURCES/patch-mtio.sh $LINUX_LOCATION/include/linux/mtio.h \
	    $FTAPE_SOURCES/include/linux/mtio.h
#
# Copy files into drivers/char/ftape
#
mkdir $LINUX_LOCATION/drivers/char/ftape
for src in Makefile setup lowlevel internal parport zftape compressor
do
    cp -aR $FTAPE_SOURCES/ftape/$src $LINUX_LOCATION/drivers/char/ftape/$src
done
cp $FTAPE_SOURCES/scripts/kernel-inclusion/v$MAJOR.$MINOR/Config.in \
   $LINUX_LOCATION/drivers/char/ftape/Config.in
find $LINUX_LOCATION/drivers/char/ftape/ -name CVS -o -name '.cvsignore' -o -name '.*.d' -o -name '*.o' -o -name '.#*' -exec rm -rf '{}' ';'
#
#
#
rm -f $(find $LINUX_LOCATION -name "*.orig")
#
# Ready
#
