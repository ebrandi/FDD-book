/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_hw.c -- Chapter 16 Stage 4 hardware access layer.
 *
 * Implements:
 *   - Allocation/teardown of the simulated register block.
 *   - The bus_space(9)-based accessor functions.
 *   - The register ticker task.
 *   - The sysctl handlers for register views and the access log.
 *
 * The main driver in myfirst.c calls myfirst_hw_attach in attach and
 * myfirst_hw_detach in detach.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <sys/taskqueue.h>
#include <sys/time.h>
#include <machine/bus.h>

#include "myfirst.h"       /* struct myfirst_softc, MYFIRST_LOCK, ASSERT */
#include "myfirst_hw.h"

/*
 * Use the driver's per-module malloc type. The MALLOC_DEFINE lives in
 * myfirst.c (the main driver file); this declaration just makes the
 * identifier visible here. If you prefer to keep M_DEVBUF (Chapter 15's
 * convention) you can simply replace M_MYFIRST with M_DEVBUF in this
 * file and drop both the MALLOC_DEFINE and MALLOC_DECLARE lines.
 */
MALLOC_DECLARE(M_MYFIRST);

/*
 * Helper: convert a nanouptime timespec to a single 64-bit value for
 * the access log's timestamp field.
 */
static inline uint64_t
myfirst_hw_now_ns(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return ((uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec);
}

/*
 * Context-tag helper. Returns a single-character code describing the
 * caller's context. Used only for diagnostics; the classification is
 * approximate.
 */
static inline uint8_t
myfirst_hw_context_tag(void)
{
	/* The simple version. A richer one would inspect curthread. */
	return ('d');
}

uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
	struct myfirst_hw *hw;
	uint32_t value;

	MYFIRST_ASSERT(sc);
	hw = sc->hw;
	KASSERT(hw != NULL,
	    ("myfirst: reg_read with hw==NULL"));
	KASSERT(offset + 4 <= hw->regs_size,
	    ("myfirst: register read past end: offset=%#x size=%zu",
	     (unsigned)offset, hw->regs_size));

	value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);

	if (hw->access_log_enabled) {
		unsigned int idx;
		struct myfirst_access_log_entry *e;

		idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
		e = &hw->access_log[idx];
		e->timestamp_ns = myfirst_hw_now_ns();
		e->value = value;
		e->offset = offset;
		e->is_write = 0;
		e->width = 4;
		e->context_tag = myfirst_hw_context_tag();
	}

	return (value);
}

void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
	struct myfirst_hw *hw;

	MYFIRST_ASSERT(sc);
	hw = sc->hw;
	KASSERT(hw != NULL,
	    ("myfirst: reg_write with hw==NULL"));
	KASSERT(offset + 4 <= hw->regs_size,
	    ("myfirst: register write past end: offset=%#x size=%zu",
	     (unsigned)offset, hw->regs_size));

	bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);

	if (hw->access_log_enabled) {
		unsigned int idx;
		struct myfirst_access_log_entry *e;

		idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
		e = &hw->access_log[idx];
		e->timestamp_ns = myfirst_hw_now_ns();
		e->value = value;
		e->offset = offset;
		e->is_write = 1;
		e->width = 4;
		e->context_tag = myfirst_hw_context_tag();
	}
}

void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
	uint32_t v;

	MYFIRST_ASSERT(sc);
	v = myfirst_reg_read(sc, offset);
	v &= ~clear_mask;
	v |= set_mask;
	myfirst_reg_write(sc, offset, v);
}

void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
	struct myfirst_hw *hw;

	MYFIRST_ASSERT(sc);
	hw = sc->hw;
	myfirst_reg_write(sc, offset, value);
	bus_space_barrier(hw->regs_tag, hw->regs_handle, 0, hw->regs_size,
	    flags);
}

/*
 * The register ticker task: increments SCRATCH_A per invocation.
 * Enqueued from the tick_source callout when reg_ticker_enabled is
 * non-zero.
 */
static void
myfirst_hw_ticker_cb(void *arg, int pending __unused)
{
	struct myfirst_softc *sc = arg;

	if (!myfirst_is_attached(sc))
		return;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->hw->regs_buf != NULL) {
		uint32_t v = CSR_READ_4(sc, MYFIRST_REG_SCRATCH_A);
		CSR_WRITE_4(sc, MYFIRST_REG_SCRATCH_A, v + 1);
	}
	MYFIRST_UNLOCK(sc);
}

int
myfirst_hw_attach(struct myfirst_softc *sc)
{
	struct myfirst_hw *hw;

	hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

	hw->regs_size = MYFIRST_REG_SIZE;
	hw->regs_buf = malloc(hw->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
	hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation path supports x86 only; see chapter text."
#endif
	hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;

	/* Initialise the task. */
	TASK_INIT(&hw->reg_ticker_task, 0, myfirst_hw_ticker_cb, sc);

	/*
	 * Initialise the fixed registers. We do this outside the mutex
	 * because nothing else can see the hw struct yet.
	 */
	bus_space_write_4(hw->regs_tag, hw->regs_handle,
	    MYFIRST_REG_DEVICE_ID, MYFIRST_DEVICE_ID_VALUE);
	bus_space_write_4(hw->regs_tag, hw->regs_handle,
	    MYFIRST_REG_FIRMWARE_REV, MYFIRST_FW_REV_VALUE);
	bus_space_write_4(hw->regs_tag, hw->regs_handle,
	    MYFIRST_REG_STATUS, MYFIRST_STATUS_READY);

	sc->hw = hw;
	device_printf(sc->dev,
	    "hardware layer attached: %zu bytes at %p\n",
	    hw->regs_size, hw->regs_buf);
	return (0);
}

void
myfirst_hw_detach(struct myfirst_softc *sc)
{
	struct myfirst_hw *hw;

	if (sc->hw == NULL)
		return;

	hw = sc->hw;

	/*
	 * Drain the ticker task before freeing anything it touches.
	 * The main detach has already cleared is_attached, which stops
	 * the tick_source callout from enqueuing new work. Anything
	 * already in flight completes here.
	 */
	if (sc->tq != NULL)
		taskqueue_drain(sc->tq, &hw->reg_ticker_task);

	/* Clear sc->hw before freeing so concurrent readers see NULL. */
	sc->hw = NULL;

	if (hw->regs_buf != NULL) {
		free(hw->regs_buf, M_MYFIRST);
		hw->regs_buf = NULL;
	}
	free(hw, M_MYFIRST);

	device_printf(sc->dev, "hardware layer detached\n");
}

/*
 * Sysctl handler for read-only register views. arg1 is the softc,
 * arg2 is the register offset.
 */
static int
myfirst_hw_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	bus_size_t offset = arg2;
	uint32_t value;

	if (!myfirst_is_attached(sc))
		return (ENODEV);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	value = CSR_READ_4(sc, offset);
	MYFIRST_UNLOCK(sc);

	return (sysctl_handle_int(oidp, &value, 0, req));
}

/*
 * Sysctl handler for writeable register views. arg1 is the softc,
 * arg2 is the register offset.
 */
static int
myfirst_hw_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	bus_size_t offset = arg2;
	uint32_t oldval, newval;
	int error;

	if (!myfirst_is_attached(sc))
		return (ENODEV);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	oldval = CSR_READ_4(sc, offset);
	MYFIRST_UNLOCK(sc);

	newval = oldval;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	CSR_WRITE_4(sc, offset, newval);
	MYFIRST_UNLOCK(sc);

	return (0);
}

/*
 * Sysctl handler for the access log dump. Walks the ring and formats
 * each entry as a line.
 */
static int
myfirst_hw_sysctl_access_log(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	struct sbuf *sb;
	unsigned int i, start;
	int error;

	sb = sbuf_new_for_sysctl(NULL, NULL,
	    256 * MYFIRST_ACCESS_LOG_SIZE, req);
	if (sb == NULL)
		return (ENOMEM);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		sbuf_printf(sb, "(hardware layer not attached)\n");
		error = sbuf_finish(sb);
		sbuf_delete(sb);
		return (error);
	}

	start = sc->hw->access_log_head;
	for (i = 0; i < MYFIRST_ACCESS_LOG_SIZE; i++) {
		unsigned int idx = (start + i) % MYFIRST_ACCESS_LOG_SIZE;
		struct myfirst_access_log_entry *e = &sc->hw->access_log[idx];

		if (e->timestamp_ns == 0)
			continue;
		sbuf_printf(sb,
		    "%16ju ns  %s%1u  off=%#04x  val=%#010x  ctx=%c\n",
		    (uintmax_t)e->timestamp_ns,
		    e->is_write ? "W" : "R", (unsigned)e->width,
		    (unsigned)e->offset, e->value, e->context_tag);
	}
	MYFIRST_UNLOCK(sc);

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

/*
 * Register the Chapter 16 sysctls. Called from myfirst_attach after
 * the sysctl tree and context are ready.
 */
void
myfirst_hw_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

#define ADD_REG_RD(name, offset, desc) \
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, name, \
	    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, \
	    sc, offset, myfirst_hw_sysctl_reg, "IU", desc)

	ADD_REG_RD("reg_ctrl",         MYFIRST_REG_CTRL,
	    "Control register (read-only view)");
	ADD_REG_RD("reg_status",       MYFIRST_REG_STATUS,
	    "Status register (read-only view)");
	ADD_REG_RD("reg_data_in",      MYFIRST_REG_DATA_IN,
	    "DATA_IN register (read-only view)");
	ADD_REG_RD("reg_data_out",     MYFIRST_REG_DATA_OUT,
	    "DATA_OUT register (read-only view)");
	ADD_REG_RD("reg_intr_mask",    MYFIRST_REG_INTR_MASK,
	    "INTR_MASK register (read-only view)");
	ADD_REG_RD("reg_intr_status",  MYFIRST_REG_INTR_STATUS,
	    "INTR_STATUS register (read-only view)");
	ADD_REG_RD("reg_device_id",    MYFIRST_REG_DEVICE_ID,
	    "DEVICE_ID register (read-only)");
	ADD_REG_RD("reg_firmware_rev", MYFIRST_REG_FIRMWARE_REV,
	    "FIRMWARE_REV register (read-only)");
	ADD_REG_RD("reg_scratch_a",    MYFIRST_REG_SCRATCH_A,
	    "SCRATCH_A register (read-only view)");
	ADD_REG_RD("reg_scratch_b",    MYFIRST_REG_SCRATCH_B,
	    "SCRATCH_B register (read-only view)");

#undef ADD_REG_RD

	/* Writeable CTRL register for Stage 1 demonstration. */
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "reg_ctrl_set",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, MYFIRST_REG_CTRL, myfirst_hw_sysctl_reg_write, "IU",
	    "Control register (writeable, Stage 1 test aid)");

	/* Ticker enable. */
	SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "reg_ticker_enabled",
	    CTLFLAG_RW, &sc->hw->reg_ticker_enabled, 0,
	    "Enable the periodic SCRATCH_A ticker");

	/* Access log controls. */
	SYSCTL_ADD_BOOL(ctx, kids, OID_AUTO, "access_log_enabled",
	    CTLFLAG_RW, &sc->hw->access_log_enabled, 0,
	    "Enable register access ring-buffer logging");

	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "access_log",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_hw_sysctl_access_log, "A",
	    "Dump the register access ring buffer");
}
