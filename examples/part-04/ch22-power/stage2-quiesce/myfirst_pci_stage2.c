/*-
 * Chapter 22 Stage 2: quiesce helpers and rewritten suspend for
 * the myfirst driver.
 *
 * These functions extend Stage 1. The mask/drain helpers are used
 * by the suspend path (this stage) and will also be used by the
 * detach path (a refactor step left to Stage 4, where the code moves
 * into myfirst_power.c).
 */

static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
	MYFIRST_ASSERT_UNLOCKED(sc);

	/* Save current mask for later restore. */
	sc->saved_intr_mask = CSR_READ_4(sc, MYFIRST_REG_INTR_MASK);

	/*
	 * Disable all interrupt sources at the device, and clear any
	 * pending status bits so we don't see a stale interrupt on resume.
	 */
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}

static int
myfirst_drain_dma(struct myfirst_softc *sc)
{
	int err = 0;

	MYFIRST_LOCK(sc);
	if (sc->dma_in_flight) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
		    MYFIRST_DMA_CTRL_ABORT);

		err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
		if (err == EWOULDBLOCK) {
			device_printf(sc->dev,
			    "drain_dma: timeout waiting for abort\n");
			sc->dma_in_flight = false;
		}
	}
	MYFIRST_UNLOCK(sc);

	return (err == EWOULDBLOCK ? ETIMEDOUT : 0);
}

static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
	/*
	 * Drain must happen with no driver lock held, because
	 * taskqueue_drain and callout_drain may wait on tasks that
	 * acquire the same lock.
	 */

	if (sc->sim != NULL)
		myfirst_sim_drain_dma_callout(sc->sim);

	if (sc->rx_vector.has_task)
		taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);
}

static int
myfirst_quiesce(struct myfirst_softc *sc)
{
	int err;

	MYFIRST_LOCK(sc);
	if (sc->suspended) {
		MYFIRST_UNLOCK(sc);
		return (0);
	}
	sc->suspended = true;
	MYFIRST_UNLOCK(sc);

	myfirst_mask_interrupts(sc);

	err = myfirst_drain_dma(sc);
	if (err != 0) {
		device_printf(sc->dev,
		    "quiesce: DMA did not stop cleanly (err %d)\n", err);
	}

	myfirst_drain_workers(sc);

	KASSERT(sc->dma_in_flight == false,
	    ("myfirst: dma_in_flight still true after drain"));

	return (err);
}

static int
myfirst_pci_suspend(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int err;

	device_printf(dev, "suspend: starting\n");

	err = myfirst_quiesce(sc);
	if (err != 0) {
		device_printf(dev,
		    "suspend: quiesce returned %d; continuing anyway\n",
		    err);
	}

	atomic_add_64(&sc->power_suspend_count, 1);
	device_printf(dev,
	    "suspend: complete (dma in flight=%d, suspended=%d)\n",
	    sc->dma_in_flight, sc->suspended);
	return (0);
}

/*
 * Suspended-flag sysctl. Add to the function that creates the
 * sysctl tree.
 */
#if 0
SYSCTL_ADD_BOOL(ctx, kids, OID_AUTO, "suspended",
    CTLFLAG_RD, &sc->suspended, 0,
    "Whether the driver is in the suspended state");
#endif
