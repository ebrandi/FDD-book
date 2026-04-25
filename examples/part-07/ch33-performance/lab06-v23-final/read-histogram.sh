#!/bin/sh
#
# read-histogram.sh - poll the v2.3 latency histogram every second
# and print a simple textual display.
#
# Usage: ./read-histogram.sh [iterations]

ITER=${1:-10}
BOUNDS="<=1us <=10us <=100us <=1ms <=10ms <=100ms <=1s >1s"

if [ ! -c /dev/perfdemo ]; then
    echo "perfdemo is not loaded."
    exit 1
fi

for i in $(seq 1 $ITER); do
    echo "=== $(date) ==="
    # sysctl -x prints raw hex; we need to turn it into 8 uint64.
    sysctl -b hw.perfdemo.latency_histogram | \
        od -An -tu8 -v -w64 | \
        tr -s ' ' '\n' | grep -v '^$' | head -8 > /tmp/perfdemo_hist

    total=0
    while read n; do
        total=$((total + n))
    done < /tmp/perfdemo_hist

    echo "Total samples: $total"
    idx=1
    for b in $BOUNDS; do
        n=$(sed -n "${idx}p" /tmp/perfdemo_hist)
        if [ "$total" -gt 0 ]; then
            pct=$(echo "scale=1; $n * 100 / $total" | bc)
        else
            pct=0
        fi
        printf "  %-10s %8d  (%.1f%%)\n" "$b" "$n" "$pct"
        idx=$((idx + 1))
    done
    sleep 1
done

rm -f /tmp/perfdemo_hist
