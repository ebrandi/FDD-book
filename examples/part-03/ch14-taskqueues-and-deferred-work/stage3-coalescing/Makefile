# Makefile for the Chapter 14 Stage 4 (final) myfirst driver.
#
# Combines every earlier stage: the Chapter 12 cv channels and sx
# configuration, the Chapter 13 callouts, and the Chapter 14 private
# taskqueue with three tasks (selwake_task, bulk_writer_task,
# reset_delayed_task).

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
