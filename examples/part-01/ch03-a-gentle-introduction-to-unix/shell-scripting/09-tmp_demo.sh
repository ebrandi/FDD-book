#!/bin/sh
# tmp_demo.sh   create and clean a temporary file safely

set -eu

tmpfile="$(mktemp -t myscript)"
# Arrange cleanup on exit for success or error
cleanup() {
  [ -f "$tmpfile" ] && rm -f "$tmpfile"
}
trap cleanup EXIT

echo "Temporary file is $tmpfile"
echo "Hello temp" > "$tmpfile"
echo "Contents: $(cat "$tmpfile")"
