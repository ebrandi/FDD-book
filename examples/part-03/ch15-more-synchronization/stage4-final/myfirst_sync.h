/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sync.h: the named synchronisation vocabulary of the
 * myfirst driver at Chapter 15 Stage 4.
 *
 * Every primitive the driver uses has a wrapper here. The main
 * source calls these wrappers; the wrappers are inlined away, so
 * the runtime cost is zero. The benefit is a readable,
 * centralised, and easily-changeable synchronisation strategy.
 *
 * This file depends on the definition of `struct myfirst_softc`
 * in myfirst.c.  Include this header after the softc definition.
 */

#ifndef MYFIRST_SYNC_H
#define MYFIRST_SYNC_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/sema.h>
#include <sys/condvar.h>
#include <sys/fcntl.h>
#include <machine/atomic.h>

/*
 * Data-path mutex wrappers.
 */
static __inline void
myfirst_sync_lock(struct myfirst_softc *sc)
{
	mtx_lock(&sc->mtx);
}

static __inline void
myfirst_sync_unlock(struct myfirst_softc *sc)
{
	mtx_unlock(&sc->mtx);
}

static __inline void
myfirst_sync_assert_locked(struct myfirst_softc *sc)
{
	mtx_assert(&sc->mtx, MA_OWNED);
}

/*
 * Configuration sx wrappers (read-mostly state).
 */
static __inline void
myfirst_sync_cfg_read_begin(struct myfirst_softc *sc)
{
	sx_slock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_read_end(struct myfirst_softc *sc)
{
	sx_sunlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_begin(struct myfirst_softc *sc)
{
	sx_xlock(&sc->cfg_sx);
}

static __inline void
myfirst_sync_cfg_write_end(struct myfirst_softc *sc)
{
	sx_xunlock(&sc->cfg_sx);
}

/*
 * Writer-cap semaphore wrappers.
 *
 * Returns 0 on success, EAGAIN for O_NONBLOCK when the semaphore is
 * exhausted, or ENXIO when detach occurred at any point.
 *
 * writers_inflight is incremented before any sema operation and
 * decremented by myfirst_sync_writer_leave.  Detach waits for it to
 * drop to zero before destroying the semaphore, which closes the
 * sema_destroy use-after-free race documented in Section 8.
 *
 * Every path that calls myfirst_sync_writer_enter and returns 0 MUST
 * later call myfirst_sync_writer_leave.  Paths that return non-zero
 * already decremented the inflight counter and must not call leave.
 */
static __inline int
myfirst_sync_writer_enter(struct myfirst_softc *sc, int ioflag)
{
	atomic_add_int(&sc->writers_inflight, 1);
	if (!atomic_load_acq_int(&sc->is_attached)) {
		atomic_subtract_int(&sc->writers_inflight, 1);
		return (ENXIO);
	}

	if (ioflag & IO_NDELAY) {
		if (!sema_trywait(&sc->writers_sema)) {
			mtx_lock(&sc->mtx);
			sc->writers_trywait_failures++;
			mtx_unlock(&sc->mtx);
			atomic_subtract_int(&sc->writers_inflight, 1);
			return (EAGAIN);
		}
	} else {
		sema_wait(&sc->writers_sema);
	}
	if (!atomic_load_acq_int(&sc->is_attached)) {
		sema_post(&sc->writers_sema);
		atomic_subtract_int(&sc->writers_inflight, 1);
		return (ENXIO);
	}
	return (0);
}

static __inline void
myfirst_sync_writer_leave(struct myfirst_softc *sc)
{
	sema_post(&sc->writers_sema);
	atomic_subtract_int(&sc->writers_inflight, 1);
}

/*
 * Wait for every in-flight writer to leave the semaphore region.
 * Called from detach AFTER is_attached has been cleared and wake-up
 * slots have been posted, and BEFORE sema_destroy.
 */
static __inline void
myfirst_sync_writers_drain(struct myfirst_softc *sc)
{
	while (atomic_load_acq_int(&sc->writers_inflight) > 0)
		pause("myfwrd", 1);
}

/*
 * Stats-cache sx wrappers (upgrade-promote-downgrade pattern).
 */
static __inline void
myfirst_sync_stats_cache_read_begin(struct myfirst_softc *sc)
{
	sx_slock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_read_end(struct myfirst_softc *sc)
{
	sx_sunlock(&sc->stats_cache_sx);
}

static __inline int
myfirst_sync_stats_cache_try_promote(struct myfirst_softc *sc)
{
	return (sx_try_upgrade(&sc->stats_cache_sx));
}

static __inline void
myfirst_sync_stats_cache_downgrade(struct myfirst_softc *sc)
{
	sx_downgrade(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_begin(struct myfirst_softc *sc)
{
	sx_xlock(&sc->stats_cache_sx);
}

static __inline void
myfirst_sync_stats_cache_write_end(struct myfirst_softc *sc)
{
	sx_xunlock(&sc->stats_cache_sx);
}

/*
 * Atomic is_attached flag wrappers.
 */
static __inline int
myfirst_sync_is_attached(struct myfirst_softc *sc)
{
	return (atomic_load_acq_int(&sc->is_attached));
}

static __inline void
myfirst_sync_mark_detaching(struct myfirst_softc *sc)
{
	atomic_store_rel_int(&sc->is_attached, 0);
}

static __inline void
myfirst_sync_mark_attached(struct myfirst_softc *sc)
{
	atomic_store_rel_int(&sc->is_attached, 1);
}

#endif /* MYFIRST_SYNC_H */
