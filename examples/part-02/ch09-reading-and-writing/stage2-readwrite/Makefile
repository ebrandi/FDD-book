# Makefile for the Chapter 9 Stage 2 myfirst driver.
#
# Introduces a malloc(9)-backed kernel buffer that d_write appends
# into and d_read serves out of, with per-descriptor offsets.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
