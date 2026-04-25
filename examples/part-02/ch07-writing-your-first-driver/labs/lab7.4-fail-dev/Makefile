# Lab 7.4 variant B: deliberate failure right after make_dev_s succeeds.
#
# Loading this driver should fail with ENOMEM. The /dev node was
# created and must be destroyed by the unwind path; verify with
# `ls /dev/myfirst0` (no such file).

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
