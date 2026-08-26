#!/bin/bash
#
# Send a single QIC-117 command to a floppy tape drive, over and over.
#
# Usage: ftaperepeat.sh <command> <count> [ftapecmd options ...]
#
#   <command>  QIC-117 command number, decimal (20) or hex (0x14)
#   <count>    how many times to send it
#
# The commands are separated by a 500 ms pause. Any further arguments are
# passed straight through to ftapecmd, so e.g. "-b 8" to read back a result,
# or "-f /dev/xrawqft0" to pick the device (which can also be set with
# $FTAPE_DEV; ftapecmd itself defaults to /dev/nrawqft0).
#
# Examples:
#   ./ftaperepeat.sh 20 5                       # soft-select, 5 times
#   ./ftaperepeat.sh 6 10 -b 8 -f /dev/xrawqft0 # poll drive status on the
#                                               # bare device, 10 times

set -u

DELAY=0.5

self=$(basename "$0")
self_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

FTAPECMD=${FTAPECMD:-$self_dir/ftapecmd}
if [ ! -x "$FTAPECMD" ]; then
	FTAPECMD=ftapecmd
fi

usage() {
	echo "Usage: $self <command> <count> [ftapecmd options ...]" >&2
	exit 1
}

if [ $# -lt 2 ]; then
	usage
fi

command=$1
count=$2
shift 2

if ! [[ $command =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then
	echo "$self: command must be an integer: $command" >&2
	usage
fi
if ! [[ $count =~ ^[0-9]+$ ]] || [ "$count" -lt 1 ]; then
	echo "$self: count must be a positive integer: $count" >&2
	usage
fi

status=0
for ((i = 1; i <= count; i++)); do
	echo "=== $self: command $command ($i/$count) ==="
	"$FTAPECMD" -c "$command" "$@" || status=$?
	if [ "$i" -lt "$count" ]; then
		sleep "$DELAY"
	fi
done

exit $status
