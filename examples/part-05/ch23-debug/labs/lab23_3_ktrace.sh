#!/bin/sh
#
# lab23_3_ktrace.sh - Lab 23.3: User-Side Tracing with ktrace
#
# Compiles a small test program, traces it with ktrace, and shows the
# resulting kdump output.

set -e

WORKDIR=$(mktemp -d)
trap "rm -rf '$WORKDIR'" EXIT

cat > "$WORKDIR/myftest.c" <<'CEOF'
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(void)
{
	int fd;
	char buf[64];
	int arg = 0;

	fd = open("/dev/myfirst0", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return (1);
	}
	/* A harmless ioctl that may or may not be supported */
	(void)ioctl(fd, _IOR('f', 99, int), &arg);
	read(fd, buf, sizeof(buf));
	close(fd);
	return (0);
}
CEOF

echo "=== Lab 23.3: User-Side Tracing with ktrace ==="
echo
echo "Compiling myftest.c in $WORKDIR"
cc -o "$WORKDIR/myftest" "$WORKDIR/myftest.c"

echo "Tracing the program..."
sudo ktrace -di -f "$WORKDIR/mytrace.out" "$WORKDIR/myftest"
echo
echo "Dumping the trace (relevant syscalls):"
kdump -f "$WORKDIR/mytrace.out" | grep -E 'myfirst0|CALL|RET' | head -40

echo
echo "=== End of Lab 23.3 ==="
