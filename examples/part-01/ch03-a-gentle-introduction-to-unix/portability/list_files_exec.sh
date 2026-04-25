#!/bin/sh
# list_files_exec.sh - handle all filenames using find -exec
#
# Pure POSIX, no bash dependency, no temporary file. Works even with
# pathological filenames (embedded newlines) because find -exec
# passes paths as arguments rather than through line-based input.

set -eu
cd "${HOME}"

find . -maxdepth 1 -type f ! -name ".*" -exec sh -c '
  for f; do
    fname=${f#./}
    printf "File found: '\''%s'\''\n" "$fname"
  done
' sh {} +
