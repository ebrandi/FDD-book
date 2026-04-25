# Lab 4: failure injection after sysctl tree built.
#
# attach() returns ENOMEM just after myfirst_sysctl_attach() returns.
# The cdev and lock must both be released; sysctl OIDs are owned by
# Newbus, so they unwind automatically when the device is torn down.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..
CFLAGS+=	-DMYFIRST_DEBUG_INJECT_FAIL_SYSCTL

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
