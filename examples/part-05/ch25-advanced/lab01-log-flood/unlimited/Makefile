# Lab 1: Unlimited (naive) log variant of myfirst.
#
# Rebuilds myfirst.ko with MYFIRST_UNLIMITED_LOG defined.  The cdev
# read path prints every call through a bare device_printf instead
# of DLOG_RL.  Use this to reproduce a log flood and compare against
# the rate-limited build under ../limited/.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..
CFLAGS+=	-DMYFIRST_UNLIMITED_LOG

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
