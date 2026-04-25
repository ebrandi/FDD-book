#!/bin/sh
#
# capture.sh - Capture USB traffic with usbdump(8) for offline
# analysis in Wireshark or another pcap-aware tool.
#
# Companion to Chapter 36, Lab 2.
#
# This script is a thin wrapper around usbdump(8) that handles
# the common-case bookkeeping: picking a sensible output filename,
# requesting the right bus, and giving the operator clear instructions
# for how to start, stop, and read the capture.
#
# Usage:
#   sudo sh capture.sh [-i usbus_interface] [-o output_file]
#                      [-c count] [-s snaplen]
#
# Defaults:
#   interface: usbus0
#   output_file: capture-YYYYMMDD-HHMMSS.pcap
#   count: 0 (no limit, capture until interrupted)
#   snaplen: 65535 (full packets)

set -eu

INTERFACE="usbus0"
OUTPUT=""
COUNT="0"
SNAPLEN="65535"

usage() {
	cat <<EOF
Usage: $(basename "$0") [-i interface] [-o output_file]
                       [-c count] [-s snaplen]

Capture USB traffic with usbdump(8).

Options:
  -i interface   USB bus interface to capture from (default: usbus0)
  -o output_file Output pcap file (default: capture-DATE.pcap)
  -c count       Number of packets to capture (default: 0 = unlimited)
  -s snaplen     Snapshot length per packet (default: 65535)
  -h             Show this help message.
EOF
}

while getopts "i:o:c:s:h" opt; do
	case "$opt" in
	i) INTERFACE="$OPTARG" ;;
	o) OUTPUT="$OPTARG" ;;
	c) COUNT="$OPTARG" ;;
	s) SNAPLEN="$OPTARG" ;;
	h) usage; exit 0 ;;
	*) usage; exit 1 ;;
	esac
done

if [ "$(id -u)" -ne 0 ]; then
	echo "This script must be run as root." >&2
	exit 1
fi

# Verify the interface exists.
if ! ifconfig "$INTERFACE" >/dev/null 2>&1; then
	echo "Interface $INTERFACE does not exist." >&2
	echo "Available USB buses:" >&2
	ifconfig -l | tr ' ' '\n' | grep '^usbus' >&2 || true
	exit 1
fi

# Default output filename if none supplied.
if [ -z "$OUTPUT" ]; then
	OUTPUT="capture-$(date '+%Y%m%d-%H%M%S').pcap"
fi

cat <<EOF
USB capture starting.

  Interface: $INTERFACE
  Output:    $OUTPUT
  Count:     $COUNT (0 = unlimited)
  Snaplen:   $SNAPLEN

Press Control-C to stop.

You can now plug in, unplug, or interact with the USB device whose
traffic you want to capture. Every transfer on $INTERFACE will be
recorded.

EOF

# Run usbdump. The -w flag writes to a pcap file. The -s flag sets
# the snapshot length. The -c flag sets the packet count (0 = no
# limit). Without -f the capture covers all device addresses on the
# bus, which is what you want for enumeration captures.
usbdump -i "$INTERFACE" -w "$OUTPUT" -s "$SNAPLEN" -c "$COUNT"

cat <<EOF

Capture finished. Output written to $OUTPUT.

Suggested next steps:
  1. Open $OUTPUT in Wireshark for graphical analysis.
  2. Use the USB filter syntax to focus on a specific device:
       usb.device_address == <N>
  3. Use the endpoint filter to focus on a specific transfer type:
       usb.endpoint_address == 0x81
  4. Compare the device descriptor in the capture against
     dump_device_desc output from Lab 1.

If your version of Wireshark does not understand FreeBSD usbdump
captures directly, install the wireshark port from the FreeBSD
Ports Collection or pkg, both of which include the necessary
dissector.
EOF
