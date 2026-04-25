/*-
 * Chapter 21 Stage 3: interrupt-driven DMA transfer helper.
 *
 * Replaces the Stage 2 polling helper with a condition-variable-based
 * wait. The rx task (myfirst_intr_additions.c) issues the POST sync
 * and broadcasts dma_cv; the helper wakes, checks status, returns.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>

#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"

/* Add to struct myfirst_softc: */
/*
	bool      dma_in_flight;
	int       dma_last_direction;
	uint32_t  dma_last_status;
	struct cv dma_cv;
	uint64_t  dma_complete_intrs;
	uint64_t  dma_complete_tasks;
 */

int
myfirst_dma_do_transfer_intr(struct myfirst_softc *sc, int direction,
    size_t length)
{
	int err;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	MYFIRST_LOCK(sc);
	if (sc->dma_in_flight) {
		MYFIRST_UNLOCK(sc);
		return (EBUSY);
	}

	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);

	sc->dma_last_direction = direction;
	sc->dma_in_flight = true;

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFFu));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, (uint32_t)direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz); /* 1 s */
	if (err == EWOULDBLOCK) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
		    MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_in_flight = false;
		atomic_add_64(&sc->dma_timeouts, 1);
		MYFIRST_UNLOCK(sc);
		return (ETIMEDOUT);
	}

	if ((sc->dma_last_status & MYFIRST_DMA_STATUS_ERR) != 0) {
		atomic_add_64(&sc->dma_errors, 1);
		MYFIRST_UNLOCK(sc);
		return (EIO);
	}

	if (direction == MYFIRST_DMA_DIR_WRITE)
		atomic_add_64(&sc->dma_transfers_write, 1);
	else
		atomic_add_64(&sc->dma_transfers_read, 1);

	MYFIRST_UNLOCK(sc);
	return (0);
}
