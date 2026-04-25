/*-
 * Chapter 21 Stage 3: interrupt filter and task additions.
 *
 * Extends the Chapter 20 rx filter and task to handle
 * MYFIRST_INTR_COMPLETE as well as MYFIRST_INTR_DATA_AV.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_msix.h"

/*
 * Replaces the Chapter 20 myfirst_msix_rx_filter. The new version
 * checks both MYFIRST_INTR_DATA_AV (Chapter 19/20 path) and
 * MYFIRST_INTR_COMPLETE (Chapter 21 completion path).
 */
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;
	bool handled = false;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);

	if ((status & MYFIRST_INTR_DATA_AV) != 0) {
		atomic_add_64(&vec->fire_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		atomic_add_64(&sc->intr_data_av_count, 1);
		handled = true;
	}

	if ((status & MYFIRST_INTR_COMPLETE) != 0) {
		atomic_add_64(&sc->dma_complete_intrs, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		handled = true;
	}

	if (!handled) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}

/*
 * Replaces the Chapter 20 myfirst_msix_rx_task_fn. The new version
 * performs the Chapter 19/20 data-available work and then calls
 * myfirst_dma_handle_complete (Stage 4 centralises this helper).
 */
void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}

	/* Chapter 19/20 data-available work. */
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);

	/* Chapter 21 DMA completion work (inlined in Stage 3;
	 * extracted to myfirst_dma_handle_complete in Stage 4). */
	if (sc->dma_in_flight) {
		sc->dma_in_flight = false;
		if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_last_status = CSR_READ_4(sc,
		    MYFIRST_REG_DMA_STATUS);
		atomic_add_64(&sc->dma_complete_tasks, 1);
		cv_broadcast(&sc->dma_cv);
	}

	MYFIRST_UNLOCK(sc);
}

/*
 * Don't forget to enable MYFIRST_INTR_COMPLETE in the INTR_MASK
 * write at the end of myfirst_msix_setup:
 *
 *	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
 *	    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
 *	    MYFIRST_INTR_COMPLETE);
 */
