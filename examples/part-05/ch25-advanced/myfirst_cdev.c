/*
 * myfirst_cdev.c - character-device callbacks for the myfirst driver.
 *
 * open/close/read/write all operate on the softc that make_dev_s
 * installed as si_drv1.  ioctl dispatch is in myfirst_ioctl.c;
 * this file intentionally does not handle ioctls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

/*
 * Lab 1 rebuilds the module with MYFIRST_UNLIMITED_LOG defined so the
 * hot-path read/write messages go straight to device_printf instead of
 * through DLOG_RL.  The production build leaves the macro undefined,
 * which is the rate-limited path.
 */
#ifdef MYFIRST_UNLIMITED_LOG
#define MYF_CDEV_HOT_LOG(sc, rlp, pps, fmt, ...)			\
	device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__)
#else
#define MYF_CDEV_HOT_LOG(sc, rlp, pps, fmt, ...)			\
	DLOG_RL(sc, rlp, pps, fmt, ##__VA_ARGS__)
#endif

int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	mtx_lock(&sc->sc_mtx);
	sc->sc_open_count++;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN, "open: count now %u\n",
	    sc->sc_open_count);
	return (0);
}

int
myfirst_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0)
		sc->sc_open_count--;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN, "close: count now %u\n",
	    sc->sc_open_count);
	return (0);
}

int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct myfirst_softc *sc = dev->si_drv1;
	size_t amt;
	int error;

	mtx_lock(&sc->sc_mtx);
	if (uio->uio_offset >= (off_t)sc->sc_msglen) {
		mtx_unlock(&sc->sc_mtx);
		return (0);
	}
	amt = MIN(sc->sc_msglen - uio->uio_offset, uio->uio_resid);
	error = uiomove(sc->sc_msg + uio->uio_offset, amt, uio);
	sc->sc_total_reads++;
	mtx_unlock(&sc->sc_mtx);

	MYF_CDEV_HOT_LOG(sc, &sc->sc_rl_io, sc->sc_log_pps,
	    "read: %zu bytes served\n", amt);
	return (error);
}

int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct myfirst_softc *sc = dev->si_drv1;
	size_t amt;
	int error;

	if (uio->uio_resid > sizeof(sc->sc_msg) - 1)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	amt = uio->uio_resid;
	error = uiomove(sc->sc_msg, amt, uio);
	if (error == 0) {
		sc->sc_msg[amt] = '\0';
		sc->sc_msglen = amt;
		sc->sc_total_writes++;
	}
	mtx_unlock(&sc->sc_mtx);

	MYF_CDEV_HOT_LOG(sc, &sc->sc_rl_io, sc->sc_log_pps,
	    "write: %zu bytes accepted\n", amt);
	return (error);
}
