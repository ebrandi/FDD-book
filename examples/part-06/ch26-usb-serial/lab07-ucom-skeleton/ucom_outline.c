/*
 * ucom_outline.c - Skeleton structure for a ucom(4)-based USB driver.
 *
 * This file is illustrative only. It is not complete, does not
 * compile as-is, and is intended to be studied alongside uftdi.c.
 *
 * The purpose is to show where each ucom_callback entry goes in
 * relation to the rest of the driver, and to make clear the
 * separation between USB transfer callbacks (the three-state
 * state machine) and ucom configuration callbacks (one-shot
 * calls issued by the framework).
 *
 * The real struct ucom_callback is defined in
 * /usr/src/sys/dev/usb/serial/usb_serial.h and contains more
 * methods than are shown here (for example ucom_cfg_get_status,
 * ucom_cfg_set_ring, ucom_ioctl, ucom_tty_name, ucom_poll,
 * and ucom_free). This skeleton implements only the commonly
 * required subset to keep the outline readable.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/mutex.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
/* #include <dev/usb/serial/usb_serial.h>  -- provides ucom API */

#if 0  /* outline only */

struct my_softc {
	device_t		sc_dev;
	struct usb_device	*sc_udev;
	struct mtx		sc_mtx;
	struct ucom_softc	sc_ucom;
	struct usb_xfer		*sc_xfer[N_CHANNELS];
	/* chip-specific fields */
};

static void
my_ucom_cfg_open(struct ucom_softc *ucom)
{
	/* Issue chip-specific init via control transfer. */
}

static void
my_ucom_cfg_close(struct ucom_softc *ucom)
{
	/* Issue chip-specific teardown. */
}

static int
my_ucom_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	/* Validate termios. Reject unsupported baud rates with EINVAL. */
	return (0);
}

static void
my_ucom_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	/*
	 * Translate the termios settings into a vendor-specific USB
	 * control transfer and issue it through the ucom framework.
	 *
	 * This is the heart of a ucom driver. Every chip has its own
	 * command set, but the ingest point for userland 'stty' calls
	 * is uniformly this function.
	 */
}

static void
my_ucom_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	/* Issue a control transfer that asserts/clears DTR. */
}

static void
my_ucom_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	/* Issue a control transfer that asserts/clears RTS. */
}

static void
my_ucom_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	/* Issue a control transfer that starts/stops a break condition. */
}

static const struct ucom_callback my_callback = {
	.ucom_cfg_open = &my_ucom_cfg_open,
	.ucom_cfg_close = &my_ucom_cfg_close,
	.ucom_pre_param = &my_ucom_pre_param,
	.ucom_cfg_param = &my_ucom_cfg_param,
	.ucom_cfg_set_dtr = &my_ucom_cfg_set_dtr,
	.ucom_cfg_set_rts = &my_ucom_cfg_set_rts,
	.ucom_cfg_set_break = &my_ucom_cfg_set_break,
	.ucom_start_read = &my_start_read,
	.ucom_stop_read = &my_stop_read,
	.ucom_start_write = &my_start_write,
	.ucom_stop_write = &my_stop_write,
};

static int
my_attach(device_t dev)
{
	/*
	 * Standard attach: init mutex, call usbd_transfer_setup, then
	 * ucom_attach with sc, callback, and method count. Last step is
	 * ucom_set_pnpinfo_usb to register for matching.
	 */
	return (0);
}

static int
my_detach(device_t dev)
{
	/*
	 * Detach order: ucom_detach first (stops new opens and the TTY
	 * side), then usbd_transfer_unsetup, then mtx_destroy.
	 */
	return (0);
}

#endif  /* outline only */
