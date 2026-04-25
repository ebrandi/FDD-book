/*
 * workload_locks.c - a lock-heavy multithreaded workload.
 *
 * Spawns a small pool of threads that repeatedly lock and unlock a
 * small set of pthread_mutexes in a rotating pattern. Userland
 * pthread_mutex ultimately rides on the kernel's lock primitives
 * (via umtx syscalls when contended), so a lock-heavy userland
 * workload amplifies the per-lock cost of kernel lock primitives
 * in the contention path, and the WITNESS/INVARIANTS overheads
 * that sit on top of them.
 *
 * Companion to Appendix F of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery".
 */

#include <sys/types.h>
#include <sys/time.h>

#include <err.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	DEFAULT_THREADS		4
#define	DEFAULT_ITER_PER_THR	10000000ULL
#define	NUM_LOCKS		4

static pthread_mutex_t locks[NUM_LOCKS];
static uint64_t per_thread_iter;

static void *
worker(void *arg)
{
	int tid = (int)(uintptr_t)arg;
	uint64_t i;
	uint64_t sum = 0;

	for (i = 0; i < per_thread_iter; i++) {
		int slot = (tid + i) % NUM_LOCKS;

		if (pthread_mutex_lock(&locks[slot]) != 0)
			err(1, "pthread_mutex_lock");
		sum += i;
		if (pthread_mutex_unlock(&locks[slot]) != 0)
			err(1, "pthread_mutex_unlock");
	}

	return ((void *)(uintptr_t)sum);
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
	int nthreads = DEFAULT_THREADS;
	uint64_t iter = DEFAULT_ITER_PER_THR;
	pthread_t *ts;
	struct timespec ts_start, ts_end;
	uint64_t total_ns, total_iter;
	int i;

	if (argc > 1)
		nthreads = atoi(argv[1]);
	if (argc > 2)
		iter = strtoull(argv[2], NULL, 10);
	per_thread_iter = iter;

	if (nthreads < 1 || nthreads > 1024)
		errx(1, "threads out of range");

	for (i = 0; i < NUM_LOCKS; i++) {
		if (pthread_mutex_init(&locks[i], NULL) != 0)
			err(1, "pthread_mutex_init");
	}

	ts = calloc(nthreads, sizeof(pthread_t));
	if (ts == NULL)
		err(1, "calloc");

	printf("# workload=locks threads=%d iter_per_thread=%" PRIu64 "\n",
	    nthreads, iter);

	if (clock_gettime(CLOCK_MONOTONIC, &ts_start) != 0)
		err(1, "clock_gettime start");

	for (i = 0; i < nthreads; i++) {
		if (pthread_create(&ts[i], NULL, worker,
		    (void *)(uintptr_t)i) != 0)
			err(1, "pthread_create");
	}
	for (i = 0; i < nthreads; i++) {
		if (pthread_join(ts[i], NULL) != 0)
			err(1, "pthread_join");
	}

	if (clock_gettime(CLOCK_MONOTONIC, &ts_end) != 0)
		err(1, "clock_gettime end");

	total_ns = ns_between(&ts_start, &ts_end);
	total_iter = (uint64_t)nthreads * iter;

	printf("wall_ns=%" PRIu64 "\n", total_ns);
	printf("total_ops=%" PRIu64 "\n", total_iter);
	printf("per_op_ns=%.2f\n",
	    (double)total_ns / (double)total_iter);

	free(ts);
	for (i = 0; i < NUM_LOCKS; i++)
		pthread_mutex_destroy(&locks[i]);

	return (0);
}
