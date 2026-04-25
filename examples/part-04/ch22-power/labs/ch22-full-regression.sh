#!/bin/sh
# ch22-full-regression.sh - combined regression for Chapter 22.
#
# Runs the cycle, stress, and cross-cycle tests in sequence. Also runs
# the runtime-PM test when the driver is built with runtime PM enabled.

set -e

HERE=$(dirname "$0")
DEV="dev.myfirst.0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present. Is the driver loaded?"
    exit 1
fi

echo "=== Single cycle ==="
sh "${HERE}/ch22-suspend-resume-cycle.sh"

echo ""
echo "=== 100-cycle stress ==="
sh "${HERE}/ch22-suspend-stress.sh" 100

echo ""
echo "=== Transfer across cycle ==="
sh "${HERE}/ch22-transfer-across-cycle.sh"

echo ""
if sysctl -N ${DEV}.runtime_state >/dev/null 2>&1; then
    echo "=== Runtime PM ==="
    sh "${HERE}/ch22-runtime-pm.sh"
else
    echo "=== Runtime PM: skipped (not enabled) ==="
fi

echo ""
echo "=== Counter check ==="
s=$(sysctl -n ${DEV}.power_suspend_count)
r=$(sysctl -n ${DEV}.power_resume_count)
if [ "${s}" != "${r}" ]; then
    echo "FAIL: suspend_count (${s}) != resume_count (${r})"
    exit 1
fi
echo "OK: suspend_count == resume_count == ${s}"

echo ""
echo "FULL REGRESSION PASSED"
