# Makefile for the myfirst driver - Chapter 24 Stage 3 (1.7-integration).
#
# Stage 3 is the chapter's final milestone.  The new myfirst_sysctl.c
# joins the SRCS list alongside the Stage 2 myfirst_ioctl.c.  This is
# the version of myfirst.ko that Chapter 25 builds on.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
