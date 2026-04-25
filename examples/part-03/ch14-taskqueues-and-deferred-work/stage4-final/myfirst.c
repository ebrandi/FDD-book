/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * Chapter 14, Stage 4: the final taskqueue-enhanced driver.
 *
 * Builds on Chapter 13 Stage 4 by adding a private taskqueue and three
 * tasks:
 *
 *   - selwake_task (plain):      defers selwakeup() out of the
 *                                tick_source callout callback.
 *   - bulk_writer_task (plain):  demonstrates deliberate coalescing
 *                                via the ta_pending rule; writes a
 *                                configurable batch of bytes per firing.
 *   - reset_delayed_task (timeout_task): performs a delayed reset in
 *                                thread context, scheduled via a sysctl.
 *
 * The taskqueue is created in attach with taskqueue_create and serviced
 * by a single worker thread started at PWAIT via taskqueue_start_threads.
 * All tasks are drained at detach before the taskqueue is freed.
 *
 * Detach ordering:
 *   1. Refuse detach if active_fhs > 0.
 *   2. Clear is_attached under sc->mtx; broadcast cvs; release mtx.
 *   3. Drain all three callouts.
 *   4. Drain all three tasks (the timeout_task via taskqueue_drain_timeout).
 *   5. seldrain(&sc->rsel), seldrain(&sc->wsel).
 *   6. taskqueue_free(sc->tq); sc->tq = NULL.
 *   7. Destroy cdev, free sysctl context, destroy cbuf, counters, cvs,
 *      sx, mutex.
 *
 * Locking strategy (unchanged from Chapter 13 except for the task
 * additions):
 *
 * sc->mtx protects:
 *   - sc->cb (the circular buffer's internal state)
 *   - sc->open_count, sc->active_fhs
 *   - sc->is_attached (writes; reads at handler entry may be unprotected)
 *   - sc->selwake_pending_drops, sc->bulk_writer_batch
 *
 * sc->bytes_read, sc->bytes_written are counter(9) per-CPU counters.
 *
 * sc->data_cv: cv signalled when bytes are added to the cbuf.
 * sc->room_cv: cv signalled when room is freed in the cbuf.
 * Both use sc->mtx as their interlock.
 *
 * Task callbacks run in thread context on the private taskqueue; they
 * acquire sc->mtx explicitly when needed and never hold it across
 * selwakeup(9).
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
#include <sys/priority.h>

#include "cbuf.h"

#define	MYFIRST_BUFSIZE		4096
#define	MYFIRST_BOUNCE		256
#define	MYFIRST_VERSION		"0.8-taskqueues"
#define	MYFIRST_BULK_MAX	64

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

	/* Callouts (from Chapter 13). */
	struct callout		heartbeat_co;
	int			heartbeat_interval_ms;
	struct callout		watchdog_co;
	int			watchdog_interval_ms;
	size_t			watchdog_last_used;
	struct callout		tick_source_co;
	int			tick_source_interval_ms;
	char			tick_source_byte;

	/* Chapter 14: private taskqueue and tasks. */
	struct taskqueue       *tq;
	struct task		selwake_task;
	int			selwake_pending_drops;
	struct task		bulk_writer_task;
	int			bulk_writer_batch;
	struct timeout_task	reset_delayed_task;
	int			reset_delay_ms;

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

/*
 * Chapter 14 task callbacks. Each runs in thread context on sc->tq with
 * no driver lock held. Callbacks acquire sc->mtx explicitly if they
 * need to touch mutex-protected state, and release it before making
 * calls like selwakeup(9) that must be invoked without unrelated locks.
 */

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
myfirst_bulk_writer_task(void *arg, int pending)
{
	struct myfirst_softc *sc = arg;
	char buf[MYFIRST_BULK_MAX];
	int batch, written;

	(void)pending;

	MYFIRST_LOCK(sc);
	batch = sc->bulk_writer_batch;
	MYFIRST_UNLOCK(sc);

	if (batch <= 0)
		return;

	if (batch > (int)sizeof(buf))
		batch = sizeof(buf);
	memset(buf, 'B', batch);

	MYFIRST_LOCK(sc);
	written = (int)cbuf_write(&sc->cb, buf, batch);
	if (written > 0) {
		counter_u64_add(sc->bytes_written, written);
		cv_signal(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);

	if (written > 0)
		selwakeup(&sc->rsel);
}

static void
myfirst_reset_delayed_task(void *arg, int pending)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);
	MYFIRST_CFG_XLOCK(sc);

	cbuf_reset(&sc->cb);
	sc->cfg.debug_level = 0;
	counter_u64_zero(sc->bytes_read);
	counter_u64_zero(sc->bytes_written);

	MYFIRST_CFG_XUNLOCK(sc);
	MYFIRST_UNLOCK(sc);

	cv_broadcast(&sc->room_cv);
	device_printf(sc->dev, "delayed reset fired (pending=%d)\n", pending);
}

/* Callout callbacks (unchanged from Chapter 13 except tick_source). */

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
		    "watchdog: buffer has %zu bytes, no progress in last "
		    "interval; reader stuck?\n", used);
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

	/* Chapter 14: defer selwakeup to thread context via sc->tq. */
	if (wake_sel)
		taskqueue_enqueue(sc->tq, &sc->selwake_task);

	interval = sc->tick_source_interval_ms;
	if (interval > 0)
		callout_reset(&sc->tick_source_co,
		    (interval * hz + 999) / 1000,
		    myfirst_tick_source, sc);
}

/* Sysctl handlers for configuration, timers, and Chapter 14 additions. */

static int
myfirst_sysctl_debug_level(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int new, error;

	MYFIRST_CFG_SLOCK(sc);
	new = sc->cfg.debug_level;
	MYFIRST_CFG_SUNLOCK(sc);

	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new < 0 || new > 3)
		return (EINVAL);

	MYFIRST_CFG_XLOCK(sc);
	sc->cfg.debug_level = new;
	MYFIRST_CFG_XUNLOCK(sc);
	return (0);
}

static int
myfirst_sysctl_soft_byte_limit(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int new, error;

	MYFIRST_CFG_SLOCK(sc);
	new = sc->cfg.soft_byte_limit;
	MYFIRST_CFG_SUNLOCK(sc);

	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new < 0)
		return (EINVAL);

	MYFIRST_CFG_XLOCK(sc);
	sc->cfg.soft_byte_limit = new;
	MYFIRST_CFG_XUNLOCK(sc);
	return (0);
}

static int
myfirst_sysctl_nickname(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	char buf[sizeof(sc->cfg.nickname)];
	int error;

	MYFIRST_CFG_SLOCK(sc);
	strlcpy(buf, sc->cfg.nickname, sizeof(buf));
	MYFIRST_CFG_SUNLOCK(sc);

	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error || req->newptr == NULL)
		return (error);

	MYFIRST_CFG_XLOCK(sc);
	strlcpy(sc->cfg.nickname, buf, sizeof(sc->cfg.nickname));
	MYFIRST_CFG_XUNLOCK(sc);
	return (0);
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

static int
myfirst_sysctl_reset(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int reset = 0;
	int error;

	error = sysctl_handle_int(oidp, &reset, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (reset != 1)
		return (0);

	MYFIRST_LOCK(sc);
	MYFIRST_CFG_XLOCK(sc);

	cbuf_reset(&sc->cb);
	sc->cfg.debug_level = 0;
	counter_u64_zero(sc->bytes_read);
	counter_u64_zero(sc->bytes_written);

	MYFIRST_CFG_XUNLOCK(sc);
	MYFIRST_UNLOCK(sc);

	cv_broadcast(&sc->room_cv);
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

/* Chapter 14: sysctls for task-driven features. */

static int
myfirst_sysctl_bulk_writer_batch(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int new, error;

	MYFIRST_LOCK(sc);
	new = sc->bulk_writer_batch;
	MYFIRST_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (new < 0 || new > MYFIRST_BULK_MAX)
		return (EINVAL);

	MYFIRST_LOCK(sc);
	sc->bulk_writer_batch = new;
	MYFIRST_UNLOCK(sc);
	return (0);
}

static int
myfirst_sysctl_bulk_writer_flood(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int flood = 0;
	int error, i;

	error = sysctl_handle_int(oidp, &flood, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (flood < 1 || flood > 10000)
		return (EINVAL);

	for (i = 0; i < flood; i++)
		taskqueue_enqueue(sc->tq, &sc->bulk_writer_task);
	return (0);
}

static int
myfirst_sysctl_reset_delayed(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	int ms = 0;
	int error;

	error = sysctl_handle_int(oidp, &ms, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (ms < 0)
		return (EINVAL);
	if (ms == 0) {
		(void)taskqueue_cancel_timeout(sc->tq,
		    &sc->reset_delayed_task, NULL);
		return (0);
	}

	sc->reset_delay_ms = ms;
	taskqueue_enqueue_timeout(sc->tq, &sc->reset_delayed_task,
	    (ms * hz + 999) / 1000);
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
	device_set_desc(dev, "My First FreeBSD Driver (Chapter 14 Stage 4)");
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

	/* Chapter 14: create the private taskqueue before anything that
	 * could enqueue onto it. */
	sc->tq = taskqueue_create("myfirst taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->tq);
	if (sc->tq == NULL) {
		error = ENOMEM;
		goto fail_sx;
	}
	error = taskqueue_start_threads(&sc->tq, 1, PWAIT,
	    "%s taskq", device_get_nameunit(dev));
	if (error != 0)
		goto fail_tq;

	MYFIRST_CO_INIT(sc, &sc->heartbeat_co);
	MYFIRST_CO_INIT(sc, &sc->watchdog_co);
	MYFIRST_CO_INIT(sc, &sc->tick_source_co);

	/* Chapter 14: initialise every task. */
	TASK_INIT(&sc->selwake_task, 0, myfirst_selwake_task, sc);
	TASK_INIT(&sc->bulk_writer_task, 0, myfirst_bulk_writer_task, sc);
	TIMEOUT_TASK_INIT(sc->tq, &sc->reset_delayed_task, 0,
	    myfirst_reset_delayed_task, sc);

	sc->cfg.debug_level = 0;
	sc->cfg.soft_byte_limit = 0;
	strlcpy(sc->cfg.nickname, "myfirst", sizeof(sc->cfg.nickname));

	sc->attach_ticks = ticks;
	sc->is_attached = 1;
	sc->active_fhs = 0;
	sc->open_count = 0;
	sc->read_timeout_ms = 0;
	sc->write_timeout_ms = 0;
	sc->heartbeat_interval_ms = 0;
	sc->watchdog_interval_ms = 0;
	sc->watchdog_last_used = 0;
	sc->tick_source_interval_ms = 0;
	sc->tick_source_byte = 't';
	sc->selwake_pending_drops = 0;
	sc->bulk_writer_batch = 0;
	sc->reset_delay_ms = 0;
	sc->bytes_read = counter_u64_alloc(M_WAITOK);
	sc->bytes_written = counter_u64_alloc(M_WAITOK);

	error = cbuf_init(&sc->cb, MYFIRST_BUFSIZE);
	if (error != 0)
		goto fail_tq;

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

	/* Chapter 14: expose the task coalescing counter. */
	SYSCTL_ADD_INT(&sc->sysctl_ctx, SYSCTL_CHILDREN(sc->sysctl_tree),
	    OID_AUTO, "selwake_pending_drops", CTLFLAG_RD,
	    &sc->selwake_pending_drops, 0,
	    "Times selwake_task coalesced two or more enqueues");

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

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "debug_level",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_debug_level, "I",
	    "Debug verbosity (0-3)");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "soft_byte_limit",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_soft_byte_limit, "I",
	    "Soft limit on cb_used; writes refused above this");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "nickname",
	    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_nickname, "A",
	    "Human-readable nickname for log lines");

	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "reset",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_reset, "I",
	    "Write 1 to reset cbuf, counters, and configuration");

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

	/* Chapter 14: task sysctls. */
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "bulk_writer_batch",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_bulk_writer_batch, "I",
	    "Bytes the bulk_writer_task writes per firing (0-64)");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "bulk_writer_flood",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_bulk_writer_flood, "I",
	    "Write N to enqueue bulk_writer_task N times (demonstrates "
	    "coalescing)");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "reset_delayed",
	    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_reset_delayed, "I",
	    "Write N (ms) to arm a delayed reset; 0 cancels any pending "
	    "delayed reset");

	device_printf(dev,
	    "Attached; version %s, node /dev/%s (alias /dev/myfirst), "
	    "cbuf=%zu bytes\n",
	    MYFIRST_VERSION, devtoname(sc->cdev), cbuf_size(&sc->cb));
	return (0);

fail_cb:
	cbuf_destroy(&sc->cb);
fail_tq:
	if (sc->tq != NULL)
		taskqueue_free(sc->tq);
fail_sx:
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
		device_printf(dev,
		    "Cannot detach: %d open descriptor(s)\n",
		    sc->active_fhs);
		return (EBUSY);
	}
	sc->is_attached = 0;
	cv_broadcast(&sc->data_cv);
	cv_broadcast(&sc->room_cv);
	MYFIRST_UNLOCK(sc);

	/* Callouts first: guarantees no more enqueues come from timers. */
	MYFIRST_CO_DRAIN(&sc->heartbeat_co);
	MYFIRST_CO_DRAIN(&sc->watchdog_co);
	MYFIRST_CO_DRAIN(&sc->tick_source_co);

	/* Tasks second: guarantees no callback is executing. */
	taskqueue_drain(sc->tq, &sc->selwake_task);
	taskqueue_drain(sc->tq, &sc->bulk_writer_task);
	taskqueue_drain_timeout(sc->tq, &sc->reset_delayed_task);

	/* selinfo third: safe now that no task will call selwakeup. */
	seldrain(&sc->rsel);
	seldrain(&sc->wsel);

	/* Free the taskqueue last, after every task is drained. */
	taskqueue_free(sc->tq);
	sc->tq = NULL;

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
