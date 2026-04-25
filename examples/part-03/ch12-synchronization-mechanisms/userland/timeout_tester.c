/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * timeout_tester.c: verify that the Chapter 12 driver's bounded
 * read returns EAGAIN after approximately the configured timeout
 * when no data is available.
 *
 * Usage:
 *   timeout_tester [timeout_ms]
 *
 * Sets dev.myfirst.0.read_timeout_ms to the supplied value (default
 * 100), opens the device, calls read(2), and prints the elapsed time
 * and the resulting errno.
 */

#include <sys/sysctl.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH	"/dev/myfirst"

int
main(int argc, char **argv)
{
	int timeout_ms = 100;
	int fd;
	char buf[1024];
	struct timeval t0, t1;
	ssize_t n;
	int saved;
	long elapsed_ms;

	if (argc > 1)
		timeout_ms = atoi(argv[1]);

	if (sysctlbyname("dev.myfirst.0.read_timeout_ms",
	    NULL, NULL, &timeout_ms, sizeof(timeout_ms)) != 0)
		err(1, "sysctlbyname set read_timeout_ms");

	fd = open(DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	gettimeofday(&t0, NULL);
	n = read(fd, buf, sizeof(buf));
	gettimeofday(&t1, NULL);
	saved = errno;

	elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000 +
	    (t1.tv_usec - t0.tv_usec) / 1000;
	printf("read returned %zd, errno=%d (%s) after %ld ms\n",
	    n, saved, strerror(saved), elapsed_ms);

	close(fd);
	return (0);
}
