/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.h -- Chapter 18 PCI interface for the myfirst driver.
 *
 * Stage 1: minimal probe/attach/detach. Declares the vendor/device
 * ID match table structure and the IDs the probe recognises.
 */

#ifndef _MYFIRST_PCI_H_
#define _MYFIRST_PCI_H_

#include <sys/types.h>

/*
 * Target vendor and device IDs for the Chapter 18 demo.
 *
 * Both bhyve's and QEMU's virtio-rnd device use these values. A
 * reader with different test hardware should replace them with the
 * IDs their device advertises (visible through pciconf -lv).
 */
#define MYFIRST_VENDOR_REDHAT		0x1af4
#define MYFIRST_DEVICE_VIRTIO_RNG	0x1005

/*
 * One entry in the supported-device table. The driver iterates this
 * table in its probe routine and returns BUS_PROBE_DEFAULT on the
 * first match.
 */
struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
};

#endif /* _MYFIRST_PCI_H_ */
