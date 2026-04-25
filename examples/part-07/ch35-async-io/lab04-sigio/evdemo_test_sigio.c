/*
 * evdemo_test_sigio.c - Lab 4 companion user-space program.
 *
 * Opens /dev/evdemo, declares the process as the signal owner via
 * FIOSETOWN, enables SIGIO delivery with FIOASYNC, and then loops
 * in pause() waiting for SIGIO. The SIGIO handler sets a flag; the
 * main loop reads events when the flag is set.
 *
 * This is the classical pattern for SIGIO-driven userland programs.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "evdemo.h"

static volatile sig_atomic_t pending;
static volatile sig_atomic_t stop;

static void
on_sigio(int sig __attribute__((unused)))
{
	pending = 1;
}

static void
on_int(int sig __attribute__((unused)))
{
	stop = 1;
}

int
main(int argc, char **argv)
{
	struct evdemo_event ev;
	struct sigaction sa;
	ssize_t n;
	pid_t pid;
	int fd;
	int flag;
	int count = 0;
	int limit = (argc > 1) ? atoi(argv[1]) : 5;

	sa.sa_handler = on_sigio;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGIO, &sa, NULL) < 0)
		err(1, "sigaction SIGIO");

	signal(SIGINT, on_int);

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		err(1, "open /dev/evdemo");

	pid = getpid();
	if (ioctl(fd, FIOSETOWN, &pid) < 0)
		err(1, "FIOSETOWN");

	flag = 1;
	if (ioctl(fd, FIOASYNC, &flag) < 0)
		err(1, "FIOASYNC");

	printf("evdemo_test_sigio: waiting for %d events (pid=%d)\n",
	    limit, (int)pid);

	while (!stop && count < limit) {
		if (!pending) {
			pause();
			continue;
		}
		pending = 0;

		for (;;) {
			n = read(fd, &ev, sizeof(ev));
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR)
					break;
				err(1, "read");
			}
			if (n == 0) {
				printf("evdemo_test_sigio: end of file\n");
				goto done;
			}
			if (n != sizeof(ev)) {
				warnx("short read: %zd", n);
				break;
			}
			printf("event: time=%jd.%09ld type=%u code=%u "
			    "value=%lld\n",
			    (intmax_t)ev.ev_time.tv_sec,
			    ev.ev_time.tv_nsec,
			    ev.ev_type, ev.ev_code,
			    (long long)ev.ev_value);
			count++;
			if (count >= limit)
				break;
		}
	}

done:
	flag = 0;
	(void)ioctl(fd, FIOASYNC, &flag);
	close(fd);
	printf("evdemo_test_sigio: read %d events\n", count);
	return (0);
}
