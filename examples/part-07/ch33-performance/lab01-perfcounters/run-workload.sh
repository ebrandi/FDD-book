#!/bin/sh
#
# run-workload.sh - run a simple read workload against /dev/perfdemo.
#
# Usage: ./run-workload.sh [count]
#
# Default count is 10000 blocks of 4096 bytes (40 MB total).

COUNT=${1:-10000}
DEV=${DEV:-/dev/perfdemo}
BS=${BS:-4096}

if [ ! -c "$DEV" ]; then
    echo "Error: $DEV does not exist. Load the driver first:"
    echo "  sudo kldload ./perfdemo.ko"
    exit 1
fi

echo "Reading $COUNT blocks of $BS bytes from $DEV..."
time dd if=$DEV of=/dev/null bs=$BS count=$COUNT 2>&1

echo
echo "Counter values after workload:"
sysctl hw.perfdemo
