# Makefile for Chapter 15 lab helpers.

CFLAGS?=	-Wall -Wextra -O2
PROGS=		writer_cap_test signal_read

all: $(PROGS)

writer_cap_test: writer_cap_test.c
	$(CC) $(CFLAGS) -o $@ $<

signal_read: signal_read.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(PROGS)

.PHONY: all clean
