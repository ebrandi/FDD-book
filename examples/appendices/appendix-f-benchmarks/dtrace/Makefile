# Makefile for Appendix F DTrace / INVARIANTS / WITNESS overhead
# workloads.
#
# Build with BSD make on FreeBSD 14.3-amd64:
#     make
# Run (root only for DTrace):
#     sh run_overhead_suite.sh
#     dtrace -q -s dtrace_overhead.d        # in another terminal
# Clean:
#     make clean

CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra -pedantic -std=c11

PROGS=		workload_syscalls workload_locks

all: ${PROGS}

workload_syscalls: workload_syscalls.c
	${CC} ${CFLAGS} -o $@ workload_syscalls.c

workload_locks: workload_locks.c
	${CC} ${CFLAGS} -o $@ workload_locks.c -lpthread

clean:
	rm -f ${PROGS}

.PHONY: all clean
