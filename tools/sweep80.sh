#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

# logical forward!
"$FTAPECMD" -f /dev/xrawqft0 -c 10

sleep 3

for ((i = 1; i <= 20; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 21
	sleep 1
done

