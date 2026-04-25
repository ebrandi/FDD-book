/*
 * intr_latency_test.c -- Chapter 19 latency harness.
 *
 * Fires N simulated interrupts via dev.myfirst.0.intr_simulate and
 * records the time between the sysctl write and the subsequent
 * increment of dev.myfirst.0.intr_task_invocations.
 *
 * This is a rough measurement: the task runs asynchronously on the
 * taskqueue worker thread, so the measurement picks up scheduling
 * plus filter plus task. Typical values on a modern system are tens
 * of microseconds.
 *
 * Build:
 *     cc -o intr_latency_test intr_latency_test.c
 *
 * Run:
 *     sudo ./intr_latency_test [iterations]
 */

#include <sys/sysctl.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t
read_u64_sysctl(const char *name)
{
	uint64_t value = 0;
	size_t len = sizeof(value);

	if (sysctlbyname(name, &value, &len, NULL, 0) != 0) {
		fprintf(stderr, "sysctlbyname %s: %s\n", name,
		    strerror(errno));
		exit(1);
	}
	return (value);
}

static void
fire_simulate(uint32_t mask)
{
	const char *name = "dev.myfirst.0.intr_simulate";

	if (sysctlbyname(name, NULL, NULL, &mask, sizeof(mask)) != 0) {
		fprintf(stderr, "sysctlbyname %s (write): %s\n", name,
		    strerror(errno));
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	long iterations = (argc > 1) ? atol(argv[1]) : 100;
	struct timespec ts_before, ts_after;
	uint64_t tasks_before, tasks_after;
	uint64_t total_ns = 0;

	for (long i = 0; i < iterations; i++) {
		tasks_before = read_u64_sysctl(
		    "dev.myfirst.0.intr_task_invocations");

		clock_gettime(CLOCK_REALTIME, &ts_before);
		fire_simulate(1);

		do {
			tasks_after = read_u64_sysctl(
			    "dev.myfirst.0.intr_task_invocations");
		} while (tasks_after == tasks_before);

		clock_gettime(CLOCK_REALTIME, &ts_after);

		uint64_t ns = (uint64_t)(ts_after.tv_sec -
		    ts_before.tv_sec) * 1000000000ULL +
		    (uint64_t)(ts_after.tv_nsec - ts_before.tv_nsec);
		total_ns += ns;
	}

	printf("iterations:    %ld\n", iterations);
	printf("total ns:      %ju\n", (uintmax_t)total_ns);
	printf("average ns:    %ju\n", (uintmax_t)(total_ns / iterations));

	return (0);
}
