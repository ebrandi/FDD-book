#!/bin/sh
#
# evdemo_stress.sh - load/unload stress for the v2.5-async driver.
#
# Usage: sudo ./evdemo_stress.sh [cycles] [lab06_path]
#
# Defaults: 100 cycles, module taken from ../lab06-v25-async/.
#
# Each cycle:
#   1. kldload the module.
#   2. trigger a burst of events.
#   3. open a reader in the background.
#   4. trigger more events.
#   5. kldunload the module.
# If any step fails, the script exits non-zero. A driver with a
# correct detach path survives many hundreds of cycles without
# panicking, leaking memory, or leaving stale /dev/evdemo nodes.

set -e

CYCLES=${1:-100}
LAB=${2:-"../lab06-v25-async"}

if [ ! -f "${LAB}/evdemo.ko" ]; then
	echo "error: ${LAB}/evdemo.ko not found. Run 'make' there first." >&2
	exit 1
fi

MODULE="${LAB}/evdemo.ko"

# Clean up any leftover module from a previous run.
kldstat | grep -q evdemo && kldunload evdemo || true

i=0
while [ "$i" -lt "${CYCLES}" ]; do
	i=$((i + 1))
	printf "cycle %4d/%d ... " "$i" "${CYCLES}"

	kldload "${MODULE}"

	sysctl dev.evdemo.burst=50 >/dev/null

	# A short-lived reader that consumes a few events and exits.
	(cat /dev/evdemo >/dev/null) &
	READER_PID=$!
	sleep 0.1

	sysctl dev.evdemo.burst=50 >/dev/null
	sleep 0.1

	kill "${READER_PID}" 2>/dev/null || true
	wait "${READER_PID}" 2>/dev/null || true

	kldunload evdemo

	echo "ok"
done

echo "stress test completed ${CYCLES} cycles without panic"
