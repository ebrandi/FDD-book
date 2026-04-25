#!/bin/sh
# ch22-suspend-resume-cycle.sh - one clean suspend-resume cycle.
#
# Triggers devctl suspend followed by devctl resume and verifies that
# the driver's counters and state flag move through the expected values.

set -e

DEV="dev.myfirst.0"
DEVNAME="myfirst0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present. Is the driver loaded?"
    exit 1
fi

before_s=$(sysctl -n ${DEV}.power_suspend_count)
before_r=$(sysctl -n ${DEV}.power_resume_count)
before_x=$(sysctl -n ${DEV}.dma_transfers_read)

# Baseline: run one transfer to prove the device works.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

# Suspend.
devctl suspend ${DEVNAME}

state=$(sysctl -n ${DEV}.suspended)
if [ "${state}" != "1" ]; then
    echo "FAIL: device not marked suspended after devctl suspend (got '${state}')"
    exit 1
fi

# Resume.
devctl resume ${DEVNAME}

state=$(sysctl -n ${DEV}.suspended)
if [ "${state}" != "0" ]; then
    echo "FAIL: device still marked suspended after devctl resume (got '${state}')"
    exit 1
fi

# Post-resume transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

after_s=$(sysctl -n ${DEV}.power_suspend_count)
after_r=$(sysctl -n ${DEV}.power_resume_count)
after_x=$(sysctl -n ${DEV}.dma_transfers_read)

delta_s=$((after_s - before_s))
delta_r=$((after_r - before_r))
delta_x=$((after_x - before_x))

fail=0
if [ ${delta_s} -ne 1 ]; then
    echo "FAIL: power_suspend_count delta=${delta_s}, expected 1"
    fail=1
fi
if [ ${delta_r} -ne 1 ]; then
    echo "FAIL: power_resume_count delta=${delta_r}, expected 1"
    fail=1
fi
if [ ${delta_x} -ne 2 ]; then
    echo "FAIL: dma_transfers_read delta=${delta_x}, expected 2"
    fail=1
fi

if [ ${fail} -ne 0 ]; then
    exit 1
fi

echo "PASS: one suspend-resume cycle completed cleanly"
