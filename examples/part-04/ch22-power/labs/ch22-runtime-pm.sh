#!/bin/sh
# ch22-runtime-pm.sh - exercise the runtime-PM path.
#
# Requires the driver built with -DMYFIRST_ENABLE_RUNTIME_PM.

set -e

DEV="dev.myfirst.0"

# Check that runtime PM is enabled.
if ! sysctl -N ${DEV}.runtime_state >/dev/null 2>&1; then
    echo "SKIP: runtime PM is not enabled (rebuild with -DMYFIRST_ENABLE_RUNTIME_PM)"
    exit 0
fi

# Set a short idle threshold so the test is quick.
sysctl -n ${DEV}.idle_threshold_seconds=3 > /dev/null

before_s=$(sysctl -n ${DEV}.runtime_suspend_count)
before_r=$(sysctl -n ${DEV}.runtime_resume_count)

# Trigger one transfer to set last_activity.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

# Wait for the idle watcher to fire.
sleep 5

state=$(sysctl -n ${DEV}.runtime_state)
if [ "${state}" != "1" ]; then
    echo "FAIL: runtime_state not suspended after idle (got '${state}')"
    exit 1
fi

after_s=$(sysctl -n ${DEV}.runtime_suspend_count)
if [ $((after_s - before_s)) -ne 1 ]; then
    echo "FAIL: runtime_suspend_count did not increment"
    exit 1
fi

# Trigger a transfer to wake the device.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

state=$(sysctl -n ${DEV}.runtime_state)
if [ "${state}" != "0" ]; then
    echo "FAIL: runtime_state not running after transfer (got '${state}')"
    exit 1
fi

after_r=$(sysctl -n ${DEV}.runtime_resume_count)
if [ $((after_r - before_r)) -ne 1 ]; then
    echo "FAIL: runtime_resume_count did not increment"
    exit 1
fi

echo "PASS: runtime PM cycle completed"
