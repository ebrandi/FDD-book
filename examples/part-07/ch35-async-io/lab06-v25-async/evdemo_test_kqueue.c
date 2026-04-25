/*
 * evdemo_test_kqueue.c - kqueue-based reader for the v2.5-async
 * driver.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
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
	struct kevent ev_set, ev_got;
	struct evdemo_event event;
	struct timespec timeout;
	ssize_t n;
	int fd, kq, ret;
	int count = 0;
	int limit = (argc > 1) ? atoi(argv[1]) : 10;

	signal(SIGINT, on_int);

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		err(1, "open /dev/evdemo");

	kq = kqueue();
	if (kq < 0)
		err(1, "kqueue");

	EV_SET(&ev_set, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(kq, &ev_set, 1, NULL, 0, NULL) < 0)
		err(1, "kevent add");

	printf("kqueue: waiting for %d events\n", limit);

	while (!stop && count < limit) {
		timeout.tv_sec = 5;
		timeout.tv_nsec = 0;
		ret = kevent(kq, NULL, 0, &ev_got, 1, &timeout);
		if (ret < 0) {
			if (stop || errno == EINTR)
				break;
			err(1, "kevent wait");
		}
		if (ret == 0)
			continue;
		if (ev_got.flags & EV_EOF) {
			printf("kqueue: device closed\n");
			break;
		}

		while (count < limit) {
			n = read(fd, &event, sizeof(event));
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR)
					break;
				err(1, "read");
			}
			if (n == 0)
				goto done;
			if (n != sizeof(event))
				break;
			printf("kqueue event %d: type=%u value=%lld\n",
			    count + 1, event.ev_type,
			    (long long)event.ev_value);
			count++;
		}
	}

done:
	close(kq);
	close(fd);
	printf("kqueue: received %d events\n", count);
	return (0);
}
