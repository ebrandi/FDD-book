#!/bin/sh
#
# lab24_2_stage2.sh - Add the ioctl interface to the myfirst driver.
#
# Lab 24.2: extend the Stage 1 driver with the four ioctl commands
# (GETVER, GETMSG, SETMSG, RESET) and the user-space companion
# myfirstctl(8).
#
# Before running this script, copy the following files from
# examples/part-05/ch24-integration/stage2-ioctl/ into your working
# tree:
#   - myfirst_ioctl.h
#   - myfirst_ioctl.c
#   - myfirstctl.c
#   - Makefile          (replaces the Stage 1 Makefile)
#   - Makefile.user

set -e

WORKDIR=${WORKDIR:-${HOME}/myfirst-lab2}

echo "=== Lab 24.2: Stage 2 ioctl interface ==="
echo "Working directory: ${WORKDIR}"

if [ ! -d "${WORKDIR}" ]; then
	echo "ERROR: ${WORKDIR} does not exist."
	echo "Copy your Stage 1 driver tree into ${WORKDIR} and apply"
	echo "the Stage 2 changes first."
	exit 1
fi

cd "${WORKDIR}"

echo
echo "--- make (kernel module) ---"
make clean
make

echo
echo "--- make (user-space companion) ---"
make -f Makefile.user clean || true
make -f Makefile.user

echo
echo "--- kldload ---"
sudo kldload ./myfirst.ko

echo
echo "--- expected: ioctl version is 1 ---"
./myfirstctl get-version

echo
echo "--- expected: default greeting ---"
./myfirstctl get-message

echo
echo "--- set, get, reset round-trip ---"
sudo ./myfirstctl set-message "drivers are fun"
./myfirstctl get-message
sudo ./myfirstctl reset
./myfirstctl get-message
echo "(an empty line above means the message buffer is cleared)"

echo
echo "Stage 2 verification PASSED if all four ioctls returned"
echo "without errors and the round-trip showed the new message."
echo
echo "To clean up: sudo kldunload myfirst"
