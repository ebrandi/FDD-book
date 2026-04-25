/*
 * portdrv.c - Chapter 29 Lab 3 (Backend Interface Extracted).
 *
 * Same functionality as Lab 2, but the accessors now dispatch through
 * a backend structure declared in portdrv_backend.h. The driver still
 * lives in a single source file; Lab 4 will break it into a PCI
 * backend file and a core file. The point of this intermediate step
 * is to get the interface right while there is still only one place
 * that has to be kept consistent.
 *
 * After this lab, no function in the core path calls bus_read_* or
 * bus_write_* directly. Every register access goes through the
 * backend pointer on the softc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "portdrv_backend.h"

#define PORTDRV_VERSION		1

#define REG_ID			0x00
#define REG_STATUS		0x04
#define REG_DATA		0x08
#define REG_CONTROL		0x0c

#define PORTDRV_EXPECTED_ID	0x504f5254

MALLOC_DECLARE(M_PORTDRV);
MALLOC_DEFINE(M_PORTDRV, "portdrv", "portdrv driver buffers");

struct portdrv_softc {
	device_t	 sc_dev;
	struct resource *sc_bar;
	int		 sc_bar_rid;
	const struct portdrv_backend *sc_be;
	struct cdev	*sc_cdev;
	uint32_t	 sc_last_data;
};

/*
 * Accessors. These no longer call bus_read_4/bus_write_4 themselves.
 * They dispatch through the backend pointer, so the same code runs
 * unchanged when a different backend is installed.
 */
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (sc->sc_be->read_reg(sc, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	sc->sc_be->write_reg(sc, off, val);
}

/*
 * PCI backend implementation. This is where the real bus_read_* and
 * bus_write_* calls live in Lab 3. In Lab 4 these functions and the
 * backend instance move into a separate portdrv_pci.c file.
 */
static uint32_t
portdrv_pci_read_reg_impl(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static void
portdrv_pci_write_reg_impl(struct portdrv_softc *sc, bus_size_t off,
    uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}

const struct portdrv_backend portdrv_pci_backend = {
	.name		= "pci",
	.attach		= NULL,
	.detach		= NULL,
	.read_reg	= portdrv_pci_read_reg_impl,
	.write_reg	= portdrv_pci_write_reg_impl,
};

static const struct portdrv_ids {
	uint16_t vendor;
	uint16_t device;
	const char *desc;
} portdrv_ids[] = {
	{ 0x1af4, 0x1005, "portdrv virtio-rnd (demo target)" },
	{ 0, 0, NULL }
};

static d_open_t		portdrv_open;
static d_close_t	portdrv_close;
static d_read_t		portdrv_read;
static d_write_t	portdrv_write;

static struct cdevsw portdrv_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	portdrv_open,
	.d_close =	portdrv_close,
	.d_read =	portdrv_read,
	.d_write =	portdrv_write,
	.d_name =	"portdrv",
};

static int
portdrv_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct portdrv_ids *id;

	for (id = portdrv_ids; id->desc != NULL; id++) {
		if (vendor == id->vendor && device == id->device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
portdrv_attach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);

	sc->sc_dev = dev;
	sc->sc_be = &portdrv_pci_backend;

	sc->sc_bar_rid = PCIR_BAR(0);
	sc->sc_bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_bar_rid, RF_ACTIVE);
	if (sc->sc_bar == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}

	sc->sc_last_data = portdrv_read_reg(sc, REG_DATA);
	device_printf(dev, "portdrv version %d attached via %s backend\n",
	    PORTDRV_VERSION, sc->sc_be->name);

	sc->sc_cdev = make_dev(&portdrv_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "portdrv%d", device_get_unit(dev));
	if (sc->sc_cdev == NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_bar_rid,
		    sc->sc_bar);
		return (ENXIO);
	}
	sc->sc_cdev->si_drv1 = sc;

	return (0);
}

static int
portdrv_detach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);

	if (sc->sc_cdev != NULL)
		destroy_dev(sc->sc_cdev);
	if (sc->sc_bar != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_bar_rid,
		    sc->sc_bar);
	return (0);
}

static int
portdrv_open(struct cdev *cdev, int flags, int devtype, struct thread *td)
{
	return (0);
}

static int
portdrv_close(struct cdev *cdev, int flags, int devtype, struct thread *td)
{
	return (0);
}

static int
portdrv_read(struct cdev *cdev, struct uio *uio, int flag)
{
	struct portdrv_softc *sc = cdev->si_drv1;
	uint32_t val = portdrv_read_reg(sc, REG_DATA);

	return (uiomove(&val, MIN(uio->uio_resid, sizeof(val)), uio));
}

static int
portdrv_write(struct cdev *cdev, struct uio *uio, int flag)
{
	struct portdrv_softc *sc = cdev->si_drv1;
	uint32_t val = 0;
	int error;

	error = uiomove(&val, MIN(uio->uio_resid, sizeof(val)), uio);
	if (error != 0)
		return (error);
	portdrv_write_reg(sc, REG_DATA, val);
	sc->sc_last_data = val;
	return (0);
}

static device_method_t portdrv_methods[] = {
	DEVMETHOD(device_probe,		portdrv_probe),
	DEVMETHOD(device_attach,	portdrv_attach),
	DEVMETHOD(device_detach,	portdrv_detach),
	DEVMETHOD_END
};

static driver_t portdrv_driver = {
	"portdrv",
	portdrv_methods,
	sizeof(struct portdrv_softc)
};

DRIVER_MODULE(portdrv, pci, portdrv_driver, NULL, NULL);
MODULE_VERSION(portdrv, 3);
MODULE_DEPEND(portdrv, pci, 1, 1, 1);
