/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * Chapter 14, Stage 1: the first task.
 *
 * Starts from Chapter 13 Stage 4 and adds one task, enqueued from the
 * tick_source callout callback so that selwakeup() runs in thread
 * context.  The task uses the shared taskqueue_thread; Stage 2 moves
 * it to a private taskqueue.
 *
 * The detach ordering adds one drain step (taskqueue_drain after the
 * callouts are drained and before seldrain).
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
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/counter.h>
#include <sys/taskqueue.h>

#include "cbuf.h"

#define	MYFIRST_BUFSIZE		4096
#define	MYFIRST_BOUNCE		256
#define	MYFIRST_VERSION		"0.8-taskqueues-stage1"

#define	MYFIRST_LOCK(sc)	mtx_lock(&(sc)->mtx)
#define	MYFIRST_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)
#define	MYFIRST_ASSERT(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)

#define	MYFIRST_CFG_SLOCK(sc)	sx_slock(&(sc)->cfg_sx)
#define	MYFIRST_CFG_SUNLOCK(sc)	sx_sunlock(&(sc)->cfg_sx)
#define	MYFIRST_CFG_XLOCK(sc)	sx_xlock(&(sc)->cfg_sx)
#define	MYFIRST_CFG_XUNLOCK(sc)	sx_xunlock(&(sc)->cfg_sx)

#define	MYFIRST_CO_INIT(sc, co)	callout_init_mtx((co), &(sc)->mtx, 0)
#define	MYFIRST_CO_DRAIN(co)	callout_drain((co))

struct myfirst_config {
	int	debug_level;
	int	soft_byte_limit;
	char	nickname[32];
};

struct myfirst_softc {
	device_t		dev;
	int			unit;

	struct mtx		mtx;
	struct cv		data_cv;
	struct cv		room_cv;

	struct sx		cfg_sx;
	struct myfirst_config	cfg;

	uint64_t		attach_ticks;
	uint64_t		open_count;
	counter_u64_t		bytes_read;
	counter_u64_t		bytes_written;

	int			active_fhs;
	int			is_attached;
	int			read_timeout_ms;
	int			write_timeout_ms;

	struct callout		heartbeat_co;
	int			heartbeat_interval_ms;
	struct callout		watchdog_co;
	int			watchdog_interval_ms;
	size_t			watchdog_last_used;
	struct callout		tick_source_co;
	int			tick_source_interval_ms;
	char			tick_source_byte;

	/* Chapter 14 Stage 1: one task on the shared taskqueue_thread. */
	struct task		selwake_task;
	int			selwake_pending_drops;

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

static int
myfirst_get_debug_level(struct myfirst_softc *sc)
{
	int level;

	MYFIRST_CFG_SLOCK(sc);
	level = sc->cfg.debug_level;
	MYFIRST_CFG_SUNLOCK(sc);
	return (level);
}

static int
myfirst_get_soft_byte_limit(struct myfirst_softc *sc)
{
	int limit;

	MYFIRST_CFG_SLOCK(sc);
	limit = sc->cfg.soft_byte_limit;
	MYFIRST_CFG_SUNLOCK(sc);
	return (limit);
}

#define	MYFIRST_DBG(sc, level, fmt, ...) do {				\
	if (myfirst_get_debug_level(sc) >= (level))			\
		device_printf((sc)->dev, fmt, ##__VA_ARGS__);		\
} while (0)

static size_t
myfirst_buf_read(struct myfirst_softc *sc, void *dst, size_t n)
{
	size_t got;

	MYFIRST_ASSERT(sc);
	got = cbuf_read(&sc->cb, dst, n);
	counter_u64_add(sc->bytes_read, got);
	return (got);
}

static size_t
myfirst_buf_write(struct myfirst_softc *sc, const void *src, size_t n)
{
	size_t put;

	MYFIRST_ASSERT(sc);
	put = cbuf_write(&sc->cb, src, n);
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

/* Chapter 14 Stage 1: the selwake task. */
static void
myfirst_selwake_task(void *arg, int pending)
{
	struct myfirst_softc *sc = arg;

	if (pending > 1) {
		MYFIRST_LOCK(sc);
		sc->selwake_pending_drops += pending - 1;
		MYFIRST_UNLOCK(sc);
	}

	selwakeup(&sc->rsel);
}

static void
myfirst_heartbeat(void *arg)
{
	struct myfirst_softc *sc = arg;
	size_t used;
	uint64_t br, bw;
	int interval;

	MYFIRST_ASSERT(sc);
	if (!sc->is_attached)
		return;

	used = cbuf_used(&sc->cb);
	br = counter_u64_fetch(sc->bytes_read);
	bw = counter_u64_fetch(sc->bytes_written);
	device_printf(sc->dev,
	    "heartbeat: cb_used=%zu, bytes_read=%ju, bytes_written=%ju\n",
	    used, (uintmax_t)br, (uintmax_t)bw);

	interval = sc->heartbeat_interval_ms;
	if (interval > 0)
		callout_reset(&sc->heartbeat_co,
		    (interval * hz + 999) / 1000,
		    myfirst_heartbeat, sc);
}

static void
myfirst_watchdog(void *arg)
{
	struct myfirst_softc *sc = arg;
	size_t used;
	int interval;

	MYFIRST_ASSERT(sc);
	if (!sc->is_attached)
		return;

	used = cbuf_used(&sc->cb);
	if (used > 0 && used == sc->watchdog_last_used) {
		device_printf(sc->dev,
		    "watchdog: buffer has %zu bytes, no progress\n", used);
	}
	sc->watchdog_last_used = used;

	interval = sc->watchdog_interval_ms;
	if (interval > 0)
		callout_reset(&sc->watchdog_co,
		    (interval * hz + 999) / 1000,
		    myfirst_watchdog, sc);
}

static void
myfirst_tick_source(void *arg)
{
	struct myfirst_softc *sc = arg;
	size_t put;
	int interval;
	bool wake_sel = false;

	MYFIRST_ASSERT(sc);
	if (!sc->is_attached)
		return;

	if (cbuf_free(&sc->cb) > 0) {
		put = cbuf_write(&sc->cb, &sc->tick_source_byte, 1);
		if (put > 0) {
			counter_u64_add(sc->bytes_written, put);
			cv_signal(&sc->data_cv);
			wake_sel = true;
		}
	}

	/* Chapter 14 Stage 1: defer selwakeup via the shared taskqueue. */
	if (wake_sel)
		taskqueue_enqueue(taskqueue_thread, &sc->selwake_task);

	interval = sc->tick_source_interval_ms;
	if (interval > 0)
		callout_reset(&sc->tick_source_co,
		    (interval * hz + 999) / 1000,
		    myfirst_tick_source, sc);
}

static int
myfirst_sysctl_heartbeat_interval_ms(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int new, old, error;

	old = sc->heartbeat_interval_ms;
	new = old;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new < 0)
		return (EINVAL);
	MYFIRST_LOCK(sc);
	sc->heartbeat_interval_ms = new;
	if (new > 0 && old == 0)
		callout_reset(&sc->heartbeat_co,
		    (new * hz + 999) / 1000, myfirst_heartbeat, sc);
	else if (new == 0 && old > 0)
		callout_stop(&sc->heartbeat_co);
	MYFIRST_UNLOCK(sc);
	return (0);
}

static int
myfirst_sysctl_watchdog_interval_ms(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int new, old, error;

	old = sc->watchdog_interval_ms;
	new = old;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new < 0)
		return (EINVAL);
	MYFIRST_LOCK(sc);
	sc->watchdog_interval_ms = new;
	if (new > 0 && old == 0) {
		sc->watchdog_last_used = cbuf_used(&sc->cb);
		callout_reset(&sc->watchdog_co,
		    (new * hz + 999) / 1000, myfirst_watchdog, sc);
	} else if (new == 0 && old > 0) {
		callout_stop(&sc->watchdog_co);
	}
	MYFIRST_UNLOCK(sc);
	return (0);
}

static int
myfirst_sysctl_tick_source_interval_ms(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int new, old, error;

	old = sc->tick_source_interval_ms;
	new = old;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new < 0)
		return (EINVAL);
	MYFIRST_LOCK(sc);
	sc->tick_source_interval_ms = new;
	if (new > 0 && old == 0)
		callout_reset(&sc->tick_source_co,
		    (new * hz + 999) / 1000, myfirst_tick_source, sc);
	else if (new == 0 && old > 0)
		callout_stop(&sc->tick_source_co);
	MYFIRST_UNLOCK(sc);
	return (0);
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
		int limit = myfirst_get_soft_byte_limit(sc);
		MYFIRST_LOCK(sc);
		if (limit > 0 && cbuf_used(&sc->cb) + sizeof(bounce) >
		    (size_t)limit) {
			MYFIRST_UNLOCK(sc);
			return (uio->uio_resid != nbefore ? 0 : EAGAIN);
		}
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
			MYFIRST_DBG(sc, 2, "wrote %zu bytes\n", put);
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
	device_set_desc(dev, "My First FreeBSD Driver (Chapter 14 Stage 1)");
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
	sx_init(&sc->cfg_sx, "myfirst cfg");

	MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
	MYFIRST_CO_INIT(sc, &sc->watchdog_co);
	MYFIRST_CO_INIT(sc, &sc->tick_source_co);

	TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);

	sc->cfg.debug_level = 0;
	sc->cfg.soft_byte_limit = 0;
	strlcpy(sc->cfg.nickname, "myfirst", sizeof(sc->cfg.nickname));

	sc->attach_ticks = ticks;
	sc->is_attached = 1;
	sc->tick_source_byte = 't';
	sc->selwake_pending_drops = 0;
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

	sysctl_ctx_init(&sc->sysctl_ctx);
	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "stats", CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
	    "Driver statistics");
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "selwake_pending_drops", CTLFLAG_RD,
	    &sc->selwake_pending_drops, 0,
	    "Times selwake_task coalesced two or more enqueues");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "heartbeat_interval_ms",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_heartbeat_interval_ms, "I",
	    "Heartbeat interval in milliseconds (0 = disabled)");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "watchdog_interval_ms",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_watchdog_interval_ms, "I",
	    "Watchdog interval in milliseconds (0 = disabled)");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "tick_source_interval_ms",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_tick_source_interval_ms, "I",
	    "Tick-source interval in milliseconds (0 = disabled)");

	device_printf(dev,
	    "Attached; version %s, node /dev/%s (alias /dev/myfirst)\n",
	    MYFIRST_VERSION, devtoname(sc->cdev));
	return (0);

fail_cb:
	cbuf_destroy(&sc->cb);
fail_mtx:
	counter_u64_free(sc->bytes_read);
	counter_u64_free(sc->bytes_written);
	cv_destroy(&sc->data_cv);
	cv_destroy(&sc->room_cv);
	sx_destroy(&sc->cfg_sx);
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
		return (EBUSY);
	}
	sc->is_attached = 0;
	cv_broadcast(&sc->data_cv);
	cv_broadcast(&sc->room_cv);
	MYFIRST_UNLOCK(sc);

	/* Callouts first, then task, then seldrain. */
	MYFIRST_CO_DRAIN(&sc->heartbeat_co);
	MYFIRST_CO_DRAIN(&sc->watchdog_co);
	MYFIRST_CO_DRAIN(&sc->tick_source_co);

	taskqueue_drain(taskqueue_thread, &sc->selwake_task);

	seldrain(&sc->rsel);
	seldrain(&sc->wsel);

	if (sc->cdev_alias != NULL)
		destroy_dev(sc->cdev_alias);
	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);
	sysctl_ctx_free(&sc->sysctl_ctx);
	cbuf_destroy(&sc->cb);
	counter_u64_free(sc->bytes_read);
	counter_u64_free(sc->bytes_written);
	cv_destroy(&sc->data_cv);
	cv_destroy(&sc->room_cv);
	sx_destroy(&sc->cfg_sx);
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
