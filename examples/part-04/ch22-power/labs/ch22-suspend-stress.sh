#!/bin/sh
# ch22-suspend-stress.sh - run 100 suspend-resume cycles.
#
# A regression test that exercises the suspend and resume paths many
# times in a row, watching for resource leaks, counter drift, and
# edge-case failures that only show up under repetition.

N=${1:-100}
DEV="dev.myfirst.0"
DEVNAME="myfirst0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present."
    exit 1
fi

before_s=$(sysctl -n ${DEV}.power_suspend_count)
before_r=$(sysctl -n ${DEV}.power_resume_count)

echo "Running ${N} suspend-resume cycles..."

i=0
while [ $i -lt $N ]; do
    if ! devctl suspend ${DEVNAME} >/dev/null 2>&1; then
        echo "FAIL: suspend failed on iteration $i"
        exit 1
    fi
    if ! devctl resume ${DEVNAME} >/dev/null 2>&1; then
        echo "FAIL: resume failed on iteration $i"
        exit 1
    fi
    i=$((i + 1))
done

after_s=$(sysctl -n ${DEV}.power_suspend_count)
after_r=$(sysctl -n ${DEV}.power_resume_count)

delta_s=$((after_s - before_s))
delta_r=$((after_r - before_r))

if [ ${delta_s} -ne ${N} ] || [ ${delta_r} -ne ${N} ]; then
    echo "FAIL: expected ${N} each, got suspend=${delta_s} resume=${delta_r}"
    exit 1
fi

# Final transfer to prove the device still works.
if ! sysctl -n ${DEV}.dma_test_read=1 > /dev/null; then
    echo "FAIL: dma_test_read failed after ${N} cycles"
    exit 1
fi

echo "PASS: ${N} cycles completed cleanly"
