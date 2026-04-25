#!/usr/sbin/dtrace -s
/*
 * profile-sample.d - kernel-wide profile sample at 997 Hz.
 *
 * Usage: sudo dtrace -s profile-sample.d
 *
 * Run a workload in another terminal. Let this script run for
 * 30 to 60 seconds, then Ctrl-C. The output is a list of stack
 * traces ordered by sample count.
 *
 * 997 Hz is deliberately not 1000 Hz. Using a prime number avoids
 * interference with any 1 Hz, 10 Hz, 100 Hz, or 1000 Hz periodic
 * activity that might create systematic bias.
 */

#pragma D option quiet

profile-997
{
	@[stack()] = count();
}

END
{
	trunc(@, 20);
	printa(@);
}
