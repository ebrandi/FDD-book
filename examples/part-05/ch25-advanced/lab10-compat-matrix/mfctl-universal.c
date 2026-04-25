/*
 * mfctl-universal.c - single user-space tool that spans three
 * driver versions.
 *
 * Chapter 25, Lab 10.  This program is the practical payoff of the
 * capability-discovery pattern: one binary works against the 1.6,
 * 1.7, and 1.8 drivers without modification.  The three-tier
 * fallback is:
 *
 *   1. Ask the driver directly via MYFIRSTIOC_GETCAPS.
 *   2. If that fails with ENOTTY, match known version strings
 *      from dev.myfirst.0.version.
 *   3. If the version string is unknown, use a minimal safe set.
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

static void
read_version(char *buf, size_t buflen)
{
	size_t len = buflen;

	if (sysctlbyname("dev.myfirst.0.version", buf, &len,
	    NULL, 0) != 0) {
		strlcpy(buf, "unknown", buflen);
		return;
	}
	if (len > 0 && buf[len - 1] == '\0')
		len--;
	if (len < buflen)
		buf[len] = '\0';
}

static uint32_t
driver_caps(int fd, const char *version, bool *getcaps_supported)
{
	uint32_t caps;

	if (ioctl(fd, MYFIRSTIOC_GETCAPS, &caps) == 0) {
		*getcaps_supported = true;
		return (caps);
	}
	if (errno != ENOTTY)
		err(1, "GETCAPS ioctl");
	*getcaps_supported = false;

	/* Fallback by version string. */
	if (strstr(version, "1.8-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.7-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.6-") != NULL)
		return (MYF_CAP_GETMSG | MYF_CAP_SETMSG);

	/* Unknown version: use the minimal safe set. */
	return (MYF_CAP_GETMSG);
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
	char version[64];
	bool getcaps_supported;
	uint32_t caps;

	read_version(version, sizeof(version));
	caps = driver_caps(fd, version, &getcaps_supported);

	printf("Driver: version %s\n", version);
	printf("GETCAPS ioctl: %s\n",
	    getcaps_supported ? "supported" : "not supported");
	if (getcaps_supported)
		printf("Driver reports capabilities:\n");
	else
		printf("Using fallback capability set:\n");
	print_caps(caps);
}

static void
cmd_reset(int fd)
{
	char version[64];
	bool getcaps_supported;
	uint32_t caps;

	read_version(version, sizeof(version));
	caps = driver_caps(fd, version, &getcaps_supported);

	if (!(caps & MYF_CAP_RESET)) {
		printf("reset: not supported on this driver "
		    "version (%s)\n", version);
		return;
	}
	if (ioctl(fd, MYFIRSTIOC_RESET) != 0)
		err(1, "MYFIRSTIOC_RESET");
}

static void
cmd_getmsg(int fd)
{
	char version[64];
	bool getcaps_supported;
	uint32_t caps;
	char buf[MYFIRST_MSG_MAX];

	read_version(version, sizeof(version));
	caps = driver_caps(fd, version, &getcaps_supported);

	if (!(caps & MYF_CAP_GETMSG)) {
		printf("getmsg: not supported on this driver "
		    "version (%s)\n", version);
		return;
	}
	if (ioctl(fd, MYFIRSTIOC_GETMSG, buf) != 0)
		err(1, "MYFIRSTIOC_GETMSG");
	printf("Current message: %s\n", buf);
}

static void
cmd_setmsg(int fd, const char *msg)
{
	char version[64];
	bool getcaps_supported;
	uint32_t caps;
	char buf[MYFIRST_MSG_MAX];

	read_version(version, sizeof(version));
	caps = driver_caps(fd, version, &getcaps_supported);

	if (!(caps & MYF_CAP_SETMSG)) {
		printf("setmsg: not supported on this driver "
		    "version (%s)\n", version);
		return;
	}
	strlcpy(buf, msg, sizeof(buf));
	if (ioctl(fd, MYFIRSTIOC_SETMSG, buf) != 0)
		err(1, "MYFIRSTIOC_SETMSG");
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mfctl-universal --caps\n"
	    "       mfctl-universal reset | getmsg | setmsg <msg>\n");
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

	if (strcmp(argv[1], "--caps") == 0 ||
	    strcmp(argv[1], "caps") == 0)
		cmd_caps(fd);
	else if (strcmp(argv[1], "reset") == 0)
		cmd_reset(fd);
	else if (strcmp(argv[1], "getmsg") == 0)
		cmd_getmsg(fd);
	else if (strcmp(argv[1], "setmsg") == 0) {
		if (argc != 3)
			usage();
		cmd_setmsg(fd, argv[2]);
	} else
		usage();

	close(fd);
	return (0);
}
