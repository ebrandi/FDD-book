/*
 * myfirstctl.c - command-line front end to the myfirst driver's ioctls.
 *
 * Build:  cc -o myfirstctl myfirstctl.c
 * Usage:  myfirstctl get-version
 *         myfirstctl get-message
 *         myfirstctl set-message "<text>"
 *         myfirstctl reset
 *
 * The program opens the device with the minimum flags required for
 * the requested operation: O_RDONLY for the read-only commands and
 * O_RDWR for the writers.  The kernel's dispatcher returns EBADF on
 * a write-class command issued through a read-only fd.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "myfirst_ioctl.h"

#define DEVPATH	"/dev/myfirst0"

static void
usage(void)
{
	fprintf(stderr,
	    "usage: myfirstctl get-version\n"
	    "       myfirstctl get-message\n"
	    "       myfirstctl set-message <text>\n"
	    "       myfirstctl reset\n");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	int fd, flags;
	const char *cmd;

	if (argc < 2)
		usage();
	cmd = argv[1];

	/*
	 * SETMSG and RESET need write access; the others only need
	 * read.  Open the device with the right flags so the
	 * dispatcher does not return EBADF.
	 */
	if (strcmp(cmd, "set-message") == 0 ||
	    strcmp(cmd, "reset") == 0)
		flags = O_RDWR;
	else
		flags = O_RDONLY;

	fd = open(DEVPATH, flags);
	if (fd < 0)
		err(EX_OSERR, "open %s", DEVPATH);

	if (strcmp(cmd, "get-version") == 0) {
		uint32_t ver;
		if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
			err(EX_OSERR, "MYFIRSTIOC_GETVER");
		printf("driver ioctl version: %u\n", ver);
	} else if (strcmp(cmd, "get-message") == 0) {
		char buf[MYFIRST_MSG_MAX];
		if (ioctl(fd, MYFIRSTIOC_GETMSG, buf) < 0)
			err(EX_OSERR, "MYFIRSTIOC_GETMSG");
		printf("%s\n", buf);
	} else if (strcmp(cmd, "set-message") == 0) {
		char buf[MYFIRST_MSG_MAX];
		if (argc < 3)
			usage();
		strlcpy(buf, argv[2], sizeof(buf));
		if (ioctl(fd, MYFIRSTIOC_SETMSG, buf) < 0)
			err(EX_OSERR, "MYFIRSTIOC_SETMSG");
	} else if (strcmp(cmd, "reset") == 0) {
		if (ioctl(fd, MYFIRSTIOC_RESET) < 0)
			err(EX_OSERR, "MYFIRSTIOC_RESET");
	} else {
		usage();
	}

	close(fd);
	return (0);
}
