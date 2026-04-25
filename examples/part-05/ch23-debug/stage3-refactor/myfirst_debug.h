/*
 * myfirst_debug.h - debug and tracing infrastructure for the myfirst driver
 *
 * This header is included from the driver's source files. It provides:
 *   - a bitmask of debug verbosity classes
 *   - the DPRINTF macro for conditional device_printf
 *   - declarations for SDT probes that the driver fires at key points
 *
 * The matching SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE calls live in
 * myfirst_debug.c, which owns the storage for the probe entries.
 */

#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>

/*
 * Debug verbosity classes.  Each class is a single bit in sc->sc_debug.
 * The operator sets sysctl dev.myfirst.0.debug.mask to a combination of
 * these bits to enable the corresponding categories of output.
 *
 * Add new classes here when the driver grows new subsystems.  Use the
 * next unused bit and update DEBUG.md accordingly.
 */
#define	MYF_DBG_INIT	0x00000001	/* probe/attach/detach */
#define	MYF_DBG_OPEN	0x00000002	/* open/close lifecycle */
#define	MYF_DBG_IO	0x00000004	/* read/write paths */
#define	MYF_DBG_IOCTL	0x00000008	/* ioctl handling */
#define	MYF_DBG_INTR	0x00000010	/* interrupt handler */
#define	MYF_DBG_DMA	0x00000020	/* DMA mapping/sync */
#define	MYF_DBG_PWR	0x00000040	/* power-management events */
#define	MYF_DBG_MEM	0x00000080	/* malloc/free trace */
/* Bits 0x0100..0x8000 reserved for future driver subsystems */

#define	MYF_DBG_ANY	0xFFFFFFFF
#define	MYF_DBG_NONE	0x00000000

/*
 * DPRINTF - conditionally log a message via device_printf when the
 * given class bit is set in the softc's debug mask.
 *
 * Usage: DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d\n", pid);
 *
 * When the bit is clear, the cost is one load and one branch, which is
 * negligible in practice.  When the bit is set, the cost equals a
 * normal device_printf call.
 */
#ifdef _KERNEL
#define	DPRINTF(sc, m, ...) do {					\
	if ((sc)->sc_debug & (m))					\
		device_printf((sc)->sc_dev, __VA_ARGS__);		\
} while (0)
#endif

/*
 * SDT probe declarations.  The matching SDT_PROBE_DEFINE calls are in
 * myfirst_debug.c.
 *
 * Probe argument conventions:
 *   open  (softc *, flags)                -- entry, before access check
 *   close (softc *, flags)                -- entry, before state update
 *   io    (softc *, is_write, resid, off) -- entry, into read or write
 */
SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);

#endif /* _MYFIRST_DEBUG_H_ */
