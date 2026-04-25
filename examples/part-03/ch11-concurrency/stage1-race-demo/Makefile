# Makefile for the Chapter 11 Stage 1 race-demo driver.
#
# DANGEROUS: this driver has had its mutex removed from the I/O path
# deliberately to demonstrate the corruption that results. It exists
# for the Section 2 lab only. Do not run in production. Expect
# inconsistent data, possible kernel warnings, and (rarely) panics.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

CFLAGS+= -DRACE_DEMO

.include <bsd.kmod.mk>
