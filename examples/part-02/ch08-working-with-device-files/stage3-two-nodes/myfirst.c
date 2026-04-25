/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 *
 * Stage 3 of Chapter 8: two-node driver with data and control nodes.
 *
 * Differences from stage 2:
 *   - Adds a second cdevsw (myfirst_ctl_cdevsw) with d_ioctl only.
 *   - Creates two cdevs in attach: data at /dev/myfirst/%d,
 *     control at /dev/myfirst/%d.ctl.
 *   - Each node carries its own permission mode: 0660 for the data
 *     node, 0640 for the control node.
 *   - Destroy order: control first, then data, on detach.
 *
 * The ioctl handler is a stub that returns ENOTTY for every command;
 * Chapter 25 will revisit ioctl design.
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
#include <sys/sysctl.h>

struct myfirst_softc {
	device_t		dev;
	int			unit;

	struct mtx		mtx;

	uint64_t		attach_ticks;
	uint64_t		open_count;
	uint64_t		bytes_read;

	int			active_fhs;
	int			is_attached;

	struct cdev		*cdev;
	struct cdev		*cdev_ctl;
	struct cdev		*cdev_alias;

	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};

struct myfirst_fh {
	struct myfirst_softc	*sc;
	uint64_t		 reads;
	uint64_t		 writes;
};

static d_open_t		myfirst_open;
static d_close_t	myfirst_close;
static d_read_t		myfirst_read;
static d_write_t	myfirst_write;
static d_ioctl_t	myfirst_ctl_ioctl;

static struct cdevsw myfirst_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	myfirst_open,
	.d_close =	myfirst_close,
	.d_read =	myfirst_read,
	.d_write =	myfirst_write,
	.d_name =	"myfirst",
};

static struct cdevsw myfirst_ctl_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	myfirst_ctl_ioctl,
	.d_name =	"myfirst_ctl",
};

static void
myfirst_fh_dtor(void *data)
{
	struct myfirst_fh *fh = data;
	struct myfirst_softc *sc = fh->sc;

	mtx_lock(&sc->mtx);
	sc->active_fhs--;
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "per-open dtor fh=%p reads=%lu writes=%lu\n",
	    fh, (unsigned long)fh->reads, (unsigned long)fh->writes);

	free(fh, M_DEVBUF);
}

static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc;
	struct myfirst_fh *fh;
	int error;

	sc = dev->si_drv1;
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	fh = malloc(sizeof(*fh), M_DEVBUF, M_WAITOK | M_ZERO);
	fh->sc = sc;

	error = devfs_set_cdevpriv(fh, myfirst_fh_dtor);
	if (error != 0) {
		free(fh, M_DEVBUF);
		return (error);
	}

	mtx_lock(&sc->mtx);
	sc->open_count++;
	sc->active_fhs++;
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev, "open via %s fh=%p (active=%d)\n",
	    devtoname(dev), fh, sc->active_fhs);
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
	struct myfirst_fh *fh;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);

	(void)fh;
	return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct myfirst_fh *fh;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);

	(void)fh;
	uio->uio_resid = 0;
	return (0);
}

/*
 * Control-node ioctl stub. Chapter 25 will revisit ioctl design.
 * For now, every command returns ENOTTY so userland sees a clean
 * "not supported" response.
 */
static int
myfirst_ctl_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{
	struct myfirst_softc *sc;

	sc = dev->si_drv1;
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	device_printf(sc->dev, "ctl ioctl cmd=0x%lx\n", cmd);
	return (ENOTTY);
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
	sc->active_fhs = 0;
	sc->open_count = 0;
	sc->bytes_read = 0;

	/* Data node: /dev/myfirst/%d, 0660 root:operator. */
	make_dev_args_init(&args);
	args.mda_devsw = &myfirst_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0660;
	args.mda_si_drv1 = sc;

	error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
	if (error != 0) {
		device_printf(dev, "data cdev make_dev_s: %d\n", error);
		goto fail_mtx;
	}

	/* Control node: /dev/myfirst/%d.ctl, 0640 root:wheel. */
	make_dev_args_init(&args);
	args.mda_devsw = &myfirst_ctl_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0640;
	args.mda_si_drv1 = sc;

	error = make_dev_s(&args, &sc->cdev_ctl, "myfirst/%d.ctl", sc->unit);
	if (error != 0) {
		device_printf(dev, "ctl cdev make_dev_s: %d\n", error);
		goto fail_data;
	}

	/* Alias: /dev/myfirst -> /dev/myfirst/0 (primary unit only). */
	if (sc->unit == 0) {
		sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
		if (sc->cdev_alias == NULL)
			device_printf(dev, "alias creation failed\n");
	}

	sysctl_ctx_init(&sc->sysctl_ctx);

	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
	    "Driver statistics");
	if (sc->sysctl_tree == NULL) {
		device_printf(dev, "sysctl tree creation failed\n");
		error = ENOMEM;
		goto fail_ctl;
	}

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "open_count", CTLFLAG_RD,
	    &sc->open_count, 0, "Lifetime number of opens");

	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "active_fhs", CTLFLAG_RD,
	    &sc->active_fhs, 0, "Currently open descriptors");

	device_printf(dev, "attached; data=/dev/%s ctl=/dev/%s\n",
	    devtoname(sc->cdev), devtoname(sc->cdev_ctl));
	return (0);

fail_ctl:
	destroy_dev(sc->cdev_ctl);
	sc->cdev_ctl = NULL;
fail_data:
	if (sc->cdev_alias != NULL) {
		destroy_dev(sc->cdev_alias);
		sc->cdev_alias = NULL;
	}
	destroy_dev(sc->cdev);
	sc->cdev = NULL;
fail_mtx:
	mtx_destroy(&sc->mtx);
	sc->is_attached = 0;
	return (error);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->mtx);
	if (sc->active_fhs > 0) {
		mtx_unlock(&sc->mtx);
		device_printf(dev, "cannot detach: %d open descriptor(s)\n",
		    sc->active_fhs);
		return (EBUSY);
	}
	mtx_unlock(&sc->mtx);

	/* Destroy in reverse order of creation: alias, ctl, data. */
	if (sc->cdev_alias != NULL) {
		destroy_dev(sc->cdev_alias);
		sc->cdev_alias = NULL;
	}
	if (sc->cdev_ctl != NULL) {
		destroy_dev(sc->cdev_ctl);
		sc->cdev_ctl = NULL;
	}
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
