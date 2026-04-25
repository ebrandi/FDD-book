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
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mydrv/mydrv.h>

static int	mydrv_probe(device_t dev);
static int	mydrv_attach(device_t dev);
static int	mydrv_detach(device_t dev);

static int
mydrv_probe(device_t dev)
{
	/*
	 * Replace the ENXIO return with a real vendor/device ID
	 * check and a call to device_set_desc() once you have
	 * hardware to match against.
	 */
	return (ENXIO);
}

static int
mydrv_attach(device_t dev)
{
	struct mydrv_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	/*
	 * Allocate bus resources, map registers, set up interrupts,
	 * and create any device nodes here. Undo each step on error
	 * before returning, in reverse order.
	 */
	return (0);
}

static int
mydrv_detach(device_t dev)
{
	struct mydrv_softc *sc;

	sc = device_get_softc(dev);

	/*
	 * Release resources in reverse order of attach().
	 */
	mtx_destroy(&sc->mtx);
	return (0);
}

static device_method_t mydrv_methods[] = {
	DEVMETHOD(device_probe,		mydrv_probe),
	DEVMETHOD(device_attach,	mydrv_attach),
	DEVMETHOD(device_detach,	mydrv_detach),
	DEVMETHOD_END
};

static driver_t mydrv_driver = {
	"mydrv",
	mydrv_methods,
	sizeof(struct mydrv_softc),
};

DRIVER_MODULE(mydrv, pci, mydrv_driver, 0, 0);
MODULE_VERSION(mydrv, 1);
MODULE_DEPEND(mydrv, pci, 1, 1, 1);
