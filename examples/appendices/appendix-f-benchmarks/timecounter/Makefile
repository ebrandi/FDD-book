# Makefile for Appendix F timecounter benchmarks.
#
# Build with BSD make on FreeBSD 14.3-amd64:
#     make
# Run (requires root for the rotation script):
#     sh run_tc_bench.sh
#     ./rdtsc_bench
# Clean up:
#     make clean

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra -pedantic -std=c11

PROGS=		tc_bench rdtsc_bench

all: ${PROGS}

tc_bench: tc_bench.c
	${CC} ${CFLAGS} -o $@ tc_bench.c

rdtsc_bench: rdtsc_bench.c
	${CC} ${CFLAGS} -o $@ rdtsc_bench.c

clean:
	rm -f ${PROGS}

.PHONY: all clean
