#!/bin/sh
# sim_stuck_busy_fault.sh -- Chapter 17 Lab: STUCK_BUSY latch.
#
# Enables the stuck-busy fault, verifies a write times out on
# wait_for_ready, clears the fault, verifies a subsequent write
# succeeds.

set -e

DEV=dev.myfirst.0
DEVNODE=/dev/myfirst0

if [ ! -c "${DEVNODE}" ]; then
    echo "FAIL: ${DEVNODE} not present." >&2
    exit 2
fi

before=$(sysctl -n "${DEV}.cmd_rdy_timeouts")

sysctl -q "${DEV}.reg_fault_mask_set=8" >/dev/null
sysctl -q "${DEV}.rdy_timeout_ms=200" >/dev/null

# Wait for the busy callout to assert BUSY.
sleep 0.2

# The write must fail (on wait_for_ready).
if echo -n "X" > ${DEVNODE} 2>/dev/null; then
    echo "FAIL: write succeeded despite STUCK_BUSY" >&2
    sysctl -q "${DEV}.reg_fault_mask_set=0" >/dev/null
    sysctl -q "${DEV}.rdy_timeout_ms=100" >/dev/null
    exit 1
fi

after=$(sysctl -n "${DEV}.cmd_rdy_timeouts")

# Clear the fault and wait for BUSY to naturally clear.
sysctl -q "${DEV}.reg_fault_mask_set=0" >/dev/null
sleep 0.2
sysctl -q "${DEV}.rdy_timeout_ms=100" >/dev/null

# The write should now succeed.
if ! echo -n "X" > ${DEVNODE} 2>/dev/null; then
    echo "FAIL: write still failed after clearing STUCK_BUSY" >&2
    exit 1
fi

if [ "${after}" -le "${before}" ]; then
    echo "FAIL: cmd_rdy_timeouts did not grow" >&2
    exit 1
fi

echo "PASS: STUCK_BUSY detected, recovered (rdy_timeouts=${after})"
exit 0
