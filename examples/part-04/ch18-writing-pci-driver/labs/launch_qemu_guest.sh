#!/bin/sh
#
# Example QEMU command to launch a FreeBSD 14.3 guest with a
# virtio-rng-pci device. Runs on Linux or macOS hosts. Adapt paths
# to your environment.

set -eu

IMAGE="freebsd-14.3-lab.img"
TAP="tap0"
BIOS_DIR="/usr/share/qemu"

qemu-system-x86_64 \
    -cpu host -m 2048 -smp 2 \
    -drive file="$IMAGE",if=virtio \
    -netdev tap,id=net0,ifname="$TAP" \
    -device virtio-net,netdev=net0 \
    -device virtio-rng-pci \
    -bios "${BIOS_DIR}/OVMF_CODE.fd" \
    -serial stdio
