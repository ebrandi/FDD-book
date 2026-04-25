#!/bin/sh
#
# lab24_7_reload.sh - Reload without restarting user-space programs.
#
# Lab 24.7: confirm that the driver's soft-detach pattern correctly
# refuses unload while a user-space program holds the device open,
# and that the unload succeeds once the file descriptor is released.
#
# Run with the Stage 3 driver loaded.  The script forks a sleep
# process that holds /dev/myfirst0 open, attempts an unload (which
# must fail), kills the holder, then attempts the unload again
# (which must succeed).

set -e

DEV=${DEV:-/dev/myfirst0}

echo "=== Lab 24.7: soft-detach / reload test ==="

if [ ! -c "${DEV}" ]; then
	echo "ERROR: ${DEV} does not exist.  Load the Stage 3 driver first."
	exit 1
fi

echo
echo "--- holding ${DEV} open in the background ---"
sleep 30 < "${DEV}" &
HOLDER=$!
sleep 1
echo "holder pid: ${HOLDER}"

echo
echo "--- expected: kldunload should FAIL with Device busy ---"
if sudo kldunload myfirst 2>&1 | grep -q "Device busy"; then
	echo "PASS: unload was correctly refused while ${DEV} was open."
else
	echo "FAIL: unload was not refused."
	kill ${HOLDER} 2>/dev/null || true
	exit 1
fi

echo
echo "--- open_count reflects the held fd ---"
sysctl dev.myfirst.0.open_count

echo
echo "--- releasing the holder ---"
kill ${HOLDER}
wait ${HOLDER} 2>/dev/null || true
sleep 1

echo
echo "--- expected: kldunload should SUCCEED now ---"
if sudo kldunload myfirst; then
	echo "PASS: unload succeeded after the fd was released."
else
	echo "FAIL: unload still failed."
	exit 1
fi

echo
echo "--- expected: sysctl tree is gone ---"
if sysctl dev.myfirst.0 >/dev/null 2>&1; then
	echo "FAIL: dev.myfirst.0 still present after unload."
	exit 1
fi
echo "PASS: sysctl tree was removed by Newbus after detach."

echo
echo "Lab 24.7 PASSED.  The soft-detach contract is honoured: the"
echo "driver refused to unload while open, and unloaded cleanly"
echo "once the fd was released."
