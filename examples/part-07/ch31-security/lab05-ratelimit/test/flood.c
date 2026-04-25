/*
 * flood.c - companion test program for Chapter 31 Lab 5.
 *
 * Opens /dev/secdev and issues a million unknown ioctls as fast as
 * possible.  Without rate limiting this floods dmesg; with the fix
 * applied, the kernel should log at most about five messages per
 * second regardless of how fast the calls arrive.
 *
 * Must be run as root because the driver refuses unprivileged opens.
 *
 * Build:  cc -Wall -O2 -o flood flood.c
 * Usage:  ./flood [iterations]
 */

#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int fd;
	long iters = 1000000L;

	if (argc >= 2)
		iters = atol(argv[1]);

	fd = open("/dev/secdev", O_RDWR);
	if (fd < 0) {
		perror("open /dev/secdev");
		return (1);
	}
	for (long i = 0; i < iters; i++)
		(void)ioctl(fd, 0xdeadbeefUL, NULL);
	close(fd);
	printf("issued %ld unknown ioctls\n", iters);
	return (0);
}
