#!/bin/sh
#
# count_driver_lines.sh - emit line counts for one driver source
# file, with three figures:
#   raw     total wc -l output.
#   nonblank lines that contain at least one non-whitespace character.
#   code    lines that are neither blank nor a pure C comment.
#
# Usage: sh count_driver_lines.sh /path/to/if_foo.c
#
# Companion to Appendix F of "FreeBSD Device Drivers: From First
# Steps to Kernel Mastery".

set -eu

if [ $# -ne 1 ]; then
	echo "usage: $0 <source-file>" >&2
	exit 2
fi

file=$1

if [ ! -f "${file}" ]; then
	echo "error: ${file} does not exist" >&2
	exit 1
fi

raw=$(wc -l < "${file}" | tr -d ' ')
nonblank=$(grep -c -v '^[[:space:]]*$' "${file}" || true)

# Strip C comments with a simple awk pass, then count non-blank.
# This is not a full C parser; it drops /* ... */ blocks (including
# multi-line) and // ... end-of-line comments. Good enough for the
# order-of-magnitude comparison the appendix is after.
code=$(awk '
	BEGIN { in_block = 0 }
	{
		line = $0
		out = ""
		i = 1
		while (i <= length(line)) {
			if (in_block) {
				j = index(substr(line, i), "*/")
				if (j == 0) { i = length(line) + 1 }
				else { in_block = 0; i = i + j + 1 }
				continue
			}
			c2 = substr(line, i, 2)
			if (c2 == "/*") { in_block = 1; i += 2; continue }
			if (c2 == "//") { break }
			out = out substr(line, i, 1)
			i++
		}
		if (out ~ /[^[:space:]]/) count++
	}
	END { print count + 0 }
' "${file}")

printf "%s\traw=%s\tnonblank=%s\tcode=%s\n" \
    "${file}" "${raw}" "${nonblank}" "${code}"
