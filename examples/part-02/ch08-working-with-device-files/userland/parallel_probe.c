/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * parallel_probe: open a device node multiple times from a single
 * process, hold all descriptors simultaneously, then close them all.
 *
 * Used for Chapter 8 Lab 8.6 to verify that per-open state is truly
 * per-descriptor.
 *
 * Usage:
 *     parallel_probe [path] [count]
 *
 * Default: path=/dev/myfirst/0, count=4. Count must be 1..MAX_FDS.
 *
 * Compile:
 *     cc -Wall -Werror -o parallel_probe parallel_probe.c
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FDS 8

int
main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
	int fds[MAX_FDS];
	int i, n;

	n = (argc > 2) ? atoi(argv[2]) : 4;
	if (n < 1 || n > MAX_FDS)
		errx(1, "count must be 1..%d", MAX_FDS);

	for (i = 0; i < n; i++) {
		fds[i] = open(path, O_RDWR);
		if (fds[i] < 0)
			err(1, "open %s (fd %d of %d)", path, i + 1, n);
		printf("opened %s as fd %d\n", path, fds[i]);
	}

	printf("holding %d descriptors; press enter to close\n", n);
	(void)getchar();

	for (i = 0; i < n; i++) {
		if (close(fds[i]) != 0)
			warn("close fd %d", fds[i]);
	}
	return (0);
}
