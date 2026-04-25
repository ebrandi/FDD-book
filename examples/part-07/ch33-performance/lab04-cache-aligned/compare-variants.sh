#!/bin/sh
#
# compare-variants.sh - compare throughput of the three variants.
#
# Loads each variant in turn, runs a concurrent read workload, and
# reports the throughput. To see the cache-line contention effect,
# run on a multi-core machine with NCPU >= 4.
#
# Usage: ./compare-variants.sh

NCPU=${NCPU:-4}
COUNT=${COUNT:-25000}

load_and_test()
{
	local variant=$1
	local dev=$2

	(cd $variant && make) || return 1
	sudo kldload ./$variant/${variant%.ko}.ko || return 1

	echo "=== Testing $variant with $NCPU concurrent readers ==="
	start=$(date +%s.%N)
	for i in $(seq 1 $NCPU); do
		dd if=$dev of=/dev/null bs=4096 count=$COUNT \
		    2>/dev/null &
	done
	wait
	end=$(date +%s.%N)
	elapsed=$(echo "$end - $start" | bc)
	total_reads=$(echo "$NCPU * $COUNT" | bc)
	rate=$(echo "scale=0; $total_reads / $elapsed" | bc)
	echo "  Elapsed: ${elapsed}s"
	echo "  Total reads: $total_reads"
	echo "  Throughput: ${rate} reads/sec"
	echo

	sudo kldunload ${variant%.ko}
}

load_and_test v1-atomic    /dev/perfdemo_v1
load_and_test v2-aligned   /dev/perfdemo_v2
load_and_test v3-counter9  /dev/perfdemo_v3

echo "Summary:"
echo "  v1 shows contention because a shared atomic is hammered"
echo "    by NCPU cores."
echo "  v2 uses a manually-aligned per-CPU array to eliminate the"
echo "    contention. It should be significantly faster."
echo "  v3 uses counter(9), which is the recommended approach. Its"
echo "    throughput should match v2 closely."
