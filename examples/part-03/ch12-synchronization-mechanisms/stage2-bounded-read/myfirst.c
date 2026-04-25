/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * Chapter 12, Stage 2: bounded reads via cv_timedwait_sig.
 *
 * Builds on Chapter 12 Stage 1 (cv channels) by adding two new sysctls:
 * read_timeout_ms and write_timeout_ms. When non-zero, the wait
 * helpers use cv_timedwait_sig and translate EWOULDBLOCK to EAGAIN.
 * Default (0) preserves the indefinite-wait behaviour from Stage 1.
 *
 * Locking strategy.
 *
 * sc->mtx protects:
 *   - sc->cb (the circular buffer's internal state)
 *   - sc->open_count, sc->active_fhs
 *   - sc->is_attached (writes; reads at handler entry may be unprotected)
 *
 * sc->bytes_read, sc->bytes_written are counter(9) per-CPU counters.
 *
 * sc->data_cv: cv signalled when bytes are added to the cbuf.
 * sc->room_cv: cv signalled when room is freed in the cbuf.
 * Both use sc->mtx as their interlock.
 *
 * Locking discipline (unchanged from Chapter 11 except for cvs):
 *   - mtx is acquired with mtx_lock and released with mtx_unlock.
 *   - cv_wait_sig(&cv, &sc->mtx) is used to block; the cv is the
 *     named replacement for the old &sc->cb channel.
 *   - cv_signal wakes one waiter when state changes per-event.
 *   - cv_broadcast wakes all waiters at detach.
 *   - The mutex is NEVER held across uiomove(9), copyin(9), or copyout(9).
 *   - The mutex is held when calling cbuf_*() helpers; the cbuf module
 *     is intentionally lock-free by itself and relies on the caller.
 *   - selwakeup(9), cv_signal(9), and cv_broadcast(9) are called with
 *     the mutex DROPPED, after the state change that warrants the wake.
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
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/counter.h>

#include "cbuf.h"

#define	MYFIRST_BUFSIZE		4096
#define	MYFIRST_BOUNCE		256
#define	MYFIRST_VERSION		"0.6-bounded-read"

/*
 * Locking abstraction. One point of indirection around the mutex.
 */
#define	MYFIRST_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	MYFIRST_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)
#define	MYFIRST_ASSERT(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)

struct myfirst_softc {
	device_t		dev;
	int			unit;

	struct mtx		mtx;
	struct cv		data_cv;
	struct cv		room_cv;

	uint64_t		attach_ticks;
	uint64_t		open_count;
	counter_u64_t		bytes_read;
	counter_u64_t		bytes_written;

	int			active_fhs;
	int			is_attached;
	int			read_timeout_ms;	/* 0 = no timeout */
	int			write_timeout_ms;	/* 0 = no timeout */

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

static struct cdevsw myfirst_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	myfirst_open,
	.d_close =	myfirst_close,
	.d_read =	myfirst_read,
	.d_write =	myfirst_write,
	.d_poll =	myfirst_poll,
	.d_name =	"myfirst",
};

/* Helpers, all called with sc->mtx held. */
static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
	size_t got;

	MYFIRST_ASSERT(sc);
	KASSERT(sc->is_attached,
	    ("myfirst_buf_read called after detach"));
	got = cbuf_read(&sc->cb, dst, n);
	KASSERT(got <= n,
	    ("cbuf_read returned %zu > n=%zu", got, n));
	counter_u64_add(sc->bytes_read, got);
	return (got);
}

static size_t
myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n)
{
	size_t put;

	MYFIRST_ASSERT(sc);
	KASSERT(sc->is_attached,
	    ("myfirst_buf_write called after detach"));
	put = cbuf_write(&sc->cb, src, n);
	KASSERT(put <= n,
	    ("cbuf_write returned %zu > n=%zu", put, n));
	counter_u64_add(sc->bytes_written, put);
	return (put);
}

static int
myfirst_wait_data(struct myfirst_softc *sc, int ioflag, ssize_t nbefore,
    struct uio *uio)
{
	int error, timo;

	MYFIRST_ASSERT(sc);
	while (cbuf_used(&sc->cb) == 0) {
		if (uio->uio_resid != nbefore)
			return (-1);
		if (ioflag & IO_NDELAY)
			return (EAGAIN);

		timo = sc->read_timeout_ms;
		if (timo > 0) {
			int ticks_total = (timo * hz + 999) / 1000;
			error = cv_timedwait_sig(&sc->data_cv, &sc->mtx,
			    ticks_total);
		} else {
			error = cv_wait_sig(&sc->data_cv, &sc->mtx);
		}
		if (error == EWOULDBLOCK)
			return (EAGAIN);
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
	int error, timo;

	MYFIRST_ASSERT(sc);
	while (cbuf_free(&sc->cb) == 0) {
		if (uio->uio_resid != nbefore)
			return (-1);
		if (ioflag & IO_NDELAY)
			return (EAGAIN);

		timo = sc->write_timeout_ms;
		if (timo > 0) {
			int ticks_total = (timo * hz + 999) / 1000;
			error = cv_timedwait_sig(&sc->room_cv, &sc->mtx,
			    ticks_total);
		} else {
			error = cv_wait_sig(&sc->room_cv, &sc->mtx);
		}
		if (error == EWOULDBLOCK)
			return (EAGAIN);
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

	MYFIRST_LOCK(sc);
	val = (unsigned int)cbuf_used(&sc->cb);
	MYFIRST_UNLOCK(sc);
	return (sysctl_handle_int(oidp, &val, 0, req));
}

static int
myfirst_sysctl_cb_free(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int val;

	MYFIRST_LOCK(sc);
	val = (unsigned int)cbuf_free(&sc->cb);
	MYFIRST_UNLOCK(sc);
	return (sysctl_handle_int(oidp, &val, 0, req));
}

static void
myfirst_fh_dtor(void *data)
{
	struct myfirst_fh *fh = data;
	struct myfirst_softc *sc = fh->sc;

	MYFIRST_LOCK(sc);
	sc->active_fhs--;
	MYFIRST_UNLOCK(sc);

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

	MYFIRST_LOCK(sc);
	sc->open_count++;
	sc->active_fhs++;
	MYFIRST_UNLOCK(sc);
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
		MYFIRST_LOCK(sc);
		error = myfirst_wait_data(sc, ioflag, nbefore, uio);
		if (error != 0) {
			MYFIRST_UNLOCK(sc);
			return (error == -1 ? 0 : error);
		}
		take = MIN((size_t)uio->uio_resid, sizeof(bounce));
		got = myfirst_buf_read(sc, bounce, take);
		fh->reads += got;
		MYFIRST_UNLOCK(sc);

		if (got > 0) {
			cv_signal(&sc->room_cv);
			selwakeup(&sc->wsel);
		}

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
		MYFIRST_LOCK(sc);
		error = myfirst_wait_room(sc, ioflag, nbefore, uio);
		if (error != 0) {
			MYFIRST_UNLOCK(sc);
			return (error == -1 ? 0 : error);
		}
		room = cbuf_free(&sc->cb);
		MYFIRST_UNLOCK(sc);

		want = MIN((size_t)uio->uio_resid, sizeof(bounce));
		want = MIN(want, room);
		error = uiomove(bounce, want, uio);
		if (error != 0)
			return (error);

		MYFIRST_LOCK(sc);
		put = myfirst_buf_write(sc, bounce, want);
		fh->writes += put;
		MYFIRST_UNLOCK(sc);

		if (put > 0) {
			cv_signal(&sc->data_cv);
			selwakeup(&sc->rsel);
		}
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

	MYFIRST_LOCK(sc);
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
	MYFIRST_UNLOCK(sc);
	return (revents);
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
	device_set_desc(dev, "My First FreeBSD Driver (Chapter 12 Stage 2)");
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
	cv_init(&sc->data_cv, "myfirst data");
	cv_init(&sc->room_cv, "myfirst room");

	sc->attach_ticks = ticks;
	sc->is_attached = 1;
	sc->active_fhs = 0;
	sc->open_count = 0;
	sc->read_timeout_ms = 0;
	sc->write_timeout_ms = 0;
	sc->bytes_read = counter_u64_alloc(M_WAITOK);
	sc->bytes_written = counter_u64_alloc(M_WAITOK);

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
	SYSCTL_ADD_COUNTER_U64(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_read", CTLFLAG_RD,
	    sc->bytes_read, "Total bytes drained from the buffer");
	SYSCTL_ADD_COUNTER_U64(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "bytes_written", CTLFLAG_RD,
	    sc->bytes_written, "Total bytes appended to the buffer");
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

	SYSCTL_ADD_INT(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "read_timeout_ms", CTLFLAG_RW,
	    &sc->read_timeout_ms, 0,
	    "Read timeout in milliseconds (0 = block indefinitely)");
	SYSCTL_ADD_INT(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "write_timeout_ms", CTLFLAG_RW,
	    &sc->write_timeout_ms, 0,
	    "Write timeout in milliseconds (0 = block indefinitely)");

	device_printf(dev,
	    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
	    "cbuf=%zu bytes\n",
	    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
	return (0);

fail_cb:
	cbuf_destroy(&sc->cb);
fail_mtx:
	cv_destroy(&sc->data_cv);
	cv_destroy(&sc->room_cv);
	mtx_destroy(&sc->mtx);
	sc->is_attached = 0;
	return (error);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc;

	sc = device_get_softc(dev);

	MYFIRST_LOCK(sc);
	if (sc->active_fhs > 0) {
		MYFIRST_UNLOCK(sc);
		device_printf(dev,
		    "Cannot detach: %d open descriptor(s)\n",
		    sc->active_fhs);
		return (EBUSY);
	}
	sc->is_attached = 0;
	cv_broadcast(&sc->data_cv);
	cv_broadcast(&sc->room_cv);
	MYFIRST_UNLOCK(sc);

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
	counter_u64_free(sc->bytes_read);
	counter_u64_free(sc->bytes_written);
	cv_destroy(&sc->data_cv);
	cv_destroy(&sc->room_cv);
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

static SYSCTL_NODE(_hw, OID_AUTO, myfirst, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "myfirst driver");
SYSCTL_STRING(_hw_myfirst, OID_AUTO, version, CTLFLAG_RD,
    MYFIRST_VERSION, 0, "Driver version string");

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
MODULE_VERSION(myfirst, 1);
