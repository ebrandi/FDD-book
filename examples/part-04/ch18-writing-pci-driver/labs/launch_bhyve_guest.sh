#!/bin/sh
#
# Example bhyve command to launch a FreeBSD 14.3 guest with a
# virtio-rnd device exposed on PCI slot 4. Adapt paths and device
# names to your environment.
#
# Prerequisites (on the host):
#   - A FreeBSD guest image (zfs zvol or a raw disk file).
#   - bhyve's kernel modules loaded: vmm, nmdm, if_bridge, if_tap.
#   - A bridged tap network interface if the guest needs networking.
#   - BHYVE_UEFI.fd available (from the uefi-edk2-bhyve package).

set -eu

GUEST="fbsd-14.3-lab"
DISK="/dev/zvol/zroot/vm/${GUEST}/disk0"
TAP="tap0"
UEFI="/usr/local/share/uefi-firmware/BHYVE_UEFI.fd"
NMDM="/dev/nmdm${GUEST}A"

kldload -n vmm nmdm if_bridge if_tap

bhyve -c 2 -m 2048 -H -w \
    -s 0:0,hostbridge \
    -s 1:0,lpc \
    -s 2:0,virtio-net,"$TAP" \
    -s 3:0,virtio-blk,"$DISK" \
    -s 4:0,virtio-rnd \
    -l com1,"$NMDM" \
    -l bootrom,"$UEFI" \
    "$GUEST"
