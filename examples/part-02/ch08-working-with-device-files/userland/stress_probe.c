/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * stress_probe: loop open() and close() on a device node many times.
 * Used as a rough correctness and leak test for the per-open state
 * path in Chapter 8.
 *
 * Usage:
 *     stress_probe [path] [iterations]
 *
 * Default: path=/dev/myfirst/0, iterations=1000.
 *
 * Compile:
 *     cc -Wall -Werror -o stress_probe stress_probe.c
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
	int iters = (argc > 2) ? atoi(argv[2]) : 1000;
	int i, fd;

	if (iters < 1)
		errx(1, "iterations must be >= 1");

	for (i = 0; i < iters; i++) {
		fd = open(path, O_RDWR);
		if (fd < 0)
			err(1, "open (iter %d)", i);
		if (close(fd) != 0)
			err(1, "close (iter %d)", i);
	}
	printf("%d iterations completed\n", iters);
	return (0);
}
