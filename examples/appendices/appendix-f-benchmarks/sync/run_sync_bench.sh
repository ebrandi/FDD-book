#!/bin/sh
#
# run_sync_bench.sh - trigger every sync_bench benchmark and print
# the results as a small table.
#
# Must be run as root, after kldload ./sync_bench.ko succeeds.
#
# Companion to Appendix F of "FreeBSD Device Drivers: From First
# Steps to Kernel Mastery".

set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "error: must run as root" >&2
	exit 1
fi

if ! kldstat -n sync_bench >/dev/null 2>&1; then
	echo "error: sync_bench is not loaded; kldload ./sync_bench.ko first" >&2
	exit 1
fi

run_one() {
	label=$1
	trigger=$2
	sysctl_ns=$3

	sysctl -w "debug.sync_bench.${trigger}=1" >/dev/null
	ns=$(sysctl -n "debug.sync_bench.${sysctl_ns}")
	printf "%-30s %s ns\n" "${label}" "${ns}"
}

echo "# host: $(hostname)"
echo "# uname: $(uname -rmp)"
echo ""

run_one "mtx_lock (uncontended)"    run_mtx      last_ns_mtx
run_one "sx_slock (uncontended)"    run_sx_slock last_ns_sx_slock
run_one "sx_xlock (uncontended)"    run_sx_xlock last_ns_sx_xlock
run_one "cv round-trip"             run_cv       last_ns_cv
run_one "sema round-trip"           run_sema     last_ns_sema

echo ""
echo "# done; dmesg has the per-run iteration counts"
