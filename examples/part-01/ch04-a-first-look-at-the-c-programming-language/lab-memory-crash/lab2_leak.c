/*
 * lab2_leak.c - DELIBERATELY LEAKY.
 *
 * Companion for "Hands-On Lab 2: Memory Leak and the Forgotten Free"
 * in Chapter 4. This program works correctly, but it never frees the
 * buffer it allocates. In userspace the OS reclaims the memory on
 * exit so the leak is invisible. In the kernel, the same bug would
 * accumulate until the system was rebooted.
 *
 * Run with AddressSanitizer to see the leak report:
 *     cc -fsanitize=address -g -o lab2_leak lab2_leak.c
 *     ./lab2_leak
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

	/* Intentionally no free(buffer) here. */
	return (0);
}
