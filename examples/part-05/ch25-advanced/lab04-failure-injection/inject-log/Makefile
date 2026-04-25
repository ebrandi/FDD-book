# Lab 4: failure injection after myfirst_log_attach.
#
# attach() returns ENOMEM just after the rate-limit state is
# initialised.  fail_log -> fail_cdev -> fail_mtx must release every
# resource acquired to that point.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..
CFLAGS+=	-DMYFIRST_DEBUG_INJECT_FAIL_LOG

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
