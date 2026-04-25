/*-
 * Chapter 21 Stage 1 snippet: DMA allocation and teardown.
 *
 * This file contains only the Chapter 21 Stage 1 additions to
 * myfirst.c. In the actual Stage 1 driver, these functions live
 * inside myfirst.c alongside the Chapter 20 Stage 4 code.
 *
 * Stage 4 (stage4-final/) splits the DMA code into its own
 * myfirst_dma.c file. Stage 1 keeps it in myfirst.c for visibility.
 *
 * The code shown here is didactic: it shows what lines to insert
 * into myfirst.c. It will not compile by itself; it depends on the
 * softc definition and the surrounding attach/detach code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include "myfirst.h"

/* The single-segment callback for bus_dmamap_load. */
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

/*
 * myfirst_dma_setup: create a DMA tag, allocate a buffer, load a map,
 * record the bus address.
 *
 * Called from the attach path after the BAR is mapped but before the
 * interrupt setup runs. On failure, returns non-zero with the softc's
 * DMA fields left NULL/zero.
 */
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

	/*
	 * Register the buffer with the simulated backend so it can
	 * resolve the bus address back to a KVA for the simulated
	 * transfer. See myfirst_sim_dma_stage2.c for the sim-side helper.
	 * A real-hardware-only build would not make this call.
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

/*
 * myfirst_dma_teardown: reverse of myfirst_dma_setup.
 *
 * Safe to call with any subset of fields set (partial-setup unwind).
 * Called from the detach path after the interrupt teardown runs.
 */
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
}
