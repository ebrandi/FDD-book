# Makefile for the myfirst driver - Chapter 24 Stage 1 (1.6-debug).
#
# Stage 1 modernises the cdev construction path but does not yet add
# the ioctl or sysctl source files.  The build is identical to the
# Chapter 23 Stage 3 build.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_debug.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
