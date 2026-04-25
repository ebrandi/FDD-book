/*
 * dtrace_overhead.d - a probe-only DTrace script used to measure
 * the per-syscall overhead of attaching DTrace.
 *
 * The script fires on every syscall entry and return but does not
 * print anything per probe. At exit it prints the total counts.
 * Attach it while the workload programs run and compare their
 * wall-clock times to the baseline.
 *
 * Usage (as root):
 *     dtrace -q -s dtrace_overhead.d
 *
 * Ctrl-C when the workload finishes.
 *
 * Companion to Appendix F of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery".
 */

syscall:::entry
{
	@entries = count();
}

syscall:::return
{
	@returns = count();
}

END
{
	printa("entries=%@d\n", @entries);
	printa("returns=%@d\n", @returns);
}
