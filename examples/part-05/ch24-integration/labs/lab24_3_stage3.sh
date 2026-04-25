#!/bin/sh
#
# lab24_3_stage3.sh - Add the sysctl tree and loader-time tunable.
#
# Lab 24.3: extend the Stage 2 driver with the sysctl OIDs from
# Chapter 24 Section 4 and read a tunable from the loader environment.
#
# Before running this script, copy the following files from
# examples/part-05/ch24-integration/stage3-sysctl/ into your working
# tree:
#   - myfirst_sysctl.c
#   - myfirst.c           (replaces the Stage 2 attach/detach)
#   - Makefile            (replaces the Stage 2 Makefile)

set -e

WORKDIR=${WORKDIR:-${HOME}/myfirst-lab3}

echo "=== Lab 24.3: Stage 3 sysctl tree ==="
echo "Working directory: ${WORKDIR}"

if [ ! -d "${WORKDIR}" ]; then
	echo "ERROR: ${WORKDIR} does not exist."
	echo "Copy your Stage 2 driver tree into ${WORKDIR} and apply"
	echo "the Stage 3 changes first."
	exit 1
fi

cd "${WORKDIR}"

echo
echo "--- make ---"
make clean
make

echo
echo "--- set the loader-time tunable before load ---"
sudo kenv hw.myfirst.debug_mask_default=0x06

echo
echo "--- kldload ---"
sudo kldload ./myfirst.ko

echo
echo "--- expected: full sysctl tree below dev.myfirst.0 ---"
sysctl -a dev.myfirst.0

echo
echo "--- expected: debug.mask reflects the tunable (0x06 = 6) ---"
sysctl dev.myfirst.0.debug.mask

echo
echo "--- drive a read and check the counter increments ---"
sudo cat /dev/myfirst0 > /dev/null
sysctl dev.myfirst.0.total_reads

echo
echo "Stage 3 verification PASSED if the sysctl tree showed all OIDs,"
echo "debug.mask was 6, and total_reads incremented after the cat."
echo
echo "To clean up: sudo kldunload myfirst"
echo "To remove the tunable: sudo kenv -u hw.myfirst.debug_mask_default"
