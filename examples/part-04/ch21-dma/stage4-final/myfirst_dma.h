/*-
 * Chapter 21 Stage 4: public DMA API for the myfirst driver.
 *
 * The DMA subsystem manages a single 4 KB host-visible buffer that
 * is also reachable by the simulated (or real) device via DMA. The
 * subsystem is set up at attach time, torn down at detach, and used
 * for both host-to-device and device-to-host transfers through the
 * public API below.
 */

#ifndef _MYFIRST_DMA_H_
#define _MYFIRST_DMA_H_

/*
 * DMA buffer size used by myfirst. Matches the Chapter 21 simulated
 * engine's scratch size. A real device would use a value from the
 * hardware's documented capabilities.
 */
#define	MYFIRST_DMA_BUFFER_SIZE		4096u

/* DMA register offsets (relative to the BAR base). */
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

/* Public API. */
struct myfirst_softc;

int	myfirst_dma_setup(struct myfirst_softc *sc);
void	myfirst_dma_teardown(struct myfirst_softc *sc);
int	myfirst_dma_do_transfer(struct myfirst_softc *sc,
	    int direction, size_t length);
int	myfirst_dma_do_transfer_intr(struct myfirst_softc *sc,
	    int direction, size_t length);
void	myfirst_dma_handle_complete(struct myfirst_softc *sc);
void	myfirst_dma_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_DMA_H_ */
