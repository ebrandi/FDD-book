/*
 * edled.c - Example Embedded LED Driver
 *
 * Companion source for Chapter 32, Section 7 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
 *
 * Demonstrates a minimal FDT-driven GPIO consumer on FreeBSD 14.3:
 *   - matches a DT node with compatible = "example,edled"
 *   - acquires a GPIO pin listed in the "leds-gpios" property
 *   - exposes dev.edled.<unit>.state as a read/write sysctl
 *   - releases the pin cleanly on detach
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>

struct edled_softc {
	device_t            sc_dev;
	gpio_pin_t          sc_pin;
	int                 sc_on;
	struct sysctl_oid  *sc_oid;
};

static const struct ofw_compat_data compat_data[] = {
	{"example,edled", 1},
	{NULL,            0}
};

static int edled_sysctl_state(SYSCTL_HANDLER_ARGS);

static int
edled_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);
	device_set_desc(dev, "Example embedded LED");
	return (BUS_PROBE_DEFAULT);
}

static int
edled_attach(device_t dev)
{
	struct edled_softc *sc = device_get_softc(dev);
	phandle_t node = ofw_bus_get_node(dev);
	int error;

	sc->sc_dev = dev;
	sc->sc_on = 0;

	error = gpio_pin_get_by_ofw_property(dev, node,
	    "leds-gpios", &sc->sc_pin);
	if (error != 0) {
		device_printf(dev, "cannot get GPIO pin: %d\n", error);
		goto fail;
	}

	error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
	if (error != 0) {
		device_printf(dev, "cannot set pin flags: %d\n", error);
		goto fail;
	}

	error = gpio_pin_set_active(sc->sc_pin, 0);
	if (error != 0) {
		device_printf(dev, "cannot set pin state: %d\n", error);
		goto fail;
	}

	sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "state",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
	    sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

	device_printf(dev, "attached, GPIO pin acquired, state=0\n");
	return (0);

fail:
	if (sc->sc_pin != NULL) {
		gpio_pin_release(sc->sc_pin);
		sc->sc_pin = NULL;
	}
	return (error);
}

static int
edled_detach(device_t dev)
{
	struct edled_softc *sc = device_get_softc(dev);

	if (sc->sc_pin != NULL) {
		(void)gpio_pin_set_active(sc->sc_pin, 0);
		gpio_pin_release(sc->sc_pin);
		sc->sc_pin = NULL;
	}
	device_printf(dev, "detached\n");
	return (0);
}

static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
	struct edled_softc *sc = arg1;
	int val = sc->sc_on;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (val != 0 && val != 1)
		return (EINVAL);

	error = gpio_pin_set_active(sc->sc_pin, val);
	if (error == 0)
		sc->sc_on = val;
	return (error);
}

static device_method_t edled_methods[] = {
	DEVMETHOD(device_probe,  edled_probe),
	DEVMETHOD(device_attach, edled_attach),
	DEVMETHOD(device_detach, edled_detach),
	DEVMETHOD_END
};

static driver_t edled_driver = {
	"edled",
	edled_methods,
	sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
