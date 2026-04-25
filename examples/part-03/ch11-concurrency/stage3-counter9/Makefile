# Makefile for the Chapter 11 Stage 2 myfirst driver.
#
# Concurrent-safe driver with explicit MYFIRST_LOCK/UNLOCK/ASSERT
# macros and mtx_assert throughout.  Functionally identical to
# Chapter 10 Stage 4; the changes are in comments, assertions,
# and the macro indirection that sets up for future lock changes.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
