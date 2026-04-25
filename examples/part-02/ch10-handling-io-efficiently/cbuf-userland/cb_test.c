/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * cb_test.c: simple sanity tests for the cbuf userland implementation.
 */

#include "cbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) \
	do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)

static void
test_basic(void)
{
	struct cbuf cb;
	char in[8] = "ABCDEFGH";
	char out[8] = {0};
	size_t n;

	CHECK(cbuf_init(&cb, 8) == 0, "init");
	CHECK(cbuf_used(&cb) == 0, "init used");
	CHECK(cbuf_free(&cb) == 8, "init free");

	n = cbuf_write(&cb, in, 4);
	CHECK(n == 4, "write 4");
	CHECK(cbuf_used(&cb) == 4, "used after write 4");

	n = cbuf_read(&cb, out, 2);
	CHECK(n == 2, "read 2");
	CHECK(memcmp(out, "AB", 2) == 0, "AB content");
	CHECK(cbuf_used(&cb) == 2, "used after read 2");

	cbuf_destroy(&cb);
	printf("test_basic OK\n");
}

static void
test_wrap(void)
{
	struct cbuf cb;
	char in[8] = "ABCDEFGH";
	char out[8] = {0};
	size_t n;

	CHECK(cbuf_init(&cb, 8) == 0, "init");

	/* Push head forward by writing and reading 6 bytes. */
	n = cbuf_write(&cb, in, 6);
	CHECK(n == 6, "write 6");
	n = cbuf_read(&cb, out, 6);
	CHECK(n == 6, "read 6");

	/* Now write 6 more, which should wrap. */
	n = cbuf_write(&cb, in, 6);
	CHECK(n == 6, "write 6 after wrap");
	CHECK(cbuf_used(&cb) == 6, "used after wrap write");

	/* Read all of it back; should return ABCDEF. */
	memset(out, 0, sizeof(out));
	n = cbuf_read(&cb, out, 6);
	CHECK(n == 6, "read 6 after wrap");
	CHECK(memcmp(out, "ABCDEF", 6) == 0, "content after wrap");
	CHECK(cbuf_used(&cb) == 0, "empty after drain");

	cbuf_destroy(&cb);
	printf("test_wrap OK\n");
}

static void
test_partial(void)
{
	struct cbuf cb;
	char in[8] = "12345678";
	char out[8] = {0};
	size_t n;

	CHECK(cbuf_init(&cb, 4) == 0, "init small");

	n = cbuf_write(&cb, in, 8);
	CHECK(n == 4, "write clamps to free space");
	CHECK(cbuf_used(&cb) == 4, "buffer full");

	n = cbuf_read(&cb, out, 8);
	CHECK(n == 4, "read clamps to live data");
	CHECK(memcmp(out, "1234", 4) == 0, "content of partial");
	CHECK(cbuf_used(&cb) == 0, "buffer empty after partial drain");

	cbuf_destroy(&cb);
	printf("test_partial OK\n");
}

static void
test_alternation(void)
{
	struct cbuf cb;
	char c, out;
	int i;

	CHECK(cbuf_init(&cb, 16) == 0, "init alt");
	for (i = 0; i < 100; i++) {
		c = (char)('a' + (i % 26));
		CHECK(cbuf_write(&cb, &c, 1) == 1, "alt write");
		CHECK(cbuf_read(&cb, &out, 1) == 1, "alt read");
		CHECK(out == c, "alt match");
	}
	CHECK(cbuf_used(&cb) == 0, "alt empty");
	cbuf_destroy(&cb);
	printf("test_alternation OK\n");
}

int
main(void)
{
	test_basic();
	test_wrap();
	test_partial();
	test_alternation();
	printf("all tests OK\n");
	return (0);
}
