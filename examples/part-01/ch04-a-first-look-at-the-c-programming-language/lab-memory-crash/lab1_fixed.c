/*
 * lab1_fixed.c - corrected version of lab1_crash.c.
 *
 * Allocates memory for a struct data on the heap, checks for
 * allocation failure, writes into the structure safely, prints the
 * value, and frees the memory before exiting.
 */

#include <stdio.h>
#include <stdlib.h>	/* malloc, free */

struct data {
	int value;
};

int
main(void)
{
	struct data *ptr;

	ptr = malloc(sizeof(struct data));
	if (ptr == NULL) {
		printf("Allocation failed!\n");
		return (1);
	}

	ptr->value = 42;
	printf("Value: %d\n", ptr->value);

	free(ptr);

	return (0);
}
