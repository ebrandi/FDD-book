/*
 * bugdemo_test - Challenge 3 (deadlock) companion.
 *
 * Usage:
 *     ./bugdemo_test path-a
 *     ./bugdemo_test path-b
 *     ./bugdemo_test race N    - spawn N children alternating
 *                                path-a and path-b
 *
 * To observe the deadlock, run "race" with a few tens of
 * iterations on an SMP system. On a single-CPU VM the race is
 * much harder to hit but WITNESS will still flag the
 * lock-order violation.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

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
	    "usage: %s path-a\n"
	    "       %s path-b\n"
	    "       %s race N\n",
	    prog, prog, prog);
	exit(2);
}

static int
issue_one(int fd, uint32_t op)
{
	struct bugdemo_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.op = op;
	return (ioctl(fd, BUGDEMO_TRIGGER, &cmd));
}

int
main(int argc, char **argv)
{
	int fd;

	if (argc < 2)
		usage(argv[0]);

	fd = open(DEVPATH, O_RDWR);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	if (strcmp(argv[1], "path-a") == 0) {
		if (issue_one(fd, BUGDEMO_OP_PATH_A) != 0)
			err(1, "ioctl path-a");
	} else if (strcmp(argv[1], "path-b") == 0) {
		if (issue_one(fd, BUGDEMO_OP_PATH_B) != 0)
			err(1, "ioctl path-b");
	} else if (strcmp(argv[1], "race") == 0) {
		int n, children = 0;

		if (argc != 3)
			usage(argv[0]);
		n = atoi(argv[2]);
		for (int i = 0; i < n; i++) {
			pid_t pid = fork();
			if (pid < 0)
				err(1, "fork");
			if (pid == 0) {
				uint32_t op = (i & 1) ?
				    BUGDEMO_OP_PATH_A : BUGDEMO_OP_PATH_B;
				for (int j = 0; j < 50; j++)
					(void)issue_one(fd, op);
				_exit(0);
			}
			children++;
		}
		for (int i = 0; i < children; i++)
			(void)wait(NULL);
	} else {
		usage(argv[0]);
	}

	close(fd);
	return (0);
}
