# Makefile for the Chapter 13 Stage 1 myfirst driver.
#
# Adds the heartbeat callout to the Chapter 12 Stage 4 driver.
# Stage 2 will add the watchdog; Stage 3 will add the tick source;
# Stage 4 is the integrated final driver.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
