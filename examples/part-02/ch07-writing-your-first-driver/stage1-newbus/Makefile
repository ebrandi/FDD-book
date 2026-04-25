# Makefile for myfirst driver
#
# Stage 1 of Chapter 7: Newbus pseudo-device with identify, probe,
# attach, and detach. No /dev node and no sysctls yet.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
