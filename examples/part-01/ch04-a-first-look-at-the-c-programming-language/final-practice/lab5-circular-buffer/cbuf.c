#include "cbuf.h"

/* Prepare a buffer wrapper around caller-provided storage. */
int
cb_init(cbuf_t *cb, int *storage, size_t n)
{
	if (!cb || !storage || n == 0)
		return (22);	/* EINVAL */
	cb->cap = n;
	cb->head = cb->tail = 0;
	cb->data = storage;
	return (CB_OK);
}

/* Empty when indices match. */
int
cb_is_empty(const cbuf_t *cb)
{
	return (cb->head == cb->tail);
}

/* Full when advancing head would collide with tail. */
int
cb_is_full(const cbuf_t *cb)
{
	return (((cb->head + 1) % cb->cap) == cb->tail);
}

/* Write the value, then advance head with wrap-around. */
int
cb_push(cbuf_t *cb, int v)
{
	if (cb_is_full(cb))
		return (CB_EFULL);
	cb->data[cb->head] = v;
	cb->head = (cb->head + 1) % cb->cap;
	return (CB_OK);
}

/* Read the value, then advance tail with wrap-around. */
int
cb_pop(cbuf_t *cb, int *out)
{
	if (cb_is_empty(cb))
		return (CB_EEMPTY);
	*out = cb->data[cb->tail];
	cb->tail = (cb->tail + 1) % cb->cap;
	return (CB_OK);
}
