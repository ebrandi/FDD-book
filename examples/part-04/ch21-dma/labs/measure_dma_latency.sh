#!/bin/sh
# measure_dma_latency.sh - Measure DMA transfer latency from user space.
#
# Runs N transfers, measures wall-clock elapsed time, and prints
# per-transfer latency. Useful for comparing Stage 2 (polling) vs
# Stage 3 (interrupt-driven), or for verifying that the simulation's
# 10 ms callout delay dominates the timing.

set -e

DEV="dev.myfirst.0"
N=${1:-100}

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "ERROR: ${DEV} not present."
    exit 1
fi

start=$(date +%s)
i=1
while [ $i -le ${N} ]; do
    sysctl -n ${DEV}.dma_test_write=$((i & 0xFF)) >/dev/null 2>&1
    i=$((i + 1))
done
end=$(date +%s)

elapsed=$((end - start))
if [ ${elapsed} -eq 0 ]; then elapsed=1; fi

avg_ms=$(((elapsed * 1000) / ${N}))

echo "Iterations:   ${N}"
echo "Elapsed:      ${elapsed} seconds"
echo "Per-transfer: ~${avg_ms} ms"
echo ""
echo "Note: the simulation uses a 10 ms callout, so ~10 ms per"
echo "transfer is the expected steady-state value. Higher values"
echo "indicate per-transfer overhead; lower values may indicate"
echo "batching (if the driver happens to coalesce completions)."
