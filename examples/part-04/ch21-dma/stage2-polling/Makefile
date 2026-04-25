# Makefile for the Chapter 21 Stage 2 myfirst driver.
#
# Same source set as Stage 1. Stage 2's changes are confined to
# myfirst.c (DMA transfer helper) and myfirst_sim.c (simulated engine).

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.4-dma-stage2\"

# Optional: simulation-only build.
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

.include <bsd.kmod.mk>
