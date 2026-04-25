# Makefile for the Chapter 13 Stage 2 myfirst driver.
#
# Adds the watchdog callout on top of Stage 1's heartbeat. Stage 3
# will add the tick source; Stage 4 is the integrated final driver.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
