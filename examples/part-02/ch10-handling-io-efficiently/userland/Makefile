# Makefile for Chapter 10 userland test programs.

PROGS=	rw_myfirst_v2 rw_myfirst_nb producer_consumer stress_rw mmap_myfirst

CFLAGS?= -O2 -Wall -Wextra -Wno-unused-parameter

.PHONY: all clean
all: ${PROGS}

rw_myfirst_v2: rw_myfirst_v2.c
	${CC} ${CFLAGS} -o $@ $<

rw_myfirst_nb: rw_myfirst_nb.c
	${CC} ${CFLAGS} -o $@ $<

producer_consumer: producer_consumer.c
	${CC} ${CFLAGS} -o $@ $<

stress_rw: stress_rw.c
	${CC} ${CFLAGS} -o $@ $<

mmap_myfirst: mmap_myfirst.c
	${CC} ${CFLAGS} -o $@ $<

clean:
	rm -f ${PROGS}
