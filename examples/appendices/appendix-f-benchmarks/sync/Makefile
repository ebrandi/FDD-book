# Makefile for the Appendix F sync_bench kmod.
#
# Build on FreeBSD 14.3-amd64 with the kernel source under
# /usr/src:
#     make
# Load and use:
#     kldload ./sync_bench.ko
#     sh run_sync_bench.sh
#     kldunload sync_bench
# Clean:
#     make clean

KMOD=	sync_bench
SRCS=	sync_bench.c

.include <bsd.kmod.mk>
