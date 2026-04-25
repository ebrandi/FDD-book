#!/bin/sh
#
# pre-submission-test.sh
#
# Run the standard pre-submission pipeline for a FreeBSD driver
# before opening a review.  This script is a companion to
# Chapter 37 of "FreeBSD Device Drivers: From First Steps to
# Kernel Mastery."
#
# It assumes a conventional file layout:
#
#   $SRC:  /usr/src/sys/dev/<driver>/
#   $MOD:  /usr/src/sys/modules/<driver>/
#   $MAN:  /usr/src/share/man/man4/<driver>.4
#
# Edit the NAME and ARCH variables to match your driver and the
# architecture you want to cross-build for.
#
# Usage:
#   ./pre-submission-test.sh
#
# Exit code 0 means every stage passed.  Non-zero means a stage
# failed; the script's -e flag stops the pipeline at the first
# failing command.

set -e

# ----- configuration -----

NAME=${NAME:-mydev}
ARCH=${ARCH:-arm64}
SRC=${SRC:-/usr/src/sys/dev/${NAME}}
MOD=${MOD:-/usr/src/sys/modules/${NAME}}
MAN=${MAN:-/usr/src/share/man/man4/${NAME}.4}

CHECKSTYLE=${CHECKSTYLE:-/usr/src/tools/build/checkstyle9.pl}

# ----- helpers -----

stage() {
	printf '\n=== %s ===\n' "$1"
}

fail() {
	printf '\n*** FAIL: %s ***\n' "$1" >&2
	exit 1
}

require_file() {
	[ -e "$1" ] || fail "missing: $1"
}

require_dir() {
	[ -d "$1" ] || fail "missing directory: $1"
}

# ----- sanity checks -----

stage "sanity checks"
require_dir "$SRC"
require_dir "$MOD"
require_file "$MAN"
require_file "$CHECKSTYLE"
echo "source directory: $SRC"
echo "module directory: $MOD"
echo "manual page:      $MAN"
echo "style checker:    $CHECKSTYLE"

# ----- style check -----

stage "style check (checkstyle9.pl)"
# shellcheck disable=SC2086
perl "$CHECKSTYLE" "$SRC"/*.c "$SRC"/*.h

# ----- mandoc lint -----

stage "manual-page lint (mandoc -Tlint)"
mandoc -Tlint "$MAN"

# ----- optional igor pass -----

if command -v igor >/dev/null 2>&1; then
	stage "manual-page prose check (igor)"
	igor "$MAN" || fail "igor reported issues"
else
	stage "igor not installed; skipping prose check"
fi

# ----- local build -----

stage "local build (make clean && make obj && make depend && make)"
(
	cd "$MOD" || fail "cannot cd to $MOD"
	make clean
	make obj
	make depend
	make
)

# ----- load and unload -----

KO_PATH="$MOD/${NAME}.ko"

if [ ! -f "$KO_PATH" ]; then
	# Some module builds place the .ko under an obj tree.
	KO_PATH=$(find "$MOD" -name "${NAME}.ko" -print -quit)
fi

if [ -z "$KO_PATH" ] || [ ! -f "$KO_PATH" ]; then
	fail "built module not found for $NAME"
fi

stage "load/unload cycle (kldload / kldunload)"
echo "loading:   $KO_PATH"
sudo kldload "$KO_PATH"

echo "verifying: kldstat"
kldstat | grep -F "$NAME" || fail "module $NAME not listed by kldstat"

echo "unloading: $NAME"
sudo kldunload "$NAME"

# ----- cross-architecture build -----

stage "cross-architecture build (TARGET=${ARCH} buildkernel)"
(
	cd /usr/src || fail "cannot cd to /usr/src"
	make TARGET="${ARCH}" buildkernel KERNCONF=GENERIC
)

# ----- done -----

stage "all pre-submission tests passed"
echo "driver ${NAME} is ready for review."
