/*-
 * Chapter 22 Stage 3: restore helper and rewritten resume/shutdown
 * for the myfirst driver.
 *
 * Replace the Stage 1 resume/shutdown skeletons with these. The
 * quiesce helpers (mask_interrupts, drain_dma, drain_workers,
 * myfirst_quiesce) from Stage 2 are still in place.
 */

static int
myfirst_restore(struct myfirst_softc *sc)
{
	/* Step 1: defensively re-enable bus-master. */
	pci_enable_busmaster(sc->dev);

	/* Step 2: restore the interrupt mask. */
	if (sc->saved_intr_mask == 0xFFFFFFFF) {
		/*
		 * saved_intr_mask is 0xFFFFFFFF only if the suspend
		 * happened before the driver had configured its mask.
		 * Fall back to a sensible default: enable DMA
		 * completion only, mask everything else.
		 */
		sc->saved_intr_mask = ~MYFIRST_INTR_COMPLETE;
	}
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);

	/* Step 3: clear the suspended flag. */
	MYFIRST_LOCK(sc);
	sc->suspended = false;
	MYFIRST_UNLOCK(sc);

	return (0);
}

static int
myfirst_pci_resume(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int err;

	device_printf(dev, "resume: starting\n");

	err = myfirst_restore(sc);
	if (err != 0) {
		device_printf(dev,
		    "resume: restore failed (err %d)\n", err);
		atomic_add_64(&sc->power_resume_errors, 1);
	}

	atomic_add_64(&sc->power_resume_count, 1);
	device_printf(dev, "resume: complete\n");
	return (0);
}

static int
myfirst_pci_shutdown(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	device_printf(dev, "shutdown: starting\n");
	(void)myfirst_quiesce(sc);
	atomic_add_64(&sc->power_shutdown_count, 1);
	device_printf(dev, "shutdown: complete\n");
	return (0);
}
