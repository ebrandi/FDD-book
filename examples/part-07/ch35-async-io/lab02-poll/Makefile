KMOD=	evdemo
SRCS=	evdemo.c

SYSDIR?= /usr/src/sys

test: evdemo_test_poll

evdemo_test_poll: evdemo_test_poll.c evdemo.h
	cc -o evdemo_test_poll evdemo_test_poll.c

testclean:
	rm -f evdemo_test_poll

CLEANFILES+= evdemo_test_poll

.include <bsd.kmod.mk>
