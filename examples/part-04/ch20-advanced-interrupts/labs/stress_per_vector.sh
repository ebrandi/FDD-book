#!/bin/sh
#
# stress_per_vector.sh -- fire N simulated interrupts at each vector
# and report per-vector counters.
#
# Usage: stress_per_vector.sh [iterations]
# Default: 1000 per vector.

set -eu

N="${1:-1000}"

BEFORE_ADMIN=$(sysctl -n dev.myfirst.0.vec0_fire_count 2>/dev/null || echo 0)
BEFORE_RX=$(sysctl -n dev.myfirst.0.vec1_fire_count 2>/dev/null || echo 0)
BEFORE_TX=$(sysctl -n dev.myfirst.0.vec2_fire_count 2>/dev/null || echo 0)

say() { printf '=== %s ===\n' "$1"; }

say "Firing $N admin interrupts"
i=1
while [ "$i" -le "$N" ]; do
    sysctl dev.myfirst.0.intr_simulate_admin=2 >/dev/null
    i=$((i + 1))
done

say "Firing $N rx interrupts"
i=1
while [ "$i" -le "$N" ]; do
    sysctl dev.myfirst.0.intr_simulate_rx=1 >/dev/null
    i=$((i + 1))
done

say "Firing $N tx interrupts"
i=1
while [ "$i" -le "$N" ]; do
    sysctl dev.myfirst.0.intr_simulate_tx=4 >/dev/null
    i=$((i + 1))
done

sleep 1

AFTER_ADMIN=$(sysctl -n dev.myfirst.0.vec0_fire_count 2>/dev/null || echo 0)
AFTER_RX=$(sysctl -n dev.myfirst.0.vec1_fire_count 2>/dev/null || echo 0)
AFTER_TX=$(sysctl -n dev.myfirst.0.vec2_fire_count 2>/dev/null || echo 0)

say "Per-vector deltas"
echo "admin: $((AFTER_ADMIN - BEFORE_ADMIN))"
echo "rx:    $((AFTER_RX    - BEFORE_RX   ))"
echo "tx:    $((AFTER_TX    - BEFORE_TX   ))"

if [ $((AFTER_ADMIN - BEFORE_ADMIN)) -lt "$N" ]; then
    echo "WARN: admin delta less than requested" >&2
fi
