/*
 * myfirst_usb.c - Chapter 26 reference USB driver.
 *
 * This driver is the companion to the USB walkthrough in Section 2
 * of Chapter 26. It declares bulk IN, bulk OUT, and interrupt IN
 * channels, and implements the three-state callback state machine
 * for each one. It is intentionally minimal: no /dev node, no
 * user-facing configuration. The goal is to give readers a working
 * skeleton they can extend.
 *
 * To bind this driver to a specific device, edit myfirst_usb_devs[]
 * below and replace the placeholder VID/PID with the values from
 * your test hardware.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/mutex.h>
#include <sys/malloc.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#include "myfirst_usb.h"

/* Debug printouts. Toggle via hw.usb.myfirst_usb.debug sysctl if you
 * extend the driver with a sysctl tree; for now this is a compile-time
 * flag. */
#ifndef MYFIRST_USB_DEBUG
#define MYFIRST_USB_DEBUG 0
#endif

#define DPRINTFN(n, ...) do {					\
	if (MYFIRST_USB_DEBUG >= (n))				\
		printf("myfirst_usb: " __VA_ARGS__);		\
} while (0)

/*
 * Match table.
 *
 * Replace the vendor and product values with those of the USB device
 * you intend to bind this driver to. As shipped, the entry uses a
 * nonexistent VID/PID so the driver does not accidentally bind to
 * a real device during testing.
 */
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
	{USB_VPI(0xffff, 0xffff, 0)},	/* placeholder */
};

/* Forward declarations. */
static device_probe_t	myfirst_usb_probe;
static device_attach_t	myfirst_usb_attach;
static device_detach_t	myfirst_usb_detach;

static usb_callback_t	myfirst_usb_bulk_read_callback;
static usb_callback_t	myfirst_usb_bulk_write_callback;
static usb_callback_t	myfirst_usb_intr_read_callback;

/*
 * Transfer configuration.
 *
 * The framework uses this array to allocate and manage the driver's
 * USB channels. Buffer sizes are sized for typical experiments.
 */
static const struct usb_config myfirst_usb_config[MYFIRST_USB_N_TRANSFER] = {
	[MYFIRST_USB_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = MYFIRST_USB_BULK_BUFSZ,
		.flags = {.pipe_bof = 1, .short_xfer_ok = 1},
		.callback = &myfirst_usb_bulk_read_callback,
	},
	[MYFIRST_USB_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = MYFIRST_USB_BULK_BUFSZ,
		.flags = {.pipe_bof = 1, .force_short_xfer = 1},
		.callback = &myfirst_usb_bulk_write_callback,
	},
	[MYFIRST_USB_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = MYFIRST_USB_INTR_BUFSZ,
		.flags = {.pipe_bof = 1, .short_xfer_ok = 1},
		.callback = &myfirst_usb_intr_read_callback,
	},
};

static int
myfirst_usb_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(myfirst_usb_devs,
	    sizeof(myfirst_usb_devs), uaa));
}

static int
myfirst_usb_attach(device_t dev)
{
	struct myfirst_usb_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;

	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
	sc->sc_iface_index = uaa->info.bIfaceIndex;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
	    sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_TRANSFER,
	    sc, &sc->sc_mtx);
	if (error != 0) {
		device_printf(dev, "usbd_transfer_setup failed: %d\n", error);
		goto fail_mtx;
	}

	device_set_desc(dev, "myfirst USB skeleton");
	return (0);

fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (ENXIO);
}

static int
myfirst_usb_detach(device_t dev)
{
	struct myfirst_usb_softc *sc = device_get_softc(dev);

	MYFIRST_USB_LOCK(sc);
	sc->sc_flags |= MYFIRST_USB_FLAG_DETACHING;
	MYFIRST_USB_UNLOCK(sc);

	usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_TRANSFER);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

/*
 * Bulk IN callback.
 *
 * On transfer completion, this example discards the received bytes.
 * Extend by copying them out with usbd_copy_out and forwarding them
 * to a consumer (for example, a character device's read queue).
 */
static void
myfirst_usb_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(2, "bulk in completed: %d bytes\n", actlen);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if (sc->sc_flags & MYFIRST_USB_FLAG_READ_STALL) {
			/* Would clear the stall here in a full driver. */
		}
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:	/* USB_ST_ERROR */
		if (error == USB_ERR_CANCELLED)
			return;
		sc->sc_flags |= MYFIRST_USB_FLAG_READ_STALL;
		goto tr_setup;
	}
}

/*
 * Bulk OUT callback.
 *
 * On USB_ST_SETUP this example submits a short zero-filled buffer
 * just so the callback is observable during testing. Extend by
 * pulling a buffer from a producer (for example, a character
 * device's write queue).
 */
static void
myfirst_usb_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(2, "bulk out completed\n");
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if (sc->sc_flags & MYFIRST_USB_FLAG_WRITE_STALL) {
			/* Would clear the stall here in a full driver. */
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_frame_zero(pc, 0, 16);
		usbd_xfer_set_frame_len(xfer, 0, 16);
		usbd_transfer_submit(xfer);
		break;

	default:	/* USB_ST_ERROR */
		if (error == USB_ERR_CANCELLED)
			return;
		sc->sc_flags |= MYFIRST_USB_FLAG_WRITE_STALL;
		goto tr_setup;
	}
}

/*
 * Interrupt IN callback.
 *
 * Runs continuously once started. On completion, the received bytes
 * would typically be handed to a status consumer.
 */
static void
myfirst_usb_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(2, "intr in: %d bytes\n", actlen);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:	/* USB_ST_ERROR */
		if (error == USB_ERR_CANCELLED)
			return;
		/* Rearm on non-cancellation errors. */
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	}
}

static device_method_t myfirst_usb_methods[] = {
	DEVMETHOD(device_probe,		myfirst_usb_probe),
	DEVMETHOD(device_attach,	myfirst_usb_attach),
	DEVMETHOD(device_detach,	myfirst_usb_detach),

	DEVMETHOD_END
};

static driver_t myfirst_usb_driver = {
	"myfirst_usb",
	myfirst_usb_methods,
	sizeof(struct myfirst_usb_softc),
};

DRIVER_MODULE(myfirst_usb, uhub, myfirst_usb_driver, NULL, NULL);
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
MODULE_VERSION(myfirst_usb, 1);
USB_PNP_HOST_INFO(myfirst_usb_devs);
