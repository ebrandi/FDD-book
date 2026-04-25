# Makefile for portdrv - Chapter 29 Lab 1 (Monolithic Starting Point).
#
# A single source file. No conditional compilation. No backend split.
# This is the baseline against which every later lab measures progress.

KMOD=	portdrv
SRCS=	portdrv.c

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
