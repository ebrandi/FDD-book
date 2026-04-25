/*
 * portdrv_core.c - hardware-independent core for portdrv.
 *
 * This file knows about the softc, the character device, the sysctl
 * tree, and the read/write path through accessors. It does not include
 * any bus-specific header. If you find yourself reaching for
 * dev/pci/pcivar.h here, you are in the wrong file.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

MALLOC_DEFINE(M_PORTDRV, "portdrv", "portdrv driver buffers");

static d_open_t		portdrv_open;
static d_close_t	portdrv_close;
static d_read_t		portdrv_read;
static d_write_t	portdrv_write;

static struct cdevsw portdrv_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	portdrv_open,
	.d_close =	portdrv_close,
	.d_read =	portdrv_read,
	.d_write =	portdrv_write,
	.d_name =	"portdrv",
};

/*
 * Register accessors. Every path that needs a register value goes
 * through these. The actual read/write primitive lives in the backend.
 */
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (sc->sc_be->read_reg(sc, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	sc->sc_be->write_reg(sc, off, val);
}

/*
 * Higher-level accessors that know the byte order of a specific
 * register. These are the functions core logic calls when it wants the
 * decoded value. The plain accessors above move raw bits; these apply
 * the documented semantics.
 */
static uint32_t
portdrv_read_data_le(struct portdrv_softc *sc)
{
	return (le32toh(portdrv_read_reg(sc, REG_DATA)));
}

static void
portdrv_write_data_le(struct portdrv_softc *sc, uint32_t val)
{
	portdrv_write_reg(sc, REG_DATA, htole32(val));
}

/*
 * sysctl tree. Called once per instance at attach time. Adds leaf
 * nodes under dev.portdrv.<unit>. The stats leaves are read-only.
 */
static void
portdrv_sysctl_init(struct portdrv_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	if (sc->sc_dev == NULL)
		return;

	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "backend",
	    CTLFLAG_RD, __DECONST(char *, sc->sc_be->name), 0,
	    "Backend name");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "version",
	    CTLFLAG_RD, NULL, PORTDRV_VERSION, "Driver version");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "transfers_ok",
	    CTLFLAG_RD, &sc->sc_stats.transfers_ok, "Successful transfers");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "transfers_fail",
	    CTLFLAG_RD, &sc->sc_stats.transfers_fail, "Failed transfers");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "bytes_in",
	    CTLFLAG_RD, &sc->sc_stats.bytes_in, "Bytes written to device");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "bytes_out",
	    CTLFLAG_RD, &sc->sc_stats.bytes_out, "Bytes read from device");
}

int
portdrv_core_attach(struct portdrv_softc *sc)
{
	int error;

	if (sc->sc_be == NULL) {
		printf("portdrv: no backend bound before core_attach\n");
		return (EINVAL);
	}

	if (sc->sc_be->version != PORTDRV_BACKEND_VERSION) {
		printf("portdrv: backend %s version %u, core expects %u\n",
		    sc->sc_be->name, sc->sc_be->version,
		    PORTDRV_BACKEND_VERSION);
		return (EPROTONOSUPPORT);
	}

	mtx_init(&sc->sc_mtx, "portdrv_sc", NULL, MTX_DEF);

	if (sc->sc_be->attach != NULL) {
		error = sc->sc_be->attach(sc);
		if (error != 0) {
			mtx_destroy(&sc->sc_mtx);
			return (error);
		}
	}

	sc->sc_cdev = make_dev(&portdrv_cdevsw, sc->sc_unit,
	    UID_ROOT, GID_WHEEL, 0600, "portdrv%d", sc->sc_unit);
	if (sc->sc_cdev == NULL) {
		if (sc->sc_be->detach != NULL)
			sc->sc_be->detach(sc);
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}
	sc->sc_cdev->si_drv1 = sc;

	portdrv_sysctl_init(sc);

	if (sc->sc_dev != NULL)
		device_printf(sc->sc_dev,
		    "portdrv version %d attached (backend %s)\n",
		    PORTDRV_VERSION, sc->sc_be->name);
	else
		printf("portdrv%d: version %d attached (backend %s)\n",
		    sc->sc_unit, PORTDRV_VERSION, sc->sc_be->name);

	return (0);
}

void
portdrv_core_detach(struct portdrv_softc *sc)
{
	if (sc->sc_cdev != NULL) {
		destroy_dev(sc->sc_cdev);
		sc->sc_cdev = NULL;
	}
	if (sc->sc_be != NULL && sc->sc_be->detach != NULL)
		sc->sc_be->detach(sc);
	mtx_destroy(&sc->sc_mtx);
}

static int
portdrv_open(struct cdev *cdev, int flags, int devtype, struct thread *td)
{
	return (0);
}

static int
portdrv_close(struct cdev *cdev, int flags, int devtype, struct thread *td)
{
	return (0);
}

static int
portdrv_read(struct cdev *cdev, struct uio *uio, int flag)
{
	struct portdrv_softc *sc = cdev->si_drv1;
	uint32_t val;
	int error;

	val = portdrv_read_data_le(sc);
	error = uiomove(&val, MIN(uio->uio_resid, sizeof(val)), uio);
	if (error == 0) {
		atomic_add_64(&sc->sc_stats.transfers_ok, 1);
		atomic_add_64(&sc->sc_stats.bytes_out, sizeof(val));
	} else {
		atomic_add_64(&sc->sc_stats.transfers_fail, 1);
	}
	return (error);
}

static int
portdrv_write(struct cdev *cdev, struct uio *uio, int flag)
{
	struct portdrv_softc *sc = cdev->si_drv1;
	uint32_t val = 0;
	int error;

	error = uiomove(&val, MIN(uio->uio_resid, sizeof(val)), uio);
	if (error != 0) {
		atomic_add_64(&sc->sc_stats.transfers_fail, 1);
		return (error);
	}
	portdrv_write_data_le(sc, val);
	sc->sc_last_data = val;
	atomic_add_64(&sc->sc_stats.transfers_ok, 1);
	atomic_add_64(&sc->sc_stats.bytes_in, sizeof(val));
	return (0);
}

static int
portdrv_core_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
		printf("portdrv: version %d loaded, backends:"
#ifdef PORTDRV_WITH_PCI
		    " pci"
#endif
#ifdef PORTDRV_WITH_SIM
		    " sim"
#endif
		    "\n", PORTDRV_VERSION);
		return (0);
	case MOD_UNLOAD:
		return (0);
	}
	return (0);
}

static moduledata_t portdrv_core_mod = {
	"portdrv_core",
	portdrv_core_modevent,
	NULL
};

DECLARE_MODULE(portdrv_core, portdrv_core_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(portdrv_core, PORTDRV_VERSION);
