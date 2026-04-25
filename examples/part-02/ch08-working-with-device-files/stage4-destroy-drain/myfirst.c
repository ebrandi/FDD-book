/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 *
 * Stage 4 of Chapter 8: multiple cdevs plus destroy_dev_drain.
 *
 * This driver creates five cdevs per attach, all served by the same
 * cdevsw. It exists to illustrate when destroy_dev_drain is needed:
 * a module that uses destroy_dev_sched to tear down many cdevs during
 * its detach path must drain the cdevsw before the module is unloaded
 * and the cdevsw is freed.
 *
 * Compile with USE_DRAIN=1 to get the correct behavior:
 *
 *     make CFLAGS+=-DUSE_DRAIN=1
 *
 * Compile without USE_DRAIN to exercise the broken path, which may
 * log "Still N threads" messages or panic under load. Run only in a
 * throwaway VM.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#define MYFIRST_UNITS 5

struct myfirst_softc {
	device_t		dev;
	int			unit;

	struct mtx		mtx;
	int			is_attached;

	struct cdev		*cdevs[MYFIRST_UNITS];
};

static d_open_t		myfirst_open;
static d_close_t	myfirst_close;

static struct cdevsw myfirst_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	myfirst_open,
	.d_close =	myfirst_close,
	.d_name =	"myfirstN",
};

static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	if (sc == NULL || !sc->is_attached)
		return (ENXIO);
	return (0);
}

static int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return (0);
}

static void
myfirst_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, driver->name, -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, driver->name, -1) == NULL)
		device_printf(parent, "myfirstN: BUS_ADD_CHILD failed\n");
}

static int
myfirst_probe(device_t dev)
{
	device_set_desc(dev, "My First FreeBSD Driver (multi-node)");
	return (BUS_PROBE_DEFAULT);
}

static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc;
	struct make_dev_args args;
	int error, i;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirstN", MTX_DEF);
	sc->is_attached = 1;

	for (i = 0; i < MYFIRST_UNITS; i++) {
		make_dev_args_init(&args);
		args.mda_devsw = &myfirst_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_WHEEL;
		args.mda_mode = 0600;
		args.mda_si_drv1 = sc;

		error = make_dev_s(&args, &sc->cdevs[i],
		    "myfirstN/%d", i);
		if (error != 0) {
			device_printf(dev,
			    "make_dev_s unit %d: %d\n", i, error);
			while (--i >= 0) {
				destroy_dev(sc->cdevs[i]);
				sc->cdevs[i] = NULL;
			}
			mtx_destroy(&sc->mtx);
			return (error);
		}
	}

	device_printf(dev, "attached; %d cdevs created\n", MYFIRST_UNITS);
	return (0);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc;
	int i;

	sc = device_get_softc(dev);

	for (i = 0; i < MYFIRST_UNITS; i++) {
		if (sc->cdevs[i] != NULL) {
			destroy_dev_sched(sc->cdevs[i]);
			sc->cdevs[i] = NULL;
		}
	}

#ifdef USE_DRAIN
	/*
	 * Correct behavior: wait for all cdevs in this cdevsw to
	 * complete destruction before returning, so the module can
	 * be safely unloaded and the cdevsw freed.
	 */
	destroy_dev_drain(&myfirst_cdevsw);
#endif

	mtx_destroy(&sc->mtx);
	sc->is_attached = 0;

	device_printf(dev, "detached\n");
	return (0);
}

static device_method_t myfirst_methods[] = {
	DEVMETHOD(device_identify,	myfirst_identify),
	DEVMETHOD(device_probe,		myfirst_probe),
	DEVMETHOD(device_attach,	myfirst_attach),
	DEVMETHOD(device_detach,	myfirst_detach),
	DEVMETHOD_END
};

static driver_t myfirst_driver = {
	"myfirstN",
	myfirst_methods,
	sizeof(struct myfirst_softc)
};

DRIVER_MODULE(myfirstN, nexus, myfirst_driver, 0, 0);
MODULE_VERSION(myfirstN, 1);
