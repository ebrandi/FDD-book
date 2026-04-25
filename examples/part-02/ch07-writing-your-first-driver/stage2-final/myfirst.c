/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 *
 * Stage 2 of Chapter 7: complete minimal pseudo-device driver.
 *
 * Demonstrates:
 *   - identify, probe, attach, detach on the nexus bus
 *   - per-device softc with mutex
 *   - /dev/myfirst0 node with stub read/write
 *   - read-only sysctls under dev.myfirst.0.stats
 *   - single-label unwind for attach error paths
 *   - exclusive open with EBUSY on second opener
 *   - detach refuses with EBUSY while the device is open
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
	struct myfirst_softc *sc;

	sc = dev->si_drv1;
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	mtx_lock(&sc->mtx);
	if (sc->is_open) {
		mtx_unlock(&sc->mtx);
		return (EBUSY);
	}
	sc->is_open = 1;
	sc->open_count++;
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "Device opened (count: %lu)\n",
	    (unsigned long)sc->open_count);
	return (0);
}

static int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct myfirst_softc *sc;

	sc = dev->si_drv1;
	if (sc == NULL)
		return (ENXIO);

	mtx_lock(&sc->mtx);
	sc->is_open = 0;
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "Device closed\n");
	return (0);
}

/*
 * Stub read: immediate EOF. Real read semantics arrive in Chapter 9.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	return (0);
}

/*
 * Stub write: pretend we accepted everything. Discards user data on
 * purpose; real data movement lives in Chapter 9.
 */
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
	struct make_dev_args args;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->unit = device_get_unit(dev);

	mtx_init(&sc->mtx, device_get_nameunit(dev), "myfirst", MTX_DEF);

	sc->attach_ticks = ticks;
	sc->is_attached = 1;
	sc->is_open = 0;
	sc->open_count = 0;
	sc->bytes_read = 0;

	make_dev_args_init(&args);
	args.mda_devsw = &myfirst_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0600;
	args.mda_si_drv1 = sc;

	error = make_dev_s(&args, &sc->cdev, "myfirst%d", sc->unit);
	if (error != 0) {
		device_printf(dev, "Failed to create device node: %d\n",
		    error);
		goto fail_mtx;
	}

	sysctl_ctx_init(&sc->sysctl_ctx);

	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
	    "Driver statistics");
	if (sc->sysctl_tree == NULL) {
		device_printf(dev, "Failed to create sysctl tree\n");
		error = ENOMEM;
		goto fail_dev;
	}

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "attach_ticks", CTLFLAG_RD,
	    &sc->attach_ticks, 0, "Tick count when driver attached");

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "open_count", CTLFLAG_RD,
	    &sc->open_count, 0, "Number of times device was opened");

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_read", CTLFLAG_RD,
	    &sc->bytes_read, 0, "Total bytes read from device");

	device_printf(dev, "Attached successfully at tick %lu\n",
	    (unsigned long)sc->attach_ticks);
	return (0);

fail_dev:
	destroy_dev(sc->cdev);
	sysctl_ctx_free(&sc->sysctl_ctx);
fail_mtx:
	mtx_destroy(&sc->mtx);
	sc->is_attached = 0;
	return (error);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc;
	uint64_t uptime_ticks;

	sc = device_get_softc(dev);

	if (sc->is_open) {
		device_printf(dev, "Cannot detach: device is open\n");
		return (EBUSY);
	}

	uptime_ticks = ticks - sc->attach_ticks;
	device_printf(dev, "Detaching: uptime %lu ticks, opened %lu times\n",
	    (unsigned long)uptime_ticks,
	    (unsigned long)sc->open_count);

	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}
	sysctl_ctx_free(&sc->sysctl_ctx);
	mtx_destroy(&sc->mtx);
	sc->is_attached = 0;

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
