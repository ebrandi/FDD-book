# Makefile for the Chapter 22 Stage 3 myfirst driver.
#
# Stage 3 adds a real resume path that restores device state after a
# suspend-resume cycle. The suspend path is unchanged from Stage 2.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power-stage3\"

.include <bsd.kmod.mk>
