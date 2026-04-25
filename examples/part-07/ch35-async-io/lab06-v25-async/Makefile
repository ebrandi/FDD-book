KMOD=	evdemo
SRCS=	evdemo.c

SYSDIR?= /usr/src/sys

TEST_PROGS= evdemo_test_poll evdemo_test_kqueue evdemo_test_sigio \
	evdemo_test_combined

test: ${TEST_PROGS}

evdemo_test_poll: evdemo_test_poll.c evdemo.h
	cc -o evdemo_test_poll evdemo_test_poll.c

evdemo_test_kqueue: evdemo_test_kqueue.c evdemo.h
	cc -o evdemo_test_kqueue evdemo_test_kqueue.c

evdemo_test_sigio: evdemo_test_sigio.c evdemo.h
	cc -o evdemo_test_sigio evdemo_test_sigio.c

evdemo_test_combined: evdemo_test_combined.c evdemo.h
	cc -o evdemo_test_combined evdemo_test_combined.c

testclean:
	rm -f ${TEST_PROGS}

CLEANFILES+= ${TEST_PROGS}

.include <bsd.kmod.mk>
