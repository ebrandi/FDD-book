# Makefile for Chapter 9 userland exercisers.
#
# Plain userland C programs, not kernel modules. Written to be
# portable between FreeBSD's bmake and GNU make.

CFLAGS?= -Wall -Werror

all: rw_myfirst stress_rw

rw_myfirst: rw_myfirst.c
	$(CC) $(CFLAGS) -o rw_myfirst rw_myfirst.c

stress_rw: stress_rw.c
	$(CC) $(CFLAGS) -o stress_rw stress_rw.c

clean:
	rm -f rw_myfirst stress_rw

.PHONY: all clean
