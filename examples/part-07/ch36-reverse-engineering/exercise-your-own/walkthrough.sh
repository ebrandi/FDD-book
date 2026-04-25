#!/bin/sh
#
# walkthrough.sh - Companion script for Chapter 36's
# "Hands-On Exercise: Your Own Observation".
#
# This script runs the suggested usbconfig(8) and usbdump(8)
# commands in order against one USB device of your choice. It is
# a template: edit the three variables below to match your device
# and your capture layout before you run it.
#
# The script performs passive observation only. It does not write
# to the device and it does not change device state. Read the
# exercise's README.md and the safety notes in Chapter 36 before
# running it.
#
# Usage:
#   sudo sh walkthrough.sh
#

set -eu

############################################################
# Edit these three variables before running.
############################################################

# The ugenB.D coordinate of your target device.
# Run "usbconfig list" first to find the right value.
UGEN="ugen0.2"

# The USB bus the device is attached to.
# On a single-controller system this is typically usbus0.
USBUS="usbus0"

# Directory in which to store the captures and dumps.
# A dedicated per-device folder is the cleanest option.
OUTDIR="./capture"

############################################################
# End of configuration.
############################################################

if [ "$(id -u)" -ne 0 ]; then
	echo "This script must be run as root." >&2
	exit 1
fi

if ! command -v usbconfig >/dev/null 2>&1; then
	echo "usbconfig(8) not found in PATH." >&2
	exit 1
fi

if ! command -v usbdump >/dev/null 2>&1; then
	echo "usbdump(8) not found in PATH." >&2
	exit 1
fi

mkdir -p "$OUTDIR"

############################################################
# Safety reminder.
############################################################

cat <<EOF
Hands-On Exercise: Your Own Observation
---------------------------------------
Target UGEN coordinate: $UGEN
USB bus:                $USBUS
Output directory:       $OUTDIR

Before continuing, confirm that you have worked through the safety
checklist in README.md:

  1. The device is one you own outright.
  2. The device's firmware and protocol are not vendor-protected.
  3. You will observe only and will not import vendor code.
  4. You are deferring to Sections 1 and 12 of Chapter 36.

This script performs passive observation only. No writes to the
device are performed.

EOF

printf 'Press Return to continue, or Control-C to abort: '
read -r _ignored

############################################################
# Step 1: static identification with usbconfig(8).
############################################################

echo "Step 1: capturing static descriptor information."

echo "  -> $OUTDIR/usbconfig-list.txt"
usbconfig list > "$OUTDIR/usbconfig-list.txt"

echo "  -> $OUTDIR/show-ifdrv.txt"
usbconfig -d "$UGEN" show_ifdrv > "$OUTDIR/show-ifdrv.txt"

echo "  -> $OUTDIR/device-descriptors.txt"
usbconfig -d "$UGEN" dump_all_desc > "$OUTDIR/device-descriptors.txt"

cat <<EOF

Static identification complete.

Look at $OUTDIR/show-ifdrv.txt now:
  - If an existing kernel driver owns the interface, detach it
    with "sudo devctl detach <name>" before running your own code
    against the device. You do not need to detach it for the
    capture step below; the capture is passive.
  - Make a note of the bInterfaceClass, bInterfaceSubClass, and
    bInterfaceProtocol fields from $OUTDIR/device-descriptors.txt.
    You will map them to driver structures in Step 3.

EOF

############################################################
# Step 2: packet-level observation with usbdump(8).
############################################################

echo "Step 2: preparing to capture packet-level traffic."

cat <<EOF

About to start usbdump(8) on $USBUS.

The capture is written to:
  $OUTDIR/trace.pcap     - binary pcap, for Wireshark
  $OUTDIR/trace.txt      - human-readable text, for annotation

After you press Return, the pcap capture will start. Exercise the
device: press a few keys on an HID keyboard, send a few bytes
through a serial adapter, cycle an LED through its states, and so
on. When you have captured enough activity, press Control-C to
stop the pcap capture. The script will then capture a short text
trace for annotation.

EOF

printf 'Press Return to start the pcap capture, or Control-C to abort: '
read -r _ignored

echo "  -> $OUTDIR/trace.pcap"
# Run usbdump to pcap. The operator stops this with Control-C.
usbdump -i "$USBUS" -s 2048 -w "$OUTDIR/trace.pcap" || true

cat <<EOF

Pcap capture stopped.

Now a short text capture will run, for the annotation pass. Press
Return when you are ready to exercise the device again briefly,
and Control-C to stop.

EOF

printf 'Press Return to start the text capture, or Control-C to abort: '
read -r _ignored

echo "  -> $OUTDIR/trace.txt"
usbdump -i "$USBUS" -s 2048 > "$OUTDIR/trace.txt" || true

cat <<EOF

Capture complete.

Artefacts in $OUTDIR:
  - usbconfig-list.txt     : "usbconfig list" output
  - show-ifdrv.txt         : interface driver bindings
  - device-descriptors.txt : full descriptor dump
  - trace.pcap             : binary USB packet capture
  - trace.txt              : human-readable USB packet capture

Next steps:
  1. Open trace.pcap in Wireshark and follow the transfers to
     and from your device's address. Use filters such as
     "usb.device_address == N" to focus on one device.
  2. Annotate trace.txt alongside device-descriptors.txt. Match
     each endpoint address you see in the capture to an endpoint
     descriptor you see in the dump. Match each transfer type in
     the capture to the bmAttributes field of the matching
     endpoint descriptor.
  3. Fill in skeleton-template.c using what you found.
EOF
