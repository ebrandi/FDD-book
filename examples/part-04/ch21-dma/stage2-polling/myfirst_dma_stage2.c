/*-
 * Chapter 21 Stage 2: DMA transfer helper (polling-based).
 *
 * This snippet shows the Stage 2 addition to myfirst.c. It assumes
 * that the softc has dma_tag, dma_map, dma_vaddr, dma_bus_addr,
 * dma_transfers_write, dma_transfers_read, dma_errors, and
 * dma_timeouts fields, and that the register accessor macros
 * (CSR_READ_4, CSR_WRITE_4) are in scope via myfirst_hw.h.
 *
 * This file is for didactic reading, not for direct compilation.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"

int
myfirst_dma_do_transfer(struct myfirst_softc *sc, int direction,
    size_t length)
{
	uint32_t status;
	int timeout;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	if (direction == MYFIRST_DMA_DIR_WRITE) {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);
	}

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFFu));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, (uint32_t)direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	for (timeout = 500; timeout > 0; timeout--) {
		pause("dma", hz / 1000);
		status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
		if ((status & MYFIRST_DMA_STATUS_DONE) != 0)
			break;
		if ((status & MYFIRST_DMA_STATUS_ERR) != 0) {
			if (direction == MYFIRST_DMA_DIR_WRITE)
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTWRITE);
			else
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTREAD);
			atomic_add_64(&sc->dma_errors, 1);
			return (EIO);
		}
	}

	if (timeout == 0) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
		    MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		atomic_add_64(&sc->dma_timeouts, 1);
		return (ETIMEDOUT);
	}

	/* Acknowledge the completion bit in INTR_STATUS. */
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);

	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	if (direction == MYFIRST_DMA_DIR_WRITE)
		atomic_add_64(&sc->dma_transfers_write, 1);
	else
		atomic_add_64(&sc->dma_transfers_read, 1);

	return (0);
}

/*
 * Sysctl handlers. Registered from myfirst_dma_add_sysctls (Stage 4).
 * Shown here inline for readability.
 */

static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev,
		    "dma_test_write: err %d\n", err);
	else
		device_printf(sc->dev,
		    "dma_test_write: pattern 0x%02x transferred\n",
		    pattern & 0xFF);
	return (err);
}

static int
myfirst_dma_sysctl_test_read(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int ignore;
	int err;
	uint8_t *bytes;

	err = sysctl_handle_int(oidp, &ignore, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	err = myfirst_dma_do_transfer(sc, MYFIRST_DMA_DIR_READ,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0) {
		device_printf(sc->dev, "dma_test_read: err %d\n", err);
		return (err);
	}

	bytes = (uint8_t *)sc->dma_vaddr;
	device_printf(sc->dev,
	    "dma_test_read: first bytes %02x %02x %02x %02x "
	    "%02x %02x %02x %02x\n",
	    bytes[0], bytes[1], bytes[2], bytes[3],
	    bytes[4], bytes[5], bytes[6], bytes[7]);
	return (0);
}
