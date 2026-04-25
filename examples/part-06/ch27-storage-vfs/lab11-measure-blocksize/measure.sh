#!/bin/sh
#
# measure.sh - Sweep transfer sizes and record rough throughput.
#
# Outputs CSV lines of the form:
#   <op>,<bs>,<count>,<rate_Bps>
#
# Requires /dev/myblk0 to exist and not be mounted.

set -eu

DEVICE=${DEVICE:-/dev/myblk0}
TOTAL=${TOTAL:-$((32 * 1024 * 1024))}

if [ ! -c "${DEVICE}" ]; then
	echo "${DEVICE} not found" >&2
	exit 1
fi

echo "op,bs,count,duration_s,bytes"

for bs in 512 4096 16384 65536 262144 1048576; do
	count=$((TOTAL / bs))
	start=$(date +%s.%N)
	dd if=/dev/zero of="${DEVICE}" bs=${bs} count=${count} \
	   conv=fsync 2>/dev/null
	end=$(date +%s.%N)
	duration=$(echo "${end} - ${start}" | bc)
	echo "WRITE,${bs},${count},${duration},${TOTAL}"

	start=$(date +%s.%N)
	dd if="${DEVICE}" of=/dev/null bs=${bs} count=${count} \
	   2>/dev/null
	end=$(date +%s.%N)
	duration=$(echo "${end} - ${start}" | bc)
	echo "READ,${bs},${count},${duration},${TOTAL}"
done
