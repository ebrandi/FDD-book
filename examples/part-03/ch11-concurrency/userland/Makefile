# Makefile for Chapter 11 userland concurrency testers.

PROGS=	mp_stress mt_reader lat_tester

.PHONY: all
all: ${PROGS}

CFLAGS?= -O2 -Wall -Wextra -Wno-unused-parameter

mp_stress: mp_stress.c
	${CC} ${CFLAGS} -o $@ $<

mt_reader: mt_reader.c
	${CC} ${CFLAGS} -pthread -o $@ $<

lat_tester: lat_tester.c
	${CC} ${CFLAGS} -o $@ $<

.PHONY: clean
clean:
	rm -f ${PROGS}
