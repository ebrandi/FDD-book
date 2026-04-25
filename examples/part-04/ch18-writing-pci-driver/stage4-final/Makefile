# Makefile for the Chapter 18 myfirst driver.
#
# Combines:
#   - The Chapter 10-15 driver (cdev, softc, locks, callouts, sx,
#     taskqueue, sema).
#   - The Chapter 16 hardware access layer (myfirst_hw.c and the
#     Chapter 18 addition myfirst_hw_pci.c).
#   - The Chapter 17 simulation backend (myfirst_sim.c).
#   - The Chapter 18 PCI attach layer (myfirst_pci.c).
#
# The driver is loadable as a standalone kernel module via
# kldload(8). When loaded, it participates in PCI probe for every
# device the pci(4) subsystem enumerates; it attaches to any device
# whose vendor/device ID matches an entry in myfirst_pci_ids[].

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       cbuf.c

# Version string. Update this line alongside any user-visible change.
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci\"

# Optional: build without PCI support (simulation only).
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

# Optional: build without simulation fallback (PCI only).
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
