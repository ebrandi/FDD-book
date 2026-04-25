#include <stdio.h>
#include "mathutils.h"

int
main(void)
{
	int x = 10, y = 4;

	printf("Add: %d\n", add(x, y));
	printf("Subtract: %d\n", subtract(x, y));

	return (0);
}
