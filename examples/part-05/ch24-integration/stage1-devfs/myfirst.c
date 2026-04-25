/*
 * myfirst.c - Chapter 24 Stage 1: modernised cdev construction.
 *
 * This stage takes the Chapter 23 Stage 3 driver (version 1.6-debug)
 * and converts the cdev creation path from the old ad-hoc make_dev()
 * pattern to the modern make_dev_args / make_dev_s pattern.  The
 * cdevsw gains the D_TRACKCLOSE flag so close fires once per last fd.
 * The mda_si_drv1 field wires the per-cdev pointer to the softc
 * atomically, closing the race between cdev creation and assignment.
 *
 * No version bump in this stage; the driver remains at 1.6-debug.
 * The version bump happens in Stage 2 when the ioctl interface is
 * introduced.
 *
 * This file shows only the pieces that change in Stage 1.  Merge the
 * excerpts below into the corresponding locations in your Chapter 23
 * Stage 3 myfirst.c.  The full driver is reconstructed in Stage 3.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>

#include "myfirst.h"
#include "myfirst_debug.h"

/* Forward declarations of cdevsw callbacks. */
static d_open_t  myfirst_open;
static d_close_t myfirst_close;
static d_read_t  myfirst_read;
static d_write_t myfirst_write;

/*
 * The cdevsw with D_TRACKCLOSE.  The d_ioctl slot is left empty for
 * Stage 1 and is filled in Stage 2 once the dispatcher exists.
 */
static struct cdevsw myfirst_cdevsw = {
	.d_version = D_VERSION,
	.d_flags   = D_TRACKCLOSE,
	.d_name    = "myfirst",
	.d_open    = myfirst_open,
	.d_close   = myfirst_close,
	.d_read    = myfirst_read,
	.d_write   = myfirst_write,
};

/*
 * Stage 1 attach.  The headline change is the use of make_dev_args /
 * make_dev_s and the assignment of mda_si_drv1 at creation time.
 */
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc;
	struct make_dev_args args;
	int error;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/*
	 * Build the cdev for /dev/myfirstN.  mda_si_drv1 ties the
	 * per-cdev pointer to the softc, so cdevsw callbacks can
	 * recover the softc with dev->si_drv1 and never see a NULL.
	 */
	make_dev_args_init(&args);
	args.mda_devsw  = &myfirst_cdevsw;
	args.mda_uid    = UID_ROOT;
	args.mda_gid    = GID_WHEEL;
	args.mda_mode   = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit   = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0) {
		device_printf(dev, "make_dev_s failed: %d\n", error);
		mtx_destroy(&sc->sc_mtx);
		return (error);
	}

	DPRINTF(sc, MYF_DBG_INIT, "cdev created at /dev/myfirst%d\n",
	    device_get_unit(dev));
	return (0);
}

/*
 * Stage 1 detach.  Soft refusal if the device is open, then destroy
 * the cdev (which drains any in-flight callbacks) before tearing
 * down anything that the cdevsw callbacks depended on.
 */
static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0) {
		mtx_unlock(&sc->sc_mtx);
		device_printf(dev,
		    "detach refused: %u open(s) outstanding\n",
		    sc->sc_open_count);
		return (EBUSY);
	}
	mtx_unlock(&sc->sc_mtx);

	if (sc->sc_cdev != NULL) {
		destroy_dev(sc->sc_cdev);
		sc->sc_cdev = NULL;
		DPRINTF(sc, MYF_DBG_INIT, "cdev destroyed\n");
	}

	mtx_destroy(&sc->sc_mtx);
	return (0);
}

/*
 * Open and close use si_drv1 to recover the softc and bump the
 * per-instance counter under the softc mutex.
 */
static int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	SDT_PROBE2(myfirst, , , open, sc, oflags);

	mtx_lock(&sc->sc_mtx);
	sc->sc_open_count++;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN,
	    "open: pid=%d flags=%#x open_count=%u\n",
	    td->td_proc->p_pid, oflags, sc->sc_open_count);
	return (0);
}

static int
myfirst_close(struct cdev *dev, int fflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	SDT_PROBE2(myfirst, , , close, sc, fflags);

	mtx_lock(&sc->sc_mtx);
	KASSERT(sc->sc_open_count > 0,
	    ("myfirst_close: open_count underflow"));
	sc->sc_open_count--;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN,
	    "close: pid=%d flags=%#x open_count=%u\n",
	    td->td_proc->p_pid, fflags, sc->sc_open_count);
	return (0);
}

/*
 * Read and write are unchanged from Chapter 23.  They are listed
 * here as forward declarations for completeness; the actual bodies
 * remain in the Chapter 23 source you copied alongside this file.
 */
static int myfirst_read(struct cdev *dev, struct uio *uio, int ioflag);
static int myfirst_write(struct cdev *dev, struct uio *uio, int ioflag);
