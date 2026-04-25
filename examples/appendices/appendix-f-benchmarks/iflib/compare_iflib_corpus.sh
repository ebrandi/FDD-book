#!/bin/sh
#
# compare_iflib_corpus.sh - walk two curated lists of Ethernet
# driver source files (one iflib-based, one plain-ifnet) and print
# a comparison table plus an aggregate ratio.
#
# Usage: sh compare_iflib_corpus.sh [SRC_ROOT]
# SRC_ROOT defaults to /usr/src.
#
# Companion to Appendix F of "FreeBSD Device Drivers: From First
# Steps to Kernel Mastery".

set -eu

SRC_ROOT=${1:-/usr/src}

# Drivers that use iflib. Verified by 'grep -l IFDI_ if_*.c'.
CORPUS_IFLIB="
sys/dev/e1000/if_em.c
sys/dev/ixgbe/if_ix.c
sys/dev/igc/if_igc.c
sys/dev/vmware/vmxnet3/if_vmx.c
"

# Drivers that do not use iflib. Chosen to span a similar range of
# hardware classes.
CORPUS_PLAIN="
sys/dev/re/if_re.c
sys/dev/bge/if_bge.c
sys/dev/fxp/if_fxp.c
"

HERE=$(cd "$(dirname "$0")" && pwd)

walk_corpus() {
	label=$1
	files=$2

	total_raw=0
	total_code=0
	count=0

	printf "=== %s ===\n" "${label}" >&2
	for rel in ${files}; do
		path="${SRC_ROOT}/${rel}"
		if [ ! -f "${path}" ]; then
			printf "  %s (missing)\n" "${rel}" >&2
			continue
		fi
		out=$(sh "${HERE}/count_driver_lines.sh" "${path}")
		printf "  %s\n" "${out}" >&2
		raw=$(echo "${out}" | awk '{print $2}' | cut -d= -f2)
		code=$(echo "${out}" | awk '{print $4}' | cut -d= -f2)
		total_raw=$((total_raw + raw))
		total_code=$((total_code + code))
		count=$((count + 1))
	done
	if [ ${count} -gt 0 ]; then
		avg_raw=$((total_raw / count))
		avg_code=$((total_code / count))
	else
		avg_raw=0
		avg_code=0
	fi
	{
		printf "  ---\n"
		printf "  corpus=%s drivers=%d total_raw=%d total_code=%d avg_raw=%d avg_code=%d\n" \
		    "${label}" "${count}" "${total_raw}" "${total_code}" \
		    "${avg_raw}" "${avg_code}"
		echo ""
	} >&2

	echo "${avg_code}"
}

echo "# SRC_ROOT=${SRC_ROOT}"
echo "# uname=$(uname -rmp 2>/dev/null || echo 'unknown')"
echo ""

avg_iflib=$(walk_corpus "iflib" "${CORPUS_IFLIB}" | tail -1)
avg_plain=$(walk_corpus "plain-ifnet" "${CORPUS_PLAIN}" | tail -1)

# Summary.
if [ "${avg_plain}" -gt 0 ] && [ "${avg_iflib}" -gt 0 ]; then
	# ratio_percent = 100 * (plain - iflib) / plain
	delta=$((avg_plain - avg_iflib))
	pct=$((100 * delta / avg_plain))
	echo "=== summary ==="
	echo "  iflib avg code lines:       ${avg_iflib}"
	echo "  plain-ifnet avg code lines: ${avg_plain}"
	echo "  delta:                      ${delta}"
	echo "  reduction:                  ${pct}%"
	echo ""
	echo "  Caveat: raw file size is a noisy proxy for driver"
	echo "  complexity. A fair per-driver comparison requires the"
	echo "  before/after conversion diff, which needs Git history."
	echo "  Run git_conversion_delta.sh on a full freebsd-src.git"
	echo "  clone for that measurement."
fi
