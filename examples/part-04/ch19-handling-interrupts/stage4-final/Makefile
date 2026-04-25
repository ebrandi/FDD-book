# Makefile for the Chapter 19 myfirst driver.
#
# Combines:
#   - Ch10-15 driver (cdev, softc, locks, callouts, sx, taskqueue, sema).
#   - Ch16 hardware access layer.
#   - Ch17 simulation backend (inactive on PCI build).
#   - Ch18 PCI attach.
#   - Ch19 interrupt handler (NEW).

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       cbuf.c

# Version string. Update this line alongside any user-visible change.
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.2-intr\"

# Optional: build without PCI support (simulation only).
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

# Optional: build without simulation fallback (PCI only).
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
