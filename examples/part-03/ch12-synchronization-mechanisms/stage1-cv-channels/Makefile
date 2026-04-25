# Makefile for the Chapter 12 Stage 1 myfirst driver.
#
# Replaces the anonymous mtx_sleep/wakeup channel from Chapter 11
# with two named condition variables (data_cv, room_cv).

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
