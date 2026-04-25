#!/bin/sh
#
# show_vector_bindings.sh -- show each myfirst vector's IRQ number,
# descriptor, CPU binding, and fire count.
#
# Requires the myfirst driver to be loaded.

set -eu

UNIT="${1:-0}"

if ! sysctl -n "dev.myfirst.${UNIT}.intr_mode" >/dev/null 2>&1; then
    echo "myfirst${UNIT} is not loaded or not attached" >&2
    exit 1
fi

mode=$(sysctl -n "dev.myfirst.${UNIT}.intr_mode")
case "$mode" in
    0) mode_str="legacy INTx" ;;
    1) mode_str="MSI" ;;
    2) mode_str="MSI-X" ;;
esac
echo "myfirst${UNIT} interrupt mode: $mode_str"

printf '%-25s %-10s %-15s %-10s\n' \
    "name" "irq" "cpu mask" "fires"

vmstat -i | awk -v unit="$UNIT" '
/myfirst/ && $0 ~ "myfirst" unit {
    # vmstat -i format: "irqN: name           total rate"
    # Parse the irq number and the handler name.
    split($0, fields, /[[:space:]]+/)
    irq = fields[1]
    gsub(":", "", irq)
    sub(/^irq/, "", irq)
    name = fields[2]
    total = fields[3]
    print irq "|" name "|" total
}' | while IFS='|' read irq name total; do
    mask=$(cpuset -g -x "$irq" 2>/dev/null | awk '{print $NF}')
    printf '%-25s %-10s %-15s %-10s\n' "$name" "$irq" "$mask" "$total"
done
