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
INITIAL_STEPS=19

# Microsteps between adjacent proprietary tracks, as a fraction: 46/5 = 9.2.
# A whole-number pitch would drift by a microstep every five tracks, so each
# transition steps the difference between the rounded cumulative positions,
# giving 9, 9, 10, 9, 9, 9, 9, 10, ... and keeping the head within half a
# microstep of the ideal position at every track.
TRACK_PITCH_NUM=46
TRACK_PITCH_DEN=5
#TRACK_PITCH_NUM=513
#TRACK_PITCH_DEN=56

# Capture length, and how long to wait before the capture is assumed done.
CAPTURE_SECS=54
CAPTURE_WAIT=56

# Cumulative microsteps below TOP_TRACK after n transitions, rounded.
steps_after() {
	echo $((($1 * TRACK_PITCH_NUM + TRACK_PITCH_DEN / 2) / TRACK_PITCH_DEN))
}

echo "Stepping down $INITIAL_STEPS microsteps..."
for ((i = 1; i <= INITIAL_STEPS; i++)); do
	"$FTAPECMD" -f "$DEV" -c 22
	sleep 0.25
done

for ((track = TOP_TRACK; track >= BOTTOM_TRACK; track--)); do
	
	
	
	# tell the drive we'll be changing direction
	# set poke pointer to 0x44
	"$FTAPECMD" -f /dev/xrawqft0 -c 42 -p 4
	"$FTAPECMD" -f /dev/xrawqft0 -c 43 -p 4
	# poke an even value into it
	"$FTAPECMD" -f /dev/xrawqft0 -c 41
	"$FTAPECMD" -f /dev/xrawqft0 -c 41
	
	# set for forward direction
	"$FTAPECMD" -f /dev/xrawqft0 -c 2
	
	
	#trackmod=$((track % 2))
	#
	# FLIP the parity of the wind direction, since we're reading
	# from top to bottom.
	#
	#if [ "$trackmod" -eq 0 ]; then
	#	"$FTAPECMD" -f /dev/xrawqft0 -c 3  # 2
	#else
	#	"$FTAPECMD" -f /dev/xrawqft0 -c 2  # 3
	#fi



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
	sleep 0.2
	
	
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
	
	
	

	if [ "$track" -gt "$BOTTOM_TRACK" ]; then
		n=$((TOP_TRACK - track + 1))
		# step down (one fewer step than we would have, since we already stepped
		# 1 down when reading in reverse.
		steps=$(($(steps_after "$n") - $(steps_after $((n - 1))) - 1))
		echo "Stepping down $steps microsteps to track $((track - 1))..."
		for ((i = 1; i <= steps; i++)); do
			"$FTAPECMD" -f "$DEV" -c 22
			sleep 0.25
		done
	fi
done

