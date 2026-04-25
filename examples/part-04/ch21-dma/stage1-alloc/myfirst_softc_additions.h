/*-
 * Chapter 21 Stage 1: additions to myfirst.h.
 *
 * Append these declarations to myfirst.h after the Chapter 20 softc
 * fields. They add the DMA-related state the Stage 1 setup uses.
 */

/* Add to struct myfirst_softc: */
/*
	bus_dma_tag_t   dma_tag;
	bus_dmamap_t    dma_map;
	void           *dma_vaddr;
	bus_addr_t      dma_bus_addr;
 */

/* DMA buffer size. A single 4 KB buffer, matching a page and avoiding
 * bounce-buffer corner cases. Increase if the simulation is extended
 * to larger transfers. */
#define	MYFIRST_DMA_BUFFER_SIZE		4096u

/* Public Stage 1 DMA API (called from attach and detach). */
int	myfirst_dma_setup(struct myfirst_softc *sc);
void	myfirst_dma_teardown(struct myfirst_softc *sc);
