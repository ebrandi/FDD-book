#!/bin/sh
#
# pci_dump_config.sh -- dump the first 64 bytes of PCI configuration
# space for the named device in a readable 4-byte-per-row format.
#
# Usage: ./pci_dump_config.sh <selector>
#
# Example: ./pci_dump_config.sh myfirst0
#          ./pci_dump_config.sh pci0:0:4:0

set -eu

SEL="${1:-myfirst0}"

printf 'PCI configuration space for %s:\n' "$SEL"
printf 'offset  value\n'

off=0
while [ "$off" -lt 64 ]; do
    printf '0x%02x    %s\n' "$off" \
        "$(pciconf -r "$SEL" "${off}:4" | tr -d ' \n')"
    off=$((off + 4))
done
