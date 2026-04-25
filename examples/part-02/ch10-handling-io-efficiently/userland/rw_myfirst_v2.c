/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * rw_myfirst_v2.c: sized fill/drain tester for /dev/myfirst.
 *
 * Usage:
 *   rw_myfirst_v2 fill BYTES
 *   rw_myfirst_v2 drain BYTES
 *
 * "fill" writes a known per-position pattern up to BYTES bytes.
 * "drain" reads up to BYTES bytes and reports the count.
 * Both honour partial transfers via the read_all/write_all loops.
 */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH "/dev/myfirst"

static int
do_fill(size_t bytes)
{
	int fd = open(DEVPATH, O_WRONLY);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	char *buf = malloc(bytes);
	if (buf == NULL)
		err(1, "malloc %zu", bytes);
	for (size_t i = 0; i < bytes; i++)
		buf[i] = (char)('A' + (i % 26));

	size_t left = bytes;
	const char *p = buf;
	while (left > 0) {
		ssize_t n = write(fd, p, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			warn("write at %zu left", left);
			break;
		}
		p += n;
		left -= n;
	}
	size_t wrote = bytes - left;
	printf("fill: wrote %zu of %zu\n", wrote, bytes);
	free(buf);
	close(fd);
	return (0);
}

static int
do_drain(size_t bytes)
{
	int fd = open(DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	char *buf = malloc(bytes);
	if (buf == NULL)
		err(1, "malloc %zu", bytes);

	size_t left = bytes;
	char *p = buf;
	while (left > 0) {
		ssize_t n = read(fd, p, left);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			warn("read at %zu left", left);
			break;
		}
		if (n == 0) {
			printf("drain: EOF at %zu left\n", left);
			break;
		}
		p += n;
		left -= n;
	}
	size_t got = bytes - left;
	printf("drain: read %zu of %zu\n", got, bytes);
	free(buf);
	close(fd);
	return (0);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s fill|drain BYTES\n", argv[0]);
		return (1);
	}
	size_t bytes = strtoul(argv[2], NULL, 0);
	if (strcmp(argv[1], "fill") == 0)
		return (do_fill(bytes));
	if (strcmp(argv[1], "drain") == 0)
		return (do_drain(bytes));
	fprintf(stderr, "unknown mode: %s\n", argv[1]);
	return (1);
}
