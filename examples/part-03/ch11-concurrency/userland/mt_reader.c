/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * mt_reader.c: multiple threads reading from one descriptor of
 * /dev/myfirst.  Exercises the driver's handling of multiple
 * concurrent readers on the same fh.
 */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH		"/dev/myfirst"
#define	NTHREADS	4
#define	BYTES_PER_THR	(256 * 1024)
#define	BLOCK		4096

static int	g_fd;
static uint64_t	total[NTHREADS];
static uint32_t	sum[NTHREADS];

static uint32_t
checksum(const char *p, size_t n)
{
	uint32_t s = 0;
	for (size_t i = 0; i < n; i++)
		s = s * 31u + (uint8_t)p[i];
	return (s);
}

static void *
reader(void *arg)
{
	int tid = *(int *)arg;
	char buf[BLOCK];
	uint64_t got = 0;
	uint32_t sm = 0;

	while (got < BYTES_PER_THR) {
		ssize_t n = read(g_fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			warn("thread %d: read", tid);
			break;
		}
		if (n == 0)
			break;
		sm += checksum(buf, n);
		got += n;
	}
	total[tid] = got;
	sum[tid] = sm;
	return (NULL);
}

int
main(void)
{
	pthread_t tids[NTHREADS];
	int ids[NTHREADS];

	g_fd = open(DEVPATH, O_RDONLY);
	if (g_fd < 0)
		err(1, "open %s", DEVPATH);

	for (int i = 0; i < NTHREADS; i++) {
		ids[i] = i;
		if (pthread_create(&tids[i], NULL, reader, &ids[i]) != 0)
			err(1, "pthread_create");
	}
	for (int i = 0; i < NTHREADS; i++)
		pthread_join(tids[i], NULL);

	uint64_t grand = 0;
	for (int i = 0; i < NTHREADS; i++) {
		printf("thread %d: %" PRIu64 " bytes, checksum 0x%08x\n",
		    i, total[i], sum[i]);
		grand += total[i];
	}
	printf("grand total: %" PRIu64 "\n", grand);

	close(g_fd);
	return (0);
}
