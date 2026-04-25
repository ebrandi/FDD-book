#!/bin/sh
# sim_sensor_during_load.sh -- Chapter 17 Lab: sensor updates under load.
#
# Starts a heavy command load in the background and verifies the sensor
# register continues to receive autonomous updates. Fails if the sensor
# value stops changing during the load.

set -e

DEV=dev.myfirst.0
DEVNODE=/dev/myfirst0

if [ ! -c "${DEVNODE}" ]; then
    echo "FAIL: ${DEVNODE} not present." >&2
    exit 2
fi

sysctl -q "${DEV}.reg_delay_ms_set=5" >/dev/null

# Background load: many writes for ~5 seconds.
(
    for i in $(jot 1000); do
        echo -n "X" > ${DEVNODE} 2>/dev/null || true
    done
) &
LOADPID=$!

# Sample sensor a few times while load runs.
prev=""
changes=0
for i in $(jot 10); do
    cur=$(sysctl -n "${DEV}.reg_sensor")
    if [ -n "${prev}" ] && [ "${cur}" != "${prev}" ]; then
        changes=$((changes + 1))
    fi
    prev="${cur}"
    sleep 0.3
done

# Wait for load to finish.
wait ${LOADPID} 2>/dev/null || true

sysctl -q "${DEV}.reg_delay_ms_set=500" >/dev/null

if [ "${changes}" -lt 3 ]; then
    echo "FAIL: sensor only changed ${changes} times under load" >&2
    exit 1
fi

echo "PASS: sensor updated ${changes} times under load"
exit 0
