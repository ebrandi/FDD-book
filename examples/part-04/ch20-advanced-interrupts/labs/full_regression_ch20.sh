#!/bin/sh
#
# Chapter 20 full regression script.
#
# Verifies the Stage 4 driver's multi-vector interrupt path end to end:
#   - Load the module.
#   - Verify attach and the reported interrupt mode.
#   - Fire per-vector simulated interrupts, check per-vector counters.
#   - Run detach/attach cycle, ensuring mode persists across.
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

say "Verifying interrupt mode"
MODE=$(sysctl -n dev.myfirst.0.intr_mode)
case "$MODE" in
    0) say "mode: legacy INTx";;
    1) say "mode: MSI";;
    2) say "mode: MSI-X";;
    *) fail "unexpected intr_mode: $MODE";;
esac

say "Verifying no GIANT-LOCKED"
if dmesg | grep -q 'myfirst0: \[GIANT-LOCKED\]'; then
    fail "driver registered without INTR_MPSAFE"
fi

say "Verifying vmstat -i entry exists"
vmstat -i | grep -q myfirst || fail "no interrupt entry"

say "Simulating admin interrupt"
sysctl dev.myfirst.0.intr_simulate_admin=2 >/dev/null
sleep 0.2

say "Simulating rx interrupt"
sysctl dev.myfirst.0.intr_simulate_rx=1 >/dev/null
sleep 0.2

say "Simulating tx interrupt"
sysctl dev.myfirst.0.intr_simulate_tx=4 >/dev/null
sleep 0.2

say "Verifying per-vector counters ticked"
AD=$(sysctl -n dev.myfirst.0.vec0_fire_count)
RX=$(sysctl -n dev.myfirst.0.vec1_fire_count 2>/dev/null || echo 0)
TX=$(sysctl -n dev.myfirst.0.vec2_fire_count 2>/dev/null || echo 0)
[ "$AD" -ge 1 ] || fail "admin counter did not tick"

# Only MSI-X (mode 2) has three distinct per-vector filters. The MSI
# tier (mode 1) uses one vector with the Chapter 19 single-handler,
# and the legacy tier (mode 0) is the same single-handler case; on
# both of those, the rx and tx per-vector counters stay at zero
# because slot 0's filter handles every bit. The Chapter 19 global
# counters (intr_count, intr_data_av_count, etc.) are what move.
if [ "$MODE" = "2" ]; then
    [ "$RX" -ge 1 ] || fail "rx counter did not tick (MSI-X)"
    [ "$TX" -ge 1 ] || fail "tx counter did not tick (MSI-X)"
fi

say "Running detach/attach cycles"
SELECTOR=$(pciconf -l | awk '/^myfirst0/{print $1}' \
    | sed 's/myfirst0@//')
[ -n "$SELECTOR" ] || fail "could not determine PCI selector"

i=1
while [ "$i" -le 3 ]; do
    devctl detach myfirst0 || fail "detach #$i"
    sleep 0.3
    devctl attach "$SELECTOR" || fail "attach #$i"
    sleep 0.3
    NEW_MODE=$(sysctl -n dev.myfirst.0.intr_mode)
    [ "$NEW_MODE" = "$MODE" ] || fail "mode changed across reattach"
    i=$((i + 1))
done

say "Unloading"
kldunload myfirst

say "Checking for leaks"
if vmstat -m | awk '/^ *myfirst /{exit ($3 > 0)}'; then
    :
else
    fail "myfirst allocations remain after unload"
fi

if vmstat -i | grep -q myfirst; then
    fail "interrupt event remains after unload"
fi

say "Success"
