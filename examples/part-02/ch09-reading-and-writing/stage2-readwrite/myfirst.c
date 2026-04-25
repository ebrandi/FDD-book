/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name
 * All rights reserved.
 *
 * Chapter 9, Stage 2: a write-once / read-many buffer.
 *
 * The driver allocates a kernel buffer at attach time.  Writes append
 * into the buffer; reads serve from the caller's per-descriptor offset
 * up to the current high-water mark (bufused).  When the buffer fills,
 * further writes return ENOSPC.  Each reader owns its own offset, so
 * two concurrent readers see independent progress through the same
 * accumulating stream.
 *
 * Differences from Stage 1:
 *   - A malloc(9)'d buffer of size MYFIRST_BUFSIZE.
 *   - myfirst_write appends data into the buffer under sc->mtx.
 *   - myfirst_read honours uio->uio_offset against bufused.
 *   - New sysctl nodes expose bytes_written and bufused.
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

#define	MYFIRST_BUFSIZE		4096

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

	char		       *buf;
	size_t			buflen;
	size_t			bufused;

	struct cdev	       *cdev;
	struct cdev	       *cdev_alias;

	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid      *sysctl_tree;
};

struct myfirst_fh {
	struct myfirst_softc   *sc;
	uint64_t		reads;
	uint64_t		writes;
	off_t			read_off;
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
	struct myfirst_softc *sc = dev->si_drv1;
	struct myfirst_fh *fh;
	off_t before;
	size_t have;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	mtx_lock(&sc->mtx);
	have = sc->bufused;
	before = uio->uio_offset;
	error = uiomove_frombuf(sc->buf, have, uio);
	if (error == 0) {
		size_t moved = uio->uio_offset - before;
		sc->bytes_read += moved;
		fh->reads += moved;
	}
	fh->read_off = uio->uio_offset;
	mtx_unlock(&sc->mtx);
	return (error);
}

static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct myfirst_softc *sc = dev->si_drv1;
	struct myfirst_fh *fh;
	size_t avail, towrite;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	mtx_lock(&sc->mtx);
	if (sc->bufused >= sc->buflen) {
		mtx_unlock(&sc->mtx);
		return (ENOSPC);
	}
	avail = sc->buflen - sc->bufused;
	towrite = MIN((size_t)uio->uio_resid, avail);
	error = uiomove(sc->buf + sc->bufused, towrite, uio);
	if (error == 0) {
		sc->bufused += towrite;
		sc->bytes_written += towrite;
		fh->writes += towrite;
	}
	mtx_unlock(&sc->mtx);
	return (error);
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
	device_set_desc(dev, "My First FreeBSD Driver (Chapter 9 Stage 2)");
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

	sc->buflen = MYFIRST_BUFSIZE;
	sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc->buf == NULL) {
		error = ENOMEM;
		goto fail_mtx;
	}
	sc->bufused = 0;

	make_dev_args_init(&args);
	args.mda_devsw = &myfirst_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0660;
	args.mda_si_drv1 = sc;

	error = make_dev_s(&args, &sc->cdev, "myfirst/%d", sc->unit);
	if (error != 0) {
		device_printf(dev, "Failed to create device node: %d\n",
		    error);
		goto fail_buf;
	}

	sc->cdev_alias = make_dev_alias(sc->cdev, "myfirst");
	if (sc->cdev_alias == NULL)
		device_printf(dev, "failed to create /dev/myfirst alias\n");

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
	    &sc->open_count, 0, "Lifetime number of opens");

	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "active_fhs", CTLFLAG_RD,
	    &sc->active_fhs, 0, "Currently open descriptors");

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_read", CTLFLAG_RD,
	    &sc->bytes_read, 0, "Total bytes read from device");

	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_written", CTLFLAG_RD,
	    &sc->bytes_written, 0, "Total bytes written into the buffer");

	SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bufused", CTLFLAG_RD,
	    (unsigned int *)&sc->bufused, 0, "Current buffer occupancy");

	SYSCTL_ADD_UINT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "buflen", CTLFLAG_RD,
	    (unsigned int *)&sc->buflen, 0, "Buffer capacity");

	device_printf(dev,
	    "Attached; node at /dev/%s (alias /dev/myfirst), buffer=%zu bytes\n",
	    devtoname(sc->cdev), sc->buflen);
	return (0);

fail_dev:
	if (sc->cdev_alias != NULL) {
		destroy_dev(sc->cdev_alias);
		sc->cdev_alias = NULL;
	}
	destroy_dev(sc->cdev);
fail_buf:
	free(sc->buf, M_DEVBUF);
	sc->buf = NULL;
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
	if (sc->buf != NULL) {
		free(sc->buf, M_DEVBUF);
		sc->buf = NULL;
	}
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
