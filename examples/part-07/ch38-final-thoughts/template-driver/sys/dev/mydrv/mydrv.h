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
 * PARTICULAR PURPOSE ARE DISCLAIMED.
 */

#ifndef _DEV_MYDRV_MYDRV_H_
#define _DEV_MYDRV_MYDRV_H_

#include <sys/lock.h>
#include <sys/mutex.h>

struct mydrv_softc {
	device_t	 dev;
	struct mtx	 mtx;

	/*
	 * Add resource handles, bus_space tags, interrupt
	 * cookies, and any per-device state below.
	 */
};

#endif /* _DEV_MYDRV_MYDRV_H_ */
