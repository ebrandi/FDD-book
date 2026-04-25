/*
 * evdemo_watch.c - Lab 5 companion diagnostic tool.
 *
 * Polls the dev.evdemo.stats.* sysctl counters once per second and
 * prints them in a compact table. Useful for watching the queue's
 * behaviour while triggering events or bursts in another terminal.
 */

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t stop;

static void
on_int(int sig __attribute__((unused)))
{
	stop = 1;
}

static u_int
get_uint(const char *name)
{
	u_int value = 0;
	size_t len = sizeof(value);

	if (sysctlbyname(name, &value, &len, NULL, 0) < 0)
		return (0);
	return (value);
}

int
main(int argc, char **argv)
{
	int interval = (argc > 1) ? atoi(argv[1]) : 1;
	int i = 0;

	if (interval < 1)
		interval = 1;

	signal(SIGINT, on_int);

	while (!stop) {
		if ((i % 20) == 0) {
			printf("%8s %8s %8s %8s %8s %8s %8s\n",
			    "qlen", "posted", "consumed", "dropped",
			    "selwake", "knotes", "sigio");
		}
		printf("%8u %8u %8u %8u %8u %8u %8u\n",
		    get_uint("dev.evdemo.stats.qlen"),
		    get_uint("dev.evdemo.stats.posted"),
		    get_uint("dev.evdemo.stats.consumed"),
		    get_uint("dev.evdemo.stats.dropped"),
		    get_uint("dev.evdemo.stats.selwakeups"),
		    get_uint("dev.evdemo.stats.knotes_delivered"),
		    get_uint("dev.evdemo.stats.sigio_sent"));
		fflush(stdout);
		sleep((unsigned)interval);
		i++;
	}
	return (0);
}
