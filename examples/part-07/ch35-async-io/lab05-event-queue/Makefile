KMOD=	evdemo
SRCS=	evdemo.c

SYSDIR?= /usr/src/sys

test: evdemo_watch

evdemo_watch: evdemo_watch.c evdemo.h
	cc -o evdemo_watch evdemo_watch.c

testclean:
	rm -f evdemo_watch

CLEANFILES+= evdemo_watch

.include <bsd.kmod.mk>
