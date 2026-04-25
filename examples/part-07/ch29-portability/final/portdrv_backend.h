/*
 * portdrv_backend.h - backend interface for portdrv.
 *
 * The core calls through this interface. Each backend provides an
 * instance. No file in the driver should describe how a specific bus
 * works; that is the backend's concern. This header is the contract
 * the core holds every backend to.
 */

#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

#include "portdrv.h"

struct portdrv_softc;

struct portdrv_backend {
	uint32_t	version;		/* must match PORTDRV_BACKEND_VERSION. */
	const char     *name;			/* "pci", "sim", "usb", ... */

	int	(*attach)(struct portdrv_softc *sc);
	void	(*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void	(*write_reg)(struct portdrv_softc *sc, bus_size_t off,
		    uint32_t val);
};

#ifdef PORTDRV_WITH_PCI
extern const struct portdrv_backend portdrv_pci_backend;
#endif

#ifdef PORTDRV_WITH_SIM
extern const struct portdrv_backend portdrv_sim_backend;
#endif

#endif /* !_PORTDRV_BACKEND_H_ */
