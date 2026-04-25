# Makefile for the Chapter 8 stage 3 myfirst driver (two nodes).
#
# Exposes a data node at /dev/myfirst/%d and a control node at
# /dev/myfirst/%d.ctl, each with its own cdevsw and permission mode.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
