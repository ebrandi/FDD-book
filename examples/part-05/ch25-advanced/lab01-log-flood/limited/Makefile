# Lab 1: Rate-limited log variant of myfirst.
#
# Rebuilds myfirst.ko as shipped at the top of ch25-advanced/ (no
# additional defines).  The cdev read path uses DLOG_RL with the
# default 10 pps cap.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
