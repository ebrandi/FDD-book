#!/bin/sh
#
# lab24_4_failure.sh - Walk the lifecycle by injecting a failure.
#
# Lab 24.4: verify that the goto err chain in myfirst_attach actually
# unwinds by deliberately failing one of the steps.  This script does
# not edit the driver for you; it documents the change and runs the
# load attempt that should fail cleanly.
#
# To prepare:
#   1. Copy your Stage 3 driver into ${WORKDIR} (default: ~/myfirst-lab4).
#   2. Edit myfirst.c.  After the make_dev_s success path, insert:
#
#        device_printf(dev, "Lab 4: injected failure after make_dev_s\n");
#        error = ENXIO;
#        goto fail_cdev;
#
#      Add the matching label and destroy_dev call to the failure tail
#      of myfirst_attach.

set -e

WORKDIR=${WORKDIR:-${HOME}/myfirst-lab4}

echo "=== Lab 24.4: failure injection ==="
echo "Working directory: ${WORKDIR}"

if [ ! -d "${WORKDIR}" ]; then
	echo "ERROR: ${WORKDIR} does not exist."
	echo "Copy your Stage 3 driver tree into ${WORKDIR} and inject"
	echo "the deliberate failure as documented above."
	exit 1
fi

cd "${WORKDIR}"

echo
echo "--- make ---"
make clean
make

echo
echo "--- kldload should FAIL because of the injected ENXIO ---"
if sudo kldload ./myfirst.ko; then
	echo "WARNING: load succeeded; did you forget the injected failure?"
	sudo kldunload myfirst
	exit 1
fi
echo "kldload returned an error as expected."

echo
echo "--- expected: device node was destroyed by the cleanup chain ---"
if [ -e /dev/myfirst0 ]; then
	echo "FAIL: /dev/myfirst0 still exists; cleanup chain missed destroy_dev."
	exit 1
fi
echo "/dev/myfirst0 is absent."

echo
echo "--- expected: module is not loaded ---"
if kldstat | grep -q myfirst; then
	echo "FAIL: myfirst still listed in kldstat."
	exit 1
fi
echo "myfirst is not in kldstat."

echo
echo "--- recent dmesg, including the injected message ---"
sudo dmesg | tail

echo
echo "Lab 24.4 PASSED.  The cleanup chain unwound correctly: the"
echo "cdev was destroyed, the mutex was destroyed, no resources"
echo "leaked.  Remove the injected failure before continuing."
