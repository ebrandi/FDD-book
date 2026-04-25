#ifndef DEV_FLAGS_H
#define DEV_FLAGS_H

#include <stdio.h>
#include <stdint.h>

/*
 * Bit flags emulate how drivers track small on/off facts:
 * enabled, open, error, TX busy, RX ready. Each flag is a
 * single bit inside an unsigned integer.
 */
enum dev_flags {
	DF_ENABLED  = 1u << 0,	/* 00001 */
	DF_OPEN     = 1u << 1,	/* 00010 */
	DF_ERROR    = 1u << 2,	/* 00100 */
	DF_TX_BUSY  = 1u << 3,	/* 01000 */
	DF_RX_READY = 1u << 4	/* 10000 */
};

/* Small helpers keep bit logic readable and portable. */
static inline void set_flag(uint32_t *st, uint32_t f)    { *st |= f; }
static inline void clear_flag(uint32_t *st, uint32_t f)  { *st &= ~f; }
static inline void toggle_flag(uint32_t *st, uint32_t f) { *st ^= f; }
static inline int  test_flag(uint32_t st, uint32_t f)    { return (st & f) != 0; }

/* Summary printer lives in flags.c to keep main.c tidy. */
void print_state(uint32_t st);

#endif
