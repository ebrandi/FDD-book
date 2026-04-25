/*
 * evdemo_test_combined.c - combined three-mechanism tester.
 *
 * Forks three children. Each child opens its own fd to /dev/evdemo
 * and watches events through a different mechanism: poll, kqueue,
 * or SIGIO. After a fixed duration, each child reports the number
 * of events it saw. The parent triggers events at a known rate and
 * prints a summary.
 *
 * The goal is to verify that all three mechanisms see the same
 * number of events, which demonstrates that the producer's
 * notifications are complete and that the locking discipline is
 * correct.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "evdemo.h"

static volatile sig_atomic_t pending_sigio;

static void
on_sigio(int sig __attribute__((unused)))
{
	pending_sigio = 1;
}

static int
drain_all(int fd, int *count, int limit)
{
	struct evdemo_event ev;
	ssize_t n;

	for (;;) {
		n = read(fd, &ev, sizeof(ev));
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR)
				return (0);
			return (-1);
		}
		if (n == 0)
			return (1);
		if (n != sizeof(ev))
			return (0);
		(*count)++;
		if (*count >= limit)
			return (1);
	}
}

static int
poll_child(int duration_s)
{
	struct pollfd pfd;
	struct evdemo_event ev;
	int fd, ret, count = 0;
	time_t deadline = time(NULL) + duration_s;

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		warn("poll_child: open");
		return (-1);
	}

	while (time(NULL) < deadline) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		ret = poll(&pfd, 1, 500);
		if (ret < 0 && errno != EINTR)
			break;
		if (ret > 0 && (pfd.revents & POLLIN)) {
			while (read(fd, &ev, sizeof(ev)) == sizeof(ev))
				count++;
		}
	}
	close(fd);
	return (count);
}

static int
kqueue_child(int duration_s)
{
	struct kevent ev_set, ev_got;
	struct evdemo_event ev;
	struct timespec timeout;
	int fd, kq, ret, count = 0;
	time_t deadline = time(NULL) + duration_s;

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		warn("kqueue_child: open");
		return (-1);
	}

	kq = kqueue();
	if (kq < 0) {
		warn("kqueue_child: kqueue");
		close(fd);
		return (-1);
	}

	EV_SET(&ev_set, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	(void)kevent(kq, &ev_set, 1, NULL, 0, NULL);

	while (time(NULL) < deadline) {
		timeout.tv_sec = 0;
		timeout.tv_nsec = 500 * 1000 * 1000;
		ret = kevent(kq, NULL, 0, &ev_got, 1, &timeout);
		if (ret < 0 && errno != EINTR)
			break;
		if (ret > 0) {
			if (ev_got.flags & EV_EOF)
				break;
			while (read(fd, &ev, sizeof(ev)) == sizeof(ev))
				count++;
		}
	}

	close(kq);
	close(fd);
	return (count);
}

static int
sigio_child(int duration_s)
{
	struct evdemo_event ev;
	struct sigaction sa;
	pid_t pid;
	int fd, flag, count = 0;
	time_t deadline = time(NULL) + duration_s;

	sa.sa_handler = on_sigio;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGIO, &sa, NULL) < 0) {
		warn("sigio_child: sigaction");
		return (-1);
	}

	fd = open("/dev/evdemo", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		warn("sigio_child: open");
		return (-1);
	}

	pid = getpid();
	if (ioctl(fd, FIOSETOWN, &pid) < 0) {
		warn("sigio_child: FIOSETOWN");
		close(fd);
		return (-1);
	}

	flag = 1;
	if (ioctl(fd, FIOASYNC, &flag) < 0) {
		warn("sigio_child: FIOASYNC");
		close(fd);
		return (-1);
	}

	while (time(NULL) < deadline) {
		if (pending_sigio) {
			pending_sigio = 0;
			while (read(fd, &ev, sizeof(ev)) == sizeof(ev))
				count++;
		} else {
			struct timespec ts = { 0, 100 * 1000 * 1000 };
			nanosleep(&ts, NULL);
		}
	}

	flag = 0;
	(void)ioctl(fd, FIOASYNC, &flag);
	close(fd);
	return (count);
}

int
main(int argc, char **argv)
{
	pid_t poll_pid, kq_pid, sig_pid;
	int status;
	int duration = (argc > 1) ? atoi(argv[1]) : 5;
	int burst = (argc > 2) ? atoi(argv[2]) : 100;
	int rounds = (argc > 3) ? atoi(argv[3]) : 5;

	if (duration < 1)
		duration = 1;
	if (burst < 1)
		burst = 10;

	printf("combined: duration=%ds burst=%d rounds=%d\n",
	    duration, burst, rounds);

	poll_pid = fork();
	if (poll_pid == 0) {
		int n = poll_child(duration);
		printf("poll child: %d events\n", n);
		exit(0);
	}
	if (poll_pid < 0)
		err(1, "fork poll_child");

	kq_pid = fork();
	if (kq_pid == 0) {
		int n = kqueue_child(duration);
		printf("kqueue child: %d events\n", n);
		exit(0);
	}
	if (kq_pid < 0)
		err(1, "fork kqueue_child");

	sig_pid = fork();
	if (sig_pid == 0) {
		int n = sigio_child(duration);
		printf("sigio child: %d events\n", n);
		exit(0);
	}
	if (sig_pid < 0)
		err(1, "fork sigio_child");

	/*
	 * Parent triggers bursts. Give children a moment to open and
	 * register before posting events.
	 */
	struct timespec ts = { 0, 500 * 1000 * 1000 };
	nanosleep(&ts, NULL);

	int i;
	char cmd[128];
	for (i = 0; i < rounds; i++) {
		snprintf(cmd, sizeof(cmd),
		    "sysctl dev.evdemo.burst=%d >/dev/null", burst);
		(void)system(cmd);
		nanosleep(&ts, NULL);
	}

	waitpid(poll_pid, &status, 0);
	waitpid(kq_pid, &status, 0);
	waitpid(sig_pid, &status, 0);

	printf("combined: done. Compare the three counts: they should "
	    "all match roughly the total of %d burst events plus %d "
	    "tick events.\n", rounds * burst, duration);
	return (0);
}
