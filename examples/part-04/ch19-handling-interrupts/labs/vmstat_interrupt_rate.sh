#!/bin/sh
#
# vmstat_interrupt_rate.sh -- sample vmstat -i twice and compute
# per-source interrupt rates over the sampling interval.
#
# Usage: ./vmstat_interrupt_rate.sh [interval_seconds]
# Default interval: 10 seconds.

set -eu

INTERVAL="${1:-10}"

TMP1=$(mktemp)
TMP2=$(mktemp)
trap 'rm -f "$TMP1" "$TMP2"' EXIT

vmstat -i > "$TMP1"
sleep "$INTERVAL"
vmstat -i > "$TMP2"

awk -v interval="$INTERVAL" '
    BEGIN {
        print "source" "\t" "delta" "\t" "rate (intr/s)"
    }
    NR==FNR {
        if (NR <= 1) next   # skip header
        before[$1] = $(NF-1)
        next
    }
    {
        if (FNR <= 1) next
        name = $1
        count_after = $(NF-1)
        if (name in before) {
            delta = count_after - before[name]
            if (delta > 0) {
                printf "%-30s\t%d\t%.1f\n", name, delta,
                    delta / interval
            }
        }
    }
' "$TMP1" "$TMP2"
