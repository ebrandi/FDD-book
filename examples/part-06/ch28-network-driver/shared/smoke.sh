#!/bin/sh
#
# smoke.sh - minimum viable lifecycle check for mynet.
#
# Loads the module, creates an instance, configures an address, prints
# the observable state, and tears everything down. Intended for use
# after any change to mynet.c as a quick regression check.
#
# Run this from the chapter directory:
#     examples/part-06/ch28-network-driver/
# after building with "make".
#
# Requires root and FreeBSD 14.3 or later.

set -e

echo "== load =="
kldload ./mynet.ko

echo "== create =="
ifconfig mynet create

echo "== configure =="
ifconfig mynet0 inet 192.0.2.1/24 up

echo "== observe =="
ifconfig mynet0
netstat -in -I mynet0

echo "== teardown =="
ifconfig mynet0 destroy
kldunload mynet

echo "OK"
