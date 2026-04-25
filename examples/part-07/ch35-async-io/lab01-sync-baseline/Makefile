KMOD=	evdemo
SRCS=	evdemo.c

SYSDIR?= /usr/src/sys

test: evdemo_test

evdemo_test: evdemo_test.c evdemo.h
	cc -o evdemo_test evdemo_test.c

testclean:
	rm -f evdemo_test

CLEANFILES+= evdemo_test

.include <bsd.kmod.mk>
