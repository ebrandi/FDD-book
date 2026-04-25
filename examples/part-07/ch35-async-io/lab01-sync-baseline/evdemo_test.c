/*
 * evdemo_test.c - Lab 1 companion user-space program.
 *
 * Opens /dev/evdemo and calls read() in a loop. Each read blocks
 * until the driver posts an event. The program prints each event
 * and exits after receiving five events, or on Ctrl+C.
 */

#include <err.h>
#include <fcntl.h>
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
	struct evdemo_event ev;
	ssize_t n;
	int fd;
	int count = 0;
	int limit = (argc > 1) ? atoi(argv[1]) : 5;

	signal(SIGINT, on_int);

	fd = open("/dev/evdemo", O_RDONLY);
	if (fd < 0)
		err(1, "open /dev/evdemo");

	printf("evdemo_test: reading up to %d events (Ctrl+C to stop)\n",
	    limit);

	while (!stop && count < limit) {
		n = read(fd, &ev, sizeof(ev));
		if (n < 0) {
			if (stop)
				break;
			err(1, "read");
		}
		if (n == 0) {
			printf("evdemo_test: end of file\n");
			break;
		}
		if (n != sizeof(ev)) {
			warnx("short read: %zd bytes", n);
			continue;
		}
		printf("event: time=%jd.%09ld type=%u code=%u value=%lld\n",
		    (intmax_t)ev.ev_time.tv_sec, ev.ev_time.tv_nsec,
		    ev.ev_type, ev.ev_code, (long long)ev.ev_value);
		count++;
	}

	close(fd);
	printf("evdemo_test: read %d events\n", count);
	return (0);
}
