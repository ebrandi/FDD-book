KMOD=	bugdemo
SRCS=	bugdemo.c

SYSDIR?= /usr/src/sys

test: bugdemo_test

bugdemo_test: bugdemo_test.c bugdemo.h
	cc -o bugdemo_test bugdemo_test.c

testclean:
	rm -f bugdemo_test

CLEANFILES+= bugdemo_test

.include <bsd.kmod.mk>
