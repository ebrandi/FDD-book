/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * Chapter 10, Stage 2: circular-buffer integration.
 *
 * Replaces the Stage 3 linear FIFO from Chapter 9 with a true
 * circular buffer (cbuf).  The handlers loop over a stack-resident
 * bounce buffer so that the wrap-around logic stays inside cbuf_*().
 *
 * No blocking or non-blocking semantics yet; an empty buffer returns
 * 0 to the reader, a full buffer causes a short write.  Stage 3 adds
 * mtx_sleep + IO_NDELAY support on top of this base.
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

#include "cbuf.h"

#define	MYFIRST_BUFSIZE		4096
#define	MYFIRST_BOUNCE		256

struct myfirst_softc {
	device_t		dev;
	int			unit;

	struct mtx		mtx;

	uint64_t		attach_ticks;
	uint64_t		open_count;
	uint64_t		bytes_read;
	uint64_t		bytes_written;

	int			active_fhs;
	int			is_attached;

	struct cbuf		cb;

	struct cdev	       *cdev;
	struct cdev	       *cdev_alias;

	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid      *sysctl_tree;
};

struct myfirst_fh {
	struct myfirst_softc   *sc;
	uint64_t		reads;
	uint64_t		writes;
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
myfirst_sysctl_cb_used(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int val;

	mtx_lock(&sc->mtx);
	val = (unsigned int)cbuf_used(&sc->cb);
	mtx_unlock(&sc->mtx);
	return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
myfirst_sysctl_cb_free(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int val;

	mtx_lock(&sc->mtx);
	val = (unsigned int)cbuf_free(&sc->cb);
	mtx_unlock(&sc->mtx);
	return (sysctl_handle_int(oidp, &val, 0, req));
}

static void
myfirst_fh_dtor(void *data)
{
	struct myfirst_fh *fh = data;
	struct myfirst_softc *sc = fh->sc;

	mtx_lock(&sc->mtx);
	sc->active_fhs--;
	mtx_unlock(&sc->mtx);

	device_printf(sc->dev,
	    "per-open dtor fh=%p reads=%lu writes=%lu\n",
	    fh, (unsigned long)fh->reads, (unsigned long)fh->writes);

	free(fh, M_DEVBUF);
}

static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	struct myfirst_fh *fh;
	int error;

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
	struct myfirst_softc *sc = dev->si_drv1;
	struct myfirst_fh *fh;
	char bounce[MYFIRST_BOUNCE];
	size_t take, got;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	while (uio->uio_resid > 0) {
		mtx_lock(&sc->mtx);
		take = MIN((size_t)uio->uio_resid, sizeof(bounce));
		got = cbuf_read(&sc->cb, bounce, take);
		if (got == 0) {
			mtx_unlock(&sc->mtx);
			break;
		}
		sc->bytes_read += got;
		fh->reads += got;
		mtx_unlock(&sc->mtx);

		error = uiomove(bounce, got, uio);
		if (error != 0)
			return (error);
	}
	return (0);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct myfirst_softc *sc = dev->si_drv1;
	struct myfirst_fh *fh;
	char bounce[MYFIRST_BOUNCE];
	size_t want, put, room;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	while (uio->uio_resid > 0) {
		mtx_lock(&sc->mtx);
		room = cbuf_free(&sc->cb);
		mtx_unlock(&sc->mtx);
		if (room == 0)
			break;

		want = MIN((size_t)uio->uio_resid, sizeof(bounce));
		want = MIN(want, room);
		error = uiomove(bounce, want, uio);
		if (error != 0)
			return (error);

		mtx_lock(&sc->mtx);
		put = cbuf_write(&sc->cb, bounce, want);
		sc->bytes_written += put;
		fh->writes += put;
		mtx_unlock(&sc->mtx);

		if (put < want)
			break;
	}
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
	device_set_desc(dev, "My First FreeBSD Driver (Chapter 10 Stage 2)");
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
	sc->bytes_written = 0;

	error = cbuf_init(&sc->cb, MYFIRST_BUFSIZE);
	if (error != 0)
		goto fail_mtx;

	make_dev_args_init(&args);
	args.mda_devsw = &myfirst_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0660;
	args.mda_si_drv1 = sc;

	error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
	if (error != 0)
		goto fail_cb;

	sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
	if (sc->cdev_alias == NULL)
		device_printf(dev, "failed to create /dev/myfirst alias\n");

	sysctl_ctx_init(&sc->sysctl_ctx);
	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
	    "Driver statistics");

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "attach_ticks", CTLFLAG_RD,
	    &sc->attach_ticks, 0, "Tick count when driver attached");
	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "open_count", CTLFLAG_RD,
	    &sc->open_count, 0, "Lifetime number of opens");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "active_fhs", CTLFLAG_RD,
	    &sc->active_fhs, 0, "Currently open descriptors");
	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_read", CTLFLAG_RD,
	    &sc->bytes_read, 0, "Total bytes drained from the FIFO");
	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_written", CTLFLAG_RD,
	    &sc->bytes_written, 0, "Total bytes appended to the FIFO");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "cb_used",
	    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_cb_used, "IU",
	    "Live bytes currently held in the circular buffer");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "cb_free",
	    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_cb_free, "IU",
	    "Free bytes available in the circular buffer");
	SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "cb_size", CTLFLAG_RD,
	    (unsigned int *)&sc->cb.cb_size, 0,
	    "Capacity of the circular buffer");

	device_printf(dev,
	    "Attached; node /dev/%s (alias /dev/myfirst), cbuf=%zu bytes\n",
	    devtoname(sc->cdev), cbuf_size(&sc->cb));
	return (0);

fail_cb:
	cbuf_destroy(&sc->cb);
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
		device_printf(dev,
		    "Cannot detach: %d open descriptor(s)\n",
		    sc->active_fhs);
		return (EBUSY);
	}
	mtx_unlock(&sc->mtx);

	if (sc->cdev_alias != NULL) {
		destroy_dev(sc->cdev_alias);
		sc->cdev_alias = NULL;
	}
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}
	sysctl_ctx_free(&sc->sysctl_ctx);
	cbuf_destroy(&sc->cb);
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
