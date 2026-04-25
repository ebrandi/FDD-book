/*
 * portdrv_pci.c - PCI backend for portdrv.
 *
 * Everything that knows about PCI lives here. Probe, attach, resource
 * allocation, and the actual bus_read_* and bus_write_* calls. The
 * core does not include any PCI header; this file is where the PCI
 * dependency is isolated.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

static const struct portdrv_pci_id {
	uint16_t	 vendor;
	uint16_t	 device;
	const char	*desc;
} portdrv_pci_ids[] = {
	{ 0x1af4, 0x1005, "portdrv virtio-rnd (demo target)" },
	{ 0, 0, NULL }
};

static uint32_t
portdrv_pci_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static void
portdrv_pci_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}

static int
portdrv_pci_backend_attach(struct portdrv_softc *sc)
{
	device_t dev = sc->sc_dev;

	sc->sc_bar_rid = PCIR_BAR(0);
	sc->sc_bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_bar_rid, RF_ACTIVE);
	if (sc->sc_bar == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}
	return (0);
}

static void
portdrv_pci_backend_detach(struct portdrv_softc *sc)
{
	if (sc->sc_bar != NULL) {
		bus_release_resource(sc->sc_dev, SYS_RES_MEMORY,
		    sc->sc_bar_rid, sc->sc_bar);
		sc->sc_bar = NULL;
	}
}

const struct portdrv_backend portdrv_pci_backend = {
	.version	= PORTDRV_BACKEND_VERSION,
	.name		= "pci",
	.attach		= portdrv_pci_backend_attach,
	.detach		= portdrv_pci_backend_detach,
	.read_reg	= portdrv_pci_read_reg,
	.write_reg	= portdrv_pci_write_reg,
};

static int
portdrv_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct portdrv_pci_id *id;

	for (id = portdrv_pci_ids; id->desc != NULL; id++) {
		if (vendor == id->vendor && device == id->device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
portdrv_pci_attach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);

	sc->sc_dev = dev;
	sc->sc_unit = device_get_unit(dev);
	sc->sc_be = &portdrv_pci_backend;

	return (portdrv_core_attach(sc));
}

static int
portdrv_pci_detach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);

	portdrv_core_detach(sc);
	return (0);
}

static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,		portdrv_pci_probe),
	DEVMETHOD(device_attach,	portdrv_pci_attach),
	DEVMETHOD(device_detach,	portdrv_pci_detach),
	DEVMETHOD_END
};

static driver_t portdrv_pci_driver = {
	"portdrv",
	portdrv_pci_methods,
	sizeof(struct portdrv_softc)
};

DRIVER_MODULE(portdrv_pci, pci, portdrv_pci_driver, NULL, NULL);
MODULE_VERSION(portdrv_pci, PORTDRV_VERSION);
MODULE_DEPEND(portdrv_pci, pci, 1, 1, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, PORTDRV_VERSION, PORTDRV_VERSION,
    PORTDRV_VERSION);

MODULE_PNP_INFO("U16:vendor;U16:device", pci, portdrv,
    portdrv_pci_ids, nitems(portdrv_pci_ids) - 1);
