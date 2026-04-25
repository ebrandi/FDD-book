# Makefile for the Chapter 21 Stage 1 myfirst driver.
#
# Same source set as the Chapter 20 Stage 4 driver; only the version
# string differs. Stage 1 adds DMA allocation and teardown but keeps
# all the code in myfirst.c (no new files).

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.4-dma-stage1\"

# Optional: simulation-only build.
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

.include <bsd.kmod.mk>
