/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * mmap_myfirst.c: read-only mmap tester for /dev/myfirst (Stage 5).
 *
 * Maps the cbuf's backing memory and dumps the first 64 bytes.  The
 * raw bytes do not necessarily match a sequential read; the cbuf
 * lives at unspecified positions inside the mapping.  Use this tool
 * mainly to confirm that the mapping itself works.
 */

#include <sys/mman.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	DEVPATH "/dev/myfirst"
#define	MAPSIZE 4096

int
main(void)
{
	int fd = open(DEVPATH, O_RDONLY);
	if (fd < 0)
		err(1, "open %s", DEVPATH);

	char *map = mmap(NULL, MAPSIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		err(1, "mmap");

	printf("first 64 bytes:\n");
	for (int i = 0; i < 64; i++) {
		printf(" %02x", (unsigned char)map[i]);
		if ((i + 1) % 16 == 0)
			putchar('\n');
	}

	munmap(map, MAPSIZE);
	close(fd);
	return (0);
}
