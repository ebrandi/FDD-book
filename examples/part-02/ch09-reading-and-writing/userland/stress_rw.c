/*
 * stress_rw.c - simple producer/consumer stress test for the
 * Chapter 9 myfirst Stage 3 driver.
 *
 * Usage:
 *   stress_rw            -- fork one writer and one reader for 2 seconds
 *   stress_rw -s SECONDS -- run for the given number of seconds
 *
 * The writer pushes fixed 64-byte chunks as fast as the driver will
 * accept them; the reader drains as fast as the driver produces.
 * Both processes exit after SECONDS and report their totals.
 *
 * Build: cc -o stress_rw stress_rw.c
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#define	DEVPATH	"/dev/myfirst/0"
#define	CHUNK	64

static volatile int running;

static int
run_writer(int seconds)
{
	int fd;
	unsigned char chunk[CHUNK];
	size_t total;
	ssize_t n;
	time_t deadline;
	int i;

	for (i = 0; i < CHUNK; i++)
		chunk[i] = (unsigned char)('A' + (i % 26));

	fd = open(DEVPATH, O_WRONLY);
	if (fd < 0) { perror("writer open"); return (1); }

	deadline = time(NULL) + seconds;
	total = 0;
	while (time(NULL) < deadline) {
		n = write(fd, chunk, CHUNK);
		if (n < 0) {
			if (errno == ENOSPC) {
				usleep(1000);
				continue;
			}
			perror("writer write");
			break;
		}
		total += n;
	}
	close(fd);
	printf("[writer] total bytes written: %zu\n", total);
	return (0);
}

static int
run_reader(int seconds)
{
	int fd;
	unsigned char chunk[CHUNK];
	size_t total;
	ssize_t n;
	time_t deadline;

	fd = open(DEVPATH, O_RDONLY);
	if (fd < 0) { perror("reader open"); return (1); }

	deadline = time(NULL) + seconds;
	total = 0;
	while (time(NULL) < deadline) {
		n = read(fd, chunk, sizeof(chunk));
		if (n < 0) {
			perror("reader read");
			break;
		}
		if (n == 0) {
			usleep(1000);
			continue;
		}
		total += n;
	}
	close(fd);
	printf("[reader] total bytes read: %zu\n", total);
	return (0);
}

int
main(int argc, char *argv[])
{
	int seconds = 2;
	int opt;
	pid_t writer, reader;

	while ((opt = getopt(argc, argv, "s:")) != -1) {
		switch (opt) {
		case 's':
			seconds = atoi(optarg);
			if (seconds <= 0) {
				fprintf(stderr,
				    "bad seconds: %s\n", optarg);
				return (1);
			}
			break;
		default:
			fprintf(stderr,
			    "usage: %s [-s SECONDS]\n", argv[0]);
			return (1);
		}
	}

	printf("[stress] running for %d second(s) against %s\n",
	    seconds, DEVPATH);

	writer = fork();
	if (writer < 0) { perror("fork writer"); return (2); }
	if (writer == 0)
		return (run_writer(seconds));

	reader = fork();
	if (reader < 0) { perror("fork reader"); return (3); }
	if (reader == 0)
		return (run_reader(seconds));

	while (waitpid(-1, NULL, 0) > 0)
		;

	return (0);
}
