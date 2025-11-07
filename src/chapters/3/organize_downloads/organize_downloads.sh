#!/bin/sh
# organize_downloads.sh - Tidy ~/Downloads by file extension
#
# Usage:
#   ./organize_downloads.sh
#
# Creates subdirectories like Documents, Images, Audio, Video, Archives, Other
# and moves matched files into them safely.

set -eu

downloads="${HOME}/Downloads"

# Create a temporary file to store the list of files
tmpfile=$(mktemp)

# Remove temporary file when script exits (normal or error)
trap 'rm -f "$tmpfile"' EXIT

# Ensure the Downloads directory exists
if [ ! -d "$downloads" ]; then
  echo "Downloads directory not found at $downloads" >&2
  exit 1
fi

cd "$downloads"

# Create target folders if missing
mkdir -p Documents Images Audio Video Archives Other

# Find all regular files in current directory (non-recursive, excluding hidden files)
# -maxdepth 1: don't search in subdirectories
# -type f: only regular files (not directories or symlinks)
# ! -name ".*": exclude hidden files (those starting with a dot)
count=0
find . -maxdepth 1 -type f ! -name ".*" > "$tmpfile"
while IFS= read -r f; do
  # Strip leading "./" from path
  fname=${f#./}
  
  # Skip if filename is empty (shouldn't happen, but safety check)
  [ -z "$fname" ] && continue

  # Convert filename extension to lowercase for matching
  lower=$(printf '%s' "$fname" | tr '[:upper:]' '[:lower:]')

  case "$lower" in
    *.pdf|*.txt|*.md|*.doc|*.docx)  dest="Documents" ;;
    *.png|*.jpg|*.jpeg|*.gif|*.bmp) dest="Images" ;;
    *.mp3|*.wav|*.flac)             dest="Audio" ;;
    *.mp4|*.mkv|*.mov|*.avi)        dest="Video" ;;
    *.zip|*.tar|*.gz|*.tgz|*.bz2)   dest="Archives" ;;
    *)                              dest="Other" ;;
  esac

  echo "Moving '$fname' -> $dest/"
  mv -n -- "$fname" "$dest/"   # -n prevents overwriting existing files
  count=$((count + 1))         # Increment the counter
done < "$tmpfile"              # Feed the temporary file into the while loop

if [ $count -eq 0 ]; then
  echo "No files to organize."
else
  echo "Done. Organized $count file(s)."
fi
