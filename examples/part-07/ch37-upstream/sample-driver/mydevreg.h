/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Your Name <you@example.com>
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_MYDEV_MYDEVREG_H_
#define	_DEV_MYDEV_MYDEVREG_H_

/*
 * Hardware register definitions for the FooCorp FC100 sensor
 * board.  Register offsets are relative to the BAR0 base
 * address.  Values are from the FC100 Programmer's Reference
 * Manual v1.4.
 *
 * This file intentionally contains only register-layout
 * information, not driver logic.  Keeping the layout in a
 * separate header makes it easy to share with test harnesses,
 * userland tools, and reverse-engineering scripts.
 */

/* PCI identifiers. */
#define	MYDEV_VENDOR_ID		0xFEED
#define	MYDEV_DEVICE_ID_FC100	0x0100
#define	MYDEV_DEVICE_ID_FC200	0x0200

/* BAR0 register offsets. */
#define	MYDEV_REG_ID		0x00	/* RO: device and revision */
#define	MYDEV_REG_CTRL		0x04	/* RW: control */
#define	MYDEV_REG_STATUS	0x08	/* RO: status */
#define	MYDEV_REG_DATA		0x0C	/* RW: data sample */

/* CTRL register bits. */
#define	MYDEV_CTRL_ENABLE	(1u << 0)
#define	MYDEV_CTRL_IRQ_ENABLE	(1u << 1)
#define	MYDEV_CTRL_RESET	(1u << 7)

/* STATUS register bits. */
#define	MYDEV_STATUS_READY	(1u << 0)
#define	MYDEV_STATUS_BUSY	(1u << 1)
#define	MYDEV_STATUS_ERROR	(1u << 2)

/* Revision field decoded from MYDEV_REG_ID. */
#define	MYDEV_ID_REV_MASK	0x000000FFu
#define	MYDEV_ID_REV_SHIFT	0

#endif /* _DEV_MYDEV_MYDEVREG_H_ */
