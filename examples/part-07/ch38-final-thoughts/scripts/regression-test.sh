#!/bin/sh
#
# regression-test.sh
#
# Skeleton regression-test script for a FreeBSD driver.  This
# script is a companion to Chapter 38 of "FreeBSD Device
# Drivers: From First Steps to Kernel Mastery."
#
# The intent is to give you a starting point you can adapt to
# the specific behaviours your driver exposes.  The skeleton
# exercises the generic lifecycle (build, load, attach, basic
# interaction, detach, unload) and fails loudly at the first
# step that misbehaves.
#
# Edit the NAME and ITERATIONS variables, and then add your
# own driver-specific test functions below the section marked
# "driver-specific tests".
#
# Usage:
#   ./regression-test.sh
#
# Exit code 0 means every check passed.  Non-zero means a
# check failed; the script's -e flag stops at the first
# failing command.

set -e

# ----- configuration -----

NAME=${NAME:-mydrv}
MOD=${MOD:-/usr/src/sys/modules/${NAME}}
ITERATIONS=${ITERATIONS:-50}

# ----- helpers -----

stage() {
	printf '\n=== %s ===\n' "$1"
}

fail() {
	printf '\n*** FAIL: %s ***\n' "$1" >&2
	exit 1
}

# ----- sanity checks -----

stage "sanity checks"
[ -d "$MOD" ] || fail "module directory not found: $MOD"
command -v kldstat >/dev/null 2>&1 || fail "kldstat not in PATH"
command -v kldload >/dev/null 2>&1 || fail "kldload not in PATH"
command -v kldunload >/dev/null 2>&1 || fail "kldunload not in PATH"

# ----- build -----

stage "build"
(
	cd "$MOD" || fail "cannot cd to $MOD"
	make clean
	make obj
	make depend
	make
)

# ----- locate the .ko -----

KO_PATH="$MOD/${NAME}.ko"
if [ ! -f "$KO_PATH" ]; then
	KO_PATH=$(find "$MOD" -name "${NAME}.ko" -print -quit)
fi
[ -f "$KO_PATH" ] || fail "built module not found for $NAME"

# ----- first load and unload -----

stage "initial load"
sudo kldload "$KO_PATH"

stage "verify kldstat"
kldstat | grep -F "$NAME" || fail "module not listed by kldstat"

# ----- driver-specific tests -----
#
# Add your own tests here.  Examples of the kinds of checks you
# might include:
#   - confirm that the expected /dev node exists
#   - open /dev/${NAME}0, issue a test ioctl, and read the
#     returned value
#   - read a sysctl and confirm it has the expected value
#   - write a sysctl and confirm the driver reacts
#   - push data through a character device and verify round
#     trip
#
# Wrap each test in a stage() call so the log is easy to read.
#
# Example:
#   stage "verify /dev node"
#   [ -c /dev/${NAME}0 ] || fail "missing /dev/${NAME}0"
#
#   stage "read sysctl dev.${NAME}.0.status"
#   sysctl -n dev.${NAME}.0.status | grep -q ready || \
#       fail "driver not in 'ready' state"
#

stage "driver-specific tests"
echo "(no driver-specific tests defined yet)"

# ----- unload -----

stage "initial unload"
sudo kldunload "$NAME"

# ----- load / unload stress loop -----

stage "load/unload stress loop (${ITERATIONS} iterations)"
i=1
while [ "$i" -le "$ITERATIONS" ]; do
	printf 'iteration %d of %d\n' "$i" "$ITERATIONS"
	sudo kldload "$KO_PATH"
	kldstat | grep -F "$NAME" >/dev/null || \
	    fail "module not present on iteration $i"
	sudo kldunload "$NAME"
	i=$((i + 1))
done

# ----- memory leak sanity check -----

stage "memory leak sanity check (vmstat -m)"
vmstat -m | grep -F "$NAME" || \
    echo "(no entries for $NAME; this is normal for drivers " \
         "that do not name their mallocs)"

# ----- dmesg scan -----

stage "dmesg scan for unexpected messages"
DMESG_OUT=$(dmesg | tail -n 200)
echo "$DMESG_OUT" | grep -Ei "panic|warn|err|fail" | \
    grep -F "$NAME" && \
    fail "unexpected messages in dmesg; review above"

# ----- done -----

stage "all regression checks passed"
echo "driver ${NAME} survived ${ITERATIONS} load/unload cycles."
