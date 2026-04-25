/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 *
 * Lab 7.4, variant A.
 *
 * Same code as stage2-final/myfirst.c, but attach is rigged to fail
 * immediately after mtx_init. The failure jumps to fail_mtx so we
 * exercise the very first cleanup label. Loading should fail with
 * ENXIO and leave no /dev node, no sysctl tree, and no leaked mutex.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

struct myfirst_softc {
	device_t		dev;
	int			unit;
	struct mtx		mtx;
	uint64_t		attach_ticks;
	uint64_t		open_count;
	uint64_t		bytes_read;
	int			is_attached;
	int			is_open;
	struct cdev		*cdev;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};

static d_open_t		myfirst_open;
static d_close_t	myfirst_close;
static d_read_t		myfirst_read;
static d_write_t	myfirst_write;

static struct cdevsw myfirst_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	myfirst_open,
	.d_close =	myfirst_close,
	.d_read =	myfirst_read,
	.d_write =	myfirst_write,
	.d_name =	"myfirst",
};

static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return (0);
}

static int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return (0);
}

static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	uio->uio_resid = 0;
	return (0);
}

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
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->unit = device_get_unit(dev);

	mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

	/* SIMULATED FAILURE FOR LAB 7.4 (variant A) */
	device_printf(dev, "Simulating attach failure for testing\n");
	error = ENXIO;
	goto fail_mtx;

	/* unreachable in this variant */
	return (0);

fail_mtx:
	mtx_destroy(&sc->mtx);
	sc->is_attached = 0;
	return (error);
}

static int
myfirst_detach(device_t dev)
{
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
