# Makefile for mynet - Chapter 28 (Writing a Network Driver).
#
# Builds the chapter's reference pseudo Ethernet driver, mynet.ko,
# which demonstrates the ifnet registration path, a synchronous
# transmit callback, a synthesised receive path, an ioctl handler
# that cooperates with ether_ioctl, and a full clone-create and
# clone-destroy lifecycle.

KMOD=	mynet
SRCS=	mynet.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
