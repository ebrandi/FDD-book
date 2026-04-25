/*
 * mfctl25.c - capability-aware control program for the myfirst driver.
 *
 * Chapter 25, Lab 7.  This program issues MYFIRSTIOC_GETCAPS before
 * each operation and skips operations the driver does not advertise.
 * When GETCAPS itself is not supported (older driver), it falls back
 * to a documented default capability set derived from Chapter 24.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../myfirst_ioctl.h"

#define DEVNODE "/dev/myfirst0"

static const uint32_t FALLBACK_CAPS =
    MYF_CAP_RESET | MYF_CAP_GETMSG | MYF_CAP_SETMSG;

static uint32_t
driver_caps(int fd)
{
	uint32_t caps;

	if (ioctl(fd, MYFIRSTIOC_GETCAPS, &caps) == 0)
		return (caps);
	if (errno != ENOTTY)
		err(1, "MYFIRSTIOC_GETCAPS");

	fprintf(stderr,
	    "GETCAPS ioctl not supported.  "
	    "Falling back to default feature set:\n");
	return (FALLBACK_CAPS);
}

static void
print_caps(uint32_t caps)
{
	if (caps & MYF_CAP_RESET)
		printf("  MYF_CAP_RESET\n");
	if (caps & MYF_CAP_GETMSG)
		printf("  MYF_CAP_GETMSG\n");
	if (caps & MYF_CAP_SETMSG)
		printf("  MYF_CAP_SETMSG\n");
	if (caps & MYF_CAP_TIMEOUT)
		printf("  MYF_CAP_TIMEOUT\n");
	if (caps & MYF_CAP_STATS)
		printf("  MYF_CAP_STATS\n");
}

static void
cmd_caps(int fd)
{
	uint32_t caps = driver_caps(fd);

	printf("Driver reports capabilities:\n");
	print_caps(caps);
}

static void
cmd_reset(int fd)
{
	uint32_t caps = driver_caps(fd);

	if (!(caps & MYF_CAP_RESET)) {
		printf("reset: not supported on this driver version.\n");
		return;
	}
	if (ioctl(fd, MYFIRSTIOC_RESET) != 0)
		err(1, "MYFIRSTIOC_RESET");
}

static void
cmd_getmsg(int fd)
{
	uint32_t caps = driver_caps(fd);
	char buf[MYFIRST_MSG_MAX];

	if (!(caps & MYF_CAP_GETMSG)) {
		printf("getmsg: not supported on this driver version.\n");
		return;
	}
	if (ioctl(fd, MYFIRSTIOC_GETMSG, buf) != 0)
		err(1, "MYFIRSTIOC_GETMSG");
	printf("Current message: %s\n", buf);
}

static void
cmd_setmsg(int fd, const char *msg)
{
	uint32_t caps = driver_caps(fd);
	char buf[MYFIRST_MSG_MAX];

	if (!(caps & MYF_CAP_SETMSG)) {
		printf("setmsg: not supported on this driver version.\n");
		return;
	}
	strlcpy(buf, msg, sizeof(buf));
	if (ioctl(fd, MYFIRSTIOC_SETMSG, buf) != 0)
		err(1, "MYFIRSTIOC_SETMSG");
}

static void
cmd_timeout(int fd)
{
	uint32_t caps = driver_caps(fd);

	if (!(caps & MYF_CAP_TIMEOUT)) {
		printf("Timeout ioctl not supported; use sysctl "
		    "dev.myfirst.0.timeout_sec instead.\n");
		return;
	}
	printf("Timeout ioctl supported; would issue the ioctl here.\n");
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mfctl25 <command> [arg]\n"
	    "commands: caps | reset | getmsg | setmsg <msg> | timeout\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int fd;

	if (argc < 2)
		usage();

	fd = open(DEVNODE, O_RDWR);
	if (fd < 0)
		err(1, "open %s", DEVNODE);

	if (strcmp(argv[1], "caps") == 0)
		cmd_caps(fd);
	else if (strcmp(argv[1], "reset") == 0)
		cmd_reset(fd);
	else if (strcmp(argv[1], "getmsg") == 0)
		cmd_getmsg(fd);
	else if (strcmp(argv[1], "setmsg") == 0) {
		if (argc != 3)
			usage();
		cmd_setmsg(fd, argv[2]);
	} else if (strcmp(argv[1], "timeout") == 0)
		cmd_timeout(fd);
	else
		usage();

	close(fd);
	return (0);
}
