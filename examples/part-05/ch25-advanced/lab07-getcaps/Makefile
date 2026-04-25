# Lab 7: Capability Discovery in User Space
#
# Builds mfctl25, a user-space control program that discovers the
# driver's capabilities via MYFIRSTIOC_GETCAPS before issuing each
# operation.  Falls back to a documented default set if GETCAPS is
# not supported (older driver).

PROG=		mfctl25
SRCS=		mfctl25.c
NO_MAN=		yes
MK_WERROR=	no

.include <bsd.prog.mk>
