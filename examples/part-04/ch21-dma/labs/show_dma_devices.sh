#!/bin/sh
# show_dma_devices.sh - Identify DMA-capable devices on the lab host.
#
# Part of Chapter 21 Lab 1. Shows which PCI devices have bus-mastering
# enabled and are therefore actively using DMA.

set -e

echo "=== PCI devices with BUSMASTEREN set ==="
pciconf -lvc | awk '
    /^[^ ]/ { dev = $1 }
    /BUSMASTEREN/ { print dev }
' | sort -u

echo ""
echo "=== Bounce-buffer subsystem status ==="
sysctl -a | grep -E '^hw\.busdma\.' || echo "(no hw.busdma sysctls found)"

echo ""
echo "=== myfirst driver DMA state (if loaded) ==="
if sysctl -a | grep -q '^dev\.myfirst\.'; then
    sysctl dev.myfirst. | grep -E 'dma_' || echo "(no dma_ sysctls; driver may be pre-Chapter-21)"
else
    echo "(myfirst not loaded)"
fi

echo ""
echo "=== Summary ==="
bm_count=$(pciconf -lvc | grep -c BUSMASTEREN || true)
echo "Devices with bus-mastering enabled: ${bm_count}"
