/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * signal_read: start a blocking read on /dev/myfirst, receive a
 * signal after a delay, report how many bytes were delivered before
 * the signal interrupted the read.
 *
 * Used in Chapter 15 Lab 3 to confirm the partial-progress
 * convention.
 *
 * Compile:
 *   cc -Wall -Wextra -o signal_read signal_read.c
 */

#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t got_sig = 0;

static void
on_sig(int sig)
{
	(void)sig;
	got_sig = 1;
}

int
main(int argc, char **argv)
{
	int fd;
	char buf[4096];
	ssize_t n;
	struct sigaction sa;

	(void)argc;
	(void)argv;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sig;
	sigaction(SIGINT, &sa, NULL);

	fd = open("/dev/myfirst", O_RDONLY);
	if (fd < 0)
		err(1, "open");

	fprintf(stderr, "reading up to %zu bytes; send SIGINT with kill -INT %d\n",
	    sizeof(buf), getpid());

	n = read(fd, buf, sizeof(buf));
	if (n < 0) {
		if (errno == EINTR)
			fprintf(stderr, "read returned EINTR, 0 bytes\n");
		else
			err(1, "read");
	} else {
		fprintf(stderr, "read returned %zd bytes (got_sig=%d)\n",
		    n, (int)got_sig);
	}

	close(fd);
	return (0);
}
