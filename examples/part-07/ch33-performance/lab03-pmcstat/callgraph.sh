#!/bin/sh
#
# callgraph.sh - collect callgraph samples while a workload runs.
#
# Usage: ./callgraph.sh [duration_seconds]
#
# Produces pmc.cg, which can be converted to a flame graph with:
#   pmcstat -R pmc.cg -G - | stackcollapse-pmc.pl | flamegraph.pl > out.svg
# (stackcollapse-pmc.pl and flamegraph.pl are from Brendan Gregg's
#  FlameGraph repository.)

DURATION=${1:-30}

if ! kldstat | grep -q hwpmc; then
    echo "Loading hwpmc..."
    sudo kldload hwpmc
fi

echo "Collecting callgraph samples for $DURATION seconds..."
sudo pmcstat -S cycles -O pmc.cg -n 1 sleep $DURATION

echo
echo "Samples written to pmc.cg. Top functions by call count:"
sudo pmcstat -R pmc.cg -G - | head -30
