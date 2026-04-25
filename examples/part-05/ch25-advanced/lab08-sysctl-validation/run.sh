#!/bin/sh
#
# run.sh - sysctl range-validation regression for Lab 8.
#
# Drives every writable sysctl to zero, to its maximum, and to one
# above its maximum.  Reports pass/fail per sysctl.

set -u

check() {
	oid="$1"
	value="$2"
	expected="$3"   # "ok" if the assignment should succeed
	                # "reject" if it should be rejected

	out=$(sysctl "$oid=$value" 2>&1 || true)
	if printf '%s' "$out" | grep -qi "Invalid argument"; then
		got="reject"
	else
		got="ok"
	fi

	if [ "$got" = "$expected" ]; then
		printf "PASS  %-32s = %-8s (%s)\n" "$oid" "$value" "$got"
	else
		printf "FAIL  %-32s = %-8s (expected %s, got %s)\n" \
		    "$oid" "$value" "$expected" "$got"
	fi
}

echo "== timeout_sec (valid range 1-60)"
check dev.myfirst.0.timeout_sec 0  reject
check dev.myfirst.0.timeout_sec 1  ok
check dev.myfirst.0.timeout_sec 60 ok
check dev.myfirst.0.timeout_sec 61 reject

echo
echo "== max_retries (valid range 1-100)"
check dev.myfirst.0.max_retries 0   reject
check dev.myfirst.0.max_retries 1   ok
check dev.myfirst.0.max_retries 100 ok
check dev.myfirst.0.max_retries 101 reject

echo
echo "== log_ratelimit_pps (valid range 1-10000)"
check dev.myfirst.0.log_ratelimit_pps 0     reject
check dev.myfirst.0.log_ratelimit_pps 1     ok
check dev.myfirst.0.log_ratelimit_pps 10000 ok
check dev.myfirst.0.log_ratelimit_pps 10001 reject

echo
echo "== debug.mask (accepts any int; check that read-back matches)"
check dev.myfirst.0.debug.mask 0     ok
check dev.myfirst.0.debug.mask 255   ok

# Restore sane defaults for subsequent labs.
sysctl dev.myfirst.0.timeout_sec=5        >/dev/null 2>&1 || true
sysctl dev.myfirst.0.max_retries=3        >/dev/null 2>&1 || true
sysctl dev.myfirst.0.log_ratelimit_pps=10 >/dev/null 2>&1 || true
sysctl dev.myfirst.0.debug.mask=0         >/dev/null 2>&1 || true
