/*
 * rw_myfirst.c - small exerciser for the Chapter 9 myfirst driver.
 *
 * Usage:
 *   rw_myfirst read        -- open, read until EOF twice
 *   rw_myfirst write TEXT  -- open, write TEXT, close
 *   rw_myfirst rt          -- write payload, close, read back, verify
 *
 * Build: cc -o rw_myfirst rw_myfirst.c
 */

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define	DEVPATH "/dev/myfirst/0"

static int
do_read(void)
{
	int fd;
	ssize_t n;
	char buf[256];
	int round;

	fd = open(DEVPATH, O_RDONLY);
	if (fd < 0) {
		perror("open " DEVPATH);
		return (1);
	}

	for (round = 1; round <= 2; round++) {
		n = read(fd, buf, sizeof(buf) - 1);
		if (n < 0) {
			perror("read");
			close(fd);
			return (2);
		}
		if (n == 0) {
			printf("[read %d] 0 bytes (EOF)\n", round);
			continue;
		}
		buf[n] = '\0';
		printf("[read %d] %zd bytes:\n%s", round, n, buf);
	}

	close(fd);
	return (0);
}

static int
do_write(const char *text)
{
	int fd;
	ssize_t n;
	size_t len;

	if (text == NULL) {
		fprintf(stderr, "write: missing text argument\n");
		return (1);
	}
	fd = open(DEVPATH, O_WRONLY);
	if (fd < 0) {
		perror("open " DEVPATH);
		return (1);
	}
	len = strlen(text);
	n = write(fd, text, len);
	if (n < 0) {
		perror("write");
		close(fd);
		return (2);
	}
	if ((size_t)n != len) {
		fprintf(stderr, "short write: %zd of %zu\n", n, len);
	}
	printf("[write] %zd bytes accepted\n", n);
	close(fd);
	return (0);
}

static const char payload[] =
    "round-trip test payload, 24b\n";

static int
do_roundtrip(void)
{
	int fd;
	ssize_t n;
	char buf[256];
	size_t plen = sizeof(payload) - 1;

	fd = open(DEVPATH, O_WRONLY);
	if (fd < 0) { perror("open W"); return (1); }
	n = write(fd, payload, plen);
	if (n < 0) { perror("write"); close(fd); return (2); }
	if ((size_t)n != plen) {
		fprintf(stderr, "short write: %zd\n", n);
		close(fd);
		return (3);
	}
	close(fd);

	memset(buf, 0, sizeof(buf));
	fd = open(DEVPATH, O_RDONLY);
	if (fd < 0) { perror("open R"); return (4); }
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) { perror("read"); close(fd); return (5); }
	close(fd);

	if ((size_t)n != plen || memcmp(buf, payload, n) != 0) {
		fprintf(stderr, "mismatch: wrote %zu, read %zd\n", plen, n);
		return (6);
	}
	printf("round-trip OK: %zd bytes\n", n);
	return (0);
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr,
		    "usage: %s read | write TEXT | rt\n", argv[0]);
		return (1);
	}
	if (strcmp(argv[1], "read") == 0)
		return (do_read());
	if (strcmp(argv[1], "write") == 0)
		return (do_write(argc > 2 ? argv[2] : NULL));
	if (strcmp(argv[1], "rt") == 0)
		return (do_roundtrip());
	fprintf(stderr, "unknown mode: %s\n", argv[1]);
	return (1);
}
