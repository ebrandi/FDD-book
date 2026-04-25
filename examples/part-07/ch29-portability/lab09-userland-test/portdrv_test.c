/*
 * portdrv_test.c - Chapter 29 Lab 9 userland harness for portdrv.
 *
 * Opens a portdrv character device, writes a known value, reads it
 * back, and prints the result. The test is meant to exercise the
 * simulation backend end-to-end without needing hardware.
 *
 * Build: cc -o portdrv_test portdrv_test.c
 * Run:   ./portdrv_test /dev/portdrv0
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

static int
round_trip(const char *dev, uint32_t value)
{
	int fd;
	uint32_t readback = 0;

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return (-1);
	}

	if (write(fd, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
		perror("write");
		close(fd);
		return (-1);
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		/* Ignore: some drivers do not support seeking. */
	}

	if (read(fd, &readback, sizeof(readback)) != (ssize_t)sizeof(readback)) {
		perror("read");
		close(fd);
		return (-1);
	}

	close(fd);

	printf("%s: wrote 0x%08" PRIx32 ", read 0x%08" PRIx32 "\n",
	    dev, value, readback);

	return (value == readback) ? 0 : 1;
}

int
main(int argc, char *argv[])
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/portdrv0";
	int failures = 0;

	failures += (round_trip(dev, 0x12345678) != 0);
	failures += (round_trip(dev, 0xcafebabe) != 0);
	failures += (round_trip(dev, 0x00000000) != 0);
	failures += (round_trip(dev, 0xffffffff) != 0);

	if (failures == 0) {
		printf("All round-trip tests passed for %s\n", dev);
		return (0);
	}

	fprintf(stderr, "%d round-trip tests failed for %s\n", failures, dev);
	return (1);
}
