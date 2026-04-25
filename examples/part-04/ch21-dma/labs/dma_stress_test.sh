#!/bin/sh
# dma_stress_test.sh - Run 1000 DMA transfers in each direction and
# check counter invariants at the end.

set -e

DEV="dev.myfirst.0"
ITERS=${1:-1000}

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present. Is the driver loaded?"
    exit 1
fi

echo "Running ${ITERS} write transfers and ${ITERS} read transfers..."

before_w=$(sysctl -n ${DEV}.dma_transfers_write)
before_r=$(sysctl -n ${DEV}.dma_transfers_read)
before_e=$(sysctl -n ${DEV}.dma_errors)
before_t=$(sysctl -n ${DEV}.dma_timeouts)
before_ci=$(sysctl -n ${DEV}.dma_complete_intrs)
before_ct=$(sysctl -n ${DEV}.dma_complete_tasks)

i=1
while [ $i -le ${ITERS} ]; do
    sysctl -n ${DEV}.dma_test_write=$((i & 0xFF)) >/dev/null 2>&1
    sysctl -n ${DEV}.dma_test_read=1 >/dev/null 2>&1
    i=$((i + 1))
done

after_w=$(sysctl -n ${DEV}.dma_transfers_write)
after_r=$(sysctl -n ${DEV}.dma_transfers_read)
after_e=$(sysctl -n ${DEV}.dma_errors)
after_t=$(sysctl -n ${DEV}.dma_timeouts)
after_ci=$(sysctl -n ${DEV}.dma_complete_intrs)
after_ct=$(sysctl -n ${DEV}.dma_complete_tasks)

d_w=$((after_w - before_w))
d_r=$((after_r - before_r))
d_e=$((after_e - before_e))
d_t=$((after_t - before_t))
d_ci=$((after_ci - before_ci))
d_ct=$((after_ct - before_ct))

echo ""
echo "Deltas:"
echo "  dma_transfers_write: ${d_w} (expected ${ITERS})"
echo "  dma_transfers_read:  ${d_r} (expected ${ITERS})"
echo "  dma_errors:          ${d_e} (expected 0)"
echo "  dma_timeouts:        ${d_t} (expected 0)"
echo "  dma_complete_intrs:  ${d_ci} (expected $((ITERS * 2)))"
echo "  dma_complete_tasks:  ${d_ct} (expected $((ITERS * 2)))"

fail=0
if [ ${d_w} -ne ${ITERS} ]; then fail=1; fi
if [ ${d_r} -ne ${ITERS} ]; then fail=1; fi
if [ ${d_e} -ne 0 ]; then fail=1; fi
if [ ${d_t} -ne 0 ]; then fail=1; fi
if [ ${d_ci} -ne ${d_ct} ]; then fail=1; fi

if [ ${fail} -eq 0 ]; then
    echo ""
    echo "PASS: invariants hold"
    exit 0
else
    echo ""
    echo "FAIL: one or more invariants violated"
    exit 1
fi
