/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.h -- Chapter 18 PCI interface for the myfirst driver.
 *
 * Stage 3: hw layer bridged to the BAR, capability walk, cdev
 * creation per PCI unit.
 */

#ifndef _MYFIRST_PCI_H_
#define _MYFIRST_PCI_H_

#include <sys/types.h>

#define MYFIRST_VENDOR_REDHAT		0x1af4
#define MYFIRST_DEVICE_VIRTIO_RNG	0x1005

struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
};

/*
 * Forward declarations for the hw layer's PCI-backed attach helper,
 * introduced in Chapter 18. See myfirst_hw.c for the definition.
 */
struct myfirst_softc;
struct resource;

int myfirst_hw_attach_pci(struct myfirst_softc *sc, struct resource *bar,
    bus_size_t bar_size);

#endif /* _MYFIRST_PCI_H_ */
