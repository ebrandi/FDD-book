/*
 * myfirst.c - module glue and attach/detach for the myfirst driver.
 *
 * Chapter 25 (version 1.8-maintenance).  This file owns the cdevsw
 * table, the MODULE_VERSION declaration, the attach routine (with a
 * full labelled-cleanup chain), and the detach routine (mirror of
 * attach).  Every other responsibility lives in a focused sibling
 * file: myfirst_cdev.c, myfirst_ioctl.c, myfirst_sysctl.c,
 * myfirst_log.c, myfirst_debug.c.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

MODULE_VERSION(myfirst, 18);

struct cdevsw myfirst_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "myfirst",
	.d_open    = myfirst_open,
	.d_close   = myfirst_close,
	.d_read    = myfirst_read,
	.d_write   = myfirst_write,
	.d_ioctl   = myfirst_ioctl,
};

static void
myfirst_shutdown(void *arg, int howto)
{
	struct myfirst_softc *sc = arg;

#ifdef MYFIRST_SHUTDOWN_NOISY
	device_printf(sc->sc_dev, "shutdown: howto=0x%x\n", howto);
#else
	DPRINTF(sc, MYF_DBG_INIT, "shutdown: howto=0x%x\n", howto);
#endif
}

static int
myfirst_probe(device_t dev)
{

	device_set_desc(dev, "myfirst pseudo-device (Chapter 25)");
	return (BUS_PROBE_NOWILDCARD);
}

static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/* Stage 1: softc basics.  Cannot fail. */
	sc->sc_dev = dev;

	/* Stage 2: lock.  Cannot fail on MTX_DEF. */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

#ifdef MYFIRST_DEBUG_INJECT_FAIL_MTX
	device_printf(dev, "attach: stage 1 complete\n");
	device_printf(dev, "attach: injected failure after mtx_init\n");
	error = ENOMEM;
	goto fail_mtx;
#endif

	/* Stage 3: defaults and tunable fetches.  Cannot fail. */
	strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
	sc->sc_msglen = strlen(sc->sc_msg);
	sc->sc_open_count = 0;
	sc->sc_total_reads = 0;
	sc->sc_total_writes = 0;
	sc->sc_debug = 0;
	sc->sc_timeout_sec = 5;
	sc->sc_max_retries = 3;
	sc->sc_log_pps = MYF_RL_DEFAULT_PPS;

	TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default",
	    &sc->sc_debug);
	TUNABLE_INT_FETCH("hw.myfirst.timeout_sec",
	    &sc->sc_timeout_sec);
	TUNABLE_INT_FETCH("hw.myfirst.max_retries",
	    &sc->sc_max_retries);
	TUNABLE_INT_FETCH("hw.myfirst.log_ratelimit_pps",
	    &sc->sc_log_pps);

	/* Stage 4: cdev.  Can fail. */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

#ifdef MYFIRST_DEBUG_INJECT_FAIL_CDEV
	device_printf(dev, "attach: injected failure after make_dev_s\n");
	error = ENOMEM;
	goto fail_cdev;
#endif

	/* Stage 5: sysctl tree.  Cannot fail (Newbus owns the context). */
	myfirst_sysctl_attach(sc);

#ifdef MYFIRST_DEBUG_INJECT_FAIL_SYSCTL
	device_printf(dev, "attach: injected failure after sysctl\n");
	error = ENOMEM;
	goto fail_cdev;
#endif

	/* Stage 6: rate-limit state.  Can fail. */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

#ifdef MYFIRST_DEBUG_INJECT_FAIL_LOG
	device_printf(dev, "attach: injected failure after log_attach\n");
	error = ENOMEM;
	goto fail_log;
#endif

	/* Stage 7: shutdown handler.  Can fail. */
	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance complete\n");
	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}

static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0) {
		mtx_unlock(&sc->sc_mtx);
		return (EBUSY);
	}
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_INIT, "detach: tearing down\n");

	EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_tag);
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static device_method_t myfirst_methods[] = {
	DEVMETHOD(device_probe,   myfirst_probe),
	DEVMETHOD(device_attach,  myfirst_attach),
	DEVMETHOD(device_detach,  myfirst_detach),
	DEVMETHOD_END
};

static driver_t myfirst_driver = {
	"myfirst",
	myfirst_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
