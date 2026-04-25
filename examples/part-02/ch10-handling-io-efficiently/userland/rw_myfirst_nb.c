/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * rw_myfirst_nb.c: non-blocking behaviour tester for /dev/myfirst.
 *
 * Exercises Stage 3+ of Chapter 10:
 *   - O_NONBLOCK + empty buffer -> EAGAIN
 *   - poll(POLLIN, 0) on empty buffer -> 0
 *   - write
 *   - poll(POLLIN, 0) on non-empty buffer -> POLLIN | POLLRDNORM
 *   - non-blocking read returns the bytes
 */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH "/dev/myfirst"

int
main(void)
{
	int fd, error;
	ssize_t n;
	char rbuf[128];
	struct pollfd pfd;

	fd = open(DEVPATH, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	n = read(fd, rbuf, sizeof(rbuf));
	if (n < 0 && errno == EAGAIN)
		printf("step 1: empty-read returned EAGAIN (expected)\n");
	else
		printf("step 1: UNEXPECTED read returned %zd errno=%d\n",
		    n, errno);

	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	error = poll(&pfd, 1, 0);
	printf("step 2: poll(POLLIN, 0) = %d revents=0x%x\n",
	    error, pfd.revents);

	n = write(fd, "hello world\n", 12);
	printf("step 3: wrote %zd bytes\n", n);

	pfd.events = POLLIN;
	pfd.revents = 0;
	error = poll(&pfd, 1, 0);
	printf("step 4: poll(POLLIN, 0) = %d revents=0x%x\n",
	    error, pfd.revents);

	memset(rbuf, 0, sizeof(rbuf));
	n = read(fd, rbuf, sizeof(rbuf));
	if (n > 0) {
		rbuf[n] = '\0';
		printf("step 5: read %zd bytes: %s", n, rbuf);
	} else {
		printf("step 5: UNEXPECTED read returned %zd errno=%d\n",
		    n, errno);
	}

	close(fd);
	return (0);
}
