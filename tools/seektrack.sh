#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

# seek QIC track 0
"$FTAPECMD" -f /dev/xrawqft0 -c 13 -p 0
sleep 2

# micro-step down all the way
for ((i = 1; i <= 120; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 22
	sleep 0.1
done

tracknum=$1
trackpos=$((24 + (tracknum * 9)))

echo "Seeking to $trackpos"

# micro-step up
for ((i = 1; i <= trackpos; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 21
	sleep 0.1
done







