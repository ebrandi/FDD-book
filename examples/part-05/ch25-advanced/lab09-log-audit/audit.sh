#!/bin/sh
#
# audit.sh - mechanical audit of every log site in the driver.
#
# Categorises each printf/device_printf/log/DPRINTF/DLOG_RL call as
# one of:
#   PASS - device_printf/DPRINTF/DLOG_RL (device-named or debug-gated)
#   WARN - bare printf (legitimate only at MOD_LOAD, usually)
#   FAIL - device_printf on a hot path without rate limiting
#
# The "hot path" detection is based on file names: myfirst_cdev.c and
# myfirst_ioctl.c are hot; everything else is cold.

set -u

SRCDIR="$(cd "$(dirname "$0")/.." && pwd)"
FILES="myfirst.c myfirst_cdev.c myfirst_ioctl.c myfirst_sysctl.c \
       myfirst_debug.c myfirst_log.c"

hot_path() {
	case "$1" in
	myfirst_cdev.c|myfirst_ioctl.c) return 0 ;;
	*) return 1 ;;
	esac
}

total_pass=0
total_warn=0
total_fail=0

for f in $FILES; do
	file="$SRCDIR/$f"
	[ -f "$file" ] || continue

	pass=0
	warn=0
	fail=0

	while IFS= read -r line; do
		case "$line" in
		*"device_printf"*|*"DPRINTF"*|*"DLOG_RL"*)
			if hot_path "$f" && \
			   printf '%s' "$line" | grep -q "device_printf" && \
			   ! printf '%s' "$line" | grep -q "DPRINTF\|DLOG_RL\|MYF_CDEV_HOT_LOG"
			then
				fail=$((fail + 1))
			else
				pass=$((pass + 1))
			fi
			;;
		*"printf("*|*" log("*)
			warn=$((warn + 1))
			;;
		esac
	done <"$file"

	total=$((pass + warn + fail))
	total_pass=$((total_pass + pass))
	total_warn=$((total_warn + warn))
	total_fail=$((total_fail + fail))

	if [ "$total" -eq 0 ]; then
		printf "%-20s %d log messages\n" "$f:" "$total"
	else
		printf "%-20s %d log messages - %d PASS, %d WARN, %d FAIL\n" \
		    "$f:" "$total" "$pass" "$warn" "$fail"
	fi
done

total=$((total_pass + total_warn + total_fail))
echo
echo "Total: $total log messages -" \
    "$total_warn WARN, $total_fail FAIL"

if [ "$total_fail" -gt 0 ]; then
	exit 1
fi
