# Makefile for the userland cbuf test program.

PROG=	cb_test
SRCS=	cbuf.c cb_test.c
CFLAGS?= -O2 -Wall -Wextra

.PHONY: all clean
all: ${PROG}

${PROG}: ${SRCS} cbuf.h
	${CC} ${CFLAGS} -o $@ ${SRCS}

clean:
	rm -f ${PROG}
