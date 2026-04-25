/*
 * testclient.c - A small user-space test for jaildev.
 *
 * Compile:
 *   cc -Wall -o testclient testclient.c
 * Run (as root on the host, and as root inside a jail):
 *   ./testclient
 */

#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>

#define	JAILDEV_IOC_PRIVOP	_IO('J', 1)

int
main(void)
{
	char buf[64];
	ssize_t n;
	int fd;

	fd = open("/dev/jaildev", O_RDONLY);
	if (fd < 0)
		err(1, "open /dev/jaildev");

	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		warn("read");
	} else {
		buf[n] = '\0';
		printf("read: %s", buf);
	}

	if (ioctl(fd, JAILDEV_IOC_PRIVOP) < 0)
		warn("ioctl PRIVOP");
	else
		printf("privileged ioctl succeeded\n");

	close(fd);
	return (0);
}
