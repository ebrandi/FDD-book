/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.c -- Chapter 18 Stage 2 PCI driver.
 *
 * Stage 2 extends Stage 1 by claiming BAR 0 as a SYS_RES_MEMORY
 * resource. The tag and handle are not yet wired to the Chapter 16
 * accessor layer (that is Stage 3). Detach releases the BAR.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_pci.h"

static const struct myfirst_pci_id myfirst_pci_ids[] = {
	{ MYFIRST_VENDOR_REDHAT, MYFIRST_DEVICE_VIRTIO_RNG,
	    "Red Hat Virtio entropy source (myfirst demo target)" },
	{ 0, 0, NULL }
};

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

static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	device_printf(dev,
	    "attaching: vendor=0x%04x device=0x%04x revid=0x%02x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev));

	/* Allocate BAR 0 as a memory resource. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}

	device_printf(dev, "BAR0 allocated: %#jx bytes at %#jx\n",
	    (uintmax_t)rman_get_size(sc->bar_res),
	    (uintmax_t)rman_get_start(sc->bar_res));

	sc->pci_attached = true;
	return (0);
}

static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	sc->pci_attached = false;
	device_printf(dev, "detaching\n");
	return (0);
}

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

DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
MODULE_VERSION(myfirst, 1);
