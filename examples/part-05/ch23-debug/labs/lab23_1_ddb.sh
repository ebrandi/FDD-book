#!/bin/sh
#
# lab23_1_ddb.sh - Lab 23.1: A First DDB Session
#
# This script does not automate DDB itself (DDB is interactive by
# nature).  It prepares the environment and prints the commands the
# reader should run step by step.

set -e

echo "=== Lab 23.1: A First DDB Session ==="
echo
echo "1. Confirm the kernel is a debug kernel with KDB/DDB:"
echo "   sysctl kern.version"
echo "   sysctl debug.kdb"
echo
echo "2. Load the myfirst driver:"
echo "   sudo kldload ./myfirst.ko   # or the full path to your module"
echo "   ls /dev/myfirst0"
echo
echo "3. Enter DDB from this shell (safe on a test system):"
echo "   sudo sysctl debug.kdb.enter=1"
echo
echo "4. At the 'db>' prompt, run these commands:"
echo "   db> show pcpu"
echo "   db> ps"
echo "   db> bt"
echo "   db> show locks"
echo
echo "5. Return to normal operation:"
echo "   db> continue"
echo
echo "6. Confirm the system is running again:"
echo "   dmesg | tail"
echo
echo "7. Unload the driver:"
echo "   sudo kldunload myfirst"
echo
echo "=== End of Lab 23.1 ==="
