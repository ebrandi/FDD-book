#!/bin/sh
#
# check_env.sh - Verify the lab environment has what Chapter 26 needs.

set -eu

ok=1

echo "Checking userland utilities..."
for cmd in usbconfig stty cu tip dtrace kldstat kldload; do
	if command -v "${cmd}" >/dev/null 2>&1; then
		echo "  OK: ${cmd}"
	else
		echo "  MISSING: ${cmd}"
		ok=0
	fi
done

echo
echo "Checking kernel modules (loadable on demand)..."
for mod in usb uhci ohci ehci xhci uhub nmdm ucom; do
	if kldstat -qm "${mod}" >/dev/null 2>&1; then
		echo "  OK: ${mod} (loaded)"
	else
		if kldload -n "${mod}" 2>/dev/null; then
			echo "  OK: ${mod} (available)"
		else
			echo "  NOT AVAILABLE: ${mod}"
		fi
	fi
done

echo
echo "Checking kernel sources..."
if [ -d /usr/src/sys/dev/usb ]; then
	echo "  OK: /usr/src/sys/dev/usb exists"
else
	echo "  MISSING: /usr/src/sys/dev/usb. Install via git or svnlite."
	ok=0
fi
if [ -d /usr/src/sys/dev/uart ]; then
	echo "  OK: /usr/src/sys/dev/uart exists"
else
	echo "  MISSING: /usr/src/sys/dev/uart. Install via git or svnlite."
	ok=0
fi

echo
if [ "${ok}" = "1" ]; then
	echo "Environment looks good for Chapter 26 labs."
	exit 0
else
	echo "Environment is missing one or more required pieces."
	exit 1
fi
