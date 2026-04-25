#ifndef CBUF_H
#define CBUF_H

#include <stddef.h>
#include <stdint.h>

/* Small error codes mirroring common patterns. */
enum cb_err { CB_OK = 0, CB_EFULL = 28, CB_EEMPTY = 35 };

/*
 * head points to the next write position.
 * tail points to the next read position.
 * We leave one slot empty so that head == tail means empty, and
 * (head + 1) % cap == tail means full. This keeps the logic simple.
 */
typedef struct {
	size_t cap;		/* number of slots in 'data' */
	size_t head;		/* next write index */
	size_t tail;		/* next read index */
	int   *data;		/* storage owned by the caller */
} cbuf_t;

int cb_init(cbuf_t *cb, int *storage, size_t n);
int cb_push(cbuf_t *cb, int v);
int cb_pop(cbuf_t *cb, int *out);
int cb_is_empty(const cbuf_t *cb);
int cb_is_full(const cbuf_t *cb);

#endif
