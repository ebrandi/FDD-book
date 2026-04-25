/*
 * safeprobe.c - Implementation of the safe register-access
 * wrappers.
 *
 * Companion to Chapter 36, Lab 5.
 *
 * See safeprobe.h for the rationale and the public interface.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "safeprobe.h"

void
safeprobe_init(struct safeprobe_ctx *ctx, device_t dev,
    struct resource *res)
{
	ctx->sp_dev = dev;
	ctx->sp_mem = res;
	mtx_init(&ctx->sp_mtx, "safeprobe", NULL, MTX_DEF);
	ctx->sp_log_reads = 0;
	ctx->sp_log_writes = 1;
	ctx->sp_read_only = 0;
	ctx->sp_n_reads = 0;
	ctx->sp_n_writes = 0;
	ctx->sp_n_failures = 0;
}

void
safeprobe_destroy(struct safeprobe_ctx *ctx)
{
	mtx_destroy(&ctx->sp_mtx);
	ctx->sp_mem = NULL;
}

uint32_t
safeprobe_read(struct safeprobe_ctx *ctx, bus_size_t off)
{
	uint32_t val;

	mtx_lock(&ctx->sp_mtx);
	val = bus_read_4(ctx->sp_mem, off);
	ctx->sp_n_reads++;
	if (ctx->sp_log_reads) {
		device_printf(ctx->sp_dev,
		    "safeprobe READ  off=0x%04jx val=0x%08x\n",
		    (uintmax_t)off, val);
	}
	mtx_unlock(&ctx->sp_mtx);
	return (val);
}

int
safeprobe_write(struct safeprobe_ctx *ctx, bus_size_t off,
    uint32_t value)
{
	mtx_lock(&ctx->sp_mtx);
	if (ctx->sp_read_only) {
		if (ctx->sp_log_writes) {
			device_printf(ctx->sp_dev,
			    "safeprobe WRITE-SUPPRESSED off=0x%04jx "
			    "val=0x%08x (read-only mode)\n",
			    (uintmax_t)off, value);
		}
		mtx_unlock(&ctx->sp_mtx);
		return (EROFS);
	}
	if (ctx->sp_log_writes) {
		device_printf(ctx->sp_dev,
		    "safeprobe WRITE off=0x%04jx val=0x%08x\n",
		    (uintmax_t)off, value);
	}
	bus_write_4(ctx->sp_mem, off, value);
	ctx->sp_n_writes++;
	mtx_unlock(&ctx->sp_mtx);
	return (0);
}

int
safeprobe_set_field(struct safeprobe_ctx *ctx, bus_size_t off,
    uint32_t mask, uint32_t value)
{
	uint32_t old, new, readback;

	mtx_lock(&ctx->sp_mtx);
	old = bus_read_4(ctx->sp_mem, off);
	new = (old & ~mask) | (value & mask);

	if (ctx->sp_read_only) {
		if (ctx->sp_log_writes) {
			device_printf(ctx->sp_dev,
			    "safeprobe SETFIELD-SUPPRESSED off=0x%04jx "
			    "mask=0x%08x value=0x%08x\n",
			    (uintmax_t)off, mask, value);
		}
		mtx_unlock(&ctx->sp_mtx);
		return (EROFS);
	}

	if (ctx->sp_log_writes) {
		device_printf(ctx->sp_dev,
		    "safeprobe SETFIELD off=0x%04jx mask=0x%08x "
		    "old=0x%08x new=0x%08x\n",
		    (uintmax_t)off, mask, old, new);
	}
	bus_write_4(ctx->sp_mem, off, new);
	ctx->sp_n_writes++;

	readback = bus_read_4(ctx->sp_mem, off);
	if ((readback & mask) != (value & mask)) {
		ctx->sp_n_failures++;
		device_printf(ctx->sp_dev,
		    "safeprobe SETFIELD off=0x%04jx mask=0x%08x "
		    "value=0x%08x readback=0x%08x mismatch\n",
		    (uintmax_t)off, mask, value, readback);
		mtx_unlock(&ctx->sp_mtx);
		return (EIO);
	}
	mtx_unlock(&ctx->sp_mtx);
	return (0);
}

int
safeprobe_wait(struct safeprobe_ctx *ctx, bus_size_t off,
    uint32_t mask, uint32_t value, int timeout_us)
{
	uint32_t observed;
	int waited_us = 0;
	const int step_us = 10;

	for (;;) {
		mtx_lock(&ctx->sp_mtx);
		observed = bus_read_4(ctx->sp_mem, off);
		ctx->sp_n_reads++;
		mtx_unlock(&ctx->sp_mtx);

		if ((observed & mask) == (value & mask))
			return (0);

		if (waited_us >= timeout_us) {
			ctx->sp_n_failures++;
			device_printf(ctx->sp_dev,
			    "safeprobe WAIT timeout off=0x%04jx "
			    "mask=0x%08x expected=0x%08x last=0x%08x\n",
			    (uintmax_t)off, mask, value, observed);
			return (ETIMEDOUT);
		}
		DELAY(step_us);
		waited_us += step_us;
	}
}
