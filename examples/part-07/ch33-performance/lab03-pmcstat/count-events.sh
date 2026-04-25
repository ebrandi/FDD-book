#!/bin/sh
#
# count-events.sh - count instructions retired and LLC misses during
# a fixed-duration workload window.
#
# Usage: ./count-events.sh [duration_seconds]

DURATION=${1:-10}

if ! kldstat | grep -q hwpmc; then
    echo "Loading hwpmc..."
    sudo kldload hwpmc
fi

echo "Counting cycles and instructions for $DURATION seconds..."
echo "Run a workload in another terminal now."
echo

sudo pmcstat -c 0 -w $DURATION -s instructions -s cycles

echo
echo "LLC_MISSES count for $DURATION seconds..."
sudo pmcstat -c 0 -w $DURATION -s LLC_MISSES
