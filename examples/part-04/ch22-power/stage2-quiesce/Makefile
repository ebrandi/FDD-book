# Makefile for the Chapter 22 Stage 2 myfirst driver.
#
# Stage 2 turns the Stage 1 suspend skeleton into a real quiesce:
# mask interrupts, drain DMA, drain workers. The resume path is still
# the Stage 1 skeleton.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power-stage2\"

.include <bsd.kmod.mk>
