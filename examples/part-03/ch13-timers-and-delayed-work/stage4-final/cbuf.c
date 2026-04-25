/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * cbuf.c: kernel-side implementation of the byte-oriented circular
 * buffer for the Chapter 10 myfirst driver.
 *
 * The buffer does not lock; the caller (the driver's I/O handlers)
 * holds sc->mtx around every cbuf_*() call.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include "cbuf.h"

MALLOC_DEFINE(M_CBUF, "cbuf", "Chapter 10 circular buffer");

int
cbuf_init(struct cbuf *cb, size_t size)
{
	if (cb == NULL || size == 0)
		return (EINVAL);
	cb->cb_data = malloc(size, M_CBUF, M_WAITOK | M_ZERO);
	cb->cb_size = size;
	cb->cb_head = 0;
	cb->cb_used = 0;
	return (0);
}

void
cbuf_destroy(struct cbuf *cb)
{
	if (cb == NULL || cb->cb_data == NULL)
		return;
	free(cb->cb_data, M_CBUF);
	cb->cb_data = NULL;
	cb->cb_size = 0;
	cb->cb_head = 0;
	cb->cb_used = 0;
}

void
cbuf_reset(struct cbuf *cb)
{
	if (cb == NULL)
		return;
	cb->cb_head = 0;
	cb->cb_used = 0;
}

size_t
cbuf_size(const struct cbuf *cb)
{
	return (cb->cb_size);
}

size_t
cbuf_used(const struct cbuf *cb)
{
	return (cb->cb_used);
}

size_t
cbuf_free(const struct cbuf *cb)
{
	return (cb->cb_size - cb->cb_used);
}

size_t
cbuf_write(struct cbuf *cb, const void *src, size_t n)
{
	size_t avail, tail, first, second;

	avail = cbuf_free(cb);
	if (n > avail)
		n = avail;
	if (n == 0)
		return (0);

	tail = (cb->cb_head + cb->cb_used) % cb->cb_size;
	first = MIN(n, cb->cb_size - tail);
	memcpy(cb->cb_data + tail, src, first);
	second = n - first;
	if (second > 0)
		memcpy(cb->cb_data, (const char *)src + first, second);

	cb->cb_used += n;
	return (n);
}

size_t
cbuf_read(struct cbuf *cb, void *dst, size_t n)
{
	size_t first, second;

	if (n > cb->cb_used)
		n = cb->cb_used;
	if (n == 0)
		return (0);

	first = MIN(n, cb->cb_size - cb->cb_head);
	memcpy(dst, cb->cb_data + cb->cb_head, first);
	second = n - first;
	if (second > 0)
		memcpy((char *)dst + first, cb->cb_data, second);

	cb->cb_head = (cb->cb_head + n) % cb->cb_size;
	cb->cb_used -= n;
	return (n);
}
