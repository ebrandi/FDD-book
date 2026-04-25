/*
 * portdrv.h - public header for the Chapter 29 reference driver.
 *
 * Included by every file in the driver. Declares the softc, the core
 * entry points used across files, and the shared constants the core
 * and its backends need to agree on.
 */

#ifndef _PORTDRV_H_
#define _PORTDRV_H_

#include <sys/cdefs.h>
#include <sys/types.h>

/* Driver version. Bump when externally observable behaviour changes. */
#define PORTDRV_VERSION		8

/* Shared register offsets. */
#define REG_ID			0x00
#define REG_STATUS		0x04
#define REG_DATA		0x08
#define REG_CONTROL		0x0c

#define PORTDRV_EXPECTED_ID	0x504f5254

/* Backend interface version. Bumped on every incompatible change. */
#define PORTDRV_BACKEND_VERSION	2

/* Malloc type for driver allocations. */
MALLOC_DECLARE(M_PORTDRV);

/* Forward declarations. */
struct portdrv_softc;

/*
 * Per-instance statistics exposed through sysctl. Update counters
 * atomically; they are read without holding the softc lock.
 */
struct portdrv_stats {
	uint64_t	transfers_ok;
	uint64_t	transfers_fail;
	uint64_t	bytes_in;
	uint64_t	bytes_out;
};

/*
 * Core entry points called from module-load hooks in each backend's
 * compilation unit. The core owns the softc; each backend fills in the
 * fields it needs and then hands the softc back to the core to finish
 * attach or start detach.
 */
int	portdrv_core_attach(struct portdrv_softc *sc);
void	portdrv_core_detach(struct portdrv_softc *sc);

#endif /* !_PORTDRV_H_ */
