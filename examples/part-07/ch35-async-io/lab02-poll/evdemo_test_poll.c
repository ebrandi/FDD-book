/*
 * evdemo_test_poll.c - Lab 2 companion user-space program.
 *
 * Opens /dev/evdemo and calls poll() with a timeout. When poll()
 * reports POLLIN, reads one event and prints it. Loops until it has
 * seen a fixed number of events or until the user presses Ctrl+C.
 *
 * This is the poll-based analogue of Lab 1's blocking read loop.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "evdemo.h"

static volatile sig_atomic_t stop;

static void
on_int(int sig __attribute__((unused)))
{
	stop = 1;
}

int
main(int argc, char **argv)
{
	struct pollfd pfd;
	struct evdemo_event ev;
	ssize_t n;
	int fd;
	int ret;
	int count = 0;
	int limit = (argc > 1) ? atoi(argv[1]) : 5;
	int timeout_ms = 5000;

	signal(SIGINT, on_int);

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		err(1, "open /dev/evdemo");

	printf("evdemo_test_poll: waiting for %d events (timeout %d ms)\n",
	    limit, timeout_ms);

	while (!stop && count < limit) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			if (stop || errno == EINTR)
				break;
			err(1, "poll");
		}
		if (ret == 0) {
			printf("evdemo_test_poll: poll timeout\n");
			continue;
		}
		if (pfd.revents & POLLIN) {
			n = read(fd, &ev, sizeof(ev));
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				err(1, "read");
			}
			if (n == 0) {
				printf("evdemo_test_poll: end of file\n");
				break;
			}
			if (n != sizeof(ev)) {
				warnx("short read: %zd", n);
				continue;
			}
			printf("event: time=%jd.%09ld type=%u code=%u "
			    "value=%lld\n",
			    (intmax_t)ev.ev_time.tv_sec,
			    ev.ev_time.tv_nsec,
			    ev.ev_type, ev.ev_code,
			    (long long)ev.ev_value);
			count++;
		}
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			warnx("poll flagged error/hangup: 0x%x", pfd.revents);
			break;
		}
	}

	close(fd);
	printf("evdemo_test_poll: read %d events\n", count);
	return (0);
}
