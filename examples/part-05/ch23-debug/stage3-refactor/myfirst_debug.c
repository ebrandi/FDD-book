/*
 * myfirst_debug.c - storage for the SDT probe entries.
 *
 * This file holds the SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE
 * declarations.  By convention in the myfirst driver, these live in a
 * dedicated source file to keep the main driver uncluttered.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>
#include "myfirst_debug.h"

/*
 * The provider "myfirst" exposes all of our static probes to DTrace.
 * Scripts select probes with "myfirst:::<name>".
 */
SDT_PROVIDER_DEFINE(myfirst);

/*
 * open: fired on every device open.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags from the open call
 */
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");

/*
 * close: fired on every device close.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags
 */
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");

/*
 * io: fired on every read or write call, at function entry.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int is_write (0 for read, 1 for write)
 *   arg2 = size_t resid (bytes requested)
 *   arg3 = off_t offset
 */
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
