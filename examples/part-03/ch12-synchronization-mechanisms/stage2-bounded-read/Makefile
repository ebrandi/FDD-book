# Makefile for the Chapter 12 Stage 2 myfirst driver.
#
# Adds a read_timeout_ms sysctl that bounds blocking reads via
# cv_timedwait_sig.  Symmetric write_timeout_ms is also exposed.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
