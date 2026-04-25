# Makefile for the Chapter 20 myfirst driver.
#
# Combines:
#   - Ch10-15 driver.
#   - Ch16 hardware layer.
#   - Ch17 simulation backend.
#   - Ch18 PCI attach.
#   - Ch19 single-vector interrupt handler (legacy INTx fallback).
#   - Ch20 multi-vector MSI/MSI-X layer (NEW).

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.3-msi\"

# Optional: simulation-only build.
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

.include <bsd.kmod.mk>
