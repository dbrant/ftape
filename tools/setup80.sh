#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi


# phantom select
"$FTAPECMD" -f /dev/xrawqft0 -c 46 -p 0

# report drive status
"$FTAPECMD" -f /dev/xrawqft0 -c 6 -b 8
# get and clear error
"$FTAPECMD" -f /dev/xrawqft0 -c 7 -b 16
# report drive status
"$FTAPECMD" -f /dev/xrawqft0 -c 6 -b 8

# enter diagnostic mode
"$FTAPECMD" -f /dev/xrawqft0 -c 28
"$FTAPECMD" -f /dev/xrawqft0 -c 28

# peek RAM address
#"$FTAPECMD" -f /dev/xrawqft0 -c 37 -b 8
# poke new value
#"$FTAPECMD" -f /dev/xrawqft0 -c 42 -p 2
#"$FTAPECMD" -f /dev/xrawqft0 -c 43 -p 7
#"$FTAPECMD" -f /dev/xrawqft0 -c 41
#"$FTAPECMD" -f /dev/xrawqft0 -c 41
#"$FTAPECMD" -f /dev/xrawqft0 -c 14 # 12 + 2

# re-consume RAM parameters (?)
"$FTAPECMD" -f /dev/xrawqft0 -c 33
sleep 5

# enter primary mode
"$FTAPECMD" -f /dev/xrawqft0 -c 30

# get and clear error
"$FTAPECMD" -f /dev/xrawqft0 -c 7 -b 16
# report drive status
"$FTAPECMD" -f /dev/xrawqft0 -c 6 -b 8

# set data rate
"$FTAPECMD" -f /dev/xrawqft0 -c 27 -p 3

# seek QIC track 0
"$FTAPECMD" -f /dev/xrawqft0 -c 13 -p 0
sleep 2

