#!/bin/sh
# list_files_exec.sh - handle all filenames using find -exec

set -eu
cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -exec sh -c '
  for f; do
    fname=${f#./}
    printf "File found: '\''%s'\''\n" "$fname"
  done
' sh {} +
