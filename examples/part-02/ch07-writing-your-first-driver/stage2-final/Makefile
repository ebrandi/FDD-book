# Makefile for myfirst driver
#
# Stage 2 of Chapter 7: complete driver with softc, lock, /dev node,
# read-only sysctls, and a single-label unwind in attach.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
