/*
 * bugdemo_test - Lab 5 variant.
 *
 * Sends a use-after-free ioctl to the bugdemo driver. With memguard
 * enabled for the bugdemo malloc type, this is expected to panic the
 * kernel. Run only on a throwaway debug VM.
 *
 * Companion example for Chapter 34 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
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
	    "usage: %s use-after-free\n", prog);
	exit(2);
}

int
main(int argc, char **argv)
{
	struct bugdemo_command cmd;
	int fd;

	if (argc != 2)
		usage(argv[0]);

	memset(&cmd, 0, sizeof(cmd));

	if (strcmp(argv[1], "use-after-free") == 0)
		cmd.op = BUGDEMO_OP_USE_AFTER_FREE;
	else
		usage(argv[0]);

	fd = open(DEVPATH, O_RDWR);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	if (ioctl(fd, BUGDEMO_TRIGGER, &cmd) != 0)
		warn("ioctl BUGDEMO_TRIGGER");
	else
		printf("ioctl issued: op=%u\n", cmd.op);

	/*
	 * The callout fires 100ms after the ioctl returns. Sleep for
	 * half a second so we do not exit before the bug manifests.
	 */
	usleep(500 * 1000);

	close(fd);
	return (0);
}
