#!/bin/sh
#
# Chapter 18 full regression script.
#
# Verifies the Stage 4 driver's behaviour end-to-end inside a bhyve
# or QEMU guest that exposes a virtio-rnd device.
#
# Runs:
#   - Unload the base-system virtio_random driver if present.
#   - Load ./myfirst.ko (expected to be in the current directory).
#   - Verify attach: myfirst0 present with a BAR resource.
#   - Exercise the cdev via myfirst_test.
#   - Run ten detach/attach cycles.
#   - Unload the driver.
#   - Check for leaks via vmstat -m.

set -eu

DRIVER="$(dirname "$0")/../stage4-final/myfirst.ko"
TESTER="$(dirname "$0")/myfirst_test"
if [ ! -f "$DRIVER" ]; then
    DRIVER="./myfirst.ko"
fi

say() { printf '=== %s ===\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

say "Checking prerequisites"
[ -f "$DRIVER" ] || fail "driver module not found: $DRIVER"
[ -x "$TESTER" ] || {
    say "Compiling myfirst_test"
    cc -o "$TESTER" "$(dirname "$0")/myfirst_test.c"
}

say "Unloading virtio_random if present"
kldstat | grep -q virtio_random && kldunload virtio_random || true

say "Loading myfirst"
kldload "$DRIVER"
sleep 1

say "Verifying attach"
devinfo -v | grep -q 'myfirst0' || fail "myfirst0 not attached"

say "Verifying BAR claim"
devinfo -v | grep -A 3 'myfirst0' | grep -q 'memory:' \
    || fail "BAR not claimed"

say "Verifying cdev"
[ -c /dev/myfirst0 ] || fail "/dev/myfirst0 missing"

say "Exercising cdev"
"$TESTER" || fail "myfirst_test failed"

say "Running detach/attach cycles"
SELECTOR=$(devinfo -r -v | awk '/^myfirst0@pci/{print $1}' \
    | sed 's/myfirst0@//')
if [ -z "$SELECTOR" ]; then
    SELECTOR=$(pciconf -l | awk '/^myfirst0/{print $1}' \
        | sed 's/myfirst0@//')
fi
[ -n "$SELECTOR" ] || fail "could not determine PCI selector"

i=1
while [ $i -le 10 ]; do
    devctl detach myfirst0 || fail "detach #$i failed"
    sleep 0.3
    devctl attach "$SELECTOR" || fail "attach #$i failed"
    sleep 0.3
    i=$((i + 1))
done

say "Unloading myfirst"
kldunload myfirst

say "Checking for leaks"
if vmstat -m | grep -q myfirst; then
    USE=$(vmstat -m | awk '/myfirst/{print $3}')
    if [ "${USE:-0}" -gt 0 ]; then
        fail "myfirst has $USE live allocations after unload"
    fi
fi

say "Success"
