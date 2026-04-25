/*
 * audit.c - errno audit driver for Lab 2.
 *
 * Performs a series of deliberately invalid calls against the myfirst
 * driver.  Run under truss(1) to see the errno each one returns and
 * calibrate your intuition about the driver's error reporting.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../myfirst_ioctl.h"

#define DEVNODE "/dev/myfirst0"

/* Cook up an ioctl number the driver will not recognise. */
#define MYFIRSTIOC_UNKNOWN _IO('M', 99)

int
main(void)
{
	int fd, rc;
	char bigbuf[512];
	u_int v;
	size_t vlen = sizeof(v);

	fd = open(DEVNODE, O_RDWR);
	if (fd < 0)
		err(1, "open %s", DEVNODE);

	/* 1. Unknown ioctl command number. */
	errno = 0;
	rc = ioctl(fd, MYFIRSTIOC_UNKNOWN);
	printf("step 1: ioctl(unknown) = %d, errno = %d (%s)\n",
	    rc, errno, strerror(errno));

	/* 2. MYFIRSTIOC_SETMSG with NULL argument. */
	errno = 0;
	rc = ioctl(fd, MYFIRSTIOC_SETMSG, (char *)NULL);
	printf("step 2: ioctl(SETMSG, NULL) = %d, errno = %d (%s)\n",
	    rc, errno, strerror(errno));

	/* 3. Zero-length write. */
	errno = 0;
	rc = write(fd, "", 0);
	printf("step 3: write(0 bytes) = %d, errno = %d (%s)\n",
	    rc, errno, strerror(errno));

	/* 4. Oversize write. */
	memset(bigbuf, 'x', sizeof(bigbuf));
	errno = 0;
	rc = write(fd, bigbuf, sizeof(bigbuf));
	printf("step 4: write(oversize) = %d, errno = %d (%s)\n",
	    rc, errno, strerror(errno));

	/* 5. Sysctl out-of-range. */
	v = 9999;
	errno = 0;
	rc = sysctlbyname("dev.myfirst.0.timeout_sec", NULL, NULL,
	    &v, sizeof(v));
	printf("step 5: sysctl(timeout_sec=9999) = %d, errno = %d (%s)\n",
	    rc, errno, strerror(errno));

	/* Read the value back to confirm it was not corrupted. */
	if (sysctlbyname("dev.myfirst.0.timeout_sec", &v, &vlen,
	    NULL, 0) == 0)
		printf("       sysctl timeout_sec = %u\n", v);

	close(fd);
	return (0);
}
