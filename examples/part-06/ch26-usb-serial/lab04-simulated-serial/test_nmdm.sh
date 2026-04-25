#!/bin/sh
#
# test_nmdm.sh - Exercise an nmdm(4) virtual serial pair.

set -eu

if ! kldstat -qm nmdm >/dev/null 2>&1; then
	echo "Loading nmdm module"
	kldload nmdm
fi

A=/dev/nmdm0.A
B=/dev/nmdm0.B

# Open the B end in a background reader.
( cat "${B}" > /tmp/nmdm0.B.out ) &
READER=$!

sleep 1

echo "hello from A" > "${A}"
echo "another line" > "${A}"

sleep 1
kill "${READER}" 2>/dev/null || true

echo "B end received:"
cat /tmp/nmdm0.B.out
