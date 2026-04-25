# Makefile for myfirst_usb - Chapter 26 (USB and Serial Drivers).
#
# Builds the chapter's reference USB driver module, myfirst_usb.ko,
# which demonstrates the USB transfer framework, the probe/attach
# lifecycle, and the labelled-goto cleanup chain applied to USB.
#
# The companion file myfirst_usb.h declares the softc and transfer
# indices; myfirst_usb.c implements the Newbus methods and the
# three-state callback state machine for bulk and interrupt transfers.

KMOD=	myfirst_usb
SRCS=	myfirst_usb.c

CFLAGS+=	-I${.CURDIR}

# Needed for usb_if.h, usbdi.h, and related headers.
SRCS+=	usb_if.h
SRCS+=	device_if.h
SRCS+=	bus_if.h

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
