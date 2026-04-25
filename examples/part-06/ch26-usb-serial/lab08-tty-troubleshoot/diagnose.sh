#!/bin/sh
#
# diagnose.sh - Compare termios settings on both ends of a TTY pair.

set -eu

A=${A:-/dev/nmdm0.A}
B=${B:-/dev/nmdm0.B}

if [ ! -c "${A}" ] || [ ! -c "${B}" ]; then
	echo "One of ${A}, ${B} is missing. Load nmdm and open the pair."
	exit 2
fi

echo "=== A: ${A} ==="
stty -a -f "${A}"
echo
echo "=== B: ${B} ==="
stty -a -f "${B}"
echo
echo "=== Diff ==="
diff -u \
	<(stty -a -f "${A}" | tr ';' '\n' | sort) \
	<(stty -a -f "${B}" | tr ';' '\n' | sort) || true
