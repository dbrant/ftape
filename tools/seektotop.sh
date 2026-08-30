#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

# seek QIC track 18 (near the top already)
"$FTAPECMD" -f /dev/xrawqft0 -c 13 -p 18
sleep 3

echo "Seeking to top..."
for ((i = 1; i <= 80; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 21
	sleep 0.25
done

