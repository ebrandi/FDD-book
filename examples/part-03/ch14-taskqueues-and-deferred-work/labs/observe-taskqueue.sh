#!/bin/sh
#
# observe-taskqueue.sh: Lab 1 companion script.
#
# Lists every taskqueue thread on the system, highlights the myfirst
# one, and samples procstat -t for a few seconds to show state
# transitions as the driver's tasks fire.

set -u

echo "=== Taskqueue threads currently on the system ==="
procstat -t 0 2>/dev/null | awk '/taskq/ { print }' || \
    ps ax | awk '/taskq/ { print }'

echo
echo "=== Watching 'myfirst.*taskq' for 10 seconds ==="
echo "(Tip: in another terminal, run"
echo "    sysctl dev.myfirst.0.tick_source_interval_ms=100"
echo " to see the thread oscillate between sleep and run.)"
echo

for i in 1 2 3 4 5 6 7 8 9 10; do
	procstat -t 0 2>/dev/null | grep 'myfirst.*taskq' || \
	    ps ax | grep '\[myfirst.*taskq\]' | grep -v grep
	sleep 1
done
