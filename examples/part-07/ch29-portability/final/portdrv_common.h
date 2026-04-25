/*
 * portdrv_common.h - types shared between the core and its backends.
 *
 * The core owns this header. Backends include it so they can access
 * the softc's public fields. Keeping this split from portdrv.h keeps
 * the backend files small: they only see what they need to see.
 */

#ifndef _PORTDRV_COMMON_H_
#define _PORTDRV_COMMON_H_

#include "portdrv.h"

struct portdrv_backend;

struct portdrv_softc {
	device_t	 sc_dev;		/* Newbus identity. */
	struct resource *sc_bar;		/* memory-mapped BAR. */
	int		 sc_bar_rid;		/* resource id. */
	const struct portdrv_backend *sc_be;	/* backend dispatch. */
	void		*sc_backend_priv;	/* backend-owned state. */
	struct cdev	*sc_cdev;		/* character device. */
	struct mtx	 sc_mtx;		/* protects softc state. */
	struct portdrv_stats sc_stats;		/* counters. */
	uint32_t	 sc_last_data;		/* last REG_DATA value. */
	int		 sc_unit;		/* unit number. */
};

#endif /* !_PORTDRV_COMMON_H_ */
