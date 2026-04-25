/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * hold_myfirst: open /dev/myfirst/0 and sleep. Used for Challenge 2 to
 * prove that the per-open destructor fires on process exit even without
 * an explicit close(2) call.
 *
 * Usage:
 *     hold_myfirst [seconds] [path]
 *
 * Default: sleep 30 seconds, path /dev/myfirst/0. Exits without calling
 * close(2) on purpose. The kernel should still fire the per-open
 * destructor; watch dmesg to confirm.
 *
 * Compile:
 *     cc -Wall -Werror -o hold_myfirst hold_myfirst.c
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	unsigned int seconds = 30;
	const char *path = "/dev/myfirst/0";
	int fd;

	if (argc > 1)
		seconds = (unsigned int)strtoul(argv[1], NULL, 10);
	if (argc > 2)
		path = argv[2];

	fd = open(path, O_RDWR);
	if (fd < 0)
		err(1, "open %s", path);

	printf("opened %s on fd %d; sleeping %u seconds then exiting "
	    "without close(2)\n", path, fd, seconds);

	sleep(seconds);

	/* Intentionally skip close(fd): let the kernel do it. */
	return (0);
}
