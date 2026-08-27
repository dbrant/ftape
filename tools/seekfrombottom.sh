#!/bin/bash

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

# Proprietary track layout, measured in microsteps from the absolute bottom of
# the tape: proprietary track 0 sits TRACK0_STEPS microsteps above the bottom,
# and each successive track is TRACK_PITCH microsteps further up.
TRACK0_STEPS=24
TRACK_PITCH=9

if [ $# -ne 1 ]; then
	echo "usage: $self <track>" >&2
	exit 1
fi

tracknum=$1
if ! [[ $tracknum =~ ^[0-9]+$ ]]; then
	echo "$self: track must be a non-negative integer" >&2
	exit 1
fi

qictrack=$((tracknum % 2))

echo "Seeking to QIC track $qictrack ..."
"$FTAPECMD" -f /dev/xrawqft0 -c 13 -p "$qictrack"
sleep 2

echo "Seeking to bottom..."
for ((i = 1; i <= 120; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 22
	sleep 0.1
done

offset=$((TRACK0_STEPS + (tracknum * TRACK_PITCH)))
echo "Seeking up $offset microsteps..."
for ((i = 1; i <= offset; i++)); do
	"$FTAPECMD" -f /dev/xrawqft0 -c 21
	sleep 0.25
done

