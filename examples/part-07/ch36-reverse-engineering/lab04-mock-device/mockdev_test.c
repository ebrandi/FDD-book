/*
 * mockdev_test.c - User-space test program for the mock device.
 *
 * Companion to Chapter 36, Lab 4.
 *
 * The test program opens /dev/mockdev, sends each of the documented
 * command codes, polls for completion, and reports the result. It
 * also demonstrates the watchdog pattern: each command is given a
 * deadline, and if the device does not complete by the deadline,
 * the test reports a failure rather than hanging.
 *
 * Build with `make test` in this directory.
 *
 * Usage:
 *   ./mockdev_test [-d device]
 *
 * The default device is /dev/mockdev.
 */

#include <sys/ioctl.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mockdev.h"

#define	WATCHDOG_TIMEOUT_MS	1000
#define	POLL_INTERVAL_US	1000

static int
poll_completion(int fd, struct mockdev_op *op, int timeout_ms)
{
	struct timespec start, now;
	long elapsed_ms;

	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
		return (-1);
	for (;;) {
		if (ioctl(fd, MOCKDEV_IOC_READ_STATUS, op) != 0)
			return (-1);
		if ((op->mo_status & MOCKDEV_STATUS_BUSY) == 0)
			return (0);
		if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
			return (-1);
		elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
		    (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed_ms >= timeout_ms) {
			errno = ETIMEDOUT;
			return (-1);
		}
		usleep(POLL_INTERVAL_US);
	}
}

static int
run_command(int fd, uint32_t cmd, const char *label,
    uint32_t *result_data, uint32_t *result_status)
{
	struct mockdev_op op;
	int err;

	memset(&op, 0, sizeof(op));
	op.mo_cmd = cmd;
	if (ioctl(fd, MOCKDEV_IOC_SUBMIT, &op) != 0) {
		warn("submit %s", label);
		return (-1);
	}
	if (poll_completion(fd, &op, WATCHDOG_TIMEOUT_MS) != 0) {
		err = errno;
		fprintf(stderr,
		    "%s: poll failed: %s\n", label, strerror(err));
		return (-1);
	}
	if (result_data != NULL)
		*result_data = op.mo_data;
	if (result_status != NULL)
		*result_status = op.mo_status;
	printf("%-12s status=0x%02x data=0x%08x\n",
	    label, op.mo_status, op.mo_data);
	return (0);
}

int
main(int argc, char **argv)
{
	const char *path = "/dev/mockdev";
	uint32_t data, status;
	int fd, ch, failures = 0;

	while ((ch = getopt(argc, argv, "d:h")) != -1) {
		switch (ch) {
		case 'd':
			path = optarg;
			break;
		case 'h':
		default:
			fprintf(stderr,
			    "usage: %s [-d device]\n", argv[0]);
			return (ch == 'h' ? 0 : 1);
		}
	}

	fd = open(path, O_RDWR);
	if (fd < 0)
		err(1, "open %s", path);

	if (ioctl(fd, MOCKDEV_IOC_RESET, NULL) != 0)
		warn("reset");

	if (run_command(fd, MOCKDEV_CMD_NOP, "NOP",
	    &data, &status) != 0)
		failures++;

	if (run_command(fd, MOCKDEV_CMD_READ_ID, "READ_ID",
	    &data, &status) != 0)
		failures++;
	else if (data != MOCKDEV_CHIP_ID) {
		fprintf(stderr,
		    "READ_ID returned 0x%08x, expected 0x%08x\n",
		    data, MOCKDEV_CHIP_ID);
		failures++;
	}

	if (run_command(fd, MOCKDEV_CMD_INCREMENT, "INCREMENT",
	    &data, &status) != 0)
		failures++;
	else if (data != 1) {
		fprintf(stderr,
		    "INCREMENT returned 0x%08x, expected 1\n", data);
		failures++;
	}

	if (run_command(fd, MOCKDEV_CMD_INCREMENT, "INCREMENT",
	    &data, &status) != 0)
		failures++;
	else if (data != 2) {
		fprintf(stderr,
		    "INCREMENT returned 0x%08x, expected 2\n", data);
		failures++;
	}

	if (run_command(fd, MOCKDEV_CMD_RESET, "RESET",
	    &data, &status) != 0)
		failures++;

	if (run_command(fd, MOCKDEV_CMD_FAIL, "FAIL",
	    &data, &status) != 0) {
		failures++;
	} else if ((status & MOCKDEV_STATUS_ERROR) == 0) {
		fprintf(stderr,
		    "FAIL did not set ERROR status (got 0x%02x)\n",
		    status);
		failures++;
	} else {
		printf("FAIL correctly reported the ERROR bit.\n");
	}

	close(fd);

	if (failures != 0) {
		fprintf(stderr, "\n%d test(s) failed.\n", failures);
		return (1);
	}
	printf("\nAll tests passed.\n");
	return (0);
}
