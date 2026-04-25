# Makefile for vtedu - Chapter 30 Case Study.
#
# A pedagogical VirtIO character device driver.  Depends on the virtio
# framework.  Requires a matching backend in bhyve(8) or QEMU; without
# one, probe will not match anything and the driver will not attach.

KMOD=	virtio_edu
SRCS=	vtedu.c
SRCS+=	device_if.h bus_if.h virtio_if.h

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
