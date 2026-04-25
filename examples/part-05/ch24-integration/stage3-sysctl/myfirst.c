/*
 * myfirst.c - Chapter 24 Stage 3: complete attach and detach.
 *
 * This is the final shape of myfirst_attach and myfirst_detach for
 * the chapter.  They tie every previous section together: the cdev
 * construction from Section 2, the ioctl wiring from Section 3, the
 * sysctl tree from Section 4, the lifecycle discipline from Section
 * 7, and the version handling from Section 8.
 *
 * Driver version: 1.7-integration.
 *
 * This file shows only the attach and detach functions.  Merge the
 * functions below into your driver alongside myfirst_ioctl.c and
 * myfirst_sysctl.c.  The cdevsw, the read/write callbacks, and the
 * SDT probe definitions remain in their existing locations.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

/*
 * MODULE_VERSION integer.  Numerically 17 (1.7).  Independent of
 * MYFIRST_VERSION (the human string in myfirst_sysctl.c) and of
 * MYFIRST_IOCTL_VERSION (the wire-format integer in myfirst_ioctl.h).
 */
MODULE_VERSION(myfirst, 17);

extern struct cdevsw myfirst_cdevsw;
extern void myfirst_sysctl_attach(struct myfirst_softc *);

static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/* 1. Stash the device pointer and initialise the lock. */
	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/* 2. Initialise the in-driver state to its defaults. */
	strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
	sc->sc_msglen = strlen(sc->sc_msg);
	sc->sc_open_count = 0;
	sc->sc_total_reads = 0;
	sc->sc_total_writes = 0;
	sc->sc_debug = 0;

	/*
	 * 3. Read the boot-time tunable for the debug mask.  If the
	 *    operator set hw.myfirst.debug_mask_default in
	 *    /boot/loader.conf, sc_debug now holds that value;
	 *    otherwise sc_debug remains zero.
	 */
	TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);

	/*
	 * 4. Construct the cdev.  The args struct gives us a typed,
	 *    versionable interface; mda_si_drv1 wires the per-cdev
	 *    pointer to the softc atomically, closing the race window
	 *    between creation and assignment.
	 */
	make_dev_args_init(&args);
	args.mda_devsw  = &myfirst_cdevsw;
	args.mda_uid    = UID_ROOT;
	args.mda_gid    = GID_WHEEL;
	args.mda_mode   = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit   = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev,
	    "myfirst%d", device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/*
	 * 5. Build the sysctl tree.  The framework owns the per-device
	 *    context, so we do not need to track or destroy it
	 *    ourselves; detach below does not call sysctl_ctx_free.
	 */
	myfirst_sysctl_attach(sc);

	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: stage 3 complete, version 1.7-integration\n");
	return (0);

fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/*
	 * 1. Refuse detach if anyone holds the device open.  The
	 *    chapter's pattern is the simple soft refusal; Challenge 3
	 *    walks through the more elaborate dev_ref/dev_rel pattern
	 *    that drains in-flight references rather than refusing.
	 */
	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0) {
		mtx_unlock(&sc->sc_mtx);
		return (EBUSY);
	}
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

	/*
	 * 2. Destroy the cdev.  destroy_dev blocks until every
	 *    in-flight cdevsw callback returns; after this call,
	 *    no new open/close/read/write/ioctl can arrive.
	 */
	destroy_dev(sc->sc_cdev);

	/*
	 * 3. The per-device sysctl context is torn down automatically
	 *    by the framework after detach returns successfully.
	 *    Nothing to do here.
	 */

	/*
	 * 4. Destroy the lock.  Safe now because the cdev is gone and
	 *    no other code path can take it.
	 */
	mtx_destroy(&sc->sc_mtx);

	return (0);
}
