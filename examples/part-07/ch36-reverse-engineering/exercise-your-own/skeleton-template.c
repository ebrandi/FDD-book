/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * skeleton-template.c
 *
 * Companion to Chapter 36 of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery", "Hands-On Exercise: Your Own
 * Observation".
 *
 * This file is a minimal Newbus-style USB driver skeleton. It is
 * not a complete driver. It shows the scaffolding every USB
 * driver needs, with TODO: markers at the points the exercise
 * asks you to fill in using your own observations:
 *
 *   - the vendor and product IDs from Step 1
 *   - the endpoint types, directions, and transfer setup that
 *     match what you catalogued in Step 3
 *   - the attach and detach bodies that allocate and release the
 *     endpoints and transfers
 *
 * The goal at this stage is a module that compiles, loads,
 * attaches to your device, prints a short message on attach, and
 * prints another on detach. Do not attempt to implement data
 * handling until the attach and detach paths are clean.
 *
 * See /usr/src/sys/dev/usb/serial/umcs.c and
 * /usr/src/sys/dev/usb/serial/uftdi.c for complete, in-tree
 * examples of the same pattern at full scale.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

/*
 * Device identification table.
 *
 * TODO: replace the placeholder vendor and product IDs with the
 * idVendor and idProduct values you observed in
 * device-descriptors.txt.
 */
static const STRUCT_USB_HOST_ID myusb_devs[] = {
	{ USB_VP(0x0000 /* TODO: VID */, 0x0000 /* TODO: PID */) },
};

static device_probe_t  myusb_probe;
static device_attach_t myusb_attach;
static device_detach_t myusb_detach;

/*
 * Probe: called by the USB subsystem for each candidate device.
 * Returns 0 on match, ENXIO on mismatch.
 */
static int
myusb_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(myusb_devs,
	    sizeof(myusb_devs), uaa));
}

/*
 * Attach: called after a successful probe.
 *
 * TODO: build a struct usb_config array that describes the
 * endpoints you catalogued in Step 3, using fields of the form:
 *
 *   {
 *       .type      = UE_BULK,        (or UE_INTERRUPT, UE_CONTROL)
 *       .endpoint  = UE_ADDR_ANY,    (or a specific address)
 *       .direction = UE_DIR_IN,      (or UE_DIR_OUT)
 *       .bufsize   = <observed max transfer size>,
 *       .callback  = &myusb_<name>_callback,
 *   }
 *
 * Then call usbd_transfer_setup() to allocate the transfers.
 * Keep the skeleton empty until those pieces are in place; the
 * attach path should still print a message and return 0 so you
 * can confirm Newbus binding independently of transfer setup.
 */
static int
myusb_attach(device_t dev)
{
	/* TODO: allocate endpoints and set up transfers here. */
	device_printf(dev, "attached\n");
	return (0);
}

/*
 * Detach: called when the module is unloaded or the device is
 * removed.
 *
 * TODO: unwind whatever attach allocated, in reverse order.
 * Typical sequence:
 *
 *   - usbd_transfer_unsetup() on each transfer group
 *   - release any mutexes and memory the driver allocated
 */
static int
myusb_detach(device_t dev)
{
	/* TODO: release endpoints and transfers here. */
	device_printf(dev, "detached\n");
	return (0);
}

static device_method_t myusb_methods[] = {
	DEVMETHOD(device_probe,  myusb_probe),
	DEVMETHOD(device_attach, myusb_attach),
	DEVMETHOD(device_detach, myusb_detach),
	DEVMETHOD_END
};

static driver_t myusb_driver = {
	.name    = "myusb",
	.methods = myusb_methods,
	.size    = 0, /* TODO: replace with sizeof(struct myusb_softc). */
};

DRIVER_MODULE(myusb, uhub, myusb_driver, NULL, NULL);
MODULE_DEPEND(myusb, usb, 1, 1, 1);
MODULE_VERSION(myusb, 1);
