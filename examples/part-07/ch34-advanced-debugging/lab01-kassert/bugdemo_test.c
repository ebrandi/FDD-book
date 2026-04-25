/*
 * bugdemo_test - user-space test program for the bugdemo driver.
 *
 * Exercises the ioctl(2) interface with one subcommand per argument:
 *
 *   noop          - triggers BUGDEMO_OP_NOOP
 *   hello         - triggers BUGDEMO_OP_HELLO
 *   count         - triggers BUGDEMO_OP_COUNT
 *   force-bad-op  - sets BUGDEMO_FLAG_FORCE_BAD_OP, which panics on
 *                   a kernel with INVARIANTS
 *
 * On a debug kernel, force-bad-op triggers a KASSERT panic.
 * On a release kernel, the KASSERT is compiled out and the call
 * returns without any error.
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
do_op(int fd, uint32_t op, uint32_t flags)
{
	struct bugdemo_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.op = op;
	cmd.flags = flags;

	if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) < 0) {
		warn("ioctl BUGDEMO_TRIGGER");
		return (1);
	}

	printf("op=%u flags=0x%x arg=0x%llx\n",
	    cmd.op, cmd.flags, (unsigned long long)cmd.arg);
	return (0);
}

int
main(int argc, char *argv[])
{
	int fd;
	int rv;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <subcommand>\n", argv[0]);
		fprintf(stderr, "subcommands:\n");
		fprintf(stderr, "  noop, hello, count, force-bad-op\n");
		return (2);
	}

	fd = open("/dev/bugdemo", O_RDWR);
	if (fd < 0)
		err(1, "open /dev/bugdemo");

	if (strcmp(argv[1], "noop") == 0)
		rv = do_op(fd, BUGDEMO_OP_NOOP, 0);
	else if (strcmp(argv[1], "hello") == 0)
		rv = do_op(fd, BUGDEMO_OP_HELLO, 0);
	else if (strcmp(argv[1], "count") == 0)
		rv = do_op(fd, BUGDEMO_OP_COUNT, 0);
	else if (strcmp(argv[1], "force-bad-op") == 0)
		rv = do_op(fd, BUGDEMO_OP_NOOP, BUGDEMO_FLAG_FORCE_BAD_OP);
	else {
		fprintf(stderr, "unknown subcommand: %s\n", argv[1]);
		rv = 2;
	}

	close(fd);
	return (rv);
}
