/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sim.c -- Chapter 17 simulated hardware backend.
 *
 * Adds callouts that drive autonomous register changes, a
 * command-scheduling callout for command-triggered delays, and the
 * fault-injection framework. Depends on Chapter 16's register access
 * layer (myfirst_hw.h, myfirst_hw.c).
 *
 * Simulation architecture:
 *   - sensor_callout: periodic, updates SENSOR every SENSOR_CONFIG.interval ms
 *   - command_callout: one-shot, scheduled by CTRL.GO, fires after DELAY_MS ms
 *   - busy_callout: periodic 50 ms, re-asserts STATUS.BUSY when FAULT_STUCK_BUSY
 *
 * Locking: all three callouts are initialised with sc->mtx, so every
 * callback runs with sc->mtx held automatically.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <sys/libkern.h>
#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_sim.h"

static void myfirst_sim_sensor_cb(void *arg);
static void myfirst_sim_command_cb(void *arg);
static void myfirst_sim_busy_cb(void *arg);
static bool myfirst_sim_should_fault(struct myfirst_softc *sc);

/*
 * Attach: allocate state, initialise callouts, seed default
 * configuration registers. The simulation is not started yet; the
 * caller calls myfirst_sim_enable under the lock after attach finishes.
 */
int
myfirst_sim_attach(struct myfirst_softc *sc)
{
	struct myfirst_sim *sim;

	sim = malloc(sizeof(*sim), M_MYFIRST, M_WAITOK | M_ZERO);

	callout_init_mtx(&sim->sensor_callout, &sc->mtx, 0);
	callout_init_mtx(&sim->command_callout, &sc->mtx, 0);
	callout_init_mtx(&sim->busy_callout, &sc->mtx, 0);

	sim->sensor_baseline = 0x1000;

	MYFIRST_LOCK(sc);
	CSR_WRITE_4(sc, MYFIRST_REG_SENSOR_CONFIG,
	    (100u << MYFIRST_SCFG_INTERVAL_SHIFT) |
	    (64u << MYFIRST_SCFG_AMPLITUDE_SHIFT));
	CSR_WRITE_4(sc, MYFIRST_REG_DELAY_MS, 500);
	CSR_WRITE_4(sc, MYFIRST_REG_FAULT_MASK, 0);
	CSR_WRITE_4(sc, MYFIRST_REG_FAULT_PROB, 0);
	CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, 0);
	MYFIRST_UNLOCK(sc);

	sc->sim = sim;
	return (0);
}

/*
 * Detach: stop callouts, drain them, free state. Must be called with
 * sc->mtx NOT held; callout_drain will sleep.
 */
void
myfirst_sim_detach(struct myfirst_softc *sc)
{
	struct myfirst_sim *sim;

	if (sc->sim == NULL)
		return;

	sim = sc->sim;
	sc->sim = NULL;

	callout_drain(&sim->sensor_callout);
	callout_drain(&sim->command_callout);
	callout_drain(&sim->busy_callout);

	free(sim, M_MYFIRST);
}

/*
 * Enable: start the periodic callouts. sc->mtx must be held.
 */
void
myfirst_sim_enable(struct myfirst_softc *sc)
{
	struct myfirst_sim *sim = sc->sim;
	uint32_t config, interval_ms;

	MYFIRST_LOCK_ASSERT(sc);

	if (sim->running)
		return;

	sim->running = true;

	config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
	interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
	    MYFIRST_SCFG_INTERVAL_SHIFT;
	if (interval_ms == 0)
		interval_ms = 100;

	callout_reset_sbt(&sim->sensor_callout,
	    interval_ms * SBT_1MS, 0,
	    myfirst_sim_sensor_cb, sc, 0);

	callout_reset_sbt(&sim->busy_callout,
	    50 * SBT_1MS, 0,
	    myfirst_sim_busy_cb, sc, 0);
}

/*
 * Disable: stop (do not drain) the callouts. sc->mtx must be held.
 */
void
myfirst_sim_disable(struct myfirst_softc *sc)
{
	struct myfirst_sim *sim = sc->sim;

	MYFIRST_LOCK_ASSERT(sc);

	if (!sim->running)
		return;

	sim->running = false;

	callout_stop(&sim->sensor_callout);
	callout_stop(&sim->command_callout);
	callout_stop(&sim->busy_callout);
}

/*
 * Sensor callout: fires periodically, updates the SENSOR register
 * with an oscillating value, re-arms itself.
 */
static void
myfirst_sim_sensor_cb(void *arg)
{
	struct myfirst_softc *sc = arg;
	struct myfirst_sim *sim = sc->sim;
	uint32_t config, interval_ms, amplitude, phase, value;

	MYFIRST_LOCK_ASSERT(sc);

	if (!sim->running)
		return;

	config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
	interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
	    MYFIRST_SCFG_INTERVAL_SHIFT;
	amplitude = (config & MYFIRST_SCFG_AMPLITUDE_MASK) >>
	    MYFIRST_SCFG_AMPLITUDE_SHIFT;

	sim->sensor_tick++;
	phase = sim->sensor_tick & 0x7;
	value = sim->sensor_baseline +
	    ((phase < 4) ? phase : (7 - phase)) * (amplitude / 4);

	CSR_WRITE_4(sc, MYFIRST_REG_SENSOR, value);

	if (interval_ms == 0)
		interval_ms = 100;

	callout_reset_sbt(&sim->sensor_callout,
	    interval_ms * SBT_1MS, 0,
	    myfirst_sim_sensor_cb, sc, 0);
}

/*
 * Command completion callout: one-shot. Honours any saved fault
 * state; otherwise completes the command by copying pending data
 * to DATA_OUT and setting STATUS.DATA_AV.
 */
static void
myfirst_sim_command_cb(void *arg)
{
	struct myfirst_softc *sc = arg;
	struct myfirst_sim *sim = sc->sim;
	uint32_t status, fault;

	MYFIRST_LOCK_ASSERT(sc);

	if (!sim->running || !sim->command_pending)
		return;

	fault = sim->pending_fault;

	status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
	status &= ~MYFIRST_STATUS_BUSY;

	if (fault & MYFIRST_FAULT_ERROR) {
		status |= MYFIRST_STATUS_ERROR;
		sc->stats.fault_injected++;
		sc->stats.fault_error++;
	} else if (fault & MYFIRST_FAULT_READ_1S) {
		CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, 0xFFFFFFFFu);
		status |= MYFIRST_STATUS_DATA_AV;
		sc->stats.fault_injected++;
		sc->stats.fault_read_1s++;
	} else {
		CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);
		status |= MYFIRST_STATUS_DATA_AV;
	}

	CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

	sim->op_counter++;
	CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

	sim->command_pending = false;
	sim->pending_fault = 0;
}

/*
 * Busy callout: periodic. When FAULT_STUCK_BUSY is set, re-assert
 * STATUS.BUSY so every command times out on wait_for_ready.
 */
static void
myfirst_sim_busy_cb(void *arg)
{
	struct myfirst_softc *sc = arg;
	struct myfirst_sim *sim = sc->sim;
	uint32_t fault_mask, status;

	MYFIRST_LOCK_ASSERT(sc);

	if (!sim->running)
		return;

	fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
	if (fault_mask & MYFIRST_FAULT_STUCK_BUSY) {
		status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
		if (!(status & MYFIRST_STATUS_BUSY)) {
			status |= MYFIRST_STATUS_BUSY;
			CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
			sc->stats.fault_injected++;
			sc->stats.fault_stuck_busy++;
		}
	}

	callout_reset_sbt(&sim->busy_callout,
	    50 * SBT_1MS, 0,
	    myfirst_sim_busy_cb, sc, 0);
}

/*
 * Should this operation have a fault injected? Consults FAULT_PROB.
 */
static bool
myfirst_sim_should_fault(struct myfirst_softc *sc)
{
	uint32_t prob, r;

	MYFIRST_LOCK_ASSERT(sc);

	prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
	if (prob == 0)
		return (false);
	if (prob >= 10000)
		return (true);

	r = arc4random_uniform(10000);
	return (r < prob);
}

/*
 * Called from myfirst_ctrl_update when CTRL.GO is written. Samples
 * DATA_IN, sets STATUS.BUSY, clears DATA_AV, schedules completion.
 */
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
	struct myfirst_sim *sim = sc->sim;
	uint32_t data_in, delay_ms, fault_mask, status;
	bool fault;

	MYFIRST_LOCK_ASSERT(sc);

	if (!sim->running)
		return;

	if (sim->command_pending) {
		sc->stats.cmd_rejected++;
		device_printf(sc->dev,
		    "sim: overlapping command; ignored\n");
		return;
	}

	data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
	sim->pending_data = data_in;

	status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
	status |= MYFIRST_STATUS_BUSY;
	status &= ~MYFIRST_STATUS_DATA_AV;
	CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

	delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
	if (delay_ms == 0)
		delay_ms = 1;

	sim->command_pending = true;

	fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
	fault = (fault_mask != 0) && myfirst_sim_should_fault(sc);

	if (fault && (fault_mask & MYFIRST_FAULT_TIMEOUT)) {
		/*
		 * Inject a timeout: do not schedule the completion
		 * callout. The driver's wait_for_data will time out
		 * and the recovery path will clean up.
		 */
		sc->stats.fault_injected++;
		sc->stats.fault_timeout++;
		device_printf(sc->dev,
		    "sim: injecting TIMEOUT fault\n");
		return;
	}

	sim->pending_fault = fault ? fault_mask : 0;

	callout_reset_sbt(&sim->command_callout,
	    delay_ms * SBT_1MS, 0,
	    myfirst_sim_command_cb, sc, 0);
}

/*
 * Register sysctls specific to the simulation.
 */
void
myfirst_sim_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid *tree = sc->sysctl_tree;

	SYSCTL_ADD_BOOL(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "sim_running", CTLFLAG_RD,
	    &sc->sim->running, 0,
	    "Whether the simulation callouts are active");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "sim_sensor_baseline", CTLFLAG_RW,
	    &sc->sim->sensor_baseline, 0,
	    "Baseline value around which SENSOR oscillates");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "sim_op_counter_mirror", CTLFLAG_RD,
	    &sc->sim->op_counter, 0,
	    "Mirror of OP_COUNTER (for observability)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "reg_delay_ms_set",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, MYFIRST_REG_DELAY_MS, myfirst_sysctl_reg_write,
	    "IU", "Command delay in ms (writeable)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "reg_sensor_config_set",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, MYFIRST_REG_SENSOR_CONFIG, myfirst_sysctl_reg_write,
	    "IU", "Sensor config interval|amplitude (writeable)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "reg_fault_mask_set",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, MYFIRST_REG_FAULT_MASK, myfirst_sysctl_reg_write,
	    "IU", "Fault mask (writeable)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "reg_fault_prob_set",
	    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, MYFIRST_REG_FAULT_PROB, myfirst_sysctl_reg_write,
	    "IU", "Fault probability 0..10000 (writeable)");
}
