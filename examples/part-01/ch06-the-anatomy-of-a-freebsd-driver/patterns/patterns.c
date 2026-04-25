/*
 * patterns.c - scratch space for Chapter 6
 *
 * Chapter 6 asks you to "type the micro-snippets yourself" so your
 * fingers remember the shape of a FreeBSD driver. This file collects
 * the recurring fragments in one place so you can look them up, retype
 * them, and get a feel for the vocabulary before you build anything in
 * Chapter 7.
 *
 * This file is NOT a working module. It will not compile. Its only
 * purpose is to show the shapes; nothing here ever gets linked.
 *
 * Matches FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/uio.h>

/* ---------------------------------------------------------------
 * 1. The softc: your per-instance private state.
 * --------------------------------------------------------------- */
struct mydriver_softc {
	device_t	 dev;		/* handy back-pointer */
	struct mtx	 mtx;		/* driver-wide lock */
	struct cdev	*cdev;		/* /dev entry, if any */
	struct resource	*mem_res;	/* MMIO resource */
	int		 mem_rid;
	uint64_t	 bytes_rx;	/* example statistic */
};

/* ---------------------------------------------------------------
 * 2. The minimal probe function.
 *
 * Probe answers one question only: "am I the driver for this
 * device?" It must not allocate resources or touch hardware.
 * --------------------------------------------------------------- */
static int
mydriver_probe(device_t dev)
{
	device_set_desc(dev, "My Example Driver");
	return (BUS_PROBE_DEFAULT);
}

/* ---------------------------------------------------------------
 * 3. A cdevsw skeleton.
 *
 * Only fill in the entry points you actually implement. Missing
 * handlers fall back to kernel defaults (open/close always succeed,
 * read returns EOF, write returns ENODEV, and so on).
 * --------------------------------------------------------------- */
static d_read_t		mydriver_read;
static d_write_t	mydriver_write;
static d_ioctl_t	mydriver_ioctl;

static struct cdevsw mydriver_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	mydriver_read,
	.d_write =	mydriver_write,
	.d_ioctl =	mydriver_ioctl,
	.d_name =	"mydriver",
};

/* ---------------------------------------------------------------
 * 4. The device method table.
 *
 * This is the routing table Newbus consults when it needs to call
 * probe/attach/detach. DEVMETHOD_END terminates the table.
 * --------------------------------------------------------------- */
static device_method_t mydriver_methods[] = {
	DEVMETHOD(device_probe,		mydriver_probe),
	DEVMETHOD(device_attach,	mydriver_attach),
	DEVMETHOD(device_detach,	mydriver_detach),
	DEVMETHOD_END
};

static driver_t mydriver_driver = {
	"mydriver",
	mydriver_methods,
	sizeof(struct mydriver_softc)
};

/* ---------------------------------------------------------------
 * 5. Module registration (FreeBSD 14.3 macro shape).
 *
 * DRIVER_MODULE takes exactly five arguments:
 *	name, parent bus, driver_t, event handler, event arg.
 *
 * Older code you may find online sometimes shows a six-argument
 * form with a separate devclass_t; that form no longer compiles.
 * --------------------------------------------------------------- */
DRIVER_MODULE(mydriver, pci, mydriver_driver, NULL, NULL);
MODULE_VERSION(mydriver, 1);

/* ---------------------------------------------------------------
 * 6. The universal first line of any driver method: grab the softc.
 * --------------------------------------------------------------- */
static int
mydriver_attach(device_t dev)
{
	struct mydriver_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* allocate resources, initialise hardware, create /dev, ... */

	device_printf(dev, "attached\n");
	return (0);
}

/* ---------------------------------------------------------------
 * 7. The failure-unwinding pattern for attach.
 *
 * Each cleanup label handles exactly the state established by the
 * step above it. Labels appear in reverse order of allocation, and
 * control jumps to the one matching the last successful step.
 * --------------------------------------------------------------- */
#if 0 /* pseudocode shape only */
static int
mydriver_attach_unwind(device_t dev)
{
	struct mydriver_softc *sc = device_get_softc(dev);
	int error;

	mtx_init(&sc->mtx, "mydriver", NULL, MTX_DEF);

	sc->mem_rid = PCIR_BAR(0);
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mem_rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		error = ENXIO;
		goto fail_mtx;
	}

	error = mydriver_hw_init(sc);
	if (error != 0)
		goto fail_mem;

	device_printf(dev, "attached\n");
	return (0);

fail_mem:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
fail_mtx:
	mtx_destroy(&sc->mtx);
	return (error);
}
#endif
