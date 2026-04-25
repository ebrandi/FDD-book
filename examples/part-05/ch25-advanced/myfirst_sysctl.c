/*
 * myfirst_sysctl.c - sysctl tree for the myfirst driver.
 *
 * Chapter 25 extends the Chapter 24 tree with three new runtime
 * knobs: timeout_sec, max_retries, log_ratelimit_pps.  Each has a
 * matching hw.myfirst.* tunable and a range-validating handler.
 *
 * The per-device sysctl context is owned by Newbus; the driver does
 * not track or destroy it explicitly.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include "myfirst.h"
#include "myfirst_debug.h"

#define MYFIRST_VERSION  "1.8-maintenance"

static int
myfirst_sysctl_message_len(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int len;

	mtx_lock(&sc->sc_mtx);
	len = (u_int)sc->sc_msglen;
	mtx_unlock(&sc->sc_mtx);

	return (sysctl_handle_int(oidp, &len, 0, req));
}

static int
myfirst_sysctl_timeout_sec(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int new_val;
	int error;

	mtx_lock(&sc->sc_mtx);
	new_val = sc->sc_timeout_sec;
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val < 1 || new_val > 60)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	sc->sc_timeout_sec = new_val;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

static int
myfirst_sysctl_max_retries(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int new_val;
	int error;

	mtx_lock(&sc->sc_mtx);
	new_val = sc->sc_max_retries;
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val < 1 || new_val > 100)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	sc->sc_max_retries = new_val;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

static int
myfirst_sysctl_log_pps(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int new_val;
	int error;

	mtx_lock(&sc->sc_mtx);
	new_val = sc->sc_log_pps;
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val < 1 || new_val > 10000)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	sc->sc_log_pps = new_val;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

void
myfirst_sysctl_attach(struct myfirst_softc *sc)
{
	device_t dev = sc->sc_dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;
	struct sysctl_oid *debug_node;
	struct sysctl_oid_list *debug_child;

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
	    CTLFLAG_RD, MYFIRST_VERSION, 0,
	    "Driver version string");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
	    CTLFLAG_RD, &sc->sc_open_count, 0,
	    "Number of currently open file descriptors");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_reads",
	    CTLFLAG_RD, &sc->sc_total_reads, 0,
	    "Total read() calls since attach");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_writes",
	    CTLFLAG_RD, &sc->sc_total_writes, 0,
	    "Total write() calls since attach");

	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "message",
	    CTLFLAG_RD, sc->sc_msg, sizeof(sc->sc_msg),
	    "Current in-driver message");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
	    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_message_len, "IU",
	    "Current length of the in-driver message in bytes");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "timeout_sec",
	    CTLTYPE_UINT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_timeout_sec, "IU",
	    "Operation timeout in seconds (range 1-60)");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "max_retries",
	    CTLTYPE_UINT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_max_retries, "IU",
	    "Maximum retry count (range 1-100)");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "log_ratelimit_pps",
	    CTLTYPE_UINT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_log_pps, "IU",
	    "Rate-limit ceiling in messages per second (range 1-10000)");

	debug_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "debug",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "Debug controls and class enumeration");
	debug_child = SYSCTL_CHILDREN(debug_node);

	SYSCTL_ADD_UINT(ctx, debug_child, OID_AUTO, "mask",
	    CTLFLAG_RWTUN, &sc->sc_debug, 0,
	    "Bitmask of enabled debug classes");

	SYSCTL_ADD_STRING(ctx, debug_child, OID_AUTO, "classes",
	    CTLFLAG_RD,
	    "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) "
	    "INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)",
	    0, "Names and bit values of debug classes");
}
