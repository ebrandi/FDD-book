#!/bin/sh
#
# test_echo.sh - Minimal test for the bulk-loopback driver.
#
# Writes a short string to /dev/myfirst_usb0 and reads the result
# back. Exits 0 on match, 1 on mismatch.

set -eu

DEV=${DEV:-/dev/myfirst_usb0}
MSG=${MSG:-"hello usb"}

if [ ! -c "${DEV}" ]; then
	echo "Device ${DEV} not present. Is the driver loaded and the"
	echo "test hardware plugged in?"
	exit 2
fi

echo "${MSG}" > "${DEV}"

# Read back up to len(MSG)+1 bytes. Allow short read.
got=$(dd if="${DEV}" bs=1 count=$((${#MSG}+1)) 2>/dev/null | tr -d '\n')

if [ "${got}" = "${MSG}" ]; then
	echo "OK: echo loopback returned '${got}'"
	exit 0
else
	echo "FAIL: sent '${MSG}' got '${got}'"
	exit 1
fi
