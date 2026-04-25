/*-
 * Chapter 21 Stage 2: header additions.
 *
 * Append to myfirst.h after the Chapter 20 register map.
 * These values are read by both the driver (programming the engine)
 * and the simulation backend (implementing the engine).
 */

/* DMA register offsets (on the MMIO BAR). */
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

/* DMA_DIR values. */
#define	MYFIRST_DMA_DIR_WRITE		0u	/* host-to-device */
#define	MYFIRST_DMA_DIR_READ		1u	/* device-to-host */

/* DMA_CTRL bits. */
#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

/* DMA_STATUS bits. */
#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)

/* Stage 2 public API. */
int	myfirst_dma_do_transfer(struct myfirst_softc *sc,
	    int direction, size_t length);

/* Add the following fields to struct myfirst_softc: */
/*
	uint64_t  dma_transfers_write;
	uint64_t  dma_transfers_read;
	uint64_t  dma_errors;
	uint64_t  dma_timeouts;
 */
