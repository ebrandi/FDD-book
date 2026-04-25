#!/usr/sbin/dtrace -s
/*
 * lockstat-simple.d - count lock acquisitions in and around perfdemo_read.
 *
 * Usage: sudo dtrace -s lockstat-simple.d
 *
 * Prints, per lock, the number of times the lock was held while
 * perfdemo_read was on-CPU. A lock that appears here many times is
 * touched by the read path.
 *
 * Note: on a perfdemo built without its own mutex, the output will
 * show only the locks touched by common kernel code (sleepq locks,
 * sched_lock, etc.). A driver with its own mutex under load will
 * show that mutex prominently.
 */

#pragma D option quiet

fbt:perfdemo:perfdemo_read:entry
{
	self->in_read = 1;
}

fbt:perfdemo:perfdemo_read:return
{
	self->in_read = 0;
}

lockstat:::adaptive-acquire
/self->in_read/
{
	@[probefunc, stringof(args[0]->lock_object.lo_name)] =
	    count();
}

END
{
	printf("%-30s %-30s %s\n", "caller", "lock", "count");
	printa("%-30s %-30s %@d\n", @);
}
