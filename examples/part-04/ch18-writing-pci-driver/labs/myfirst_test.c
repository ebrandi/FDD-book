/*
 * myfirst_test.c -- Chapter 18 user-space smoke test.
 *
 * Opens /dev/myfirst0, reads up to 64 bytes, writes 16 bytes, and
 * closes the descriptor. Exits non-zero on any failure.
 *
 * Build:
 *     cc -o myfirst_test myfirst_test.c
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int
main(void)
{
	int fd;
	ssize_t n;
	char rbuf[64];
	char wbuf[16] = "hello-ch18\n";

	fd = open("/dev/myfirst0", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open: %s\n", strerror(errno));
		return (1);
	}

	n = read(fd, rbuf, sizeof(rbuf));
	printf("read returned %zd\n", n);

	n = write(fd, wbuf, sizeof(wbuf));
	printf("write returned %zd\n", n);

	if (close(fd) < 0) {
		fprintf(stderr, "close: %s\n", strerror(errno));
		return (1);
	}

	return (0);
}
