# Makefile for the myfirst driver - Chapter 23 Stage 3 (1.6-debug)
#
# Builds the kernel module with the debug and SDT infrastructure
# introduced in Chapter 23.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_debug.c

# Include current directory for the myfirst_debug.h header.
CFLAGS+=	-I${.CURDIR}

# Build against the running kernel's sources.  Adjust if the kernel
# source is in a non-default location.
SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
