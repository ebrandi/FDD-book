# Lab 7.4 variant A: deliberate failure right after mutex init.
#
# Loading this driver should fail with ENXIO and leave nothing behind:
# no /dev/myfirst0, no dev.myfirst.0 sysctl tree, no leaked mutex.

KMOD=    myfirst
SRCS=    myfirst.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
