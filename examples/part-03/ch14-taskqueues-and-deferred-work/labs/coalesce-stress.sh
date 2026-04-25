#!/bin/sh
#
# coalesce-stress.sh: Lab 2 companion script.
#
# Drives the Stage 4 driver hard enough to trigger task coalescing,
# then reports the selwake_pending_drops counter.  Assumes the driver
# is loaded and instance 0 exists.

set -u

DEV=dev.myfirst.0

echo "Baseline selwake_pending_drops: $(sysctl -n $DEV.stats.selwake_pending_drops 2>/dev/null || echo 0)"

echo "Enabling tick_source at 1 ms..."
sysctl $DEV.tick_source_interval_ms=1 >/dev/null

echo "Running 20 bulk_writer floods of 1000 enqueues each..."
sysctl $DEV.bulk_writer_batch=32 >/dev/null
for i in $(jot 20 1); do
	sysctl $DEV.bulk_writer_flood=1000 >/dev/null
done

sleep 2

echo "Final selwake_pending_drops: $(sysctl -n $DEV.stats.selwake_pending_drops)"

echo "Stopping tick_source..."
sysctl $DEV.tick_source_interval_ms=0 >/dev/null
sysctl $DEV.bulk_writer_batch=0 >/dev/null

echo
echo "bytes_read:    $(sysctl -n $DEV.stats.bytes_read)"
echo "bytes_written: $(sysctl -n $DEV.stats.bytes_written)"
