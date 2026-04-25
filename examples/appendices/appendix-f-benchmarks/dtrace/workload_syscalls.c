/*
 * workload_syscalls.c - a tight syscall-call loop.
 *
 * Hammers the kernel's syscall entry and exit path with a mix of
 * trivial system calls. Intended to be run under several kernel
 * conditions (base, INVARIANTS, WITNESS, active DTrace) so the
 * reader can compare wall-clock times and see the per-condition
 * overhead.
 *
 * Companion to Appendix F of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery".
 */

#include <sys/types.h>
#include <sys/time.h>

#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	DEFAULT_ITERATIONS	1000000ULL

static uint64_t
ns_between(const struct timespec *start, const struct timespec *end)
{
	return ((uint64_t)(end->tv_sec - start->tv_sec) * 1000000000ULL +
	    (uint64_t)(end->tv_nsec - start->tv_nsec));
}

int
main(int argc, char **argv)
{
	uint64_t iterations = DEFAULT_ITERATIONS;
	struct timespec ts_start, ts_end, ts_loop;
	struct timeval tv_loop;
	uint64_t i, total_ns, sum = 0;
	pid_t pid;
	uid_t uid;

	if (argc > 1)
		iterations = strtoull(argv[1], NULL, 10);

	printf("# workload=syscalls iterations=%" PRIu64 "\n", iterations);

	if (clock_gettime(CLOCK_MONOTONIC, &ts_start) != 0)
		err(1, "clock_gettime start");

	for (i = 0; i < iterations; i++) {
		pid = getpid();
		uid = getuid();
		if (gettimeofday(&tv_loop, NULL) != 0)
			err(1, "gettimeofday");
		if (clock_gettime(CLOCK_MONOTONIC, &ts_loop) != 0)
			err(1, "clock_gettime inner");
		sum += (uint64_t)pid + (uint64_t)uid +
		    (uint64_t)tv_loop.tv_usec + (uint64_t)ts_loop.tv_nsec;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &ts_end) != 0)
		err(1, "clock_gettime end");

	total_ns = ns_between(&ts_start, &ts_end);
	printf("wall_ns=%" PRIu64 "\n", total_ns);
	printf("per_iter_ns=%.2f\n", (double)total_ns / (double)iterations);
	printf("# sum=0x%" PRIx64 "  # prevent loop elimination\n", sum);

	return (0);
}
