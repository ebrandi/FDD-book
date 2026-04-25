#!/bin/sh
# ci-check.sh - validate portdrv in every supported configuration.
#
# Combines the build matrix from Lab 8 with a load test. Runs from the
# driver's build directory. Requires sudo to load and unload kernel
# modules; if run without sudo, the load step is skipped.

set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

HAVE_SUDO=0
if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
	HAVE_SUDO=1
fi

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> Build: %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi

	if [ $HAVE_SUDO -eq 1 ]; then
		printf "==> Load : %s ... " "$cfg"
		if sudo kldload ./portdrv.ko > load.log 2>&1; then
			sudo kldunload portdrv > /dev/null 2>&1 || true
			echo "OK"
		else
			echo "FAIL"
			cat load.log
			exit 1
		fi
	else
		printf "==> Load : %s ... SKIPPED (no sudo)\n" "$cfg"
	fi
done
echo "All configurations passed."
