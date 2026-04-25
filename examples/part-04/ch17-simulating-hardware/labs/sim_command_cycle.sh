#!/bin/sh
# sim_command_cycle.sh -- Chapter 17 Lab: command cycle works under load.
#
# Runs 50 write commands, then checks that cmd_successes and
# reg_op_counter both grew by at least 50.

set -e

DEV=dev.myfirst.0
DEVNODE=/dev/myfirst0

if [ ! -c "${DEVNODE}" ]; then
    echo "FAIL: ${DEVNODE} not present. Load myfirst.ko first." >&2
    exit 2
fi

# Configure fast commands and clean counters.
sysctl -q "${DEV}.reg_delay_ms_set=20" >/dev/null

before_succ=$(sysctl -n "${DEV}.cmd_successes")
before_op=$(sysctl -n "${DEV}.reg_op_counter")

# 50 single-byte writes.
for i in $(jot 50); do
    echo -n "X" > ${DEVNODE}
done

after_succ=$(sysctl -n "${DEV}.cmd_successes")
after_op=$(sysctl -n "${DEV}.reg_op_counter")

delta_succ=$((after_succ - before_succ))
delta_op=$((after_op - before_op))

sysctl -q "${DEV}.reg_delay_ms_set=500" >/dev/null

if [ "${delta_succ}" -lt 50 ]; then
    echo "FAIL: cmd_successes grew by ${delta_succ}, expected >= 50" >&2
    exit 1
fi

if [ "${delta_op}" -lt 50 ]; then
    echo "FAIL: op_counter grew by ${delta_op}, expected >= 50" >&2
    exit 1
fi

echo "PASS: command cycle OK. successes=+${delta_succ} ops=+${delta_op}"
exit 0
