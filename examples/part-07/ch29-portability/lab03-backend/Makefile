# Makefile for portdrv - Chapter 29 Lab 3 (Backend Interface Extracted).
#
# The driver is still a single source file. The backend header is not
# listed here because bsd.kmod.mk picks it up automatically through
# the include in portdrv.c. Lab 4 will add a second source file and
# the Makefile will grow accordingly.

KMOD=	portdrv
SRCS=	portdrv.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
