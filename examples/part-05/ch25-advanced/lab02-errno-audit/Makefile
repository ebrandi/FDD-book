# Lab 2: errno Audit with truss
#
# Builds the audit driver.  Run under truss(1) to watch each errno:
#
#   # make
#   # truss -f -o /tmp/audit.truss ./audit
#   # less /tmp/audit.truss

PROG=		audit
SRCS=		audit.c
NO_MAN=		yes
MK_WERROR=	no

.include <bsd.prog.mk>
