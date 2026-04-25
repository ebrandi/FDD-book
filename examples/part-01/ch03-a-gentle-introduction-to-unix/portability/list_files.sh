#!/bin/sh
# list_files.sh - count files in home directory
#
# DELIBERATELY NAIVE: this version is the "naive approach that breaks"
# in the chapter's "Shell Portability" section. It counts a file with
# a newline in its name as two separate files. Compare with the
# _bash, _posix, and _exec variants in this directory.

set -eu
cd "${HOME}"

count=0
while IFS= read -r f; do
  fname=${f#./}
  echo "File found: '$fname'"
  count=$((count + 1))
done << EOF
$(find . -maxdepth 1 -type f ! -name ".*" -print)
EOF

echo "Total files found: $count"
