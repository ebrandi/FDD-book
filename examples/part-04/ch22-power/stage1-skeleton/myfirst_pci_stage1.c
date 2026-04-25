/*-
 * Chapter 22 Stage 1: skeleton power-management methods for the
 * myfirst driver.
 *
 * These three functions should be added near the end of myfirst_pci.c,
 * and the matching DEVMETHOD lines should be added to the
 * myfirst_pci_methods[] array:
 *
 *    DEVMETHOD(device_suspend,  myfirst_pci_suspend),
 *    DEVMETHOD(device_resume,   myfirst_pci_resume),
 *    DEVMETHOD(device_shutdown, myfirst_pci_shutdown),
 *
 * Stage 1 intentionally does no real work; it only logs and increments
 * counters. Stage 2 turns the suspend path into a real quiesce;
 * Stage 3 turns the resume path into a real restore; Stage 4 refactors
 * everything into myfirst_power.c.
 */

static int
myfirst_pci_suspend(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	device_printf(dev, "suspend (stage 1 skeleton)\n");
	atomic_add_64(&sc->power_suspend_count, 1);
	return (0);
}

static int
myfirst_pci_resume(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	device_printf(dev, "resume (stage 1 skeleton)\n");
	atomic_add_64(&sc->power_resume_count, 1);
	return (0);
}

static int
myfirst_pci_shutdown(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	device_printf(dev, "shutdown (stage 1 skeleton)\n");
	atomic_add_64(&sc->power_shutdown_count, 1);
	return (0);
}

/*
 * Add these sysctl lines to the function that already creates the
 * myfirst sysctl tree (usually myfirst_add_sysctls in myfirst.c).
 */
#if 0
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_suspend_count",
    CTLFLAG_RD, &sc->power_suspend_count, 0,
    "Number of times DEVICE_SUSPEND has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_resume_count",
    CTLFLAG_RD, &sc->power_resume_count, 0,
    "Number of times DEVICE_RESUME has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_shutdown_count",
    CTLFLAG_RD, &sc->power_shutdown_count, 0,
    "Number of times DEVICE_SHUTDOWN has been called");
#endif
