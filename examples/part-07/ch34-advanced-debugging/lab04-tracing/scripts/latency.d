#!/usr/sbin/dtrace -s
/*
 * latency.d - measure ioctl latency per op.
 *
 * Usage:
 *     # dtrace -s latency.d
 *
 * Ctrl-C to print the aggregation.
 */

#pragma D option quiet

sdt:bugdemo::cmd-start
{
	self->start = timestamp;
	self->op = arg1;
}

sdt:bugdemo::cmd-done
/self->start != 0/
{
	@lat[self->op] = quantize(timestamp - self->start);
	self->start = 0;
	self->op = 0;
}

END
{
	printa("op=%d latency(ns):%@d\n", @lat);
}
