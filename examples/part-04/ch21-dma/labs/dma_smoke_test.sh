#!/bin/sh
# dma_smoke_test.sh - Quick sanity check of the myfirst DMA path.
#
# Runs one host-to-device transfer and one device-to-host transfer.
# Verifies the counters incremented correctly.

set -e

DEV="dev.myfirst.0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present. Is the driver loaded?"
    exit 1
fi

# Record starting counters.
before_w=$(sysctl -n ${DEV}.dma_transfers_write)
before_r=$(sysctl -n ${DEV}.dma_transfers_read)

# Host-to-device.
sysctl -n ${DEV}.dma_test_write=0xaa >/dev/null
# Device-to-host.
sysctl -n ${DEV}.dma_test_read=1 >/dev/null

# Record ending counters.
after_w=$(sysctl -n ${DEV}.dma_transfers_write)
after_r=$(sysctl -n ${DEV}.dma_transfers_read)

# Check the increments.
delta_w=$((after_w - before_w))
delta_r=$((after_r - before_r))

if [ ${delta_w} -eq 1 ] && [ ${delta_r} -eq 1 ]; then
    echo "PASS: one write and one read transfer completed"
    echo "  dma_transfers_write: ${before_w} -> ${after_w}"
    echo "  dma_transfers_read:  ${before_r} -> ${after_r}"
    echo ""
    echo "=== Recent dmesg lines ==="
    dmesg | tail -4
    exit 0
else
    echo "FAIL: counters did not increment as expected"
    echo "  dma_transfers_write: ${before_w} -> ${after_w} (delta ${delta_w}, expected 1)"
    echo "  dma_transfers_read:  ${before_r} -> ${after_r} (delta ${delta_r}, expected 1)"
    exit 1
fi
