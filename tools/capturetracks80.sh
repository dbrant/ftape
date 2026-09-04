#!/bin/bash
#
# Capture the flux of every QIC-80 track on a tape, from 0 to 27.
#

set -u

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

DEV=${DEV:-/dev/xrawqft0}
OUTDIR=${OUTDIR:-.}

# Capture length, and how long to wait before the capture is assumed done.
CAPTURE_SECS=78
CAPTURE_WAIT=80

for ((track = 6; track < 7; track++)); do
	
	echo "=== moving to track $track ==="
	"$FTAPECMD" -f "$DEV" -c 13 -p "$track"
	sleep 5



	echo "Stepping down 1 microsteps..."
	"$FTAPECMD" -f "$DEV" -c 22
	sleep 0.25



	
	# tell the drive we'll be changing direction
	# set poke pointer to 0x44
	"$FTAPECMD" -f /dev/xrawqft0 -c 42 -p 4
	"$FTAPECMD" -f /dev/xrawqft0 -c 43 -p 4
	# poke an even value into it
	"$FTAPECMD" -f /dev/xrawqft0 -c 41
	"$FTAPECMD" -f /dev/xrawqft0 -c 41
	# set for forward direction
	"$FTAPECMD" -f /dev/xrawqft0 -c 2
	

	out=$OUTDIR/tape_track${track}_a_flux.bin
	echo "=== track $track -> $out ==="
	
	sigrok-cli -d fx2lafw --channels D0 --config samplerate=24m -O binary --time=${CAPTURE_SECS}s --output-file "$out" &
	capture_pid=$!

	# logical forward, to get the tape moving under the capture
	"$FTAPECMD" -f "$DEV" -c 10

	sleep "$CAPTURE_WAIT"
	wait "$capture_pid"
	
	
	
	echo "Stepping down 1 microstep..."
	"$FTAPECMD" -f "$DEV" -c 22
	sleep 0.25
	
	
	
	# tell the drive we'll be changing direction
	# set poke pointer to 0x44
	"$FTAPECMD" -f /dev/xrawqft0 -c 42 -p 4
	"$FTAPECMD" -f /dev/xrawqft0 -c 43 -p 4
	# poke an even value into it
	"$FTAPECMD" -f /dev/xrawqft0 -c 41
	"$FTAPECMD" -f /dev/xrawqft0 -c 41
	# set for reverse direction
	"$FTAPECMD" -f /dev/xrawqft0 -c 3
	
	
	out=$OUTDIR/tape_track${track}_b_flux.bin
	echo "=== track $track -> $out ==="
	
	sigrok-cli -d fx2lafw --channels D0 --config samplerate=24m -O binary --time=${CAPTURE_SECS}s --output-file "$out" &
	capture_pid=$!

	# logical forward, to get the tape moving under the capture
	"$FTAPECMD" -f "$DEV" -c 10

	sleep "$CAPTURE_WAIT"
	wait "$capture_pid"
	
	
	echo "Stepping up 2 microsteps..."
	"$FTAPECMD" -f "$DEV" -c 21
	sleep 0.25
	"$FTAPECMD" -f "$DEV" -c 21
	sleep 0.25

	
done

