# Makefile for the Chapter 22 Stage 1 myfirst driver.
#
# Stage 1 adds skeleton DEVICE_SUSPEND/RESUME/SHUTDOWN methods that
# only log and count transitions. The source file list is unchanged
# from Chapter 21 Stage 4; the new code lives inside myfirst_pci.c.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power-stage1\"

# Optional: simulation-only build.
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

.include <bsd.kmod.mk>
