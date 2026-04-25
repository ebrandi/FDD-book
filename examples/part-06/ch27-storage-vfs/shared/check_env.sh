#!/bin/sh
#
# check_env.sh - Verify the lab environment has what Chapter 27 needs.

set -eu

ok=1

echo "Checking userland utilities..."
for cmd in newfs_ufs mount umount fsck kldstat kldload diskinfo gstat dtrace; do
	if command -v "${cmd}" >/dev/null 2>&1; then
		echo "  OK: ${cmd}"
	else
		echo "  MISSING: ${cmd}"
		ok=0
	fi
done

echo
echo "Checking kernel modules (loadable on demand)..."
for mod in g_disk geom ufs; do
	if kldstat -qm "${mod}" >/dev/null 2>&1; then
		echo "  OK: ${mod} (loaded)"
	else
		if kldload -n "${mod}" 2>/dev/null; then
			echo "  OK: ${mod} (available)"
		else
			echo "  NOT AVAILABLE: ${mod}"
		fi
	fi
done

echo
echo "Checking kernel sources..."
if [ -d /usr/src/sys/geom ]; then
	echo "  OK: /usr/src/sys/geom exists"
else
	echo "  MISSING: /usr/src/sys/geom. Install via git or fetch."
	ok=0
fi
if [ -d /usr/src/sys/dev/md ]; then
	echo "  OK: /usr/src/sys/dev/md exists"
else
	echo "  MISSING: /usr/src/sys/dev/md. Install via git or fetch."
	ok=0
fi
if [ -d /usr/src/sys/ufs ]; then
	echo "  OK: /usr/src/sys/ufs exists"
else
	echo "  MISSING: /usr/src/sys/ufs. Install via git or fetch."
	ok=0
fi

echo
echo "Checking /mnt directory..."
if [ -d /mnt ]; then
	echo "  OK: /mnt exists"
else
	echo "  MISSING: /mnt. Create it with: mkdir /mnt"
	ok=0
fi

echo
if [ "${ok}" = "1" ]; then
	echo "Environment looks good for Chapter 27 labs."
	exit 0
else
	echo "Environment is missing one or more required pieces."
	exit 1
fi
