# Makefile for the Chapter 8 stage 2 myfirst driver.
#
# Adds per-open state through devfs_set_cdevpriv() and drops the
# exclusive-open restriction.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
