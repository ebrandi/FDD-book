/*
 * bugdemo_test - Challenge 1 (silent-bug) companion.
 *
 * Drives the driver through INCREMENT and READ ioctls. On a
 * single thread the bug is hard to observe; it shows up under
 * concurrency. Try running several copies of this program in
 * parallel and compare the reported totals against the number
 * of increments.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bugdemo.h"

#define	DEVPATH	"/dev/bugdemo"

static void
usage(const char *prog)
{
	fprintf(stderr,
	    "usage: %s increment [N]\n"
	    "       %s read\n",
	    prog, prog);
	exit(2);
}

int
main(int argc, char **argv)
{
	struct bugdemo_command cmd;
	int fd, n = 1;

	if (argc < 2)
		usage(argv[0]);

	fd = open(DEVPATH, O_RDWR);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	memset(&cmd, 0, sizeof(cmd));

	if (strcmp(argv[1], "increment") == 0) {
		if (argc == 3)
			n = atoi(argv[2]);
		cmd.op = BUGDEMO_OP_INCREMENT;
		for (int i = 0; i < n; i++) {
			if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) != 0)
				err(1, "ioctl increment");
		}
		printf("issued %d increments\n", n);
	} else if (strcmp(argv[1], "read") == 0) {
		cmd.op = BUGDEMO_OP_READ;
		if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) != 0)
			err(1, "ioctl read");
		printf("counter = %llu\n",
		    (unsigned long long)cmd.arg);
	} else {
		usage(argv[0]);
	}

	close(fd);
	return (0);
}
