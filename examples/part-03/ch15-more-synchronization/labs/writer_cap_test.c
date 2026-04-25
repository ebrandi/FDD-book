/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * writer_cap_test: start N concurrent writers that open /dev/myfirst
 * and write a short burst.  Used to stress the writer-cap semaphore
 * introduced in Chapter 15 Stage 1.
 *
 * Usage:
 *   ./writer_cap_test [num_writers] [bytes_per_write] [writes_per_proc]
 *
 * Compile:
 *   cc -Wall -Wextra -o writer_cap_test writer_cap_test.c
 */

#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int fd, i, j;
	int writers = (argc > 1) ? atoi(argv[1]) : 16;
	int bytes = (argc > 2) ? atoi(argv[2]) : 64;
	int writes = (argc > 3) ? atoi(argv[3]) : 100;
	char *buf;

	buf = malloc(bytes);
	if (buf == NULL)
		err(1, "malloc");

	for (i = 0; i < writers; i++) {
		pid_t pid = fork();
		if (pid < 0)
			err(1, "fork");
		if (pid == 0) {
			fd = open("/dev/myfirst", O_WRONLY);
			if (fd < 0)
				err(1, "writer %d: open", i);
			snprintf(buf, bytes, "writer-%d-data", i);
			for (j = 0; j < writes; j++) {
				if (write(fd, buf, bytes) < 0) {
					if (errno == ENXIO)
						break;
					err(1, "writer %d: write", i);
				}
				usleep(1000);
			}
			close(fd);
			_exit(0);
		}
	}

	while (wait(NULL) > 0)
		;

	free(buf);
	return (0);
}
