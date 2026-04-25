/*
 * evdemo_test_kqueue.c - Lab 3 companion user-space program.
 *
 * Opens /dev/evdemo, creates a kqueue, registers an EVFILT_READ
 * filter on the device descriptor, and loops calling kevent(2) to
 * receive events. Prints each event and exits after seeing a fixed
 * number of them or on Ctrl+C.
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
	int limit = (argc > 1) ? atoi(argv[1]) : 5;

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

	printf("evdemo_test_kqueue: waiting for %d events\n", limit);

	while (!stop && count < limit) {
		timeout.tv_sec = 5;
		timeout.tv_nsec = 0;
		ret = kevent(kq, NULL, 0, &ev_got, 1, &timeout);
		if (ret < 0) {
			if (stop || errno == EINTR)
				break;
			err(1, "kevent wait");
		}
		if (ret == 0) {
			printf("evdemo_test_kqueue: kevent timeout\n");
			continue;
		}

		if (ev_got.flags & EV_EOF) {
			printf("evdemo_test_kqueue: device closed (EV_EOF)\n");
			break;
		}

		printf("kqueue: %lld events queued in driver\n",
		    (long long)ev_got.data);

		while (count < limit) {
			n = read(fd, &event, sizeof(event));
			if (n < 0) {
				if (errno == EAGAIN || errno == EINTR)
					break;
				err(1, "read");
			}
			if (n == 0) {
				printf("evdemo_test_kqueue: end of file\n");
				goto done;
			}
			if (n != sizeof(event)) {
				warnx("short read: %zd", n);
				break;
			}
			printf("event: time=%jd.%09ld type=%u code=%u "
			    "value=%lld\n",
			    (intmax_t)event.ev_time.tv_sec,
			    event.ev_time.tv_nsec,
			    event.ev_type, event.ev_code,
			    (long long)event.ev_value);
			count++;
		}
	}

done:
	close(kq);
	close(fd);
	printf("evdemo_test_kqueue: read %d events\n", count);
	return (0);
}
