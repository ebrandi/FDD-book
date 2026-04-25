/*
 * bugdemo_test - Lab 4 test program.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include "bugdemo.h"

static int
do_op(int fd, uint32_t op)
{
	struct bugdemo_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.op = op;

	if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) < 0) {
		warn("ioctl BUGDEMO_TRIGGER");
		return (1);
	}
	printf("op=%u arg=0x%llx\n", cmd.op, (unsigned long long)cmd.arg);
	return (0);
}

int
main(int argc, char *argv[])
{
	int fd;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <noop|hello|count|bad-op>\n",
		    argv[0]);
		return (2);
	}

	fd = open("/dev/bugdemo", O_RDWR);
	if (fd < 0)
		err(1, "open /dev/bugdemo");

	if (strcmp(argv[1], "noop") == 0)
		rv = do_op(fd, BUGDEMO_OP_NOOP);
	else if (strcmp(argv[1], "hello") == 0)
		rv = do_op(fd, BUGDEMO_OP_HELLO);
	else if (strcmp(argv[1], "count") == 0)
		rv = do_op(fd, BUGDEMO_OP_COUNT);
	else if (strcmp(argv[1], "bad-op") == 0)
		rv = do_op(fd, BUGDEMO_OP_BAD);
	else {
		fprintf(stderr, "unknown subcommand: %s\n", argv[1]);
		rv = 2;
	}

	close(fd);
	return (rv);
}
