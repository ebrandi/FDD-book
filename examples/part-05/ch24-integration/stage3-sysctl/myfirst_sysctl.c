/*
 * myfirst_sysctl.c - sysctl tree for the myfirst driver.
 *
 * Builds dev.myfirst.<unit>.* with version, counters, message, and a
 * debug subtree (debug.mask, debug.classes).  The per-device sysctl
 * context is owned by Newbus, so the driver does not have to track
 * or destroy the OIDs explicitly.  myfirst_detach can stay quiet
 * about sysctl: the framework cleans up after detach returns.
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

/*
 * MYFIRST_VERSION - human-readable release string.  Read by the
 * dev.myfirst.<unit>.version OID.  Bumped per the convention in
 * Section 8: major.minor with a -tag suffix that names the chapter
 * milestone.
 */
#define MYFIRST_VERSION		"1.7-integration"

/*
 * Handler for the message_len OID.  Static OIDs work for fields the
 * kernel can dereference directly; message_len is computed from
 * sc_msglen under the softc mutex, so it needs a custom handler.
 */
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

void
myfirst_sysctl_attach(struct myfirst_softc *sc)
{
	device_t dev = sc->sc_dev;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct sysctl_oid_list *child;
	struct sysctl_oid *debug_node;
	struct sysctl_oid_list *debug_child;

	/*
	 * Use the per-device context that Newbus already created.  The
	 * tree corresponds to dev.myfirst.<unit>; child is its OID list.
	 */
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

	/* Read-only: driver version. */
	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "version",
	    CTLFLAG_RD, MYFIRST_VERSION, 0,
	    "Driver version string");

	/* Read-only: counters. */
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "open_count",
	    CTLFLAG_RD, &sc->sc_open_count, 0,
	    "Number of currently open file descriptors");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_reads",
	    CTLFLAG_RD, &sc->sc_total_reads, 0,
	    "Total read() calls since attach");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "total_writes",
	    CTLFLAG_RD, &sc->sc_total_writes, 0,
	    "Total write() calls since attach");

	/* Read-only: message buffer (no copy through user). */
	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "message",
	    CTLFLAG_RD, sc->sc_msg, sizeof(sc->sc_msg),
	    "Current in-driver message");

	/* Read-only handler: message length, computed. */
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "message_len",
	    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, myfirst_sysctl_message_len, "IU",
	    "Current length of the in-driver message in bytes");

	/* Subtree: debug.* */
	debug_node = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "debug",
	    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
	    "Debug controls and class enumeration");
	debug_child = SYSCTL_CHILDREN(debug_node);

	/*
	 * debug.mask is read-write at runtime and tunable at boot.
	 * The TUN flag tells the kernel to consult the loader env
	 * for hw.myfirst.debug_mask_default before this OID becomes
	 * accessible.
	 */
	SYSCTL_ADD_UINT(ctx, debug_child, OID_AUTO, "mask",
	    CTLFLAG_RW | CTLFLAG_TUN, &sc->sc_debug, 0,
	    "Bitmask of enabled debug classes");

	/*
	 * debug.classes is a static read-only string that lists the
	 * symbolic names and bit values.  Useful for operators who
	 * do not want to hunt through the source for the bits.
	 */
	SYSCTL_ADD_STRING(ctx, debug_child, OID_AUTO, "classes",
	    CTLFLAG_RD,
	    "INIT(0x1) OPEN(0x2) IO(0x4) IOCTL(0x8) "
	    "INTR(0x10) DMA(0x20) PWR(0x40) MEM(0x80)",
	    0, "Names and bit values of debug classes");
}
