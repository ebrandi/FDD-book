#!/bin/sh
#
# reload.sh - Rebuild and reload the chapter driver.
#
# Run from the chapter root (one directory above shared/).

set -eu

cd "$(dirname "$0")/.."

if kldstat -qm myblk >/dev/null 2>&1; then
	echo "Unloading existing myblk..."
	umount /mnt/myblk 2>/dev/null || true
	kldunload myblk
fi

echo "Building..."
make clean
make

echo "Loading..."
kldload ./myblk.ko

echo "Loaded. /dev/myblk0 should now exist."
ls -l /dev/myblk0 || true
