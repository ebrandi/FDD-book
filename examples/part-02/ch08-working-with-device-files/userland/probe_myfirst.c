/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * probe_myfirst: a tiny userland probe for the Chapter 8 myfirst driver.
 *
 * Usage:
 *     probe_myfirst [path]
 *
 * With no argument, opens /dev/myfirst/0. With an argument, opens the
 * given path. Performs one read(2) into a fixed buffer, reports the
 * number of bytes, closes, and exits.
 *
 * Compile:
 *     cc -Wall -Werror -o probe_myfirst probe_myfirst.c
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "/dev/myfirst/0";
	char buf[64];
	ssize_t n;
	int fd;

	fd = open(path, O_RDWR);
	if (fd < 0)
		err(1, "open %s", path);

	n = read(fd, buf, sizeof(buf));
	if (n < 0)
		err(1, "read %s", path);

	printf("read %zd bytes from %s\n", n, path);

	if (close(fd) != 0)
		err(1, "close %s", path);

	return (0);
}
