/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.c -- Chapter 18 final PCI attach layer.
 *
 * Registers the myfirst driver against the pci(4) bus. Probe matches
 * a static table of vendor/device ID pairs. Attach claims BAR 0,
 * walks the PCI capability list, hands the BAR to the Chapter 16
 * hardware layer, creates the per-instance cdev, and performs a
 * diagnostic register read. Detach undoes every step in reverse.
 *
 * See PCI.md for the user-facing documentation.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_sim.h"
#include "myfirst_pci.h"

/* ------------------------------------------------------------------ */
/* ID table                                                           */
/* ------------------------------------------------------------------ */

static const struct myfirst_pci_id myfirst_pci_ids[] = {
	{ MYFIRST_VENDOR_REDHAT, MYFIRST_DEVICE_VIRTIO_RNG,
	    "Red Hat Virtio entropy source (myfirst demo target)" },
	{ 0, 0, NULL }
};

/* ------------------------------------------------------------------ */
/* Probe                                                              */
/* ------------------------------------------------------------------ */

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

/* ------------------------------------------------------------------ */
/* Capability walk (informational)                                    */
/* ------------------------------------------------------------------ */

static void
myfirst_pci_walk_caps(device_t dev)
{
	int capreg;

	if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0)
		device_printf(dev, "PCIe capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0)
		device_printf(dev, "MSI capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0)
		device_printf(dev, "MSI-X capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_PMG, &capreg) == 0)
		device_printf(dev, "Power Management at 0x%x\n", capreg);
	if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0)
		device_printf(dev, "PCIe AER extended cap at 0x%x\n", capreg);
}

/* ------------------------------------------------------------------ */
/* Attach                                                             */
/* ------------------------------------------------------------------ */

static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);

	device_printf(dev,
	    "attaching: vendor=0x%04x device=0x%04x revid=0x%02x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev));

	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	myfirst_pci_walk_caps(dev);

	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		device_printf(dev, "cannot create cdev\n");
		error = ENXIO;
		goto fail_hw;
	}
	sc->cdev->si_drv1 = sc;

	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_hw:
	myfirst_hw_detach(sc);
fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}

/* ------------------------------------------------------------------ */
/* Detach                                                             */
/* ------------------------------------------------------------------ */

static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	myfirst_quiesce(sc);

	/*
	 * Release the simulation backend if it was attached. The PCI
	 * attach path leaves sc->sim == NULL, so this is a no-op on
	 * the default build. A simulation-only build has sc->sim set
	 * and needs the full teardown.
	 */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	myfirst_hw_detach(sc);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}

/* ------------------------------------------------------------------ */
/* Newbus registration                                                */
/* ------------------------------------------------------------------ */

static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};

static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};

#ifndef MYFIRST_SIMULATION_ONLY
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
#endif
MODULE_VERSION(myfirst, 1);
