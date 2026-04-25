# Makefile for Chapter 12 userland synchronization testers.

PROGS=	timeout_tester config_writer

.PHONY: all
all: ${PROGS}

CFLAGS?= -O2 -Wall -Wextra -Wno-unused-parameter

timeout_tester: timeout_tester.c
	${CC} ${CFLAGS} -o $@ $<

config_writer: config_writer.c
	${CC} ${CFLAGS} -o $@ $<

.PHONY: clean
clean:
	rm -f ${PROGS}
