#!/bin/sh
#
# apply_tunable.sh - helper for Lab 3.
#
# Unloads myfirst, sets the tunable via kenv(1), reloads, and prints
# the resulting initial sysctl value.  Compare with and without the
# tunable set to see that TUNABLE_INT_FETCH actually overrides the
# attach-time default.

set -e

TUNABLE="${1:-hw.myfirst.timeout_sec}"
VALUE="${2:-12}"
KMOD="${3:-../myfirst.ko}"
SYSCTL="dev.myfirst.0.${TUNABLE#hw.myfirst.}"

echo "== Step 1: unload any existing myfirst instance"
kldunload myfirst 2>/dev/null || true

echo "== Step 2: set ${TUNABLE} = ${VALUE} via kenv(1)"
kenv "${TUNABLE}=${VALUE}"

echo "== Step 3: load ${KMOD}"
kldload "${KMOD}"

echo "== Step 4: observe the initial sysctl value"
sysctl "${SYSCTL}"

echo
echo "To reset, run:"
echo "    kldunload myfirst"
echo "    kenv -u ${TUNABLE}"
echo "    kldload ${KMOD}"
