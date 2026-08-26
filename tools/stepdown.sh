#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

steps=$1

for ((i = 1; i <= steps; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 22
	sleep 0.1
done

