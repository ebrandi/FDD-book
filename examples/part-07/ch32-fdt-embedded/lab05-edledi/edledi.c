/*
 * edledi.c - edled with a GPIO interrupt input
 *
 * Companion source for Chapter 32, Lab 5 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
 *
 * Extends the Lab 4 edled driver with a second GPIO that is
 * configured as an input with a pull-up and as an interrupt source.
 * Pressing a button wired to that pin toggles the LED.
 *
 * DT binding:
 *   compatible = "example,edledi";
 *   leds-gpios = <&gpio LED_PIN 0>;
 *   button-gpios = <&gpio BTN_PIN 1>;
 *   interrupt-parent = <&gpio>;
 *   interrupts = <BTN_PIN 3>;   // either edge
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>

struct edledi_softc {
	device_t          sc_dev;
	gpio_pin_t        sc_led;
	gpio_pin_t        sc_button;
	struct resource  *sc_irq;
	void             *sc_irq_cookie;
	int               sc_irq_rid;
	struct mtx        sc_mtx;
	int               sc_on;
};

static const struct ofw_compat_data compat_data[] = {
	{"example,edledi", 1},
	{NULL,             0}
};

static void
edledi_intr(void *arg)
{
	struct edledi_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	sc->sc_on = !sc->sc_on;
	(void)gpio_pin_set_active(sc->sc_led, sc->sc_on);
	mtx_unlock(&sc->sc_mtx);
}

static int
edledi_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);
	device_set_desc(dev, "Example embedded LED with button");
	return (BUS_PROBE_DEFAULT);
}

static int
edledi_attach(device_t dev)
{
	struct edledi_softc *sc = device_get_softc(dev);
	phandle_t node = ofw_bus_get_node(dev);
	int error;

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), "edledi", MTX_DEF);

	error = gpio_pin_get_by_ofw_property(dev, node,
	    "leds-gpios", &sc->sc_led);
	if (error != 0) {
		device_printf(dev, "cannot get LED pin: %d\n", error);
		goto fail;
	}

	error = gpio_pin_setflags(sc->sc_led, GPIO_PIN_OUTPUT);
	if (error != 0) {
		device_printf(dev, "cannot set LED flags: %d\n", error);
		goto fail;
	}
	(void)gpio_pin_set_active(sc->sc_led, 0);

	error = gpio_pin_get_by_ofw_property(dev, node,
	    "button-gpios", &sc->sc_button);
	if (error != 0) {
		device_printf(dev, "cannot get button pin: %d\n", error);
		goto fail;
	}

	error = gpio_pin_setflags(sc->sc_button,
	    GPIO_PIN_INPUT | GPIO_PIN_PULLUP);
	if (error != 0) {
		device_printf(dev, "cannot configure button: %d\n", error);
		goto fail;
	}

	sc->sc_irq_rid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_irq_rid, RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "cannot allocate IRQ\n");
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, sc->sc_irq,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, edledi_intr, sc, &sc->sc_irq_cookie);
	if (error != 0) {
		device_printf(dev, "cannot setup interrupt: %d\n", error);
		goto fail;
	}

	device_printf(dev, "attached, press button to toggle LED\n");
	return (0);

fail:
	if (sc->sc_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
		    sc->sc_irq);
		sc->sc_irq = NULL;
	}
	if (sc->sc_button != NULL) {
		gpio_pin_release(sc->sc_button);
		sc->sc_button = NULL;
	}
	if (sc->sc_led != NULL) {
		gpio_pin_release(sc->sc_led);
		sc->sc_led = NULL;
	}
	mtx_destroy(&sc->sc_mtx);
	return (error);
}

static int
edledi_detach(device_t dev)
{
	struct edledi_softc *sc = device_get_softc(dev);

	if (sc->sc_irq_cookie != NULL) {
		bus_teardown_intr(dev, sc->sc_irq, sc->sc_irq_cookie);
		sc->sc_irq_cookie = NULL;
	}
	if (sc->sc_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
		    sc->sc_irq);
		sc->sc_irq = NULL;
	}
	if (sc->sc_button != NULL) {
		gpio_pin_release(sc->sc_button);
		sc->sc_button = NULL;
	}
	if (sc->sc_led != NULL) {
		(void)gpio_pin_set_active(sc->sc_led, 0);
		gpio_pin_release(sc->sc_led);
		sc->sc_led = NULL;
	}
	mtx_destroy(&sc->sc_mtx);
	device_printf(dev, "detached\n");
	return (0);
}

static device_method_t edledi_methods[] = {
	DEVMETHOD(device_probe,  edledi_probe),
	DEVMETHOD(device_attach, edledi_attach),
	DEVMETHOD(device_detach, edledi_detach),
	DEVMETHOD_END
};

static driver_t edledi_driver = {
	"edledi",
	edledi_methods,
	sizeof(struct edledi_softc)
};

DRIVER_MODULE(edledi, simplebus, edledi_driver, 0, 0);
DRIVER_MODULE(edledi, ofwbus,    edledi_driver, 0, 0);
MODULE_DEPEND(edledi, gpiobus, 1, 1, 1);
MODULE_VERSION(edledi, 1);
