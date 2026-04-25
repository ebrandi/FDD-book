#!/bin/sh
# full_regression_ch17.sh -- Chapter 17 regression suite.
#
# Runs every Chapter 17 test script in sequence. Exits non-zero if any
# test fails. Intended to be run after loading the Chapter 17 Stage 5
# driver (myfirst at version 1.0-simulated).

set -e

DIR=$(dirname "$0")

TESTS='
sim_sensor_oscillates.sh
sim_command_cycle.sh
sim_timeout_fault.sh
sim_error_fault.sh
sim_stuck_busy_fault.sh
sim_mixed_faults_under_load.sh
sim_sensor_during_load.sh
'

FAILED=0

for t in ${TESTS}; do
    echo "--- Running ${t}"
    if ! "${DIR}/${t}"; then
        echo "--- FAILED: ${t}"
        FAILED=$((FAILED + 1))
    fi
done

if [ "${FAILED}" -ne 0 ]; then
    echo "--- ${FAILED} test(s) failed" >&2
    exit 1
fi

echo "--- All Chapter 17 regression tests passed"
exit 0
