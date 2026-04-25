# Makefile for hello_world kernel module
#
# Companion for Chapter 2. The KMOD name is "hello_world", so this
# Makefile produces "hello_world.ko" on build, NOT "hello.ko".
# (The chapter text has a minor phrasing glitch that says "hello.ko";
#  the filename you actually load is hello_world.ko.)

KMOD=	hello_world
SRCS=	hello_world.c

.include <bsd.kmod.mk>
