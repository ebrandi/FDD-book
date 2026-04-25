#!/bin/sh
#
# watch_usb.sh - Sample USB transfer stats periodically.

set -eu

DEV=${DEV:-ugen0.2}
PERIOD=${PERIOD:-5}

while :; do
	echo "=== $(date) ==="
	usbconfig -d "${DEV}" dump_stats 2>/dev/null || true
	sleep "${PERIOD}"
done
