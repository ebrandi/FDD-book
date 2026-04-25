/*
 * evdemo_test_sigio.c - SIGIO-based reader for the v2.5-async
 * driver.
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
	int fd, flag;
	int count = 0;
	int limit = (argc > 1) ? atoi(argv[1]) : 10;

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

	printf("sigio: waiting for %d events\n", limit);

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
			if (n == 0)
				goto done;
			if (n != sizeof(ev))
				break;
			printf("sigio event %d: type=%u value=%lld\n",
			    count + 1, ev.ev_type,
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
	printf("sigio: received %d events\n", count);
	return (0);
}
