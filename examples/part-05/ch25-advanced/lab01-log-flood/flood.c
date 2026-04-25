/*
 * flood.c - user-space log-flood driver for Lab 1.
 *
 * Calls read() on /dev/myfirst0 as fast as the kernel will let us.
 * With MYF_DBG_IO enabled on the read path, each call produces one
 * log message.  Run once against the "unlimited" driver variant and
 * once against the "limited" (DLOG_RL) variant to see the difference.
 */

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVNODE "/dev/myfirst0"

int
main(int argc, char **argv)
{
	int fd;
	long i, n;
	char buf[64];

	if (argc != 2) {
		fprintf(stderr, "usage: flood <count>\n");
		return (2);
	}
	n = strtol(argv[1], NULL, 10);
	if (n <= 0)
		errx(2, "count must be positive");

	fd = open(DEVNODE, O_RDONLY);
	if (fd < 0)
		err(1, "open %s", DEVNODE);

	for (i = 0; i < n; i++) {
		if (lseek(fd, 0, SEEK_SET) < 0)
			err(1, "lseek");
		if (read(fd, buf, sizeof(buf)) < 0)
			err(1, "read");
	}

	close(fd);
	printf("flood: %ld reads completed\n", n);
	return (0);
}
