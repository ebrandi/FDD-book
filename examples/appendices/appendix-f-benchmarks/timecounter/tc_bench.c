/*
 * tc_bench.c - measure clock_gettime(CLOCK_MONOTONIC) cost.
 *
 * The kernel resolves CLOCK_MONOTONIC through sbinuptime(), which
 * reads whichever timecounter kern.timecounter.hardware currently
 * selects. This program issues a large number of clock_gettime calls
 * in a tight loop and reports the average nanoseconds per call.
 *
 * Combined with a run_tc_bench.sh that rotates the sysctl, this gives
 * a measured read cost for each available timecounter.
 *
 * Companion to Appendix F of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery".
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	DEFAULT_ITERATIONS	10000000ULL
#define	DEFAULT_WARMUP		 1000000ULL

static uint64_t
ns_between(const struct timespec *start, const struct timespec *end)
{
	return ((uint64_t)(end->tv_sec - start->tv_sec) * 1000000000ULL +
	    (uint64_t)(end->tv_nsec - start->tv_nsec));
}

static void
report_timecounter(void)
{
	char buf[128];
	size_t len = sizeof(buf);

	if (sysctlbyname("kern.timecounter.hardware", buf, &len,
	    NULL, 0) == 0) {
		buf[sizeof(buf) - 1] = '\0';
		printf("# kern.timecounter.hardware = %s\n", buf);
	}
}

int
main(int argc, char **argv)
{
	uint64_t iterations = DEFAULT_ITERATIONS;
	uint64_t warmup = DEFAULT_WARMUP;
	struct timespec ts_start, ts_end, ts_loop;
	uint64_t i, total_ns;

	if (argc > 1)
		iterations = strtoull(argv[1], NULL, 10);
	if (argc > 2)
		warmup = strtoull(argv[2], NULL, 10);

	report_timecounter();
	printf("# warmup=%" PRIu64 " iterations=%" PRIu64 "\n",
	    warmup, iterations);

	for (i = 0; i < warmup; i++) {
		if (clock_gettime(CLOCK_MONOTONIC, &ts_loop) != 0)
			err(1, "clock_gettime warmup");
	}

	if (clock_gettime(CLOCK_MONOTONIC, &ts_start) != 0)
		err(1, "clock_gettime start");

	for (i = 0; i < iterations; i++) {
		if (clock_gettime(CLOCK_MONOTONIC, &ts_loop) != 0)
			err(1, "clock_gettime loop");
	}

	if (clock_gettime(CLOCK_MONOTONIC, &ts_end) != 0)
		err(1, "clock_gettime end");

	total_ns = ns_between(&ts_start, &ts_end);
	printf("average_ns_per_call=%.2f\n",
	    (double)total_ns / (double)iterations);
	printf("total_ns=%" PRIu64 "\n", total_ns);
	printf("iterations=%" PRIu64 "\n", iterations);

	return (0);
}
