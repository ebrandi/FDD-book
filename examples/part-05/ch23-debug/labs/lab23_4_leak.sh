#!/bin/sh
#
# lab23_4_leak.sh - Lab 23.4: Finding a Memory Leak with vmstat -m
#
# This script does not automatically modify source files.  It runs the
# workload and checks vmstat -m, but the lab expects the reader to
# introduce the deliberate leak in myfirst_open() themselves, reload,
# run this script, fix the leak in myfirst_close(), reload, and run
# again.

set -e

echo "=== Lab 23.4: Finding a Memory Leak with vmstat -m ==="
echo
echo "Snapshot 1: baseline memory pool state for myfirst"
vmstat -m | grep -E 'Type|myfirst' || echo "(no myfirst pool - is the driver loaded?)"
echo
echo "Running workload: 1000 open/read/close cycles..."
for i in $(seq 1 1000); do
	cat /dev/myfirst0 > /dev/null
done
sync
echo
echo "Snapshot 2: memory pool state after workload"
vmstat -m | grep -E 'Type|myfirst'
echo
echo "Expected behaviour:"
echo "  - If the leak is present, InUse grew by ~1000 since snapshot 1."
echo "  - If the leak is fixed, InUse is essentially unchanged."
echo
echo "=== End of Lab 23.4 ==="
