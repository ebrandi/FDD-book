/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * lat_tester.c: measure read latency against /dev/myfirst and print
 * a simple bucketed histogram.  Useful for observing the cost of
 * the driver's mutex under various load conditions.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	DEVPATH		"/dev/myfirst"
#define	NSAMPLES	10000
#define	BLOCK		1024

static uint64_t
nanos(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec);
}

int
main(void)
{
	int fd;
	char buf[BLOCK];
	uint64_t samples[NSAMPLES];
	int nvalid = 0;
	uint64_t buckets[8] = {0};
	const char *labels[8] = {
		"<1us   ", "<10us  ", "<100us ", "<1ms   ",
		"<10ms  ", "<100ms ", "<1s    ", ">=1s   "
	};

	fd = open(DEVPATH, O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		err(1, "open");

	for (int i = 0; i < NSAMPLES; i++) {
		uint64_t t0 = nanos();
		ssize_t n = read(fd, buf, sizeof(buf));
		uint64_t t1 = nanos();
		if (n > 0)
			samples[nvalid++] = t1 - t0;
		else
			usleep(100);
	}
	close(fd);

	for (int i = 0; i < nvalid; i++) {
		uint64_t ns = samples[i];
		int b;
		if (ns < 1000) b = 0;
		else if (ns < 10000) b = 1;
		else if (ns < 100000) b = 2;
		else if (ns < 1000000) b = 3;
		else if (ns < 10000000) b = 4;
		else if (ns < 100000000) b = 5;
		else if (ns < 1000000000) b = 6;
		else b = 7;
		buckets[b]++;
	}

	printf("Latency histogram (%d samples):\n", nvalid);
	for (int i = 0; i < 8; i++)
		printf("  %s %6llu\n",
		    labels[i], (unsigned long long)buckets[i]);
	return (0);
}
