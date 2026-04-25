/*
 * leak_check.c - companion test program for Chapter 31 Lab 3.
 *
 * Issues SECDEV_GET_INFO multiple times and dumps the returned
 * structure as a hex dump.  With the unfixed driver you should see
 * non-zero bytes in the padding slots that differ between invocations.
 * Those bytes are whatever happened to be on the kernel stack when the
 * structure was populated.
 *
 * Build on the host:
 *   cc -Wall -O2 -o leak_check leak_check.c
 *
 * Run (with the module loaded and /dev/secdev accessible):
 *   ./leak_check 5
 */

#include <sys/ioccom.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct secdev_info {
	uint32_t	version;
	uint64_t	flags;
	uint16_t	id;
	char		name[32];
};

#define	SECDEV_GET_INFO	_IOR('S', 1, struct secdev_info)

static void
hex_dump(const void *buf, size_t len)
{
	const unsigned char *p = buf;

	for (size_t i = 0; i < len; i++) {
		printf("%02x", p[i]);
		if ((i + 1) % 8 == 0)
			printf(" ");
		if ((i + 1) % 16 == 0)
			printf("\n");
	}
	if (len % 16 != 0)
		printf("\n");
}

int
main(int argc, char *argv[])
{
	int fd, n = 5;

	if (argc >= 2)
		n = atoi(argv[1]);

	fd = open("/dev/secdev", O_RDWR);
	if (fd < 0) {
		perror("open /dev/secdev");
		return (1);
	}

	printf("sizeof(struct secdev_info) = %zu bytes\n",
	    sizeof(struct secdev_info));
	for (int i = 0; i < n; i++) {
		struct secdev_info info;

		/*
		 * Pre-fill with a recognisable pattern so we can tell
		 * which bytes the driver overwrites and which it leaves
		 * untouched.  The ioctl is _IOR (kernel -> user) so the
		 * kernel will overwrite the whole buffer in the copyout
		 * path, but zeroing here before the call still helps us
		 * reason about the driver's own fill behaviour in the
		 * INOUT case.
		 */
		memset(&info, 0xaa, sizeof(info));
		if (ioctl(fd, SECDEV_GET_INFO, &info) < 0) {
			perror("ioctl");
			close(fd);
			return (1);
		}
		printf("--- invocation %d ---\n", i + 1);
		hex_dump(&info, sizeof(info));
	}
	close(fd);
	return (0);
}
