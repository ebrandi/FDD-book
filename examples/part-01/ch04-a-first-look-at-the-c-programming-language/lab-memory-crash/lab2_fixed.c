/*
 * lab2_fixed.c - corrected version of lab2_leak.c.
 *
 * The only change from lab2_leak.c is the final free(). That one line
 * is enough to turn a leaky program into a clean one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
	char *buffer = malloc(128);
	if (buffer == NULL)
		return (1);

	strcpy(buffer, "FreeBSD device drivers are awesome!");
	printf("%s\n", buffer);

	free(buffer);

	return (0);
}
