#!/bin/sh
#
# run.sh - 100-cycle load/unload regression for Lab 6.
#
# Loads, sleeps, and unloads the myfirst module 100 times in a row.
# Records pass/fail per cycle and reports any leaks visible through
# vmstat -m.  Log lives at /tmp/myfirst-cycles.log.

set -u

CYCLES="${1:-100}"
KMOD="${2:-../myfirst.ko}"
LOGFILE="${LOGFILE:-/tmp/myfirst-cycles.log}"

: >"$LOGFILE"

# Sanity check: the module must start unloaded.
if kldstat -n myfirst >/dev/null 2>&1; then
	echo "myfirst is already loaded.  Unload it first." >&2
	exit 2
fi

echo "== Recording baseline vmstat -m"
vmstat -m >/tmp/myfirst-before.txt

failures=0
for i in $(seq 1 "$CYCLES"); do
	if ! kldload "$KMOD" >>"$LOGFILE" 2>&1; then
		echo "cycle $i/$CYCLES: load FAILED"
		failures=$((failures + 1))
		continue
	fi
	sleep 0.1
	if ! kldunload myfirst >>"$LOGFILE" 2>&1; then
		echo "cycle $i/$CYCLES: unload FAILED"
		failures=$((failures + 1))
		continue
	fi
	echo "cycle $i/$CYCLES: ok"
done

echo "== Recording final vmstat -m"
vmstat -m >/tmp/myfirst-after.txt

leaks=0
if ! diff -q /tmp/myfirst-before.txt /tmp/myfirst-after.txt \
    >/dev/null 2>&1; then
	# Some churn is normal (kernel bookkeeping).  The lab reports
	# the diff for human inspection rather than flagging every
	# delta as a leak.
	leaks=$(diff /tmp/myfirst-before.txt /tmp/myfirst-after.txt | \
	    grep -c '^[<>]' || true)
fi

echo
echo "done: $CYCLES cycles, $failures failures, " \
    "$leaks lines changed in vmstat -m."
if [ "$failures" -ne 0 ]; then
	echo "See $LOGFILE for the first failing cycle."
	exit 1
fi
if [ "$leaks" -gt 4 ]; then
	echo "Significant vmstat -m drift.  Compare:"
	echo "    /tmp/myfirst-before.txt"
	echo "    /tmp/myfirst-after.txt"
	exit 1
fi
exit 0
