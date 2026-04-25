#!/bin/sh
# ch22-transfer-across-cycle.sh - suspend in the middle of a transfer.
#
# Starts a DMA transfer, immediately calls devctl suspend, and verifies
# that the quiesce path aborted the transfer cleanly. Then resumes and
# verifies that a post-resume transfer succeeds.

set -e

DEV="dev.myfirst.0"
DEVNAME="myfirst0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present."
    exit 1
fi

# Start a transfer in the background.
(sysctl -n ${DEV}.dma_test_read=1 > /dev/null 2>&1 || true) &
BG=$!

# Give it a moment to enter the engine.
sleep 0.001 2>/dev/null || :

# Suspend immediately.
devctl suspend ${DEVNAME}

# Wait for the background to finish (aborted, possibly).
wait ${BG} 2>/dev/null || :

# Check the state.
state=$(sysctl -n ${DEV}.suspended)
if [ "${state}" != "1" ]; then
    echo "FAIL: device not marked suspended (got '${state}')"
    exit 1
fi

in_flight=$(sysctl -n ${DEV}.dma_in_flight)
if [ "${in_flight}" != "0" ]; then
    echo "FAIL: dma_in_flight still 1 after suspend"
    exit 1
fi

# Resume.
devctl resume ${DEVNAME}

# A post-resume transfer should succeed.
if ! sysctl -n ${DEV}.dma_test_read=1 > /dev/null 2>&1; then
    echo "FAIL: post-resume dma_test_read failed"
    exit 1
fi

echo "PASS: transfer-across-cycle completed"
