/*
 * myfirst_usb.h - Chapter 26 reference USB driver header.
 *
 * Declares the softc structure, transfer indices, and public
 * prototypes for the myfirst_usb driver. The driver is a thin
 * skeleton with bulk IN, bulk OUT, and interrupt IN channels
 * suitable for experiments with a USB gadget-mode peer or with
 * a programmable USB device running matching firmware.
 */

#ifndef _MYFIRST_USB_H_
#define _MYFIRST_USB_H_

/* Transfer indices. Keep in sync with myfirst_usb_config[] in myfirst_usb.c. */
enum {
	MYFIRST_USB_BULK_DT_RD = 0,
	MYFIRST_USB_BULK_DT_WR,
	MYFIRST_USB_INTR_DT_RD,
	MYFIRST_USB_N_TRANSFER,
};

/*
 * Per-device software context.
 *
 * Every field below is protected by sc_mtx except for sc_udev and
 * sc_dev, which are set once during attach and never changed.
 */
struct myfirst_usb_softc {
	device_t		sc_dev;
	struct usb_device	*sc_udev;
	struct mtx		sc_mtx;
	struct usb_xfer		*sc_xfer[MYFIRST_USB_N_TRANSFER];
	uint8_t			sc_iface_index;
	uint8_t			sc_flags;
#define MYFIRST_USB_FLAG_DETACHING	0x01
#define MYFIRST_USB_FLAG_READ_STALL	0x02
#define MYFIRST_USB_FLAG_WRITE_STALL	0x04
};

#define MYFIRST_USB_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define MYFIRST_USB_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define MYFIRST_USB_LOCK_ASSERT(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)

/* Buffer sizes. Large enough for typical bulk activity without excess. */
#define MYFIRST_USB_BULK_BUFSZ		4096
#define MYFIRST_USB_INTR_BUFSZ		64

#endif /* _MYFIRST_USB_H_ */
