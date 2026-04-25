# Makefile for the Chapter 18 Stage 3 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_hw_pci.c myfirst_sim.c \
       myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage3\"

.include <bsd.kmod.mk>
