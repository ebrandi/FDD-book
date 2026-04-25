#!/bin/sh
# sim_sensor_oscillates.sh -- Chapter 17 Lab: sensor breathes.
#
# Confirms that the SENSOR register value changes autonomously over
# time. Samples the register several times with sleeps between samples
# and fails if all samples are identical.

set -e

DEV=dev.myfirst.0

if ! sysctl -q "${DEV}" >/dev/null 2>&1; then
    echo "FAIL: ${DEV} not present. Load myfirst.ko first." >&2
    exit 2
fi

sensor_value() {
    sysctl -n "${DEV}.reg_sensor"
}

first=$(sensor_value)
sleep 0.5
second=$(sensor_value)
sleep 0.5
third=$(sensor_value)

if [ "${first}" = "${second}" ] && [ "${second}" = "${third}" ]; then
    echo "FAIL: sensor value did not change: ${first}" >&2
    exit 1
fi

echo "PASS: sensor oscillates. Samples: ${first} ${second} ${third}"
exit 0
