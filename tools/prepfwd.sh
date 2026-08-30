#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

# set poke pointer to 0x44
"$FTAPECMD" -f /dev/xrawqft0 -c 42 -p 4
"$FTAPECMD" -f /dev/xrawqft0 -c 43 -p 4
# poke an even value into it
"$FTAPECMD" -f /dev/xrawqft0 -c 41
"$FTAPECMD" -f /dev/xrawqft0 -c 41
"$FTAPECMD" -f /dev/xrawqft0 -c 2

echo "Ready to wind tape FORWARD."

