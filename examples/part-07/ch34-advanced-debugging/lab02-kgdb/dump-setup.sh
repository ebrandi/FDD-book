#!/bin/sh
#
# dump-setup.sh - verify that a dump device is configured.
#
# This script does not change the system. It reports the current dump
# configuration and, if no dump device is set, prints the commands the
# reader should run to configure one.

echo "current dump configuration:"
dumpon -l

if ! dumpon -l | grep -q '^/dev/'; then
	cat <<'EOF'

No dump device is currently configured. To set one, identify your
swap partition and run:

    # dumpon /dev/DEVICE

and persist across reboots by adding to /etc/rc.conf:

    dumpdev="/dev/DEVICE"
    savecore_enable="YES"

Replace DEVICE with your actual swap partition name. You can find it
with:

    # swapinfo

EOF
	exit 1
fi

echo "dump device is configured. ready to capture panics."
exit 0
