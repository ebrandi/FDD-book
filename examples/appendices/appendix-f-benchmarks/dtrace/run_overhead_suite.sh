#!/bin/sh
#
# run_overhead_suite.sh - run the workload programs and print
# their reports, tagged with the current uname and kernel config.
#
# Intended to be run four times: once on a base kernel, once on an
# INVARIANTS kernel, once on a WITNESS kernel, and once on any of
# them with dtrace_overhead.d attached. Compare the result files
# afterward.
#
# Companion to Appendix F of "FreeBSD Device Drivers: From First
# Steps to Kernel Mastery".

set -eu

if [ ! -x ./workload_syscalls ] || [ ! -x ./workload_locks ]; then
	echo "error: build the workloads first (run 'make')" >&2
	exit 1
fi

# Capture as much of the kernel context as is easy to grab.
echo "# host: $(hostname)"
echo "# uname: $(uname -rmp)"
if sysctl kern.conftxt >/dev/null 2>&1; then
	invariants=$(sysctl -n kern.conftxt 2>/dev/null | \
	    grep -c '^options.*INVARIANTS$' || true)
	witness=$(sysctl -n kern.conftxt 2>/dev/null | \
	    grep -c '^options.*WITNESS$' || true)
	echo "# kernel_has_INVARIANTS=${invariants}"
	echo "# kernel_has_WITNESS=${witness}"
fi
echo ""

echo "=== syscalls workload ==="
./workload_syscalls
echo ""

echo "=== locks workload ==="
./workload_locks
echo ""

echo "# done"
