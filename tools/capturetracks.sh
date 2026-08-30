#!/bin/bash
#
# Capture the flux of every proprietary track on a tape, top to bottom.
#
# The head is assumed to already be at the top track (TOP_TRACK) when this
# starts. For each track: start the logic analyzer in the background, set the
# tape moving with a logical forward, wait for the capture to finish, then
# micro-step down onto the next track.

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

DEV=${DEV:-/dev/xrawqft0}
OUTDIR=${OUTDIR:-.}

TOP_TRACK=19
BOTTOM_TRACK=0

# Microsteps between adjacent proprietary tracks.
TRACK_PITCH=9

# Capture length, and how long to wait before the capture is assumed done.
CAPTURE_SECS=54
CAPTURE_WAIT=56

for ((track = TOP_TRACK; track >= BOTTOM_TRACK; track--)); do
	out=$OUTDIR/tape_track${track}_flux.bin

	echo "=== track $track -> $out ==="
	sigrok-cli --driver fx2lafw --channels D0 --config samplerate=24m \
		--time=${CAPTURE_SECS}s --output-file "$out" &
	capture_pid=$!

	# logical forward, to get the tape moving under the capture
	"$FTAPECMD" -f "$DEV" -c 10

	sleep "$CAPTURE_WAIT"
	wait "$capture_pid"

	echo "Stepping down $TRACK_PITCH microsteps to track $((track - 1))..."
	for ((i = 1; i <= TRACK_PITCH; i++)); do
		"$FTAPECMD" -f "$DEV" -c 22
		sleep 0.25
	done
done

echo "Done: tracks $TOP_TRACK..$BOTTOM_TRACK captured into $OUTDIR"
