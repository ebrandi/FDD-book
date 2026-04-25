# Makefile for the Chapter 8 stage 0 myfirst driver.
#
# Moves the device node from /dev/myfirst0 to /dev/myfirst/0 and
# uses group operator, mode 0660 as the baseline.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
