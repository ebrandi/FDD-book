# Makefile for the Chapter 13 Stage 3 myfirst driver.
#
# Adds the tick source callout on top of Stage 2's heartbeat +
# watchdog. Stage 4 is the integrated final driver with the standard
# macro vocabulary and the seven-phase detach pattern documented.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
