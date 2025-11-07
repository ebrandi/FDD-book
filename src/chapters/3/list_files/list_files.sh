#!/bin/sh
# list_files.sh - count files in home directory

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
