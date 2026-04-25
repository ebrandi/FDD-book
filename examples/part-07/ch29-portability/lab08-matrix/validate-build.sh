#!/bin/sh
# validate-build.sh - build portdrv in every supported configuration.
#
# Usage: run from the directory that contains the driver's Makefile.
# Exit status 0 means every listed configuration built; non-zero means
# the first failing configuration. Build logs land in build.log.

set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
done
echo "All configurations built successfully."
