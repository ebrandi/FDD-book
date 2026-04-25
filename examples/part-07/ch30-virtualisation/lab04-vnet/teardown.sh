#!/bin/sh
#
# teardown.sh - Clean up the VNET jail set up by setup.sh.
# Chapter 30, Lab 4.

set -eu

JAIL_NAME="vnet-test"

if [ "$(id -u)" -ne 0 ]; then
	echo "$0: must be run as root" >&2
	exit 1
fi

if jls | awk '{ print $3 }' | grep -q "^${JAIL_NAME}$"; then
	jail -r "${JAIL_NAME}"
	echo "jail ${JAIL_NAME} removed"
else
	echo "no jail named ${JAIL_NAME}"
fi

# Remove the epair (will succeed whichever end is left on the host).
if ifconfig epair0a >/dev/null 2>&1; then
	ifconfig epair0a destroy
	echo "epair destroyed"
fi
