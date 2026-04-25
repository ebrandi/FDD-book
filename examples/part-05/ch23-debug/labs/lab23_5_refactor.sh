#!/bin/sh
#
# lab23_5_refactor.sh - Lab 23.5: Installing the 1.6-debug Refactor
#
# Walks the reader through loading the refactored driver and confirming
# the new debug infrastructure is active.

set -e

if [ "$(id -u)" -ne 0 ]; then
	echo "This script must run as root" >&2
	exit 1
fi

echo "=== Lab 23.5: Installing the 1.6-debug Refactor ==="
echo
echo "1. Unload any previous myfirst driver:"
kldunload myfirst 2>/dev/null || true

echo
echo "2. Load the refactored driver:"
MODULE=${1:-./myfirst.ko}
kldload "$MODULE"

echo
echo "3. Confirm the version:"
kldstat -v | grep -i myfirst | head -5

echo
echo "4. Confirm the sysctl is present:"
sysctl dev.myfirst.0.debug.mask

echo
echo "5. Enable full debug:"
sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF

echo
echo "6. Exercise the device:"
cat /dev/myfirst0 > /dev/null

echo
echo "7. Read the log:"
dmesg | tail -20

echo
echo "8. Disable debug:"
sysctl dev.myfirst.0.debug.mask=0

echo
echo "9. Count SDT probes via DTrace (5 seconds):"
dtrace -q -n 'myfirst::: { @[probename] = count(); }
              tick-5s { exit(0); }' &
DT_PID=$!
sleep 1
for i in $(seq 1 50); do
	cat /dev/myfirst0 > /dev/null
done
wait $DT_PID

echo
echo "=== End of Lab 23.5 ==="
