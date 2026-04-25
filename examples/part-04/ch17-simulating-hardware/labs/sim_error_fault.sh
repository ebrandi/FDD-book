#!/bin/sh
# sim_error_fault.sh -- Chapter 17 Lab: error fault detection.
#
# Enables the error fault at 25% probability, runs 40 writes,
# verifies cmd_errors grew by roughly the expected number.

set -e

DEV=dev.myfirst.0
DEVNODE=/dev/myfirst0

if [ ! -c "${DEVNODE}" ]; then
    echo "FAIL: ${DEVNODE} not present." >&2
    exit 2
fi

before=$(sysctl -n "${DEV}.cmd_errors")

sysctl -q "${DEV}.reg_fault_mask_set=4" >/dev/null
sysctl -q "${DEV}.reg_fault_prob_set=2500" >/dev/null
sysctl -q "${DEV}.reg_delay_ms_set=20" >/dev/null

ok=0
err=0
for i in $(jot 40); do
    if echo -n "X" > ${DEVNODE} 2>/dev/null; then
        ok=$((ok + 1))
    else
        err=$((err + 1))
    fi
done

after=$(sysctl -n "${DEV}.cmd_errors")
delta=$((after - before))

sysctl -q "${DEV}.reg_fault_mask_set=0" >/dev/null
sysctl -q "${DEV}.reg_fault_prob_set=0" >/dev/null
sysctl -q "${DEV}.reg_delay_ms_set=500" >/dev/null

if [ "${delta}" -lt 3 ] || [ "${delta}" -gt 25 ]; then
    echo "FAIL: cmd_errors grew by ${delta}, expected ~10" >&2
    exit 1
fi

echo "PASS: error fault detected. user: ${err} errors, counter: +${delta}"
exit 0
