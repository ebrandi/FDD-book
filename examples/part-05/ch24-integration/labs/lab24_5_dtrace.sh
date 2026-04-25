#!/bin/sh
#
# lab24_5_dtrace.sh - Trace the integration surfaces with DTrace.
#
# Lab 24.5: use the SDT probes from Chapter 23 (open, close, io) and
# the Chapter 24 ioctl probe to watch driver traffic in real time.
#
# Run with the Stage 3 driver loaded.  The script starts a DTrace
# session in the foreground; press Ctrl-C to stop.

set -e

DEV=${DEV:-/dev/myfirst0}

echo "=== Lab 24.5: DTrace integration trace ==="

if [ ! -c "${DEV}" ]; then
	echo "ERROR: ${DEV} does not exist.  Load the Stage 3 driver first."
	exit 1
fi

echo
echo "--- registered myfirst SDT probes ---"
sudo dtrace -l -P myfirst

echo
echo "--- starting trace (Ctrl-C to stop) ---"
echo "In another terminal, exercise the driver, e.g.:"
echo "    cat /dev/myfirst0"
echo "    ./myfirstctl get-version"
echo "    sudo ./myfirstctl set-message 'hello DTrace'"
echo "    sudo ./myfirstctl reset"
echo

sudo dtrace -n '
    myfirst:::open  { printf("open  pid=%d", pid); }
    myfirst:::close { printf("close pid=%d", pid); }
    myfirst:::io    { printf("io    pid=%d write=%d resid=%d", pid, arg1, arg2); }
    myfirst:::ioctl { printf("ioctl pid=%d cmd=0x%x", pid, arg1); }
'
