/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_intr.c -- Chapter 19 interrupt-handling layer.
 *
 * Implements:
 *   - The filter handler (myfirst_intr_filter).
 *   - The deferred taskqueue task (myfirst_intr_data_task_fn).
 *   - The setup and teardown helpers called from the PCI layer.
 *   - The interrupt-related sysctls, including the simulated-
 *     interrupt sysctl for testing.
 *
 * The filter runs in primary interrupt context: it must not sleep,
 * must not take sc->mtx, and should return quickly. The deferred
 * task runs in thread context on a single-worker taskqueue.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/types.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"

/* Forward declarations. */
static void myfirst_intr_data_task_fn(void *arg, int npending);
static int  myfirst_intr_simulate_sysctl(SYSCTL_HANDLER_ARGS);

/* ------------------------------------------------------------------ */
/* Filter handler                                                     */
/* ------------------------------------------------------------------ */

int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;
	uint32_t status;
	int rv = 0;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if (status == 0)
		return (FILTER_STRAY);

	atomic_add_64(&sc->intr_count, 1);

	if (status & MYFIRST_INTR_DATA_AV) {
		atomic_add_64(&sc->intr_data_av_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		if (sc->intr_tq != NULL)
			taskqueue_enqueue(sc->intr_tq, &sc->intr_data_task);
		rv |= FILTER_HANDLED;
	}

	if (status & MYFIRST_INTR_ERROR) {
		atomic_add_64(&sc->intr_error_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_ERROR);
		rv |= FILTER_HANDLED;
	}

	if (status & MYFIRST_INTR_COMPLETE) {
		atomic_add_64(&sc->intr_complete_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		rv |= FILTER_HANDLED;
	}

	if (rv == 0)
		return (FILTER_STRAY);

	return (rv);
}

/* ------------------------------------------------------------------ */
/* Deferred task                                                      */
/* ------------------------------------------------------------------ */

static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	struct myfirst_softc *sc = arg;
	uint32_t data;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}
	data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_last_data = data;
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);
	MYFIRST_UNLOCK(sc);
}

/* ------------------------------------------------------------------ */
/* Setup                                                              */
/* ------------------------------------------------------------------ */

int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		device_printf(sc->dev, "bus_setup_intr failed (%d)\n",
		    error);
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie,
	    "legacy");

	/* Enable the interrupts we care about at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	device_printf(sc->dev,
	    "attached filter handler on IRQ resource\n");
	return (0);
}

/* ------------------------------------------------------------------ */
/* Teardown                                                           */
/* ------------------------------------------------------------------ */

void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable device-side interrupts first. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the kernel handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the deferred-work taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* Simulated interrupts                                               */
/* ------------------------------------------------------------------ */

static int
myfirst_intr_simulate_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	uint32_t mask = 0;
	int error;

	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || sc->bar_res == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	(void)myfirst_intr_filter(sc);
	return (0);
}

/* ------------------------------------------------------------------ */
/* sysctl registration                                                */
/* ------------------------------------------------------------------ */

void
myfirst_intr_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "intr_count",
	    CTLFLAG_RD, &sc->intr_count, 0,
	    "Total interrupt filter invocations");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "intr_data_av_count",
	    CTLFLAG_RD, &sc->intr_data_av_count, 0,
	    "DATA_AV events seen");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "intr_error_count",
	    CTLFLAG_RD, &sc->intr_error_count, 0,
	    "ERROR events seen");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "intr_complete_count",
	    CTLFLAG_RD, &sc->intr_complete_count, 0,
	    "COMPLETE events seen");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "intr_task_invocations",
	    CTLFLAG_RD, &sc->intr_task_invocations, 0,
	    "Deferred task runs");
	SYSCTL_ADD_UINT(ctx, kids, OID_AUTO, "intr_last_data",
	    CTLFLAG_RD, &sc->intr_last_data, 0,
	    "Last DATA_OUT value read by the task");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_intr_simulate_sysctl, "IU",
	    "Simulate an interrupt by setting INTR_STATUS bits and "
	    "invoking the filter");
}
