/*
 * portdrv_backend.h - backend interface for portdrv (Lab 3).
 *
 * The core dispatches every register access through this interface.
 * The Lab 3 driver still lives in a single portdrv.c, so the backend
 * instance is defined in that same file. Lab 4 will move each backend
 * into its own source file without changing this header.
 */

#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

struct portdrv_softc;

struct portdrv_backend {
	const char	*name;		/* "pci", "sim", ... */
	int		(*attach)(struct portdrv_softc *sc);
	void		(*detach)(struct portdrv_softc *sc);
	uint32_t	(*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void		(*write_reg)(struct portdrv_softc *sc, bus_size_t off,
			    uint32_t val);
};

extern const struct portdrv_backend portdrv_pci_backend;

#endif /* !_PORTDRV_BACKEND_H_ */
