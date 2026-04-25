# Makefile for the Chapter 9 Stage 1 myfirst driver.
#
# Serves a static kernel-space message through uiomove_frombuf(9).

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
