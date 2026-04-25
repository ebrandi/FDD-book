# Makefile for the Chapter 12 Stage 4 myfirst driver.
#
# The final Chapter 12 driver. Combines cv channels, bounded reads,
# sx-protected configuration, and a reset sysctl that exercises the
# canonical lock order (mtx first, sx second).

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
