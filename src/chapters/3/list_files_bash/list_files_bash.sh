#!/usr/local/bin/bash
# list_files_bash.sh - correctly handle unusual filenames with bash

set -eu
cd "${HOME}"

count=0
while IFS= read -r -d '' f; do
  fname=${f#./}
  echo "File found: '$fname'"
  count=$((count + 1))
done < <(find . -maxdepth 1 -type f ! -name ".*" -print0)

echo "Total files found: $count"
