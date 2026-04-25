/*
 * bugdemo_test - Challenge 2 (leaky-driver) companion.
 *
 * Usage:
 *     ./bugdemo_test alloc [N]            - N allocations, normal path
 *     ./bugdemo_test alloc-leaky [N]      - N allocations, leaky path
 *     ./bugdemo_test free [N]             - free the top N objects
 *     ./bugdemo_test stats                - inflight allocations
 *
 * Exercise: drive enough leaky allocations to make vmstat -m
 * show the leak, then use DTrace to capture the stack trace of
 * the leaking path.
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
	    "usage: %s alloc [N]\n"
	    "       %s alloc-leaky [N]\n"
	    "       %s free [N]\n"
	    "       %s stats\n",
	    prog, prog, prog, prog);
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

	if (strcmp(argv[1], "alloc") == 0 ||
	    strcmp(argv[1], "alloc-leaky") == 0) {
		if (argc == 3)
			n = atoi(argv[2]);
		cmd.op = BUGDEMO_OP_ALLOC;
		if (strcmp(argv[1], "alloc-leaky") == 0)
			cmd.flags = BUGDEMO_FLAG_RETURN_EARLY;
		for (int i = 0; i < n; i++) {
			if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) != 0)
				err(1, "ioctl alloc");
		}
		printf("issued %d allocations (flags=0x%x)\n",
		    n, cmd.flags);
	} else if (strcmp(argv[1], "free") == 0) {
		if (argc == 3)
			n = atoi(argv[2]);
		cmd.op = BUGDEMO_OP_FREE;
		for (int i = 0; i < n; i++) {
			if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) != 0) {
				warn("ioctl free (after %d)", i);
				break;
			}
		}
	} else if (strcmp(argv[1], "stats") == 0) {
		cmd.op = BUGDEMO_OP_STATS;
		if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) != 0)
			err(1, "ioctl stats");
		printf("inflight = %llu\n",
		    (unsigned long long)cmd.arg);
	} else {
		usage(argv[0]);
	}

	close(fd);
	return (0);
}
