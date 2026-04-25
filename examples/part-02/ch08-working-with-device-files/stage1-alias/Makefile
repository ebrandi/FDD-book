# Makefile for the Chapter 8 stage 1 myfirst driver.
#
# Adds an alias so /dev/myfirst also reaches unit 0.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
