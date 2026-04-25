#!/bin/sh
#
# setup.sh - Create a VNET jail with an epair interface.
# Chapter 30, Lab 4.
#
# Run as root on the FreeBSD host.  Creates /jails/vnet-test, mounts
# a minimal filesystem, and starts a VNET jail with one end of an
# epair moved into it.
#
# Companion to chapter-30.md.

set -eu

JAIL_NAME="vnet-test"
JAIL_ROOT="/jails/${JAIL_NAME}"
HOST_IP="10.100.0.1"
JAIL_IP="10.100.0.2"
PREFIX="24"

if [ "$(id -u)" -ne 0 ]; then
	echo "$0: must be run as root" >&2
	exit 1
fi

# Ensure required modules are available.
kldload -n if_epair
kldload -n vmm 2>/dev/null || true

# Create jail root directory if missing.
if [ ! -d "${JAIL_ROOT}" ]; then
	mkdir -p "${JAIL_ROOT}"
	# Minimal filesystem: copy a statically-linked shell in /rescue.
	cp /rescue/sh "${JAIL_ROOT}/sh"
	cp /rescue/ifconfig "${JAIL_ROOT}/ifconfig"
	cp /rescue/ping "${JAIL_ROOT}/ping"
	echo "created minimal jail root at ${JAIL_ROOT}"
fi

# Create the epair if it does not already exist.
if ! ifconfig epair0a >/dev/null 2>&1; then
	EPAIR=$(ifconfig epair create)
	# epair create returns epairNa; the partner is the matching b.
	HOST_END="${EPAIR}"
	JAIL_END="${EPAIR%a}b"
else
	HOST_END="epair0a"
	JAIL_END="epair0b"
fi

echo "host end: ${HOST_END}  jail end: ${JAIL_END}"

ifconfig "${HOST_END}" inet "${HOST_IP}/${PREFIX}" up

# Start the VNET jail.
jail -c \
	name="${JAIL_NAME}" \
	path="${JAIL_ROOT}" \
	host.hostname="${JAIL_NAME}" \
	vnet \
	vnet.interface="${JAIL_END}" \
	persist \
	command=/sh

# Configure the jail's end of the epair.
jexec "${JAIL_NAME}" /ifconfig "${JAIL_END}" inet "${JAIL_IP}/${PREFIX}" up

echo
echo "jail ${JAIL_NAME} is running.  Try:"
echo "  jexec ${JAIL_NAME} /ping -c 2 ${HOST_IP}"
echo "  ping -c 2 ${JAIL_IP}"
echo "To tear down:"
echo "  ./teardown.sh"
