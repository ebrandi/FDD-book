#!/bin/sh
#
# sample-cycles.sh - sample system-wide CPU cycles while a workload runs.
#
# Usage: ./sample-cycles.sh [duration_seconds]

DURATION=${1:-30}

if ! kldstat | grep -q hwpmc; then
    echo "Loading hwpmc..."
    sudo kldload hwpmc
fi

echo "Sampling CPU cycles for $DURATION seconds..."
echo "Run a workload (for example, dd if=/dev/perfdemo of=/dev/null \\"
echo "    bs=4096 count=100000) in another terminal now."
echo

sudo pmcstat -S cycles -O pmc.out sleep $DURATION
echo
echo "Samples written to pmc.out. Producing top-N report..."
sudo pmcstat -R pmc.out -T | head -30
