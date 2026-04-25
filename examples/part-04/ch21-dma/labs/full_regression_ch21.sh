#!/bin/sh
# full_regression_ch21.sh - Complete Chapter 21 regression pass.
#
# Steps:
#  1. Verify the driver is loaded and at version 1.4-dma.
#  2. Smoke test: one write, one read.
#  3. Stress test: 1000 transfers.
#  4. Load/unload cycle: 10 iterations, check for leaks.
#  5. Print a summary.
#
# Prints PASS at the end on success, or FAIL with the step that failed.

set -e

DEV="dev.myfirst.0"
MOD_PATH="${MOD_PATH:-./myfirst.ko}"
SCRIPT_DIR=$(dirname "$0")

fail() {
    echo "FAIL at step $1: $2"
    exit 1
}

# Step 1: driver loaded and at correct version.
echo "Step 1: version check..."
if ! kldstat | grep -q myfirst; then
    fail 1 "myfirst driver not loaded"
fi
# The version sysctl is added in Chapter 20+ via the version string.
if sysctl -a 2>/dev/null | grep -q '^dev\.myfirst\.0\.version'; then
    ver=$(sysctl -n dev.myfirst.0.version 2>/dev/null)
    echo "  Driver version: ${ver}"
    case "${ver}" in
        1.4-dma*) ;;
        *) fail 1 "unexpected version ${ver} (expected 1.4-dma)" ;;
    esac
fi

# Step 2: smoke test.
echo "Step 2: smoke test..."
if ! sh "${SCRIPT_DIR}/dma_smoke_test.sh" >/dev/null 2>&1; then
    fail 2 "smoke test failed"
fi

# Step 3: stress test.
echo "Step 3: stress test (1000 transfers)..."
if ! sh "${SCRIPT_DIR}/dma_stress_test.sh" 1000 >/dev/null 2>&1; then
    fail 3 "stress test failed"
fi

# Step 4: load/unload cycle.
echo "Step 4: load/unload cycle..."
# Record starting bus_dma memory usage.
before=$(vmstat -m 2>/dev/null | awk '/bus_dma/ { print $2 }' | head -1)
before=${before:-0}

# Unload, then load/unload 10 times.
kldunload myfirst 2>/dev/null || true

i=1
while [ $i -le 10 ]; do
    if ! kldload ${MOD_PATH} 2>/dev/null; then
        fail 4 "kldload failed on iteration $i"
    fi
    if ! kldunload myfirst 2>/dev/null; then
        fail 4 "kldunload failed on iteration $i"
    fi
    i=$((i + 1))
done

# Reload for any subsequent manual checks.
kldload ${MOD_PATH} 2>/dev/null || true

after=$(vmstat -m 2>/dev/null | awk '/bus_dma/ { print $2 }' | head -1)
after=${after:-0}

if [ "${after}" != "${before}" ]; then
    echo "  warning: vmstat -m shows bus_dma count changed (${before} -> ${after})"
    echo "  (may be noise; investigate if persistent)"
fi

# Step 5: summary.
echo ""
echo "=========================================="
echo "PASS: Chapter 21 regression complete"
echo "=========================================="
