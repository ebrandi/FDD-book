# Makefile for the Chapter 16 stand-alone hwsim practice module.
#
# Builds a minimal kernel module that demonstrates bus_space(9) on
# a malloc'd register block, exposing four 32-bit registers through
# sysctl at dev.hwsim.reg{0,4,8,c}.

KMOD=  hwsim
SRCS=  hwsim.c

.include <bsd.kmod.mk>
