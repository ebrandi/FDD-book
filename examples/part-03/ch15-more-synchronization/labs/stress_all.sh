#!/bin/sh
#
# stress_all.sh: the Chapter 15 Lab 4 stress kit.
#
# Runs every Part 3 primitive simultaneously: writers, readers, tick
# source, heartbeat, watchdog, bulk_writer floods, writers_limit
# adjustments, and stats-cache reads.  Used to stress-test detach
# under maximum load.
#
# Usage:
#   ./stress_all.sh [run_seconds]
# Defaults to 30 seconds.

DURATION=${1:-30}
DEV=dev.myfirst.0

echo "Enabling Chapter 13/14 primitives..."
sysctl $DEV.tick_source_interval_ms=1 >/dev/null
sysctl $DEV.heartbeat_interval_ms=100 >/dev/null
sysctl $DEV.watchdog_interval_ms=1000 >/dev/null

# Background writers.
./writer_cap_test 8 128 200 &
WRITERS=$!

# Background readers.
for i in 1 2 3 4; do
	(cat /dev/myfirst >/dev/null 2>&1) &
done

# Bulk_writer flood every 100ms.
(while :; do
	sysctl $DEV.bulk_writer_batch=32 >/dev/null
	sysctl $DEV.bulk_writer_flood=1000 >/dev/null
	sleep 0.1
done) &
FLOODER=$!

# Limit adjustment every 500ms.
(while :; do
	for lim in 1 2 4 8; do
		sysctl $DEV.writers_limit=$lim >/dev/null
		sleep 0.5
	done
done) &
ADJUSTER=$!

# Stats cache reads.
(while :; do
	sysctl -n $DEV.stats.bytes_written_10s >/dev/null
done) &
READER=$!

echo "Stress running for $DURATION seconds..."
sleep "$DURATION"

echo "Stopping stressors..."
kill $WRITERS $FLOODER $ADJUSTER $READER 2>/dev/null
wait 2>/dev/null

echo "Stress ended.  Driver stats:"
sysctl $DEV.stats
