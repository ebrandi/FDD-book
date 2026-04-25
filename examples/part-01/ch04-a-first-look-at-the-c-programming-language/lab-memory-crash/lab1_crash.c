/*
 * lab1_crash.c - DELIBERATELY BROKEN.
 *
 * Companion for "Hands-On Lab 1: Crashing with an Uninitialized
 * Pointer" in Chapter 4. This program compiles cleanly but almost
 * always segfaults at runtime because it dereferences an
 * uninitialised pointer. That is the whole point of the exercise.
 *
 * Compare with lab1_fixed.c in this directory for the correct
 * version using malloc().
 */

#include <stdio.h>

struct data {
	int value;
};

int
main(void)
{
	struct data *ptr;	/* declared but not initialised */

	/*
	 * At this point, 'ptr' points to whatever bytes happened to be
	 * on the stack when main() started. Writing through it is
	 * undefined behaviour and will very likely crash.
	 */
	ptr->value = 42;

	printf("Value: %d\n", ptr->value);

	return (0);
}
