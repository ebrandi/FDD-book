#!/usr/sbin/dtrace -s
/*
 * trace-ioctls.d - print each bugdemo command as it starts and ends.
 *
 * Usage:
 *     # dtrace -s trace-ioctls.d
 */

#pragma D option quiet

sdt:bugdemo::cmd-start
{
	printf("%Y %s:%d start  op=%d\n", walltimestamp, execname, pid, arg1);
}

sdt:bugdemo::cmd-done
{
	printf("%Y %s:%d done   op=%d rv=%d\n",
	    walltimestamp, execname, pid, arg1, arg2);
}
