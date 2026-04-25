# Makefile for Chapter 8 userland probes.
#
# Plain userland C programs, not kernel modules. Written to be
# portable between FreeBSD's bmake and GNU make.

CFLAGS?= -Wall -Werror

all: probe_myfirst hold_myfirst stat_myfirst parallel_probe stress_probe

probe_myfirst: probe_myfirst.c
	$(CC) $(CFLAGS) -o probe_myfirst probe_myfirst.c

hold_myfirst: hold_myfirst.c
	$(CC) $(CFLAGS) -o hold_myfirst hold_myfirst.c

stat_myfirst: stat_myfirst.c
	$(CC) $(CFLAGS) -o stat_myfirst stat_myfirst.c

parallel_probe: parallel_probe.c
	$(CC) $(CFLAGS) -o parallel_probe parallel_probe.c

stress_probe: stress_probe.c
	$(CC) $(CFLAGS) -o stress_probe stress_probe.c

clean:
	rm -f probe_myfirst hold_myfirst stat_myfirst parallel_probe stress_probe

.PHONY: all clean
