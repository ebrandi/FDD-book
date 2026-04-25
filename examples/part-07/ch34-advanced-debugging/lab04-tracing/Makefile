KMOD=	bugdemo
SRCS=	bugdemo.c

SYSDIR?= /usr/src/sys

# Build with WITH_CTF=1 to let DTrace resolve struct fields by name:
#     $ WITH_CTF=1 make

test: bugdemo_test

bugdemo_test: bugdemo_test.c bugdemo.h
	cc -o bugdemo_test bugdemo_test.c

CLEANFILES+= bugdemo_test

.include <bsd.kmod.mk>
