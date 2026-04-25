# Makefile for the Chapter 12 Stage 3 myfirst driver.
#
# Adds an sx-protected configuration subsystem (debug_level,
# soft_byte_limit, nickname) on top of Stage 2's bounded reads.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
