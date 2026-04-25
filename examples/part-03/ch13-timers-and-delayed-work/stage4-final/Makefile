# Makefile for the Chapter 13 Stage 4 (final) myfirst driver.
#
# Combines all of Chapter 12 Stage 4 (cv channels, bounded reads,
# sx-protected configuration, reset sysctl) with Chapter 13's three
# callouts (heartbeat, watchdog, tick source). This is the canonical
# integrated driver with the standard macro vocabulary and the
# seven-phase detach pattern documented in comments.

KMOD=    myfirst
SRCS=    myfirst.c cbuf.c
SRCS+=   device_if.h bus_if.h

.include <bsd.kmod.mk>
