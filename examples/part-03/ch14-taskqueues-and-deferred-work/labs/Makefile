# Makefile for the Chapter 14 lab helpers.

CFLAGS?=	-Wall -Wextra -O2
PROGS=		poll_waiter

all: $(PROGS)

poll_waiter: poll_waiter.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)

.PHONY: all clean
