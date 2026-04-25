/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * config_writer.c: stress the Chapter 12 driver's configuration
 * sysctls by toggling debug_level and soft_byte_limit at high
 * frequency for a configurable duration.
 *
 * Usage:
 *   config_writer [seconds]
 *
 * Default duration is 30 seconds. Run in parallel with mp_stress
 * (from Chapter 11) to verify that data-path I/O and configuration
 * updates can coexist without lock-order reversals.
 */

#include <sys/sysctl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int seconds = (argc > 1) ? atoi(argv[1]) : 30;
	time_t end = time(NULL) + seconds;
	int v = 0;
	int limit;

	while (time(NULL) < end) {
		v = (v + 1) % 4;
		if (sysctlbyname("dev.myfirst.0.debug_level",
		    NULL, NULL, &v, sizeof(v)) != 0)
			warn("sysctl debug_level");

		limit = (v == 0) ? 0 : 4096;
		if (sysctlbyname("dev.myfirst.0.soft_byte_limit",
		    NULL, NULL, &limit, sizeof(limit)) != 0)
			warn("sysctl soft_byte_limit");

		usleep(10000);  /* 10 ms */
	}
	return (0);
}
