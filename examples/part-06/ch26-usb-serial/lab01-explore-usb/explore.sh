#!/bin/sh
#
# explore.sh - Dump descriptors for all attached USB devices.
#
# For each ugenN.M device listed by usbconfig(8), print the device
# descriptor, configuration descriptor, and string descriptors.

set -eu

for dev in $(usbconfig list | awk '/^ugen/ {print $1}' | tr -d ':'); do
	echo "==================== ${dev} ===================="
	usbconfig -d "${dev}" dump_device_desc  || true
	echo "------ current config ------"
	usbconfig -d "${dev}" dump_curr_config_desc || true
	echo "------ strings ------"
	usbconfig -d "${dev}" dump_string 0x00 || true
	usbconfig -d "${dev}" dump_string 0x01 || true
	usbconfig -d "${dev}" dump_string 0x02 || true
	usbconfig -d "${dev}" dump_string 0x03 || true
	echo
done
