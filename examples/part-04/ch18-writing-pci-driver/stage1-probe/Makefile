# Makefile for the Chapter 18 Stage 1 myfirst driver.
#
# The build assumes that the Chapter 17 Stage 5 files
# (myfirst.c, myfirst_hw.c, myfirst_sim.c, cbuf.c, and their
# associated headers) are present in the same directory as the
# Chapter 18 Stage 1 files (myfirst_pci.c, myfirst_pci.h).

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage1\"

.include <bsd.kmod.mk>
