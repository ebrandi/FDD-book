#!/bin/sh
#
# run.sh - lifecycle baseline for Chapter 28 Lab 5.
#
# Loads the mynet module, creates an interface, configures an IPv4
# address, captures traffic, and tears everything down. Use it as a
# regression check after edits to mynet.c.
#
# Run this script from the chapter directory:
#     examples/part-06/ch28-network-driver/
# after building mynet.ko with "make".
#
# Requires root and FreeBSD 14.3 or later.

set -e

echo "== load =="
kldload ./mynet.ko

echo "== create =="
ifconfig mynet create

echo "== configure =="
ifconfig mynet0 inet 192.0.2.1/24 up

echo "== traffic =="
(tcpdump -i mynet0 -nn -c 5 > /tmp/mynet-tcpdump.txt 2>&1) &
sleep 3
ping -c 2 192.0.2.99 || true
wait
cat /tmp/mynet-tcpdump.txt

echo "== counters =="
netstat -in -I mynet0

echo "== teardown =="
ifconfig mynet0 destroy
kldunload mynet
