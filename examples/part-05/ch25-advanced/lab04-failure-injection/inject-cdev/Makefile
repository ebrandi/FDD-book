# Lab 4: failure injection after make_dev_s.
#
# attach() returns ENOMEM after the cdev is created.  The
# fail_cdev -> fail_mtx chain has to destroy the cdev and the lock.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..
CFLAGS+=	-DMYFIRST_DEBUG_INJECT_FAIL_CDEV

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
