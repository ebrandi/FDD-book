#!/bin/sh
# debug_demo.sh   show simple tracing

# set -x comment to disable verbose trace:
set -x

echo "Step 1"
ls /etc >/dev/null

echo "Step 2"
date
