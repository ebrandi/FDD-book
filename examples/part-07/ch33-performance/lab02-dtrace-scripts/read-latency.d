#!/usr/sbin/dtrace -s
/*
 * read-latency.d - histogram the latency of perfdemo_read in nanoseconds.
 *
 * Usage: sudo dtrace -s read-latency.d
 *
 * Run a workload against /dev/perfdemo in another terminal. Let this
 * script run for 30 seconds, then Ctrl-C. The output is a quantize()
 * histogram of latencies in ns, bucketed by power of two.
 */

#pragma D option quiet
#pragma D option dynvarsize=16m

fbt:perfdemo:perfdemo_read:entry
{
	self->ts = timestamp;
}

fbt:perfdemo:perfdemo_read:return
/self->ts/
{
	@ = quantize(timestamp - self->ts);
	self->ts = 0;
}

END
{
	printa(@);
}
