#!/bin/sh
#
# git_conversion_delta.sh - look up the commit that converted a
# specific Ethernet driver to iflib and report the line-count delta
# at that commit.
#
# Needs a full FreeBSD-src Git clone with history. Run against the
# top of the clone.
#
# Usage: sh git_conversion_delta.sh <repo-path> <driver-basename>
# e.g.:  sh git_conversion_delta.sh /srv/freebsd-src if_em.c
#
# Heuristic: the conversion commit is the one whose subject mentions
# "iflib" AND touches the target file with a large net deletion or
# rewrite.
#
# Companion to Appendix F of "FreeBSD Device Drivers: From First
# Steps to Kernel Mastery".

set -eu

if [ $# -ne 2 ]; then
	echo "usage: $0 <repo-path> <driver-basename>" >&2
	exit 2
fi

repo=$1
base=$2

if [ ! -d "${repo}/.git" ]; then
	echo "error: ${repo} is not a Git working copy" >&2
	exit 1
fi

cd "${repo}"

# Find the best candidate commit.
commits=$(git log --all --oneline --grep='iflib' -- "*/${base}" 2>/dev/null | head -20)
if [ -z "${commits}" ]; then
	echo "no iflib-related commits touch ${base}" >&2
	exit 1
fi

echo "# candidate commits touching ${base} with 'iflib' in the log:"
echo "${commits}" | sed 's/^/  /'
echo ""

# Pick the largest-diff candidate.
best_sha=""
best_touched=0
for sha in $(echo "${commits}" | awk '{print $1}'); do
	touched=$(git show --numstat "${sha}" 2>/dev/null | \
	    awk -v base="${base}" '$3 ~ base { print $1 + $2 }' | head -1)
	if [ -z "${touched}" ]; then
		continue
	fi
	if [ "${touched}" -gt "${best_touched}" ]; then
		best_touched=${touched}
		best_sha=${sha}
	fi
done

if [ -z "${best_sha}" ]; then
	echo "no large-diff candidate found" >&2
	exit 1
fi

echo "# best candidate: ${best_sha}  (touched ${best_touched} lines)"
echo ""

# Before and after line counts for that path.
path=$(git show --name-only "${best_sha}" | grep "/${base}$" | head -1)
if [ -z "${path}" ]; then
	echo "could not resolve path in the commit" >&2
	exit 1
fi

before_sha=$(git rev-parse "${best_sha}^" 2>/dev/null || echo "")
if [ -n "${before_sha}" ]; then
	before_lines=$(git show "${before_sha}:${path}" 2>/dev/null | wc -l | tr -d ' ')
else
	before_lines=0
fi
after_lines=$(git show "${best_sha}:${path}" 2>/dev/null | wc -l | tr -d ' ')

if [ "${before_lines}" -gt 0 ] && [ "${after_lines}" -gt 0 ]; then
	delta=$((before_lines - after_lines))
	if [ "${delta}" -gt 0 ]; then
		pct=$((100 * delta / before_lines))
		change="reduction"
	else
		pct=$((100 * -delta / before_lines))
		change="growth"
	fi
else
	delta=0
	pct=0
	change="n/a"
fi

echo "=== ${base} conversion commit summary ==="
echo "  commit:      ${best_sha}"
echo "  path:        ${path}"
echo "  before:      ${before_lines} lines"
echo "  after:       ${after_lines} lines"
echo "  delta:       ${delta} (${pct}% ${change})"
echo ""
echo "# inspect with:  git show ${best_sha}"
