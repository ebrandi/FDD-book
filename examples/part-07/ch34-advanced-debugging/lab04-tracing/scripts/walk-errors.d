#!/usr/sbin/dtrace -s
/*
 * walk-errors.d - print a stack trace each time bugdemo returns an
 *                 error.
 *
 * Usage:
 *     # dtrace -s walk-errors.d
 */

#pragma D option quiet

sdt:bugdemo::cmd-error
{
	printf("error op=%d at %Y by %s:%d\n",
	    arg1, walltimestamp, execname, pid);
	stack();
	printf("\n");
}
