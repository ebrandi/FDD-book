#!/bin/sh
#
# lab24_6_smoke.sh - End-to-end smoke test for the myfirst driver.
#
# Lab 24.6: run a single shell script that exercises every
# integration surface and prints a green/red summary.  Run with the
# Stage 3 driver loaded and ./myfirstctl built in the working dir.
#
# Exit code is 0 if every check passed and non-zero otherwise.

set -u
fail=0

check() {
	if eval "$1" >/dev/null 2>&1; then
		printf "  PASS  %s\n" "$2"
	else
		printf "  FAIL  %s\n" "$2"
		fail=$((fail + 1))
	fi
}

echo "=== myfirst integration smoke test ==="

# 1. Module is loaded.
check "kldstat | grep -q myfirst" "module is loaded"

# 2. /dev node exists with the right mode.
check "test -c /dev/myfirst0" "/dev/myfirst0 exists as a character device"
check "test \"\$(stat -f %Lp /dev/myfirst0)\" = \"660\"" "/dev/myfirst0 is mode 0660"

# 3. Sysctl tree is present.
check "sysctl -N dev.myfirst.0.version" "version OID is present"
check "sysctl -N dev.myfirst.0.debug.mask" "debug.mask OID is present"
check "sysctl -N dev.myfirst.0.open_count" "open_count OID is present"

# 4. Ioctls work (requires myfirstctl built in CWD).
check "./myfirstctl get-version" "MYFIRSTIOC_GETVER returns success"
check "./myfirstctl get-message" "MYFIRSTIOC_GETMSG returns success"
check "sudo ./myfirstctl set-message smoke && [ \"\$(./myfirstctl get-message)\" = smoke ]" "MYFIRSTIOC_SETMSG round-trip works"
check "sudo ./myfirstctl reset && [ -z \"\$(./myfirstctl get-message)\" ]" "MYFIRSTIOC_RESET clears state"

# 5. Read/write basic path.
check "echo hello | sudo tee /dev/myfirst0" "write to /dev/myfirst0 succeeds"
check "[ \"\$(cat /dev/myfirst0)\" = hello ]" "read returns the previously written message"

# 6. Counters update.
sudo ./myfirstctl reset >/dev/null
cat /dev/myfirst0 >/dev/null
check "[ \"\$(sysctl -n dev.myfirst.0.total_reads)\" = 1 ]" "total_reads incremented after one read"

# 7. SDT probes are registered.
check "sudo dtrace -l -P myfirst | grep -q open" "myfirst:::open SDT probe is visible"

echo "=== summary ==="
if [ $fail -eq 0 ]; then
	echo "ALL PASS"
	exit 0
else
	printf "%d FAIL\n" "$fail"
	exit 1
fi
