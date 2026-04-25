#!/bin/sh
#
# rebuild.sh - convenience script for Chapter 6 labs.
#
# Given a directory that contains a Makefile and a .c source, this
# script:
#
#   1. kldunload's the module (if it is already loaded)
#   2. runs `make clean && make`
#   3. kldload's the freshly built .ko
#   4. prints the tail of dmesg so you can see what happened
#
# Usage:
#   ./scripts/rebuild.sh lab2-hello-module
#   ./scripts/rebuild.sh lab3-demo-device
#
# Run it as root (or through sudo) because kldload/kldunload require
# privilege.

set -eu

if [ $# -ne 1 ]; then
	echo "usage: $0 <lab-directory>" >&2
	exit 1
fi

DIR="$1"
if [ ! -d "$DIR" ]; then
	echo "$0: no such directory: $DIR" >&2
	exit 1
fi

cd "$DIR"

# Try to derive the module name from KMOD= in the Makefile.
MODULE="$(awk '/^KMOD[[:space:]]*=/ { print $NF; exit }' Makefile)"
if [ -z "$MODULE" ]; then
	echo "$0: could not parse KMOD from Makefile" >&2
	exit 1
fi

if kldstat -q -n "$MODULE" >/dev/null 2>&1; then
	echo "==> kldunload $MODULE"
	kldunload "$MODULE"
fi

echo "==> make clean && make in $DIR"
make clean >/dev/null
make

echo "==> kldload ./$MODULE.ko"
kldload "./$MODULE.ko"

echo "==> dmesg tail"
dmesg | tail -n 10
