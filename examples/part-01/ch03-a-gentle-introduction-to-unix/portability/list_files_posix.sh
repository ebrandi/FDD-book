#!/bin/sh
# list_files_posix.sh - POSIX-compliant file listing
#
# Uses a temporary file instead of a pipe to preserve the counter
# variable across iterations. Handles filenames with spaces and
# most special characters, but cannot distinguish a filename that
# contains an embedded newline. For most real-world filenames, this
# is enough.

set -eu
cd "${HOME}"

# Use a temporary file instead of a pipe
tmpfile=$(mktemp)
trap 'rm -f "$tmpfile"' EXIT

# Store find results in temporary file
find . -maxdepth 1 -type f ! -name ".*" > "$tmpfile"

count=0
while IFS= read -r f; do
  fname=${f#./}
  [ -z "$fname" ] && continue

  echo "File found: '$fname'"
  count=$((count + 1))
done < "$tmpfile"

echo "Total files found: $count"
