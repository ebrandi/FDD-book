#!/bin/sh
#
# lab23_2_dtrace.sh - Lab 23.2: Measuring the Driver with DTrace
#
# Exercises the driver while DTrace counts function entries.

set -e

if [ "$(id -u)" -ne 0 ]; then
	echo "This script must run as root (for dtrace and kldload)" >&2
	exit 1
fi

echo "=== Lab 23.2: Measuring the Driver with DTrace ==="
echo

MODULE=${1:-./myfirst.ko}
if ! kldstat | grep -q myfirst; then
	echo "Loading module: $MODULE"
	kldload "$MODULE"
fi

echo
echo "Starting DTrace count in the background..."
dtrace -q -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }
              tick-5s { exit(0); }' &
DT_PID=$!

sleep 1
echo
echo "Exercising the driver (100 reads)..."
for i in $(seq 1 100); do
	cat /dev/myfirst0 > /dev/null
done

wait $DT_PID
echo
echo "Expected: 100 open, 100 close, 100 read entries."
echo
echo "=== End of Lab 23.2 ==="
