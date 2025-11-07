# Makefile for hello_world kernel module

KMOD= mydriver 
SRCS= mydriver.c

# Required for Newbus drivers - generates device_if.h and bus_if.h
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
