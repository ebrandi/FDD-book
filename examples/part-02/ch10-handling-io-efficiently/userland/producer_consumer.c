/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * producer_consumer.c: a two-process load test for /dev/myfirst.
 *
 * Forks a child writer and uses the parent as the reader.  Each side
 * generates / verifies a known per-position byte pattern and computes
 * a rolling checksum.  Mismatches and checksum disagreements indicate
 * either a wrap-around bug or a locking bug in the driver.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH		"/dev/myfirst"
#define	TOTAL_BYTES	(1024 * 1024)
#define	BLOCK		4096

static uint32_t
checksum(const char *p, size_t n)
{
	uint32_t s = 0;
	for (size_t i = 0; i < n; i++)
		s = s * 31u + (uint8_t)p[i];
	return (s);
}

static int
do_writer(void)
{
	int fd = open(DEVPATH, O_WRONLY);
	if (fd < 0)
		err(1, "writer: open");

	char *buf = malloc(BLOCK);
	if (buf == NULL)
		err(1, "writer: malloc");

	size_t written = 0;
	uint32_t sum = 0;
	while (written < TOTAL_BYTES) {
		size_t left = TOTAL_BYTES - written;
		size_t block = left < BLOCK ? left : BLOCK;
		for (size_t i = 0; i < block; i++)
			buf[i] = (char)((written + i) & 0xff);
		sum += checksum(buf, block);

		const char *p = buf;
		size_t remain = block;
		while (remain > 0) {
			ssize_t n = write(fd, p, remain);
			if (n < 0) {
				if (errno == EINTR)
					continue;
				warn("writer: write");
				close(fd);
				free(buf);
				return (1);
			}
			p += n;
			remain -= n;
		}
		written += block;
	}

	printf("writer: %zu bytes, checksum 0x%08x\n", written, sum);
	close(fd);
	free(buf);
	return (0);
}

static int
do_reader(void)
{
	int fd = open(DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "reader: open");

	char *buf = malloc(BLOCK);
	if (buf == NULL)
		err(1, "reader: malloc");

	size_t got = 0;
	uint32_t sum = 0;
	int mismatches = 0;
	while (got < TOTAL_BYTES) {
		ssize_t n = read(fd, buf, BLOCK);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			warn("reader: read");
			break;
		}
		if (n == 0) {
			printf("reader: EOF at %zu\n", got);
			break;
		}
		for (ssize_t i = 0; i < n; i++) {
			if ((uint8_t)buf[i] != (uint8_t)((got + i) & 0xff))
				mismatches++;
		}
		sum += checksum(buf, n);
		got += n;
	}

	printf("reader: %zu bytes, checksum 0x%08x, mismatches %d\n",
	    got, sum, mismatches);
	close(fd);
	free(buf);
	return (mismatches == 0 ? 0 : 2);
}

int
main(void)
{
	pid_t pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid == 0)
		_exit(do_writer());

	int rc = do_reader();
	int status;
	waitpid(pid, &status, 0);
	int wexit = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	printf("exit: reader=%d writer=%d\n", rc, wexit);
	return (rc || wexit);
}
