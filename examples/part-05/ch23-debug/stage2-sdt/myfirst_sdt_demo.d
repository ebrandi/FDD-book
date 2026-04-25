/*
 * myfirst_sdt_demo.d - a small DTrace script that demonstrates attaching
 * to the SDT probes in the myfirst driver.
 *
 * Run with:   sudo dtrace -s myfirst_sdt_demo.d
 */

#pragma D option quiet

BEGIN
{
	printf("myfirst DTrace demo: counting open, close, and io probes\n");
	printf("press Ctrl-C to stop\n");
}

myfirst:::open
{
	@opens[pid] = count();
}

myfirst:::close
{
	@closes[pid] = count();
}

myfirst:::io
{
	@io_bytes = sum(arg2);
	@io_events = count();
}

END
{
	printf("\n=== Opens per PID ===\n");
	printa("  pid=%-10d count=%@d\n", @opens);

	printf("\n=== Closes per PID ===\n");
	printa("  pid=%-10d count=%@d\n", @closes);

	printf("\n=== Total I/O ===\n");
	printa("  events=%@d\n", @io_events);
	printa("  bytes=%@d\n", @io_bytes);
}
