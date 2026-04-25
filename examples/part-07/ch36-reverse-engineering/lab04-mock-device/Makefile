KMOD=	mockdev
SRCS=	mockdev.c

SYSDIR?= /usr/src/sys

CFLAGS+= -I${.CURDIR}

# User-space test program is built with `make test`.
test: mockdev_test

mockdev_test: mockdev_test.c mockdev.h
	cc -Wall -Wextra -O2 -o mockdev_test mockdev_test.c

clean: cleantest
cleantest:
	rm -f mockdev_test

.include <bsd.kmod.mk>
