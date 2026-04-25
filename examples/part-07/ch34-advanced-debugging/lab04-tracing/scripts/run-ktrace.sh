#!/bin/sh
#
# run-ktrace.sh - run bugdemo_test under ktrace and print the
#                 resulting trace with kdump.
#
# Usage: ./run-ktrace.sh <subcommand>
#
# The subcommand is passed directly to bugdemo_test.

if [ $# -lt 1 ]; then
	echo "usage: $0 <subcommand>"
	exit 2
fi

rm -f ktrace.out
ktrace -t ci ../bugdemo_test "$@"
kdump | grep -E 'ioctl|bugdemo' || kdump
rm -f ktrace.out
