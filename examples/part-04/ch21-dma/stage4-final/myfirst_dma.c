/*-
 * Chapter 21 Stage 4: myfirst_dma.c
 *
 * Consolidated DMA subsystem for the myfirst driver. Moves the DMA
 * code from myfirst.c (Stage 1-3) into its own compilation unit with
 * a documented public API.
 *
 * This file implements the API declared in myfirst_dma.h:
 * - myfirst_dma_setup / myfirst_dma_teardown: lifecycle.
 * - myfirst_dma_do_transfer (polling) / do_transfer_intr (interrupt).
 * - myfirst_dma_handle_complete: called from the rx task.
 * - myfirst_dma_add_sysctls: registers the test and counter sysctls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/proc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"

static void	myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs,
		    int nseg, int error);
static int	myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS);
static int	myfirst_dma_sysctl_test_read(SYSCTL_HANDLER_ARGS);

static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg,
    int error)
{
	bus_addr_t *addr = arg;

	if (error != 0) {
		printf("myfirst: dma load callback error %d\n", error);
		*addr = 0;
		return;
	}

	KASSERT(nseg == 1,
	    ("myfirst: unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}

int
myfirst_dma_setup(struct myfirst_softc *sc)
{
	int err;

	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
	    /* alignment */    4,
	    /* boundary */     0,
	    /* lowaddr */      BUS_SPACE_MAXADDR,
	    /* highaddr */     BUS_SPACE_MAXADDR,
	    /* filtfunc */     NULL,
	    /* filtfuncarg */  NULL,
	    /* maxsize */      MYFIRST_DMA_BUFFER_SIZE,
	    /* nsegments */    1,
	    /* maxsegsz */     MYFIRST_DMA_BUFFER_SIZE,
	    /* flags */        0,
	    /* lockfunc */     NULL,
	    /* lockfuncarg */  NULL,
	    &sc->dma_tag);
	if (err != 0) {
		device_printf(sc->dev,
		    "bus_dma_tag_create failed: %d\n", err);
		return (err);
	}

	err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->dma_map);
	if (err != 0) {
		device_printf(sc->dev,
		    "bus_dmamem_alloc failed: %d\n", err);
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err);
	}

	sc->dma_bus_addr = 0;
	err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
	    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
	    myfirst_dma_single_map, &sc->dma_bus_addr,
	    BUS_DMA_NOWAIT);
	if (err != 0 || sc->dma_bus_addr == 0) {
		device_printf(sc->dev,
		    "bus_dmamap_load failed: %d\n", err);
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err != 0 ? err : ENOMEM);
	}

	cv_init(&sc->dma_cv, "myfirst_dma");

	/*
	 * Register the buffer with the simulation backend so it can
	 * resolve the bus address back to a KVA when the simulated
	 * engine runs. This call exists only for the simulated backend;
	 * a real-hardware build would omit it.
	 */
	if (sc->sim != NULL)
		myfirst_sim_register_dma_buffer(sc->sim, sc->dma_vaddr,
		    sc->dma_bus_addr, MYFIRST_DMA_BUFFER_SIZE);

	device_printf(sc->dev,
	    "DMA buffer %zu bytes at KVA %p bus addr %#jx\n",
	    (size_t)MYFIRST_DMA_BUFFER_SIZE,
	    sc->dma_vaddr, (uintmax_t)sc->dma_bus_addr);

	return (0);
}

void
myfirst_dma_teardown(struct myfirst_softc *sc)
{
	if (sc->sim != NULL)
		myfirst_sim_unregister_dma_buffer(sc->sim);
	if (sc->dma_bus_addr != 0) {
		bus_dmamap_unload(sc->dma_tag, sc->dma_map);
		sc->dma_bus_addr = 0;
	}
	if (sc->dma_vaddr != NULL) {
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		sc->dma_map = NULL;
	}
	if (sc->dma_tag != NULL) {
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
	}
	cv_destroy(&sc->dma_cv);
}

int
myfirst_dma_do_transfer(struct myfirst_softc *sc, int direction,
    size_t length)
{
	uint32_t status;
	int timeout;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);

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

	err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
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

void
myfirst_dma_handle_complete(struct myfirst_softc *sc)
{
	MYFIRST_ASSERT_LOCKED(sc);

	if (!sc->dma_in_flight)
		return;

	sc->dma_in_flight = false;
	if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	sc->dma_last_status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
	atomic_add_64(&sc->dma_complete_tasks, 1);
	cv_broadcast(&sc->dma_cv);
}

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
	err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
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

	err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_READ,
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

void
myfirst_dma_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_write",
	    CTLFLAG_RD, &sc->dma_transfers_write, 0,
	    "Successful host-to-device DMA transfers");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_read",
	    CTLFLAG_RD, &sc->dma_transfers_read, 0,
	    "Successful device-to-host DMA transfers");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_errors",
	    CTLFLAG_RD, &sc->dma_errors, 0,
	    "DMA transfers that returned EIO");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_timeouts",
	    CTLFLAG_RD, &sc->dma_timeouts, 0,
	    "DMA transfers that timed out");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_intrs",
	    CTLFLAG_RD, &sc->dma_complete_intrs, 0,
	    "DMA completion interrupts observed");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_tasks",
	    CTLFLAG_RD, &sc->dma_complete_tasks, 0,
	    "DMA completion task invocations");
	SYSCTL_ADD_UQUAD(ctx, kids, OID_AUTO, "dma_bus_addr",
	    CTLFLAG_RD, &sc->dma_bus_addr,
	    "Bus address of the DMA buffer");

	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
	    myfirst_dma_sysctl_test_write, "IU",
	    "Trigger a host-to-device DMA transfer");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
	    myfirst_dma_sysctl_test_read, "IU",
	    "Trigger a device-to-host DMA transfer");
}
