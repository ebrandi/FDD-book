/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_hw_pci.c -- Chapter 18 addition to the Chapter 16
 * hardware layer. Provides myfirst_hw_attach_pci(), which binds an
 * existing struct resource * (allocated by myfirst_pci.c) to the
 * hardware layer's tag and handle fields.
 *
 * The rest of the Chapter 16 hardware layer (myfirst_hw.c) is
 * unchanged. The detach path (myfirst_hw_detach) handles both the
 * simulation-backed and the PCI-backed hardware wrappers because it
 * checks `hw->regs_buf != NULL` before attempting to free the buffer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"

MALLOC_DECLARE(M_MYFIRST);

int
myfirst_hw_attach_pci(struct myfirst_softc *sc, struct resource *bar,
    bus_size_t bar_size)
{
	struct myfirst_hw *hw;

	if (bar_size < MYFIRST_REG_SIZE) {
		device_printf(sc->dev,
		    "BAR is too small: %ju bytes, need at least %u\n",
		    (uintmax_t)bar_size, (unsigned)MYFIRST_REG_SIZE);
		return (ENXIO);
	}

	hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);
	hw->regs_buf = NULL;
	hw->regs_size = (size_t)bar_size;
	hw->regs_tag = rman_get_bustag(bar);
	hw->regs_handle = rman_get_bushandle(bar);
	hw->access_log_enabled = true;
	hw->access_log_head = 0;

	sc->hw = hw;

	device_printf(sc->dev,
	    "hardware layer attached to BAR: %zu bytes "
	    "(tag=%p handle=%p)\n",
	    hw->regs_size, (void *)hw->regs_tag,
	    (void *)hw->regs_handle);
	return (0);
}
