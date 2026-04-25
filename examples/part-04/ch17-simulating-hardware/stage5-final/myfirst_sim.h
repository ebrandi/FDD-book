/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sim.h -- Chapter 17 simulation API.
 *
 * The simulation layer turns the Chapter 16 static register block into
 * a dynamic device: autonomous sensor updates, command-triggered
 * delayed completions, read-to-clear semantics for INTR_STATUS, and a
 * fault-injection framework. The API is small; most of the simulation
 * is in myfirst_sim.c.
 */

#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

#include <sys/callout.h>
#include <sys/stdbool.h>
#include <sys/types.h>

struct myfirst_softc;

/*
 * Simulation state. One per driver instance. Allocated in
 * myfirst_sim_attach, freed in myfirst_sim_detach.
 */
struct myfirst_sim {
	/* The three simulation callouts. */
	struct callout		sensor_callout;
	struct callout		command_callout;
	struct callout		busy_callout;

	/* Last scheduled command's data. Saved so command_cb can
	 * latch DATA_OUT when it fires. */
	uint32_t		pending_data;

	/* Saved fault state for this command. Set in start_command,
	 * consumed by command_cb. */
	uint32_t		pending_fault;

	/* Whether a command is currently in flight. */
	bool			command_pending;

	/* Baseline sensor value; the sensor callout oscillates
	 * around this. */
	uint32_t		sensor_baseline;

	/* Counter used by the sensor oscillation algorithm. */
	uint32_t		sensor_tick;

	/* Local operation counter; mirrors OP_COUNTER register. */
	uint32_t		op_counter;

	/* Whether the simulation callouts are running. Checked by
	 * every callout before doing work. */
	bool			running;
};

/*
 * API. All functions assume sc->sim is valid (that is,
 * myfirst_sim_attach has been called successfully) unless noted
 * otherwise.
 */

int  myfirst_sim_attach(struct myfirst_softc *sc);
void myfirst_sim_detach(struct myfirst_softc *sc);
void myfirst_sim_enable(struct myfirst_softc *sc);
void myfirst_sim_disable(struct myfirst_softc *sc);
void myfirst_sim_start_command(struct myfirst_softc *sc);
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
