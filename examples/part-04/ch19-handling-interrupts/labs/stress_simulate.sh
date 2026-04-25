#!/bin/sh
#
# stress_simulate.sh -- fire intr_simulate in a tight loop to
# stress the filter and task pipeline.
#
# Usage: ./stress_simulate.sh [iterations]
#
# Default 10000 iterations.

set -eu

ITER="${1:-10000}"

say()  { printf '=== %s ===\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

[ -e /dev/myfirst0 ] || fail "/dev/myfirst0 missing; load the driver first"

say "Firing $ITER simulated interrupts"
START=$(date +%s)
i=1
while [ "$i" -le "$ITER" ]; do
    sysctl dev.myfirst.0.intr_simulate=1 >/dev/null
    i=$((i + 1))
done
END=$(date +%s)
ELAPSED=$((END - START))

say "Elapsed: $ELAPSED seconds"

sleep 1  # give the task a chance to drain

COUNT=$(sysctl -n dev.myfirst.0.intr_count)
TASKS=$(sysctl -n dev.myfirst.0.intr_task_invocations)

say "Final counters: intr_count=$COUNT intr_task_invocations=$TASKS"

if [ "$COUNT" -lt "$ITER" ]; then
    fail "intr_count ($COUNT) < iterations ($ITER)"
fi

say "Success"
