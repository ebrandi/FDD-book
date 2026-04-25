/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 *
 * Stage 1 of Chapter 7: minimal Newbus pseudo-device.
 *
 * Adds identify, probe, attach, and detach methods on the nexus bus.
 * The driver does not yet allocate locks, /dev nodes, or sysctls.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>

/*
 * Per-device state. Newbus zeroes and frees this for us based on the
 * size declared in the driver_t below.
 */
struct myfirst_softc {
	device_t	dev;
	uint64_t	attach_ticks;
	int		is_ready;
};

/*
 * identify creates the myfirst child device on the parent bus. Without
 * this, nexus would have no myfirst device to probe and our driver
 * would never be called.
 */
static void
myfirst_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, driver->name, -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, driver->name, -1) == NULL)
		device_printf(parent, "myfirst: BUS_ADD_CHILD failed\n");
}

static int
myfirst_probe(device_t dev)
{
	device_set_desc(dev, "My First FreeBSD Driver");
	return (BUS_PROBE_DEFAULT);
}

static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->attach_ticks = ticks;
	sc->is_ready = 1;

	device_printf(dev, "Attached at tick %lu\n",
	    (unsigned long)sc->attach_ticks);
	return (0);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc;

	sc = device_get_softc(dev);
	device_printf(dev, "Detaching (was attached for %lu ticks)\n",
	    (unsigned long)(ticks - sc->attach_ticks));
	sc->is_ready = 0;
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
	"myfirst",
	myfirst_methods,
	sizeof(struct myfirst_softc)
};

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
MODULE_VERSION(myfirst, 1);
