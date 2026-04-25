/*
 * spin.c - companion test program for Chapter 31 Lab 6.
 *
 * Opens /dev/secdev and hammers the SECDEV_SLEEP ioctl in a loop.
 * Running this while another terminal issues `kldunload secdev`
 * exercises the detach race.
 *
 * Build:  cc -Wall -O2 -o spin spin.c
 * Usage:  sudo ./spin
 */

#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define	SECDEV_SLEEP	_IO('S', 2)

int
main(void)
{
	int fd;

	fd = open("/dev/secdev", O_RDWR);
	if (fd < 0) {
		perror("open /dev/secdev");
		return (1);
	}
	for (;;) {
		if (ioctl(fd, SECDEV_SLEEP, NULL) < 0) {
			perror("ioctl SECDEV_SLEEP");
			break;
		}
	}
	close(fd);
	return (0);
}
