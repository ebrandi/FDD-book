# Makefile for the myfirst driver - Chapter 24 Stage 2
# (1.7-integration-stage-2).
#
# Stage 2 adds the ioctl interface.  The new myfirst_ioctl.c file
# joins the SRCS list; nothing else in the build changes.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_debug.c myfirst_ioctl.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
