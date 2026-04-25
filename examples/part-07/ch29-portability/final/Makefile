# Makefile for portdrv - Chapter 29 final reference driver.
#
# Builds portdrv.ko with one or more backends selected at build time.
# If no backend is selected, the Makefile falls back to SIM so that a
# plain `make` always produces a loadable module.
#
# Usage:
#   make                                       # SIM backend only (default).
#   make PORTDRV_WITH_PCI=yes                  # PCI backend only.
#   make PORTDRV_WITH_SIM=yes                  # SIM backend only (explicit).
#   make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
#                                              # Both backends.

KMOD=	portdrv
SRCS=	portdrv_core.c

.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+=	portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

.if !defined(PORTDRV_WITH_PCI) && !defined(PORTDRV_WITH_SIM)
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SRCS+=	device_if.h bus_if.h pci_if.h

CFLAGS+= -I${.CURDIR}

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
