# Makefile for the Chapter 18 Stage 2 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage2\"

.include <bsd.kmod.mk>
