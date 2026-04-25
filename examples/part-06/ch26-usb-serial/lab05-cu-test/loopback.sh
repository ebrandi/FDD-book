#!/bin/sh
#
# loopback.sh - Exercise a USB-to-serial loopback.
#
# Assumes a loopback cable (TX->RX) on /dev/cuaU0.

set -eu

DEV=${DEV:-/dev/cuaU0}
SPEED=${SPEED:-9600}
MSG=${MSG:-"loopback test"}

if [ ! -c "${DEV}" ]; then
	echo "Device ${DEV} not present"
	exit 2
fi

stty -f "${DEV}" "${SPEED}" cs8 -parenb -cstopb

# Reader first.
( dd if="${DEV}" bs=1 count=${#MSG} 2>/dev/null > /tmp/loopback.out ) &
READER=$!

sleep 1

echo "${MSG}" > "${DEV}"

sleep 1
wait "${READER}" 2>/dev/null || true

got=$(cat /tmp/loopback.out)
if [ "${got}" = "${MSG}" ]; then
	echo "OK: loopback returned '${got}'"
	exit 0
else
	echo "FAIL: sent '${MSG}' got '${got}'"
	exit 1
fi
