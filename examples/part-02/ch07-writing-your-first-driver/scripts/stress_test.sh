#!/bin/sh
#
# Repeatedly load and unload the myfirst module to look for leaks or
# crashes that only show up over many cycles. Run from the same
# directory as your built myfirst.ko.
#
# Usage: sudo ./stress_test.sh [iterations]

set -e

ITERATIONS="${1:-100}"
MODULE="myfirst"

if [ ! -f "./${MODULE}.ko" ]; then
    echo "stress_test.sh: ./${MODULE}.ko not found" >&2
    exit 1
fi

i=1
while [ "$i" -le "$ITERATIONS" ]; do
    echo "Iteration $i / $ITERATIONS"
    kldload "./${MODULE}.ko"
    sleep 0.1
    kldunload "${MODULE}"
    i=$((i + 1))
done

echo "Stress test completed: ${ITERATIONS} cycles without errors."
