#!/bin/sh
# sim_timeout_fault.sh -- Chapter 17 Lab: timeout fault and recovery.
#
# Enables the timeout fault at 100% probability, issues a write,
# verifies the driver reports an error, counters increment, and a
# subsequent write after clearing the fault succeeds.

set -e

DEV=dev.myfirst.0
DEVNODE=/dev/myfirst0

if [ ! -c "${DEVNODE}" ]; then
    echo "FAIL: ${DEVNODE} not present." >&2
    exit 2
fi

before_to=$(sysctl -n "${DEV}.cmd_data_timeouts")
before_rec=$(sysctl -n "${DEV}.cmd_recoveries" 2>/dev/null || echo 0)

# Enable timeout fault at 100%.
sysctl -q "${DEV}.reg_fault_mask_set=1" >/dev/null
sysctl -q "${DEV}.reg_fault_prob_set=10000" >/dev/null
sysctl -q "${DEV}.cmd_timeout_ms=200" >/dev/null

# This write must fail.
if echo -n "X" > ${DEVNODE} 2>/dev/null; then
    echo "FAIL: write succeeded when a timeout fault was enabled" >&2
    sysctl -q "${DEV}.reg_fault_mask_set=0" >/dev/null
    sysctl -q "${DEV}.reg_fault_prob_set=0" >/dev/null
    sysctl -q "${DEV}.cmd_timeout_ms=2000" >/dev/null
    exit 1
fi

after_to=$(sysctl -n "${DEV}.cmd_data_timeouts")

# Clear the fault.
sysctl -q "${DEV}.reg_fault_mask_set=0" >/dev/null
sysctl -q "${DEV}.reg_fault_prob_set=0" >/dev/null
sysctl -q "${DEV}.cmd_timeout_ms=2000" >/dev/null

# This write should succeed.
if ! echo -n "X" > ${DEVNODE} 2>/dev/null; then
    echo "FAIL: write still failed after clearing the fault" >&2
    exit 1
fi

if [ "${after_to}" -le "${before_to}" ]; then
    echo "FAIL: cmd_data_timeouts did not grow" >&2
    exit 1
fi

echo "PASS: timeout fault handled and recovered (timeouts=${after_to})"
exit 0
