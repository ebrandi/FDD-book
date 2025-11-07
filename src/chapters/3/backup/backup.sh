#!/bin/sh
# backup.sh   create a timestamped tar archive of a directory
# Usage: ./backup.sh /path/to/source
# Notes:
#  - Uses /bin/sh so it runs on a clean FreeBSD 14.x install.
#  - Creates ~/backups if it does not exist.
#  - Names the archive sourcebasename-YYYYMMDD-HHMMSS.tar.gz

set -eu
# set -e: exit immediately if any command fails
# set -u: treat use of unset variables as an error

# Validate input
if [ $# -ne 1 ]; then
  echo "Usage: $0 /path/to/source" >&2
  exit 2
fi

src="$1"

# Verify that source is a directory
if [ ! -d "$src" ]; then
  echo "Error: $src is not a directory" >&2
  exit 3
fi

# Prepare destination directory
dest="${HOME}/backups"
mkdir -p "$dest"

# Build a safe archive name using only the last path component
base="$(basename "$src")"
stamp="$(date +%Y%m%d-%H%M%S)"
archive="${dest}/${base}-${stamp}.tar.gz"

# Create the archive
# tar(1) is in the base system. The flags mean:
#  - c: create  - z: gzip  - f: file name  - C: change to directory
tar -czf "$archive" -C "$(dirname "$src")" "$base"

echo "Backup created: $archive"
