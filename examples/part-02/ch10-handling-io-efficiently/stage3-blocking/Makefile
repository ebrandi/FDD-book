# Makefile for the Chapter 10 Stage 2 myfirst driver.
#
# Splices the cbuf abstraction into the Chapter 9 Stage 3 echo driver.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
