/*
 * evdemo_test_poll.c - poll-based reader for the v2.5-async driver.
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
	int fd, ret;
	int count = 0;
	int limit = (argc > 1) ? atoi(argv[1]) : 10;

	signal(SIGINT, on_int);

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		err(1, "open /dev/evdemo");

	printf("poll: waiting for %d events\n", limit);

	while (!stop && count < limit) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		ret = poll(&pfd, 1, 5000);
		if (ret < 0) {
			if (stop || errno == EINTR)
				break;
			err(1, "poll");
		}
		if (ret == 0)
			continue;
		if (pfd.revents & (POLLHUP | POLLNVAL)) {
			printf("poll: device closed\n");
			break;
		}
		if (pfd.revents & POLLIN) {
			n = read(fd, &ev, sizeof(ev));
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				err(1, "read");
			}
			if (n == 0)
				break;
			if (n != sizeof(ev))
				continue;
			printf("poll event %d: type=%u value=%lld\n",
			    count + 1, ev.ev_type,
			    (long long)ev.ev_value);
			count++;
		}
	}
	close(fd);
	printf("poll: received %d events\n", count);
	return (0);
}
