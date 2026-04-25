#!/bin/sh
#
# run.sh - combined regression for Lab 4 failure injection.
#
# Builds each of the four injection variants, attempts to load it,
# verifies that the load fails, and verifies that the kernel is in a
# clean state (no sysctl, no cdev, no witness complaints).  One-line
# pass/fail report per variant.

set -u

SCRIPT_DIR=$(dirname "$0")
cd "$SCRIPT_DIR"

VARIANTS="inject-mtx inject-cdev inject-sysctl inject-log"

fail=0
for v in $VARIANTS; do
	echo "== Variant: $v"

	make -C "$v" clean >/dev/null 2>&1 || true
	if ! make -C "$v" >/dev/null 2>"$v/build.log"; then
		echo "  FAIL: build error; see $v/build.log"
		fail=$((fail + 1))
		continue
	fi

	# The load is expected to fail.
	if kldload "$v/myfirst.ko" >/dev/null 2>&1; then
		echo "  FAIL: load succeeded; should have failed"
		kldunload myfirst >/dev/null 2>&1 || true
		fail=$((fail + 1))
		continue
	fi

	# Confirm clean state.
	if sysctl dev.myfirst >/dev/null 2>&1; then
		echo "  FAIL: dev.myfirst sysctl still exists"
		fail=$((fail + 1))
		continue
	fi
	if ls /dev/myfirst0 >/dev/null 2>&1; then
		echo "  FAIL: /dev/myfirst0 still exists"
		fail=$((fail + 1))
		continue
	fi
	if dmesg | tail -20 | grep -iq "witness\|leak"; then
		echo "  FAIL: witness/leak warning in dmesg"
		fail=$((fail + 1))
		continue
	fi

	echo "  PASS"
done

echo
if [ $fail -eq 0 ]; then
	echo "All $(echo $VARIANTS | wc -w) variants passed."
	exit 0
else
	echo "$fail variant(s) failed."
	exit 1
fi
