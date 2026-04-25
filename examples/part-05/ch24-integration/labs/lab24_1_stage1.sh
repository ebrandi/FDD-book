#!/bin/sh
#
# lab24_1_stage1.sh - Build and load the Stage 1 myfirst driver.
#
# Lab 24.1: bring the driver from the Chapter 23 baseline (1.6-debug)
# up to the Stage 1 milestone of Chapter 24 (a properly constructed
# cdev under /dev/myfirst0).
#
# This script does not modify the driver source.  Apply the Stage 1
# changes from examples/part-05/ch24-integration/stage1-devfs/myfirst.c
# to your working tree before running it.

set -e

WORKDIR=${WORKDIR:-${HOME}/myfirst-lab1}

echo "=== Lab 24.1: Stage 1 build and load ==="
echo "Working directory: ${WORKDIR}"

if [ ! -d "${WORKDIR}" ]; then
	echo "ERROR: ${WORKDIR} does not exist."
	echo "Copy your Chapter 23 Stage 3 driver tree into ${WORKDIR}"
	echo "and apply the Stage 1 changes from"
	echo "examples/part-05/ch24-integration/stage1-devfs/ first."
	exit 1
fi

cd "${WORKDIR}"

echo
echo "--- make ---"
make clean
make

echo
echo "--- kldload ---"
sudo kldload ./myfirst.ko

echo
echo "--- expected: device node exists with mode 0660 ---"
ls -l /dev/myfirst0

echo
echo "--- expected: cat returns the default greeting ---"
sudo cat /dev/myfirst0

echo
echo "--- expected: kldstat shows the loaded module ---"
kldstat | grep myfirst || true

echo
echo "--- recent dmesg from the driver ---"
sudo dmesg | tail

echo
echo "Stage 1 verification PASSED if the device exists, the cat"
echo "returned the greeting, and kldstat shows myfirst.ko."
echo
echo "To clean up: sudo kldunload myfirst"
