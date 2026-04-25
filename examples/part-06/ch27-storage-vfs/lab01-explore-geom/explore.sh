#!/bin/sh
#
# explore.sh - Dump the current GEOM topology to a text file.
#
# Produces a human-readable inventory suitable for later comparison
# before and after loading new GEOM classes.

set -eu

echo "==================== geom -t ===================="
geom -t || true
echo

echo "==================== geom disk list ===================="
geom disk list || true
echo

echo "==================== geom provider list ===================="
geom provider list || true
echo

echo "==================== gstat snapshot (1s) ===================="
gstat -b -d -I 1s || true
echo

echo "==================== kldstat ===================="
kldstat || true
