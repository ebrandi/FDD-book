# Lab 5: shutdown_pre_sync handler demonstration.
#
# Rebuilds myfirst.ko with MYFIRST_SHUTDOWN_NOISY defined so the
# shutdown handler writes an unconditional device_printf instead of
# the debug-mask-gated DPRINTF.  Use this build on a test machine
# to watch the message appear on the console before filesystems sync.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_ioctl.c \
	myfirst_sysctl.c myfirst_debug.c myfirst_log.c

CFLAGS+=	-I${.CURDIR}/../..
CFLAGS+=	-DMYFIRST_SHUTDOWN_NOISY

.PATH:	${.CURDIR}/../..

.include <bsd.kmod.mk>
