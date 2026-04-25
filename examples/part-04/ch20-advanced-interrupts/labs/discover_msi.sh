#!/bin/sh
#
# discover_msi.sh -- list MSI and MSI-X capabilities of all PCI
# devices on the current host.

set -eu

pciconf -lvc | awk '
/^[a-z]/ { device = $0; msi = ""; msix = "" }
/cap 05\[/ { msi = $0 }
/cap 11\[/ { msix = $0 }
/^$/ {
    if (msi != "" || msix != "") {
        print device
        if (msi != "")  print "  " msi
        if (msix != "") print "  " msix
        print ""
    }
}'
