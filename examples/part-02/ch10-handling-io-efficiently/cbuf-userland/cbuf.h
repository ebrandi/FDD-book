/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * cbuf.h: a fixed-size byte-oriented circular buffer.
 *
 * The buffer does not lock; the caller is responsible for whatever
 * synchronization the surrounding system requires.
 */

#ifndef CBUF_H
#define CBUF_H

#include <stddef.h>

struct cbuf {
	char	*cb_data;	/* backing storage */
	size_t	 cb_size;	/* total capacity */
	size_t	 cb_head;	/* next byte to read */
	size_t	 cb_used;	/* live byte count */
};

int	cbuf_init(struct cbuf *cb, size_t size);
void	cbuf_destroy(struct cbuf *cb);
void	cbuf_reset(struct cbuf *cb);

size_t	cbuf_size(const struct cbuf *cb);
size_t	cbuf_used(const struct cbuf *cb);
size_t	cbuf_free(const struct cbuf *cb);

size_t	cbuf_write(struct cbuf *cb, const void *src, size_t n);
size_t	cbuf_read(struct cbuf *cb, void *dst, size_t n);

#endif /* CBUF_H */
