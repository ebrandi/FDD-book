#!/bin/sh
# sim_mixed_faults_under_load.sh -- Chapter 17 Lab: mixed faults under load.
#
# Enables TIMEOUT and ERROR faults at 2% probability. Runs 8 parallel
# workers each doing 100 commands. Verifies that a reasonable number
# of faults fired and the driver made progress overall.

set -e

DEV=dev.myfirst.0
DEVNODE=/dev/myfirst0

if [ ! -c "${DEVNODE}" ]; then
    echo "FAIL: ${DEVNODE} not present." >&2
    exit 2
fi

before_succ=$(sysctl -n "${DEV}.cmd_successes")
before_fault=$(sysctl -n "${DEV}.fault_injected")

sysctl -q "${DEV}.reg_fault_mask_set=5" >/dev/null    # TIMEOUT | ERROR
sysctl -q "${DEV}.reg_fault_prob_set=200" >/dev/null  # 2%
sysctl -q "${DEV}.reg_delay_ms_set=10" >/dev/null
sysctl -q "${DEV}.cmd_timeout_ms=100" >/dev/null

for i in 1 2 3 4 5 6 7 8; do
    (
        for j in $(jot 100); do
            echo -n "X" > ${DEVNODE} 2>/dev/null || true
        done
    ) &
done
wait

after_succ=$(sysctl -n "${DEV}.cmd_successes")
after_fault=$(sysctl -n "${DEV}.fault_injected")

sysctl -q "${DEV}.reg_fault_mask_set=0" >/dev/null
sysctl -q "${DEV}.reg_fault_prob_set=0" >/dev/null
sysctl -q "${DEV}.reg_delay_ms_set=500" >/dev/null
sysctl -q "${DEV}.cmd_timeout_ms=2000" >/dev/null

delta_succ=$((after_succ - before_succ))
delta_fault=$((after_fault - before_fault))

# Most commands should succeed.
if [ "${delta_succ}" -lt 500 ]; then
    echo "FAIL: only ${delta_succ} commands succeeded (expected >=500)" >&2
    exit 1
fi

# At least a few faults should have fired.
if [ "${delta_fault}" -lt 3 ]; then
    echo "FAIL: only ${delta_fault} faults injected (expected ~16)" >&2
    exit 1
fi

echo "PASS: mixed-fault load test. successes=+${delta_succ} faults=+${delta_fault}"
exit 0
