/*
 * portdrv_sim.c - simulation backend for portdrv.
 *
 * A software-only backend. Keeps a flat array of register values and
 * serves reads and writes from it. The core is unaware that no real
 * hardware is present. This backend is what makes the driver testable
 * without a physical device.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

#define PORTDRV_SIM_NREGS	256

struct portdrv_sim_priv {
	uint32_t regs[PORTDRV_SIM_NREGS];
};

static int portdrv_sim_instances = 1;
static struct portdrv_softc **portdrv_sim_softcs;
static struct portdrv_sim_priv **portdrv_sim_privs;

SYSCTL_NODE(_debug, OID_AUTO, portdrv, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "portdrv debug");

SYSCTL_INT(_debug_portdrv, OID_AUTO, sim_instances,
    CTLFLAG_RDTUN, &portdrv_sim_instances, 0,
    "Number of simulation backend instances to create at load time");

static uint32_t
portdrv_sim_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;

	if (off / 4 >= PORTDRV_SIM_NREGS)
		return (0);
	return (psp->regs[off / 4]);
}

static void
portdrv_sim_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;

	if (off / 4 < PORTDRV_SIM_NREGS)
		psp->regs[off / 4] = val;
}

static int
portdrv_sim_attach_be(struct portdrv_softc *sc)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;

	psp->regs[REG_ID / 4] = PORTDRV_EXPECTED_ID;
	psp->regs[REG_STATUS / 4] = 0;
	return (0);
}

static void
portdrv_sim_detach_be(struct portdrv_softc *sc)
{
	(void)sc;
}

const struct portdrv_backend portdrv_sim_backend = {
	.version	= PORTDRV_BACKEND_VERSION,
	.name		= "sim",
	.attach		= portdrv_sim_attach_be,
	.detach		= portdrv_sim_detach_be,
	.read_reg	= portdrv_sim_read_reg,
	.write_reg	= portdrv_sim_write_reg,
};

static int
portdrv_sim_create_instance(int unit)
{
	struct portdrv_softc *sc;
	struct portdrv_sim_priv *psp;
	int error;

	sc = malloc(sizeof(*sc), M_PORTDRV, M_WAITOK | M_ZERO);
	psp = malloc(sizeof(*psp), M_PORTDRV, M_WAITOK | M_ZERO);
	sc->sc_unit = unit;
	sc->sc_be = &portdrv_sim_backend;
	sc->sc_backend_priv = psp;

	error = portdrv_core_attach(sc);
	if (error != 0) {
		free(psp, M_PORTDRV);
		free(sc, M_PORTDRV);
		return (error);
	}

	portdrv_sim_softcs[unit] = sc;
	portdrv_sim_privs[unit] = psp;
	return (0);
}

static void
portdrv_sim_destroy_instance(int unit)
{
	struct portdrv_softc *sc = portdrv_sim_softcs[unit];
	struct portdrv_sim_priv *psp = portdrv_sim_privs[unit];

	if (sc == NULL)
		return;
	portdrv_core_detach(sc);
	free(psp, M_PORTDRV);
	free(sc, M_PORTDRV);
	portdrv_sim_softcs[unit] = NULL;
	portdrv_sim_privs[unit] = NULL;
}

static int
portdrv_sim_modevent(module_t mod, int type, void *arg)
{
	int i, error;

	switch (type) {
	case MOD_LOAD:
		if (portdrv_sim_instances <= 0)
			return (0);
		portdrv_sim_softcs = malloc(
		    portdrv_sim_instances * sizeof(*portdrv_sim_softcs),
		    M_PORTDRV, M_WAITOK | M_ZERO);
		portdrv_sim_privs = malloc(
		    portdrv_sim_instances * sizeof(*portdrv_sim_privs),
		    M_PORTDRV, M_WAITOK | M_ZERO);
		for (i = 0; i < portdrv_sim_instances; i++) {
			error = portdrv_sim_create_instance(i);
			if (error != 0) {
				while (i-- > 0)
					portdrv_sim_destroy_instance(i);
				free(portdrv_sim_softcs, M_PORTDRV);
				free(portdrv_sim_privs, M_PORTDRV);
				portdrv_sim_softcs = NULL;
				portdrv_sim_privs = NULL;
				return (error);
			}
		}
		return (0);
	case MOD_UNLOAD:
		if (portdrv_sim_softcs != NULL) {
			for (i = 0; i < portdrv_sim_instances; i++)
				portdrv_sim_destroy_instance(i);
			free(portdrv_sim_softcs, M_PORTDRV);
			free(portdrv_sim_privs, M_PORTDRV);
			portdrv_sim_softcs = NULL;
			portdrv_sim_privs = NULL;
		}
		return (0);
	}
	return (0);
}

static moduledata_t portdrv_sim_mod = {
	"portdrv_sim",
	portdrv_sim_modevent,
	NULL
};

DECLARE_MODULE(portdrv_sim, portdrv_sim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(portdrv_sim, PORTDRV_VERSION);
MODULE_DEPEND(portdrv_sim, portdrv_core, PORTDRV_VERSION, PORTDRV_VERSION,
    PORTDRV_VERSION);
