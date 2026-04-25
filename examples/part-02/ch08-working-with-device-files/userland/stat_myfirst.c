/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * stat_myfirst: print stat(2) metadata for one or more device-file
 * paths. Useful to confirm that a primary node and its alias resolve
 * to the same cdev (same rdev) and that the advertised mode and
 * ownership match what the driver asked for.
 *
 * Usage:
 *     stat_myfirst path [path ...]
 *
 * Compile:
 *     cc -Wall -Werror -o stat_myfirst stat_myfirst.c
 */

#include <err.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int
main(int argc, char **argv)
{
	struct stat sb;
	int i;

	if (argc < 2) {
		fprintf(stderr, "usage: %s path [path ...]\n", argv[0]);
		return (1);
	}

	for (i = 1; i < argc; i++) {
		if (stat(argv[i], &sb) != 0)
			err(1, "stat %s", argv[i]);
		printf("%s: mode=%06o uid=%u gid=%u rdev=%#jx\n",
		    argv[i],
		    (unsigned)(sb.st_mode & 07777),
		    (unsigned)sb.st_uid,
		    (unsigned)sb.st_gid,
		    (uintmax_t)sb.st_rdev);
	}
	return (0);
}
