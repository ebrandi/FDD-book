# Makefile for the Chapter 16 Stage 4 myfirst driver.
#
# Adds the Chapter 16 hardware-access layer (myfirst_hw.c) to the
# Chapter 15 Stage 4 source list. The additional include flag allows
# myfirst_hw.c to pick up the shared myfirst.h header.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c myfirst_hw.c
SRCS+=   device_if.h bus_if.h

CFLAGS+= -I.

.include <bsd.kmod.mk>
