/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * mp_stress.c: a multi-process stress tester for /dev/myfirst.
 *
 * Forks NWRITERS writer children and NREADERS reader children.
 * Each runs for SECONDS seconds and reports its total byte count.
 * The sum across writers should match the sum across readers plus
 * whatever remains in the buffer when the run ends.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH		"/dev/myfirst"
#define	NWRITERS	2
#define	NREADERS	2
#define	SECONDS		30

static volatile sig_atomic_t stop;

static void
sigalrm(int s __unused)
{
	stop = 1;
}

static int
child_writer(int id)
{
	int fd;
	char buf[1024];
	unsigned long long written = 0;

	fd = open(DEVPATH, O_WRONLY);
	if (fd < 0)
		err(1, "writer %d: open", id);

	memset(buf, 'a' + id, sizeof(buf));

	while (!stop) {
		ssize_t n = write(fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		written += n;
	}
	close(fd);
	printf("writer %d: %llu bytes\n", id, written);
	return (0);
}

static int
child_reader(int id)
{
	int fd;
	char buf[1024];
	unsigned long long got = 0;

	fd = open(DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "reader %d: open", id);

	while (!stop) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		got += n;
	}
	close(fd);
	printf("reader %d: %llu bytes\n", id, got);
	return (0);
}

int
main(void)
{
	pid_t pids[NWRITERS + NREADERS];
	int n = 0;

	signal(SIGALRM, sigalrm);

	for (int i = 0; i < NWRITERS; i++) {
		pid_t pid = fork();
		if (pid < 0)
			err(1, "fork");
		if (pid == 0) {
			signal(SIGALRM, sigalrm);
			alarm(SECONDS);
			_exit(child_writer(i));
		}
		pids[n++] = pid;
	}
	for (int i = 0; i < NREADERS; i++) {
		pid_t pid = fork();
		if (pid < 0)
			err(1, "fork");
		if (pid == 0) {
			signal(SIGALRM, sigalrm);
			alarm(SECONDS);
			_exit(child_reader(i));
		}
		pids[n++] = pid;
	}

	for (int i = 0; i < n; i++) {
		int status;
		waitpid(pids[i], &status, 0);
	}
	return (0);
}
