#!/bin/sh
#
# full_regression.sh -- Chapter 16 regression pass.
#
# Runs every Chapter 11 through Chapter 16 check against the
# Stage 4 driver. Designed to be quick and tell the caller
# whether the driver's composition is sound.
#
# Usage:
#   ./full_regression.sh
#
# Requires:
#   - The myfirst driver loaded at 0.9-mmio.
#   - The labs/ register_dump tool built.

set -eu

DEV=/dev/myfirst0

echo "Chapter 16 regression pass"
echo "=========================="

if [ ! -c "$DEV" ]; then
	echo "FAIL: $DEV not present"
	exit 1
fi

echo "--- Step 1: Register-level sanity ---"
./register_dump

# Confirm DEVICE_ID matches.
devid=$(sysctl -n dev.myfirst.0.reg_device_id)
if [ "$devid" -ne 1298498121 ]; then
	echo "FAIL: DEVICE_ID mismatch (got $devid, expected 1298498121)"
	exit 1
fi
echo "OK: DEVICE_ID correct."

# Confirm STATUS has READY bit set.
status=$(sysctl -n dev.myfirst.0.reg_status)
if [ $(( status & 1 )) -ne 1 ]; then
	echo "FAIL: STATUS.READY not set"
	exit 1
fi
echo "OK: STATUS.READY set."

echo "--- Step 2: Data path produces register updates ---"
echo -n "ABCDEF" > "$DEV"
data_in=$(sysctl -n dev.myfirst.0.reg_data_in)
if [ "$data_in" -ne 70 ]; then
	echo "FAIL: DATA_IN should be 70 ('F'), got $data_in"
	exit 1
fi
echo "OK: DATA_IN reflects last written byte."

dd if="$DEV" bs=6 count=1 of=/dev/null 2>/dev/null
data_out=$(sysctl -n dev.myfirst.0.reg_data_out)
if [ "$data_out" -ne 70 ]; then
	echo "FAIL: DATA_OUT should be 70 ('F'), got $data_out"
	exit 1
fi
echo "OK: DATA_OUT reflects last read byte."

echo "--- Step 3: Ticker task updates SCRATCH_A ---"
before=$(sysctl -n dev.myfirst.0.reg_scratch_a)
sysctl dev.myfirst.0.reg_ticker_enabled=1 > /dev/null
sleep 3
after=$(sysctl -n dev.myfirst.0.reg_scratch_a)
sysctl dev.myfirst.0.reg_ticker_enabled=0 > /dev/null
if [ "$after" -le "$before" ]; then
	echo "FAIL: ticker did not advance SCRATCH_A ($before -> $after)"
	exit 1
fi
echo "OK: ticker advanced SCRATCH_A ($before -> $after)."

echo "--- Step 4: Access log records events ---"
sysctl dev.myfirst.0.access_log_enabled=1 > /dev/null
echo -n "xyz" > "$DEV"
dd if="$DEV" bs=3 count=1 of=/dev/null 2>/dev/null
log=$(sysctl -n dev.myfirst.0.access_log)
sysctl dev.myfirst.0.access_log_enabled=0 > /dev/null
n=$(printf "%s\n" "$log" | grep -c "^")
if [ "$n" -lt 4 ]; then
	echo "FAIL: access log has only $n lines"
	exit 1
fi
echo "OK: access log recorded $n events."

echo "--- Step 5: Short stress pass (5 seconds) ---"
./reg_stress.sh 5
echo "OK: stress pass completed."

echo
echo "Chapter 16 regression: all checks passed."
