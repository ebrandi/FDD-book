#!/bin/sh
#
# identify.sh - Capture the static identity of every PCI and USB
# device on a FreeBSD system.
#
# Companion to Chapter 36, Lab 1.
#
# Output goes to a directory named after the current date and time,
# so that successive runs do not overwrite each other. Each captured
# block is saved to its own file so you can diff captures from
# different machines, different boots, or different kernel
# configurations.
#
# Usage:
#   sudo sh identify.sh [output_directory]
#
# If the output directory is not given, a directory of the form
# identify-YYYYMMDD-HHMMSS is created in the current working
# directory.
#
# The script must be run as root because pciconf -lvc and usbconfig
# need access to privileged interfaces. usbdump capture is not
# performed here; that is the job of Lab 2.

set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "This script must be run as root." >&2
	exit 1
fi

OUTDIR="${1:-identify-$(date '+%Y%m%d-%H%M%S')}"
mkdir -p "$OUTDIR"

echo "Writing captures to $OUTDIR"

# Static system identity. Useful for correlating captures from
# different machines.
{
	echo "# uname -a"
	uname -a
	echo
	echo "# date"
	date
	echo
	echo "# sysctl hw.machine hw.model hw.ncpu"
	sysctl hw.machine hw.model hw.ncpu
} >"$OUTDIR/system.txt"

# PCI devices. The -lvc flags ask pciconf for a long listing with
# verbose output and capability dumps, which is the most informative
# form available for static identification.
echo "Capturing PCI device list..."
pciconf -lvc >"$OUTDIR/pciconf.txt" 2>&1

# Devices that have no in-tree driver attached appear as "noneN@..."
# in the pciconf output. Those are the candidate targets for any
# reverse-engineering investigation.
grep '^none' "$OUTDIR/pciconf.txt" >"$OUTDIR/pciconf-none.txt" || true

# USB devices. usbconfig with no arguments lists every USB device
# the kernel has enumerated.
echo "Capturing USB device list..."
usbconfig >"$OUTDIR/usbconfig.txt" 2>&1

# For each USB device, dump the device, configuration, and string
# descriptors. The descriptors are the static identity of the device
# at the USB level.
echo "Capturing USB descriptors..."
mkdir -p "$OUTDIR/usb-descriptors"
usbconfig list 2>/dev/null | awk '{print $1}' | sed 's/:$//' |
    while read -r dev; do
	    if [ -z "$dev" ]; then
		    continue
	    fi
	    safedev=$(echo "$dev" | tr '/.' '__')
	    {
		    echo "# usbconfig -d $dev dump_device_desc"
		    usbconfig -d "$dev" dump_device_desc 2>&1 || true
		    echo
		    echo "# usbconfig -d $dev dump_curr_config_desc"
		    usbconfig -d "$dev" dump_curr_config_desc 2>&1 || true
		    echo
		    echo "# usbconfig -d $dev dump_string_desc"
		    usbconfig -d "$dev" dump_string_desc 2>&1 || true
	    } >"$OUTDIR/usb-descriptors/$safedev.txt"
    done

# Newbus devices. devinfo gives a tree view of every device the
# kernel knows about, with parent-child relationships visible.
echo "Capturing devinfo tree..."
devinfo -v >"$OUTDIR/devinfo.txt" 2>&1
devinfo -r >"$OUTDIR/devinfo-resources.txt" 2>&1

# Boot-time device messages. dmesg records the attach messages of
# every driver that successfully probed a device, which often
# includes useful identifying information.
echo "Capturing boot-time device messages..."
dmesg | grep -E 'pci|usb|attached|detached|on usbus|<' \
    >"$OUTDIR/dmesg-devices.txt" 2>&1 || true

# Optional: kernel module list. Knowing which modules are loaded can
# explain why some devices have drivers and others do not.
echo "Capturing loaded kernel modules..."
kldstat -v >"$OUTDIR/kldstat.txt" 2>&1

echo
echo "Capture complete."
echo "Output directory: $OUTDIR"
echo
echo "Suggested next steps:"
echo "  1. Open $OUTDIR/pciconf-none.txt and look for unknown devices."
echo "  2. Open $OUTDIR/usbconfig.txt and pick a USB device of interest."
echo "  3. Compare $OUTDIR/usb-descriptors/<device>.txt with the public"
echo "     USB.org class definitions for the corresponding device class."
