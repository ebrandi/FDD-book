/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * poll_waiter.c: a trivial poll(2) client for /dev/myfirst.
 *
 * Compile with:
 *   cc -Wall -Wextra -o poll_waiter poll_waiter.c
 *
 * Usage:
 *   ./poll_waiter              -> prints every byte the driver delivers.
 *   ./poll_waiter > /dev/null  -> drains silently (for stress tests).
 *
 * Used in Chapter 14 Lab 1 and Lab 2 to verify that selwakeup(9) is
 * correctly delivered from the driver's task callback.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int fd, n;
	struct pollfd pfd;
	char c;

	(void)argc;
	(void)argv;

	fd = open("/dev/myfirst", O_RDONLY);
	if (fd < 0)
		err(1, "open /dev/myfirst");

	pfd.fd = fd;
	pfd.events = POLLIN;

	for (;;) {
		n = poll(&pfd, 1, -1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			err(1, "poll");
		}
		if (pfd.revents & POLLIN) {
			n = read(fd, &c, 1);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno == EINTR)
					continue;
				err(1, "read");
			}
			(void)write(STDOUT_FILENO, &c, 1);
		}
	}

	(void)close(fd);
	return (0);
}
