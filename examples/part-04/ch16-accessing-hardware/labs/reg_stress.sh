#!/bin/sh
#
# reg_stress.sh -- Chapter 16 register-access stress test.
#
# Exercises concurrent writers, concurrent readers, concurrent
# sysctl queries, and the ticker task against the Chapter 16 driver.
# Runs for the number of seconds given on the command line (default
# 30). On a debug kernel with INVARIANTS and WITNESS, any locking
# violation or out-of-bounds access should produce an immediate
# warning or panic.
#
# Usage:
#   ./reg_stress.sh [duration]
#
# Run as root; requires the myfirst driver to be loaded.

set -eu

DEV=/dev/myfirst0
DURATION=${1:-30}

if [ ! -c "$DEV" ]; then
	echo "Device $DEV not present. Did you kldload myfirst?"
	exit 1
fi

echo "Stress test running for ${DURATION}s..."

# Enable access logging for the duration of the run.
sysctl dev.myfirst.0.access_log_enabled=1 > /dev/null

# Enable the ticker.
sysctl dev.myfirst.0.reg_ticker_enabled=1 > /dev/null

# Background writers: each emits bytes to the device file.
for i in 1 2 3 4 5 6 7 8; do
	(
		end=$(( $(date +%s) + DURATION ))
		while [ $(date +%s) -lt $end ]; do
			printf "W%d" "$i" > "$DEV"
		done
	) &
done

# Background readers.
for i in 1 2 3 4; do
	(
		end=$(( $(date +%s) + DURATION ))
		while [ $(date +%s) -lt $end ]; do
			dd if="$DEV" bs=16 count=1 of=/dev/null 2>/dev/null || true
		done
	) &
done

# Background sysctl queries.
(
	end=$(( $(date +%s) + DURATION ))
	while [ $(date +%s) -lt $end ]; do
		sysctl dev.myfirst.0.reg_data_in > /dev/null
		sysctl dev.myfirst.0.reg_status > /dev/null
		sysctl dev.myfirst.0.reg_scratch_a > /dev/null
	done
) &

# Background sysctl CTRL writer.
(
	end=$(( $(date +%s) + DURATION ))
	v=1
	while [ $(date +%s) -lt $end ]; do
		sysctl dev.myfirst.0.reg_ctrl_set="$v" > /dev/null
		v=$(( (v + 1) % 256 ))
	done
) &

wait

echo "Stress test complete."

# Disable ticker and access log, print the last few access-log lines.
sysctl dev.myfirst.0.reg_ticker_enabled=0 > /dev/null
sysctl dev.myfirst.0.access_log_enabled=0 > /dev/null

echo "Final register state:"
./register_dump 2>/dev/null || sysctl dev.myfirst.0 | grep reg_

echo "Last few access-log lines:"
sysctl dev.myfirst.0.access_log 2>/dev/null | tail -n 10

exit 0
