# Makefile for the Chapter 17 Stage 5 myfirst driver.
#
# Combines Chapter 11-15 driver, Chapter 16 hardware layer, and the
# new Chapter 17 simulation layer. The build takes place inside the
# chapter's driver directory; this Makefile assumes the full driver
# source tree (myfirst.c, myfirst_hw.c, myfirst_sim.c, cbuf.c) is
# present in the same directory as the Chapter 16 Stage 4 files.

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c cbuf.c

# Chapter 17 specifies x86 only for the simulation shortcut.
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.0-simulated\"

.include <bsd.kmod.mk>
