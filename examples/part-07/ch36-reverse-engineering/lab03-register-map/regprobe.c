/*
 * regprobe.c - A read-only register probing kernel module.
 *
 * Companion to Chapter 36, Lab 3, of "FreeBSD Device Drivers:
 * From First Steps to Kernel Mastery."
 *
 * This module attaches as a child of the PCI bus when the operator
 * tells it to (the probe method returns BUS_PROBE_NOWILDCARD, so
 * the module never claims a device automatically). When attached,
 * it allocates the first SYS_RES_MEMORY resource of the device and
 * exposes a sysctl that, when read, dumps every word of that
 * resource as hexadecimal.
 *
 * The module performs no writes. It is purely an observation tool,
 * intended for the early stages of a reverse-engineering project
 * when you want to see the contents of a device's memory-mapped
 * register region without changing anything.
 *
 * Suggested use:
 *
 *   # Detach the in-tree driver from the device first.
 *   sudo devctl detach pci0:1:0:0
 *
 *   # Load this module.
 *   sudo kldload ./regprobe.ko
 *
 *   # Attach this module to the device.
 *   sudo devctl set driver pci0:1:0:0 regprobe
 *
 *   # Read the dump sysctl.
 *   sysctl dev.regprobe.0.dump
 *
 *   # Compare two dumps separated by some time.
 *   sysctl dev.regprobe.0.dump > dump1.txt
 *   sleep 10
 *   sysctl dev.regprobe.0.dump > dump2.txt
 *   diff dump1.txt dump2.txt
 *
 *   # Detach when done.
 *   sudo devctl detach pci0:1:0:0
 *   sudo kldunload regprobe
 *   sudo devctl attach pci0:1:0:0
 *
 * Do not attach this module to a device that is currently in use.
 * Do not attach it to the device that backs your console or your
 * storage. The act of detaching the in-tree driver and attaching
 * this one will, at minimum, stop the original driver from working
 * until you reverse the operation.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#define	REGPROBE_MAX_DUMP	(64 * 1024)

struct regprobe_softc {
	device_t	 sc_dev;
	struct mtx	 sc_mtx;
	struct resource	*sc_mem;
	int		 sc_rid;
	bus_size_t	 sc_size;
};

static int regprobe_probe(device_t dev);
static int regprobe_attach(device_t dev);
static int regprobe_detach(device_t dev);
static int regprobe_dump_sysctl(SYSCTL_HANDLER_ARGS);

static int
regprobe_probe(device_t dev)
{
	/*
	 * BUS_PROBE_NOWILDCARD is the protective return value. It
	 * means: do not attach to any device unless the operator
	 * explicitly tells me to with devctl(8). Without this, the
	 * module would attempt to attach to every PCI device in the
	 * system, which would be both useless and dangerous.
	 */
	device_set_desc(dev, "Register probing tool (read-only)");
	return (BUS_PROBE_NOWILDCARD);
}

static int
regprobe_attach(device_t dev)
{
	struct regprobe_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	sc->sc_dev = dev;
	sc->sc_rid = PCIR_BAR(0);
	mtx_init(&sc->sc_mtx, "regprobe", NULL, MTX_DEF);

	/*
	 * Allocate the device's first memory BAR. We accept whatever
	 * the bus gives us; the size of the resource determines how
	 * much we will dump in the sysctl handler.
	 */
	sc->sc_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_rid, RF_ACTIVE);
	if (sc->sc_mem == NULL) {
		device_printf(dev,
		    "could not allocate memory resource\n");
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}
	sc->sc_size = rman_get_size(sc->sc_mem);
	device_printf(dev, "mapped %ju bytes at BAR0\n",
	    (uintmax_t)sc->sc_size);

	/* Sanity check the size. We refuse to map regions larger than
	 * REGPROBE_MAX_DUMP because the sysctl-dump output would not
	 * fit in any sensible buffer. */
	if (sc->sc_size > REGPROBE_MAX_DUMP) {
		device_printf(dev,
		    "BAR0 is %ju bytes, larger than the limit of %u\n",
		    (uintmax_t)sc->sc_size, REGPROBE_MAX_DUMP);
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_rid, sc->sc_mem);
		mtx_destroy(&sc->sc_mtx);
		return (E2BIG);
	}

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "dump",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    sc, 0, regprobe_dump_sysctl, "A",
	    "Hexadecimal dump of the device's memory region");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "size",
	    CTLFLAG_RD, &sc->sc_size,
	    "Size in bytes of the mapped region");

	return (0);
}

static int
regprobe_detach(device_t dev)
{
	struct regprobe_softc *sc = device_get_softc(dev);

	if (sc->sc_mem != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_rid, sc->sc_mem);
		sc->sc_mem = NULL;
	}
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static int
regprobe_dump_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct regprobe_softc *sc = arg1;
	char *buf, *p;
	bus_size_t off, size;
	uint32_t val;
	int error, n;
	size_t bufsize;

	size = sc->sc_size;
	/* Each 4-byte word produces about 24 bytes of output text
	 * ("ffff: 0xdeadbeef\n" is 18 bytes; round up). Allocate
	 * generously. */
	bufsize = (size_t)(size / 4 + 1) * 32 + 64;
	if (bufsize > REGPROBE_MAX_DUMP * 32)
		return (E2BIG);
	buf = malloc(bufsize, M_TEMP, M_WAITOK | M_ZERO);
	p = buf;

	mtx_lock(&sc->sc_mtx);
	for (off = 0; off + 4 <= size; off += 4) {
		val = bus_read_4(sc->sc_mem, off);
		n = snprintf(p, bufsize - (p - buf),
		    "%04jx: 0x%08x\n", (uintmax_t)off, val);
		if (n < 0 || (size_t)n >= bufsize - (p - buf))
			break;
		p += n;
	}
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_string(oidp, buf, p - buf, req);
	free(buf, M_TEMP);
	return (error);
}

static device_method_t regprobe_methods[] = {
	DEVMETHOD(device_probe,		regprobe_probe),
	DEVMETHOD(device_attach,	regprobe_attach),
	DEVMETHOD(device_detach,	regprobe_detach),
	DEVMETHOD_END
};

static driver_t regprobe_driver = {
	"regprobe",
	regprobe_methods,
	sizeof(struct regprobe_softc)
};

DRIVER_MODULE(regprobe, pci, regprobe_driver, NULL, NULL);
MODULE_VERSION(regprobe, 1);
