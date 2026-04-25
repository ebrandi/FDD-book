#!/bin/sh
#
# race.sh - Stress test with concurrent readers and writers.
#
# Spawns background dd processes and lets them run for a fixed
# duration. Useful for exercising lock discipline.

set -eu

DEVICE=${DEVICE:-/dev/myblk0}
DURATION=${DURATION:-10}

if [ ! -c "${DEVICE}" ]; then
	echo "${DEVICE} not found" >&2
	exit 1
fi

echo "Launching 4 writers and 4 readers against ${DEVICE}..."
pids=""
for i in 1 2 3 4; do
	dd if=/dev/urandom of="${DEVICE}" bs=4096 count=999999 \
	   >/dev/null 2>&1 &
	pids="${pids} $!"
	dd if="${DEVICE}" of=/dev/null bs=4096 count=999999 \
	   >/dev/null 2>&1 &
	pids="${pids} $!"
done

echo "Running for ${DURATION} seconds..."
sleep "${DURATION}"

echo "Stopping..."
for p in ${pids}; do
	kill -TERM "${p}" 2>/dev/null || true
done
wait 2>/dev/null || true

echo "Done. Check gstat and geom disk list for anomalies."
