# Makefile for the Chapter 21 Stage 3 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.4-dma-stage3\"

# Optional: simulation-only build.
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

.include <bsd.kmod.mk>
