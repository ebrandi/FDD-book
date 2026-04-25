#!/bin/sh
#
# attach_detach_cycle.sh -- stress the attach/detach path.
#
# Runs N detach/attach cycles against the myfirst0 device. Reports
# any failed cycle and any malloc leak observed afterwards.

set -eu

CYCLES="${1:-50}"

say()  { printf '=== %s ===\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

SELECTOR=$(pciconf -l | awk '/^myfirst0/{print $1}' \
    | sed 's/myfirst0@//')
[ -n "$SELECTOR" ] || fail "myfirst0 not present; load the driver first"

say "Cycling $CYCLES times"

i=1
while [ "$i" -le "$CYCLES" ]; do
    devctl detach myfirst0 || fail "detach on cycle $i"
    sleep 0.1
    devctl attach "$SELECTOR" || fail "attach on cycle $i"
    sleep 0.1
    i=$((i + 1))
done

say "Cycle complete; checking leaks"

LEAK=$(vmstat -m | awk '/^ *myfirst /{print $3}')
if [ "${LEAK:-0}" -gt 0 ]; then
    fail "$LEAK live myfirst allocations after cycle"
fi

say "Success"
