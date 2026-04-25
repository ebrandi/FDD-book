/*-
 * Chapter 22 Stage 4: myfirst_power.c
 *
 * Consolidated power-management subsystem for the myfirst driver.
 * Moves the power code from myfirst_pci.c (Stage 1-3) into its own
 * compilation unit with a documented public API.
 *
 * This file implements the API declared in myfirst_power.h:
 * - myfirst_power_setup / myfirst_power_teardown: lifecycle.
 * - myfirst_power_suspend / _resume / _shutdown: the three kobj
 *   power-management methods.
 * - myfirst_power_runtime_suspend / _resume / mark_active: optional
 *   runtime PM, compiled only with -DMYFIRST_ENABLE_RUNTIME_PM.
 * - myfirst_power_add_sysctls: registers counters and policy knobs.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/time.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
#include "myfirst_msix.h"
#include "myfirst_power.h"

static void	myfirst_mask_interrupts(struct myfirst_softc *sc);
static int	myfirst_drain_dma(struct myfirst_softc *sc);
static void	myfirst_drain_workers(struct myfirst_softc *sc);
static int	myfirst_quiesce(struct myfirst_softc *sc);
static int	myfirst_restore(struct myfirst_softc *sc);

#ifdef MYFIRST_ENABLE_RUNTIME_PM
static void	myfirst_idle_watcher_cb(void *arg);
#endif

/* ----- Quiesce helpers ----- */

static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
	MYFIRST_ASSERT_UNLOCKED(sc);

	sc->saved_intr_mask = CSR_READ_4(sc, MYFIRST_REG_INTR_MASK);

	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}

static int
myfirst_drain_dma(struct myfirst_softc *sc)
{
	int err = 0;

	MYFIRST_LOCK(sc);
	if (sc->dma_in_flight) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
		    MYFIRST_DMA_CTRL_ABORT);

		err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
		if (err == EWOULDBLOCK) {
			device_printf(sc->dev,
			    "drain_dma: timeout waiting for abort\n");
			sc->dma_in_flight = false;
		}
	}
	MYFIRST_UNLOCK(sc);

	return (err == EWOULDBLOCK ? ETIMEDOUT : 0);
}

static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
	if (sc->sim != NULL)
		myfirst_sim_drain_dma_callout(sc->sim);

	if (sc->rx_vector.has_task)
		taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);
}

static int
myfirst_quiesce(struct myfirst_softc *sc)
{
	int err;

	MYFIRST_LOCK(sc);
	if (sc->suspended) {
		MYFIRST_UNLOCK(sc);
		return (0);
	}
	sc->suspended = true;
	MYFIRST_UNLOCK(sc);

	myfirst_mask_interrupts(sc);

	err = myfirst_drain_dma(sc);
	if (err != 0) {
		device_printf(sc->dev,
		    "quiesce: DMA did not stop cleanly (err %d)\n", err);
	}

	myfirst_drain_workers(sc);

	KASSERT(sc->dma_in_flight == false,
	    ("myfirst: dma_in_flight still true after drain"));

	return (err);
}

/* ----- Restore helper ----- */

static int
myfirst_restore(struct myfirst_softc *sc)
{
	pci_enable_busmaster(sc->dev);

	if (sc->saved_intr_mask == 0xFFFFFFFF) {
		sc->saved_intr_mask = ~MYFIRST_INTR_COMPLETE;
	}
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);

	MYFIRST_LOCK(sc);
	sc->suspended = false;
	MYFIRST_UNLOCK(sc);

	return (0);
}

/* ----- Public kobj-method implementations ----- */

int
myfirst_power_suspend(struct myfirst_softc *sc)
{
	int err;

	device_printf(sc->dev, "suspend: starting\n");

	err = myfirst_quiesce(sc);
	if (err != 0) {
		device_printf(sc->dev,
		    "suspend: quiesce returned %d; continuing\n", err);
	}

	atomic_add_64(&sc->power_suspend_count, 1);
	device_printf(sc->dev,
	    "suspend: complete (dma in flight=%d, suspended=%d)\n",
	    sc->dma_in_flight, sc->suspended);
	return (0);
}

int
myfirst_power_resume(struct myfirst_softc *sc)
{
	int err;

	device_printf(sc->dev, "resume: starting\n");

	err = myfirst_restore(sc);
	if (err != 0) {
		device_printf(sc->dev,
		    "resume: restore failed (err %d)\n", err);
		atomic_add_64(&sc->power_resume_errors, 1);
	}

	atomic_add_64(&sc->power_resume_count, 1);
	device_printf(sc->dev, "resume: complete\n");
	return (0);
}

int
myfirst_power_shutdown(struct myfirst_softc *sc)
{
	device_printf(sc->dev, "shutdown: starting\n");
	(void)myfirst_quiesce(sc);
	atomic_add_64(&sc->power_shutdown_count, 1);
	device_printf(sc->dev, "shutdown: complete\n");
	return (0);
}

/* ----- Runtime PM ----- */

#ifdef MYFIRST_ENABLE_RUNTIME_PM

int
myfirst_power_runtime_suspend(struct myfirst_softc *sc)
{
	int err;

	device_printf(sc->dev, "runtime suspend: starting\n");

	err = myfirst_quiesce(sc);
	if (err != 0) {
		device_printf(sc->dev,
		    "runtime suspend: quiesce failed (err %d)\n", err);
		MYFIRST_LOCK(sc);
		sc->suspended = false;
		MYFIRST_UNLOCK(sc);
		return (err);
	}

	pci_save_state(sc->dev);
	err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
	if (err != 0) {
		device_printf(sc->dev,
		    "runtime suspend: set_powerstate(D3) failed (err %d)\n",
		    err);
		pci_restore_state(sc->dev);
		myfirst_restore(sc);
		return (err);
	}

	MYFIRST_LOCK(sc);
	sc->runtime_state = MYFIRST_RT_SUSPENDED;
	MYFIRST_UNLOCK(sc);
	atomic_add_64(&sc->runtime_suspend_count, 1);

	device_printf(sc->dev, "runtime suspend: device in D3\n");
	return (0);
}

int
myfirst_power_runtime_resume(struct myfirst_softc *sc)
{
	int err;

	MYFIRST_LOCK(sc);
	if (sc->runtime_state != MYFIRST_RT_SUSPENDED) {
		MYFIRST_UNLOCK(sc);
		return (0);
	}
	MYFIRST_UNLOCK(sc);

	device_printf(sc->dev, "runtime resume: starting\n");

	err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
	if (err != 0) {
		device_printf(sc->dev,
		    "runtime resume: set_powerstate(D0) failed (err %d)\n",
		    err);
		return (err);
	}
	pci_restore_state(sc->dev);

	err = myfirst_restore(sc);
	if (err != 0) {
		device_printf(sc->dev,
		    "runtime resume: restore failed (err %d)\n", err);
		return (err);
	}

	MYFIRST_LOCK(sc);
	sc->runtime_state = MYFIRST_RT_RUNNING;
	MYFIRST_UNLOCK(sc);
	atomic_add_64(&sc->runtime_resume_count, 1);

	device_printf(sc->dev, "runtime resume: device in D0\n");
	return (0);
}

void
myfirst_power_mark_active(struct myfirst_softc *sc)
{
	MYFIRST_LOCK(sc);
	microtime(&sc->last_activity);
	MYFIRST_UNLOCK(sc);
}

static void
myfirst_idle_watcher_cb(void *arg)
{
	struct myfirst_softc *sc = arg;
	struct timeval now, diff;

	MYFIRST_ASSERT_LOCKED(sc);

	if (sc->runtime_state == MYFIRST_RT_RUNNING && !sc->suspended) {
		microtime(&now);
		timersub(&now, &sc->last_activity, &diff);

		if (diff.tv_sec >= sc->idle_threshold_seconds) {
			MYFIRST_UNLOCK(sc);
			(void)myfirst_power_runtime_suspend(sc);
			MYFIRST_LOCK(sc);
		}
	}

	callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}

#endif /* MYFIRST_ENABLE_RUNTIME_PM */

/* ----- Subsystem lifecycle ----- */

int
myfirst_power_setup(struct myfirst_softc *sc)
{
	sc->suspended = false;
	sc->saved_intr_mask = 0;
	sc->power_suspend_count = 0;
	sc->power_resume_count = 0;
	sc->power_shutdown_count = 0;
	sc->power_resume_errors = 0;

#ifdef MYFIRST_ENABLE_RUNTIME_PM
	sc->runtime_state = MYFIRST_RT_RUNNING;
	sc->idle_threshold_seconds = 5;
	sc->runtime_suspend_count = 0;
	sc->runtime_resume_count = 0;
	microtime(&sc->last_activity);
	callout_init_mtx(&sc->idle_watcher, &sc->mtx, 0);
	callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
#endif

	return (0);
}

void
myfirst_power_teardown(struct myfirst_softc *sc)
{
#ifdef MYFIRST_ENABLE_RUNTIME_PM
	callout_drain(&sc->idle_watcher);

	/* If we were runtime-suspended, bring the device back before the
	 * rest of detach runs. */
	if (sc->runtime_state == MYFIRST_RT_SUSPENDED)
		(void)myfirst_power_runtime_resume(sc);
#endif
}

/* ----- Sysctls ----- */

void
myfirst_power_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

	SYSCTL_ADD_BOOL(ctx, kids, OID_AUTO, "suspended",
	    CTLFLAG_RD, &sc->suspended, 0,
	    "Whether the driver is in the suspended state");

	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_suspend_count",
	    CTLFLAG_RD, &sc->power_suspend_count, 0,
	    "Number of times DEVICE_SUSPEND has been called");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_resume_count",
	    CTLFLAG_RD, &sc->power_resume_count, 0,
	    "Number of times DEVICE_RESUME has been called");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_shutdown_count",
	    CTLFLAG_RD, &sc->power_shutdown_count, 0,
	    "Number of times DEVICE_SHUTDOWN has been called");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_resume_errors",
	    CTLFLAG_RD, &sc->power_resume_errors, 0,
	    "Number of resume attempts that encountered an error");

#ifdef MYFIRST_ENABLE_RUNTIME_PM
	SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "idle_threshold_seconds",
	    CTLFLAG_RW, &sc->idle_threshold_seconds, 0,
	    "Runtime PM idle threshold (seconds)");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_suspend_count",
	    CTLFLAG_RD, &sc->runtime_suspend_count, 0,
	    "Runtime suspends performed");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_resume_count",
	    CTLFLAG_RD, &sc->runtime_resume_count, 0,
	    "Runtime resumes performed");
	SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "runtime_state",
	    CTLFLAG_RD, (int *)&sc->runtime_state, 0,
	    "Runtime state: 0=running, 1=suspended");
#endif
}
