# Makefile for myfirst_blk - Chapter 27 (Storage Devices and VFS).
#
# Builds the chapter's reference pseudo block device driver,
# myblk.ko, which demonstrates the g_disk registration path, a
# complete BIO handler, a flat-buffer backing store, and safe
# load/unload lifecycle.

KMOD=	myblk
SRCS=	myfirst_blk.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
