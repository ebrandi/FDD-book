KMOD=	evdemo
SRCS=	evdemo.c

SYSDIR?= /usr/src/sys

test: evdemo_test_kqueue

evdemo_test_kqueue: evdemo_test_kqueue.c evdemo.h
	cc -o evdemo_test_kqueue evdemo_test_kqueue.c

testclean:
	rm -f evdemo_test_kqueue

CLEANFILES+= evdemo_test_kqueue

.include <bsd.kmod.mk>
