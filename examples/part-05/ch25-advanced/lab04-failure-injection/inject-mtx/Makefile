# Lab 4: failure injection after mtx_init.
#
# Compiles myfirst.ko with MYFIRST_DEBUG_INJECT_FAIL_MTX defined.
# attach() returns ENOMEM at the first injection label; only the
# lock should need releasing.  A clean load/fail cycle confirms
# that fail_mtx: releases everything it should.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..
CFLAGS+=	-DMYFIRST_DEBUG_INJECT_FAIL_MTX

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
