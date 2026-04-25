/*
 * rdtsc_bench.c - measure the cost of the rdtsc instruction itself.
 *
 * On an amd64 system with invariant TSC (essentially every CPU from
 * the mid-2000s onward) the TSC is the floor under which no other
 * timecounter can go. This program reads rdtsc in a tight loop and
 * reports the average cost per read in CPU cycles and nanoseconds.
 *
 * The nanosecond conversion uses clock_gettime(CLOCK_MONOTONIC) to
 * calibrate TSC frequency against wall-clock time. On hosts without
 * invariant TSC the calibration is noisy and the output should be
 * treated as a rough order-of-magnitude figure only.
 *
 * Companion to Appendix F of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery".
 */

#include <sys/types.h>

#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	DEFAULT_ITERATIONS	100000000ULL
#define	DEFAULT_WARMUP		 10000000ULL

static inline uint64_t
read_tsc(void)
{
	uint32_t lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return (((uint64_t)hi << 32) | lo);
}

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
	uint64_t warmup = DEFAULT_WARMUP;
	struct timespec ts_start, ts_end;
	uint64_t tsc_before, tsc_after, tsc_cycles, wall_ns;
	uint64_t i, dummy = 0;

	if (argc > 1)
		iterations = strtoull(argv[1], NULL, 10);
	if (argc > 2)
		warmup = strtoull(argv[2], NULL, 10);

	printf("# warmup=%" PRIu64 " iterations=%" PRIu64 "\n",
	    warmup, iterations);

	for (i = 0; i < warmup; i++)
		dummy += read_tsc();

	if (clock_gettime(CLOCK_MONOTONIC, &ts_start) != 0)
		err(1, "clock_gettime start");
	tsc_before = read_tsc();

	for (i = 0; i < iterations; i++)
		dummy += read_tsc();

	tsc_after = read_tsc();
	if (clock_gettime(CLOCK_MONOTONIC, &ts_end) != 0)
		err(1, "clock_gettime end");

	tsc_cycles = tsc_after - tsc_before;
	wall_ns = ns_between(&ts_start, &ts_end);

	printf("tsc_cycles_total=%" PRIu64 "\n", tsc_cycles);
	printf("wall_ns_total=%" PRIu64 "\n", wall_ns);
	printf("average_cycles_per_read=%.2f\n",
	    (double)tsc_cycles / (double)iterations);
	printf("average_ns_per_read=%.2f\n",
	    (double)wall_ns / (double)iterations);
	printf("dummy=0x%" PRIx64 "  # prevent loop elimination\n", dummy);

	return (0);
}
