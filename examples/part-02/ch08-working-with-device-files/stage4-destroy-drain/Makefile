# Makefile for the Chapter 8 stage 4 myfirst driver (destroy-dev-drain).
#
# Exercise the broken path (no drain):
#     make clean && make
#
# Exercise the fixed path (with drain):
#     make clean && make CFLAGS+=-DUSE_DRAIN=1
#
# Only run in a throwaway VM. The broken path is deliberately unsafe.

KMOD=    myfirstN
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
