/*
 * mockdev.c - A software-only mock device.
 *
 * Companion to Chapter 36, Lab 4, of "FreeBSD Device Drivers:
 * From First Steps to Kernel Mastery."
 *
 * This module simulates a tiny "command and status" device entirely
 * in software. There is no hardware involved, no bus_alloc_resource
 * call, no real memory-mapped I/O. The point of the mock is to give
 * a driver author a target for testing driver code without needing
 * the real device, and to give a learner a safe environment to
 * practice the simulation pattern that real reverse-engineering
 * projects rely on.
 *
 * The mock exposes a character device at /dev/mockdev. Operations
 * happen through ioctl, with the operation codes defined in
 * mockdev.h. Commands take a simulated time to complete (modeled
 * with a callout), during which the BUSY status bit is set. When
 * the simulated processing time elapses, the BUSY bit is cleared,
 * the DONE bit is set, and the result of the command is placed in
 * the data register.
 *
 * The simulated commands are:
 *
 *   MOCKDEV_CMD_NOP       - succeeds immediately, returns zero
 *   MOCKDEV_CMD_RESET     - resets the internal counter
 *   MOCKDEV_CMD_READ_ID   - returns the constant chip identifier
 *   MOCKDEV_CMD_INCREMENT - increments and returns the counter
 *   MOCKDEV_CMD_FAIL      - always reports the ERROR status bit
 *
 * Any other command code is treated as unknown and produces an
 * ERROR status, which is the same behaviour a real device would
 * exhibit if the driver sent a command code outside its valid
 * range.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>

#include "mockdev.h"

#define	MOCKDEV_PROC_TICKS	(hz / 10)	/* 100 ms */

struct mockdev_softc {
	struct cdev	*sc_dev;
	struct mtx	 sc_mtx;
	struct callout	 sc_callout;
	uint32_t	 sc_cmd;
	uint32_t	 sc_status;
	uint32_t	 sc_data;
	uint32_t	 sc_counter;
};

static struct mockdev_softc mockdev_sc;

static d_open_t		mockdev_open;
static d_close_t	mockdev_close;
static d_ioctl_t	mockdev_ioctl;

static struct cdevsw mockdev_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	mockdev_open,
	.d_close =	mockdev_close,
	.d_ioctl =	mockdev_ioctl,
	.d_name =	MOCKDEV_DEVICE_NAME,
};

static void mockdev_complete(void *arg);
static void mockdev_dispatch(struct mockdev_softc *sc, uint32_t cmd);

static int
mockdev_open(struct cdev *dev __unused, int oflags __unused,
    int devtype __unused, struct thread *td __unused)
{
	return (0);
}

static int
mockdev_close(struct cdev *dev __unused, int fflag __unused,
    int devtype __unused, struct thread *td __unused)
{
	return (0);
}

static void
mockdev_dispatch(struct mockdev_softc *sc, uint32_t cmd)
{
	mtx_assert(&sc->sc_mtx, MA_OWNED);

	sc->sc_cmd = cmd;
	sc->sc_status = MOCKDEV_STATUS_BUSY;
	sc->sc_data = 0;
	callout_reset(&sc->sc_callout, MOCKDEV_PROC_TICKS,
	    mockdev_complete, sc);
}

static void
mockdev_complete(void *arg)
{
	struct mockdev_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	switch (sc->sc_cmd) {
	case MOCKDEV_CMD_NOP:
		sc->sc_data = 0;
		sc->sc_status = MOCKDEV_STATUS_DONE;
		break;
	case MOCKDEV_CMD_RESET:
		sc->sc_counter = 0;
		sc->sc_data = 0;
		sc->sc_status = MOCKDEV_STATUS_DONE;
		break;
	case MOCKDEV_CMD_READ_ID:
		sc->sc_data = MOCKDEV_CHIP_ID;
		sc->sc_status = MOCKDEV_STATUS_DONE;
		break;
	case MOCKDEV_CMD_INCREMENT:
		sc->sc_counter++;
		sc->sc_data = sc->sc_counter;
		sc->sc_status = MOCKDEV_STATUS_DONE;
		break;
	case MOCKDEV_CMD_FAIL:
		sc->sc_data = 0;
		sc->sc_status = MOCKDEV_STATUS_ERROR;
		break;
	default:
		sc->sc_data = 0;
		sc->sc_status = MOCKDEV_STATUS_ERROR;
		break;
	}
	mtx_unlock(&sc->sc_mtx);
}

static int
mockdev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int fflag __unused, struct thread *td __unused)
{
	struct mockdev_softc *sc = &mockdev_sc;
	struct mockdev_op *op;

	switch (cmd) {
	case MOCKDEV_IOC_SUBMIT:
		op = (struct mockdev_op *)data;
		mtx_lock(&sc->sc_mtx);
		if (sc->sc_status & MOCKDEV_STATUS_BUSY) {
			mtx_unlock(&sc->sc_mtx);
			return (EBUSY);
		}
		mockdev_dispatch(sc, op->mo_cmd);
		op->mo_status = sc->sc_status;
		op->mo_data = sc->sc_data;
		mtx_unlock(&sc->sc_mtx);
		return (0);

	case MOCKDEV_IOC_READ_STATUS:
		op = (struct mockdev_op *)data;
		mtx_lock(&sc->sc_mtx);
		op->mo_cmd = sc->sc_cmd;
		op->mo_status = sc->sc_status;
		op->mo_data = sc->sc_data;
		mtx_unlock(&sc->sc_mtx);
		return (0);

	case MOCKDEV_IOC_RESET:
		mtx_lock(&sc->sc_mtx);
		callout_stop(&sc->sc_callout);
		sc->sc_cmd = 0;
		sc->sc_status = 0;
		sc->sc_data = 0;
		sc->sc_counter = 0;
		mtx_unlock(&sc->sc_mtx);
		return (0);

	default:
		return (ENOTTY);
	}
}

static int
mockdev_modevent(module_t mod __unused, int type, void *data __unused)
{
	struct mockdev_softc *sc = &mockdev_sc;
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&sc->sc_mtx, "mockdev", NULL, MTX_DEF);
		callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
		sc->sc_dev = make_dev(&mockdev_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0600, MOCKDEV_DEVICE_NAME);
		if (sc->sc_dev == NULL) {
			callout_drain(&sc->sc_callout);
			mtx_destroy(&sc->sc_mtx);
			return (ENOMEM);
		}
		printf("mockdev: loaded, /dev/%s ready\n",
		    MOCKDEV_DEVICE_NAME);
		break;
	case MOD_UNLOAD:
		if (sc->sc_dev != NULL) {
			destroy_dev(sc->sc_dev);
			sc->sc_dev = NULL;
		}
		callout_drain(&sc->sc_callout);
		mtx_destroy(&sc->sc_mtx);
		printf("mockdev: unloaded\n");
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t mockdev_mod = {
	"mockdev",
	mockdev_modevent,
	NULL
};

DECLARE_MODULE(mockdev, mockdev_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(mockdev, 1);
