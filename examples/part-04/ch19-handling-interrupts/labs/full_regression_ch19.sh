#!/bin/sh
#
# Chapter 19 full regression script.
#
# Verifies the Stage 4 driver's interrupt path end-to-end:
#   - Load the module.
#   - Verify attach, filter registration, no GIANT-LOCKED.
#   - Fire simulated interrupts, check counters.
#   - Run the attach/detach cycle.
#   - Unload cleanly, check for leaks.

set -eu

DRIVER="$(dirname "$0")/../stage4-final/myfirst.ko"
if [ ! -f "$DRIVER" ]; then
    DRIVER="./myfirst.ko"
fi

say()  { printf '=== %s ===\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

say "Checking prerequisites"
[ -f "$DRIVER" ] || fail "driver module not found: $DRIVER"

say "Unloading virtio_random if present"
kldstat | grep -q virtio_random && kldunload virtio_random || true

say "Loading myfirst"
kldload "$DRIVER"
sleep 1

say "Verifying attach"
devinfo -v | grep -q 'myfirst0' || fail "myfirst0 not attached"

say "Verifying GIANT-LOCKED is NOT set"
if dmesg | grep -q 'myfirst0: \[GIANT-LOCKED\]'; then
    fail "filter is missing INTR_MPSAFE"
fi

say "Verifying interrupt event is present"
vmstat -i | grep -q myfirst || fail "no interrupt event for myfirst"

say "Simulating DATA_AV interrupt"
sysctl dev.myfirst.0.intr_simulate=1 >/dev/null
sleep 0.2
BEFORE=$(sysctl -n dev.myfirst.0.intr_count)
[ "$BEFORE" -ge 1 ] || fail "intr_count did not increment"

say "Simulating ten interrupts in a loop"
i=1
while [ "$i" -le 10 ]; do
    sysctl dev.myfirst.0.intr_simulate=1 >/dev/null
    i=$((i + 1))
done
sleep 0.5
AFTER=$(sysctl -n dev.myfirst.0.intr_count)
[ "$AFTER" -ge $((BEFORE + 10)) ] || fail "intr_count did not reach expected value"

say "Verifying task invocations"
TASKS=$(sysctl -n dev.myfirst.0.intr_task_invocations)
[ "$TASKS" -ge 1 ] || fail "task never ran"

say "Simulating all three event types"
sysctl dev.myfirst.0.intr_simulate=2 >/dev/null
sysctl dev.myfirst.0.intr_simulate=4 >/dev/null
sysctl dev.myfirst.0.intr_simulate=7 >/dev/null
sleep 0.2

say "Verifying each per-bit counter is non-zero"
DAV=$(sysctl -n dev.myfirst.0.intr_data_av_count)
ERR=$(sysctl -n dev.myfirst.0.intr_error_count)
COMP=$(sysctl -n dev.myfirst.0.intr_complete_count)
[ "$DAV" -ge 1 ] || fail "intr_data_av_count is zero"
[ "$ERR" -ge 1 ] || fail "intr_error_count is zero"
[ "$COMP" -ge 1 ] || fail "intr_complete_count is zero"

say "Running detach/attach cycles"
SELECTOR=$(pciconf -l | awk '/^myfirst0/{print $1}' \
    | sed 's/myfirst0@//')
[ -n "$SELECTOR" ] || fail "could not determine PCI selector"

i=1
while [ "$i" -le 5 ]; do
    devctl detach myfirst0 || fail "detach #$i failed"
    sleep 0.3
    devctl attach "$SELECTOR" || fail "attach #$i failed"
    sleep 0.3
    i=$((i + 1))
done

say "Unloading myfirst"
kldunload myfirst

say "Checking for leaks"
if vmstat -m | awk '/^ *myfirst /{exit ($3 > 0)}'; then
    :
else
    fail "myfirst allocations still present after unload"
fi

if vmstat -i | grep -q myfirst; then
    fail "interrupt event remains after unload"
fi

say "Success"
