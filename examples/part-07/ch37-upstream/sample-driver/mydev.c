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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mydev/mydev.h>

static int	mydev_probe(device_t dev);
static int	mydev_attach(device_t dev);
static int	mydev_detach(device_t dev);

static int
mydev_probe(device_t dev)
{
	/*
	 * Match your PCI vendor/device ID here.  Returning ENXIO
	 * from the skeleton means the driver will never attach
	 * to any real hardware; that is intentional for a
	 * template.  Replace the body with a vendor/device ID
	 * check and device_set_desc() once you have real
	 * hardware to match.
	 */
	return (ENXIO);
}

static int
mydev_attach(device_t dev)
{
	/*
	 * Allocate resources, initialise the device, set up
	 * interrupts, and create any device nodes here.  The
	 * skeleton returns 0 without doing anything because the
	 * probe routine above never matches.
	 */
	return (0);
}

static int
mydev_detach(device_t dev)
{
	/*
	 * Release resources and quiesce the device here.  Undo
	 * everything attach() did, in reverse order.
	 */
	return (0);
}

static device_method_t mydev_methods[] = {
	DEVMETHOD(device_probe,		mydev_probe),
	DEVMETHOD(device_attach,	mydev_attach),
	DEVMETHOD(device_detach,	mydev_detach),
	DEVMETHOD_END
};

static driver_t mydev_driver = {
	"mydev",
	mydev_methods,
	sizeof(struct mydev_softc),
};

DRIVER_MODULE(mydev, pci, mydev_driver, 0, 0);
MODULE_VERSION(mydev, 1);
MODULE_DEPEND(mydev, pci, 1, 1, 1);
