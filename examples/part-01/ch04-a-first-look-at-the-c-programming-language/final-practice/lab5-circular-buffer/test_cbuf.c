#include <stdio.h>
#include "cbuf.h"

/*
 * We use a 4-slot store. With the "leave one empty" rule,
 * the buffer holds at most 3 values at a time.
 */
int
main(void)
{
	int store[4];
	cbuf_t cb;
	int v;

	cb_init(&cb, store, 4);

	/* Fill three slots; the fourth remains empty to signal full. */
	for (int i = 1; i <= 3; i++) {
		int rc = cb_push(&cb, i);
		printf("push %d -> rc=%d\n", i, rc);
	}

	printf("full? %d (1 means yes)\n", cb_is_full(&cb));

	/* Drain to empty. */
	while (cb_pop(&cb, &v) == 0)
		printf("pop -> %d\n", v);

	/* Wrap-around scenario: push, push, pop, then push twice more. */
	cb_push(&cb, 7);
	cb_push(&cb, 8);
	cb_pop(&cb, &v);
	printf("pop -> %d\n", v);	/* frees one slot */
	cb_push(&cb, 9);
	cb_push(&cb, 10);		/* should wrap indices */

	while (cb_pop(&cb, &v) == 0)
		printf("pop -> %d\n", v);

	return (0);
}
