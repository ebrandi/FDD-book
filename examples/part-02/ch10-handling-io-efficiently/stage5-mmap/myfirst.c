/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * Chapter 10, Stage 5: minimal d_mmap on top of Stage 4.
 *
 * Lets user space mmap(2) the cbuf's backing memory read-only.
 * Writable mappings are refused; the cbuf would otherwise race
 * with the I/O handlers.  vtophys(9) translates the kernel virtual
 * buffer into the physical address the VM system needs.
 *
 * Stage 4 features (d_poll, selinfo, helper refactor) are inherited
 * unchanged.
 *
 * Locking strategy.
 *
 * sc->mtx protects:
 *   - sc->cb (the circular buffer's internal state)
 *   - sc->bytes_read, sc->bytes_written
 *   - sc->open_count, sc->active_fhs
 *   - sc->is_attached
 *
 * Locking discipline:
 *   - The mutex is acquired with mtx_lock and released with mtx_unlock.
 *   - mtx_sleep(&sc->cb, &sc->mtx, PCATCH, ...) is used to block while
 *     waiting on buffer state.  wakeup(&sc->cb) is the matching call.
 *   - The mutex is NEVER held across uiomove(9), copyin(9), or copyout(9),
 *     all of which may sleep.
 *   - The mutex is held when calling cbuf_*() helpers; the cbuf module is
 *     intentionally lock-free by itself and relies on the caller for safety.
 *   - selwakeup(9) and wakeup(9) are called with the mutex DROPPED, after
 *     the state change that warrants the wake.
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
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/selinfo.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

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

	struct selinfo		rsel;
	struct selinfo		wsel;

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
static d_poll_t		myfirst_poll;
static d_mmap_t		myfirst_mmap;

static struct cdevsw myfirst_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	myfirst_open,
	.d_close =	myfirst_close,
	.d_read =	myfirst_read,
	.d_write =	myfirst_write,
	.d_poll =	myfirst_poll,
	.d_mmap =	myfirst_mmap,
	.d_name =	"myfirst",
};

/* Helpers, all called with sc->mtx held. */
static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
	size_t got;

	mtx_assert(&sc->mtx, MA_OWNED);
	got = cbuf_read(&sc->cb, dst, n);
	sc->bytes_read += got;
	return (got);
}

static size_t
myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n)
{
	size_t put;

	mtx_assert(&sc->mtx, MA_OWNED);
	put = cbuf_write(&sc->cb, src, n);
	sc->bytes_written += put;
	return (put);
}

static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
	int error;

	mtx_assert(&sc->mtx, MA_OWNED);
	while (cbuf_used(&sc->cb) == 0) {
		if (uio->uio_resid != nbefore)
			return (-1);
		if (ioflag & IO_NDELAY)
			return (EAGAIN);
		error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfrd", 0);
		if (error != 0)
			return (error);
		if (!sc->is_attached)
			return (ENXIO);
	}
	return (0);
}

static int
myfirst_wait_room(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
	int error;

	mtx_assert(&sc->mtx, MA_OWNED);
	while (cbuf_free(&sc->cb) == 0) {
		if (uio->uio_resid != nbefore)
			return (-1);
		if (ioflag & IO_NDELAY)
			return (EAGAIN);
		error = mtx_sleep(&sc->cb, &sc->mtx, PCATCH, "myfwr", 0);
		if (error != 0)
			return (error);
		if (!sc->is_attached)
			return (ENXIO);
	}
	return (0);
}

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
	ssize_t nbefore;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	nbefore = uio->uio_resid;
	while (uio->uio_resid > 0) {
		mtx_lock(&sc->mtx);
		error = myfirst_wait_data(sc, ioflag, nbefore, uio);
		if (error != 0) {
			mtx_unlock(&sc->mtx);
			return (error == -1 ? 0 : error);
		}
		take = MIN((size_t)uio->uio_resid, sizeof(bounce));
		got = myfirst_buf_read(sc, bounce, take);
		fh->reads += got;
		mtx_unlock(&sc->mtx);

		wakeup(&sc->cb);
		selwakeup(&sc->wsel);

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
	ssize_t nbefore;
	int error;

	error = devfs_get_cdevpriv((void **)&fh);
	if (error != 0)
		return (error);
	if (sc == NULL || !sc->is_attached)
		return (ENXIO);

	nbefore = uio->uio_resid;
	while (uio->uio_resid > 0) {
		mtx_lock(&sc->mtx);
		error = myfirst_wait_room(sc, ioflag, nbefore, uio);
		if (error != 0) {
			mtx_unlock(&sc->mtx);
			return (error == -1 ? 0 : error);
		}
		room = cbuf_free(&sc->cb);
		mtx_unlock(&sc->mtx);

		want = MIN((size_t)uio->uio_resid, sizeof(bounce));
		want = MIN(want, room);
		error = uiomove(bounce, want, uio);
		if (error != 0)
			return (error);

		mtx_lock(&sc->mtx);
		put = myfirst_buf_write(sc, bounce, want);
		fh->writes += put;
		mtx_unlock(&sc->mtx);

		wakeup(&sc->cb);
		selwakeup(&sc->rsel);
	}
	return (0);
}

static int
myfirst_poll(struct cdev *dev, int events, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	int revents = 0;

	if (sc == NULL || !sc->is_attached)
		return (POLLERR);

	mtx_lock(&sc->mtx);
	if (events & (POLLIN | POLLRDNORM)) {
		if (cbuf_used(&sc->cb) > 0)
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &sc->rsel);
	}
	if (events & (POLLOUT | POLLWRNORM)) {
		if (cbuf_free(&sc->cb) > 0)
			revents |= events & (POLLOUT | POLLWRNORM);
		else
			selrecord(td, &sc->wsel);
	}
	mtx_unlock(&sc->mtx);
	return (revents);
}

static int
myfirst_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr __unused)
{
	struct myfirst_softc *sc = dev->si_drv1;

	if (sc == NULL || !sc->is_attached)
		return (ENXIO);
	if ((nprot & VM_PROT_WRITE) != 0)
		return (EACCES);
	if (offset >= sc->cb.cb_size)
		return (-1);
	*paddr = vtophys((char *)sc->cb.cb_data + (offset & ~PAGE_MASK));
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
	device_set_desc(dev, "My First FreeBSD Driver (Chapter 10 Stage 5)");
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
	    &sc->bytes_read, 0, "Total bytes drained from the buffer");
	SYSCTL_ADD_U64(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_written", CTLFLAG_RD,
	    &sc->bytes_written, 0, "Total bytes appended to the buffer");
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
	sc->is_attached = 0;
	wakeup(&sc->cb);
	mtx_unlock(&sc->mtx);

	seldrain(&sc->rsel);
	seldrain(&sc->wsel);

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
