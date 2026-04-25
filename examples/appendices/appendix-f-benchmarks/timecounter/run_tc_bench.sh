#!/bin/sh
#
# run_tc_bench.sh - rotate kern.timecounter.hardware through every
# available source and run tc_bench under each.
#
# Must be run as root. Restores the original timecounter selection
# on exit.
#
# Companion to Appendix F of "FreeBSD Device Drivers: From First
# Steps to Kernel Mastery".

set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "error: must run as root (kern.timecounter.hardware is root-writable)" >&2
	exit 1
fi

if [ ! -x ./tc_bench ]; then
	echo "error: ./tc_bench not found; run 'make' first" >&2
	exit 1
fi

original=$(sysctl -n kern.timecounter.hardware)
choices=$(sysctl -n kern.timecounter.choice)

cleanup() {
	sysctl "kern.timecounter.hardware=${original}" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

echo "# host: $(hostname)"
echo "# uname: $(uname -rmp)"
echo "# kern.timecounter.choice: ${choices}"
echo "# original kern.timecounter.hardware: ${original}"
echo ""

# kern.timecounter.choice is a space-separated list of "name(quality)"
# entries. Extract just the names.
for entry in ${choices}; do
	name=$(echo "${entry}" | sed 's/(.*//')

	# Try to select this timecounter; if the kernel refuses (because
	# the hardware is not actually usable right now), skip it.
	if ! sysctl "kern.timecounter.hardware=${name}" >/dev/null 2>&1; then
		echo "# ${name}: could not select, skipping"
		continue
	fi

	# Give the kernel a moment to settle.
	sleep 1
	echo "=== timecounter: ${name} ==="
	./tc_bench
	echo ""
done

echo "# restored kern.timecounter.hardware to ${original}"
