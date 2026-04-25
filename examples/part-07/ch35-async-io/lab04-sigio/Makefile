KMOD=	evdemo
SRCS=	evdemo.c

SYSDIR?= /usr/src/sys

test: evdemo_test_sigio

evdemo_test_sigio: evdemo_test_sigio.c evdemo.h
	cc -o evdemo_test_sigio evdemo_test_sigio.c

testclean:
	rm -f evdemo_test_sigio

CLEANFILES+= evdemo_test_sigio

.include <bsd.kmod.mk>
