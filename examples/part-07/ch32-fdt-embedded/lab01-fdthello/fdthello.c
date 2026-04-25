/*
 * fdthello.c - Minimal FDT-aware driver skeleton
 *
 * Companion source file for Chapter 32, Section 4 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
 *
 * This driver matches a Device Tree node with
 *   compatible = "freebsd,fdthello";
 * and logs its attach and detach.  It does nothing useful on its own;
 * it is a starting skeleton for any further FDT driver you write.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

struct fdthello_softc {
	device_t sc_dev;
};

static const struct ofw_compat_data compat_data[] = {
	{"freebsd,fdthello", 1},
	{NULL,               0}
};

static int
fdthello_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "FDT Hello Example");
	return (BUS_PROBE_DEFAULT);
}

static int
fdthello_attach(device_t dev)
{
	struct fdthello_softc *sc = device_get_softc(dev);
	phandle_t node = ofw_bus_get_node(dev);

	sc->sc_dev = dev;
	device_printf(dev, "attached, node phandle 0x%lx\n",
	    (unsigned long)node);
	return (0);
}

static int
fdthello_detach(device_t dev)
{
	device_printf(dev, "detached\n");
	return (0);
}

static device_method_t fdthello_methods[] = {
	DEVMETHOD(device_probe,  fdthello_probe),
	DEVMETHOD(device_attach, fdthello_attach),
	DEVMETHOD(device_detach, fdthello_detach),
	DEVMETHOD_END
};

static driver_t fdthello_driver = {
	"fdthello",
	fdthello_methods,
	sizeof(struct fdthello_softc)
};

DRIVER_MODULE(fdthello, simplebus, fdthello_driver, 0, 0);
DRIVER_MODULE(fdthello, ofwbus,    fdthello_driver, 0, 0);
MODULE_VERSION(fdthello, 1);
