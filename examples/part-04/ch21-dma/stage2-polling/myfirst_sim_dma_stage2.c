/*-
 * Chapter 21 Stage 2: simulated DMA engine.
 *
 * This snippet shows the Stage 2 additions to myfirst_sim.c. It
 * extends the Chapter 17 simulation backend with a DMA engine whose
 * completion is driven by a callout(9).
 *
 * A simulation running inside the kernel cannot physically reach host
 * memory through the memory controller. Instead, the driver registers
 * a (KVA, bus address, size) triple with the sim at myfirst_dma_setup
 * time. When the callout fires, it looks up the KVA by bus address
 * and uses it directly for the memcpy.
 *
 * This file is for didactic reading, not for direct compilation.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_sim.h"

/* Add to struct myfirst_sim (defined in myfirst_sim.h): */
/*
	struct myfirst_sim_dma {
		uint32_t      addr_low;
		uint32_t      addr_high;
		uint32_t      len;
		uint32_t      dir;
		uint32_t      ctrl;
		uint32_t      status;
		struct callout done_co;
		bool          armed;
	} dma;

	/* Back-channel: the driver registers its DMA buffer here so the
	 * sim can reach the CPU-visible side during the simulated
	 * transfer. Real hardware never uses this path. */
	void       *dma_host_kva;
	bus_addr_t  dma_host_bus_addr;
	size_t      dma_host_size;

	void  *dma_scratch;
	size_t dma_scratch_size;
 */

static void	myfirst_sim_dma_done_co(void *arg);
static void	myfirst_sim_dma_ctrl_written(struct myfirst_sim *sim,
		    uint32_t val);
static void	myfirst_sim_dma_raise_complete(struct myfirst_sim *sim);

/* Called from myfirst_sim_init (Chapter 17). */
void
myfirst_sim_dma_init(struct myfirst_sim *sim)
{
	sim->dma_scratch_size = MYFIRST_DMA_BUFFER_SIZE;
	sim->dma_scratch = malloc(sim->dma_scratch_size, M_MYFIRST,
	    M_WAITOK | M_ZERO);
	memset(sim->dma_scratch, 0x5A, sim->dma_scratch_size);
	callout_init(&sim->dma.done_co, 1);
	sim->dma.armed = false;
	sim->dma.status = 0;
	sim->dma_host_kva = NULL;
	sim->dma_host_bus_addr = 0;
	sim->dma_host_size = 0;
}

/* Called from myfirst_sim_destroy (Chapter 17). */
void
myfirst_sim_dma_destroy(struct myfirst_sim *sim)
{
	callout_drain(&sim->dma.done_co);
	if (sim->dma_scratch != NULL) {
		free(sim->dma_scratch, M_MYFIRST);
		sim->dma_scratch = NULL;
	}
	sim->dma_host_kva = NULL;
	sim->dma_host_bus_addr = 0;
	sim->dma_host_size = 0;
}

/*
 * Back-channel: register the driver's DMA buffer with the sim.
 * Called from myfirst_dma_setup after bus_dmamap_load has filled in
 * the bus address. In a real driver this call does not exist; it is
 * only needed because the sim is software and cannot physically reach
 * the host through the memory controller.
 */
void
myfirst_sim_register_dma_buffer(struct myfirst_sim *sim, void *kva,
    bus_addr_t bus_addr, size_t size)
{
	sim->dma_host_kva = kva;
	sim->dma_host_bus_addr = bus_addr;
	sim->dma_host_size = size;
}

/* Symmetrical teardown, called from myfirst_dma_teardown. */
void
myfirst_sim_unregister_dma_buffer(struct myfirst_sim *sim)
{
	sim->dma_host_kva = NULL;
	sim->dma_host_bus_addr = 0;
	sim->dma_host_size = 0;
}

/*
 * Called from myfirst_sim_write_4 (Chapter 17) when the write targets
 * one of the DMA registers. Extend the switch statement.
 */
void
myfirst_sim_dma_write(struct myfirst_sim *sim, bus_size_t off,
    uint32_t val)
{
	switch (off) {
	case MYFIRST_REG_DMA_ADDR_LOW:
		sim->dma.addr_low = val;
		break;
	case MYFIRST_REG_DMA_ADDR_HIGH:
		sim->dma.addr_high = val;
		break;
	case MYFIRST_REG_DMA_LEN:
		sim->dma.len = val;
		break;
	case MYFIRST_REG_DMA_DIR:
		sim->dma.dir = val;
		break;
	case MYFIRST_REG_DMA_CTRL:
		sim->dma.ctrl = val;
		myfirst_sim_dma_ctrl_written(sim, val);
		break;
	default:
		break;
	}
}

/*
 * Called from myfirst_sim_read_4 (Chapter 17) when the read targets
 * MYFIRST_REG_DMA_STATUS.
 */
uint32_t
myfirst_sim_dma_read(struct myfirst_sim *sim, bus_size_t off)
{
	if (off == MYFIRST_REG_DMA_STATUS)
		return (sim->dma.status);
	return (0);
}

static void
myfirst_sim_dma_ctrl_written(struct myfirst_sim *sim, uint32_t val)
{
	if ((val & MYFIRST_DMA_CTRL_RESET) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status = 0;
		sim->dma.addr_low = 0;
		sim->dma.addr_high = 0;
		sim->dma.len = 0;
		sim->dma.dir = 0;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_ABORT) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status &= ~MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_START) != 0) {
		if ((sim->dma.status & MYFIRST_DMA_STATUS_RUNNING) != 0) {
			sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
			return;
		}
		sim->dma.status = MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.armed = true;
		callout_reset(&sim->dma.done_co, hz / 100,
		    myfirst_sim_dma_done_co, sim);
		return;
	}
}

static void
myfirst_sim_dma_done_co(void *arg)
{
	struct myfirst_sim *sim = arg;
	bus_addr_t bus_addr;
	uint32_t len;
	void *kva;

	bus_addr = ((bus_addr_t)sim->dma.addr_high << 32) |
	    sim->dma.addr_low;
	len = sim->dma.len;

	/*
	 * Back-channel lookup: find the KVA for this bus address.
	 * A real device would not need this; the device's own DMA
	 * engine would perform the memory-controller access.
	 */
	if (sim->dma_host_kva == NULL ||
	    bus_addr != sim->dma_host_bus_addr ||
	    len == 0 || len > sim->dma_host_size ||
	    len > sim->dma_scratch_size) {
		sim->dma.status = MYFIRST_DMA_STATUS_ERR;
		sim->dma.armed = false;
		myfirst_sim_dma_raise_complete(sim);
		return;
	}
	kva = sim->dma_host_kva;

	if (sim->dma.dir == MYFIRST_DMA_DIR_WRITE) {
		/* Host-to-device: sim reads from host KVA, writes scratch. */
		memcpy(sim->dma_scratch, kva, len);
	} else {
		/* Device-to-host: sim reads from scratch, writes to host. */
		memcpy(kva, sim->dma_scratch, len);
	}

	sim->dma.status = MYFIRST_DMA_STATUS_DONE;
	sim->dma.armed = false;
	myfirst_sim_dma_raise_complete(sim);
}

static void
myfirst_sim_dma_raise_complete(struct myfirst_sim *sim)
{
	sim->intr_status |= MYFIRST_INTR_COMPLETE;
	if ((sim->intr_mask & MYFIRST_INTR_COMPLETE) != 0)
		myfirst_sim_raise_intr(sim);
}
