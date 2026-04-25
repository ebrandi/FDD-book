/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * stress_rw.c: a single-process stress harness for /dev/myfirst.
 *
 * Runs through a small table of (block size, repeat count) pairs.
 * For each pair, performs N round-trips of:
 *   - write(BLOCK) of a known pattern
 *   - read(BLOCK) and compare
 * Reports timing and any data mismatches.
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
#include <unistd.h>

#define	DEVPATH "/dev/myfirst"

struct probe {
	size_t block;
	int    iters;
};

static const struct probe table[] = {
	{   64, 1000 },
	{  256,  500 },
	{ 1024,  200 },
	{ 4096,  100 },
};

static double
seconds(struct timeval *t)
{
	return (t->tv_sec + t->tv_usec / 1e6);
}

static int
run_probe(int rfd, int wfd, size_t block, int iters)
{
	char *src = malloc(block);
	char *dst = malloc(block);
	if (src == NULL || dst == NULL)
		err(1, "malloc");

	int mismatches = 0;
	struct timeval t0, t1;
	gettimeofday(&t0, NULL);

	for (int it = 0; it < iters; it++) {
		for (size_t i = 0; i < block; i++)
			src[i] = (char)((it + i) & 0xff);

		size_t left = block;
		const char *p = src;
		while (left > 0) {
			ssize_t n = write(wfd, p, left);
			if (n < 0) {
				if (errno == EINTR) continue;
				warn("write");
				goto out;
			}
			p += n; left -= n;
		}

		left = block;
		char *q = dst;
		while (left > 0) {
			ssize_t n = read(rfd, q, left);
			if (n < 0) {
				if (errno == EINTR) continue;
				warn("read");
				goto out;
			}
			if (n == 0) {
				warn("EOF before draining");
				goto out;
			}
			q += n; left -= n;
		}

		if (memcmp(src, dst, block) != 0)
			mismatches++;
	}

out:
	gettimeofday(&t1, NULL);
	double elapsed = seconds(&t1) - seconds(&t0);
	double bytes = (double)block * iters;
	printf("block=%zu iters=%d mismatches=%d %.3f sec %.2f MB/s\n",
	    block, iters, mismatches, elapsed,
	    elapsed > 0 ? (bytes / elapsed) / (1024 * 1024) : 0);
	free(src); free(dst);
	return (mismatches);
}

int
main(void)
{
	int rfd = open(DEVPATH, O_RDONLY);
	int wfd = open(DEVPATH, O_WRONLY);
	if (rfd < 0 || wfd < 0)
		err(1, "open %s", DEVPATH);

	int total = 0;
	for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
		total += run_probe(rfd, wfd, table[i].block, table[i].iters);

	close(rfd); close(wfd);
	return (total == 0 ? 0 : 1);
}
