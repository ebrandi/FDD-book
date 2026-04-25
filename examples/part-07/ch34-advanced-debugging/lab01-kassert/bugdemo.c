/*
 * bugdemo - pseudo-device used to illustrate advanced debugging
 *           techniques in FreeBSD device drivers.
 *
 * Lab 1: baseline. Implements a minimal ioctl(2) interface guarded by
 *        KASSERT, MPASS, and CTASSERT checks. The driver exposes a
 *        few ops through sysctl; one op deliberately generates an
 *        out-of-range value to demonstrate a KASSERT firing.
 *
 * This is a companion example for Chapter 34 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/counter.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

CTASSERT(sizeof(struct bugdemo_command) == 16);
CTASSERT(BUGDEMO_OP_MAX <= 16);

struct bugdemo_softc {
	struct cdev		*dev;
	struct mtx		 sc_mutex;
	int			 state;
#define	BUGDEMO_STATE_INIT	 0
#define	BUGDEMO_STATE_READY	 1
#define	BUGDEMO_STATE_DEAD	 2
	counter_u64_t		 ioctls;
	counter_u64_t		 errors;
};

#define	BUGDEMO_LOCK(sc)	mtx_lock(&(sc)->sc_mutex)
#define	BUGDEMO_UNLOCK(sc)	mtx_unlock(&(sc)->sc_mutex)
#define	BUGDEMO_LOCK_ASSERT(sc)	mtx_assert(&(sc)->sc_mutex, MA_OWNED)

static d_open_t		bugdemo_open;
static d_close_t	bugdemo_close;
static d_ioctl_t	bugdemo_ioctl;

static struct cdevsw bugdemo_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =	"bugdemo",
	.d_open =	bugdemo_open,
	.d_close =	bugdemo_close,
	.d_ioctl =	bugdemo_ioctl,
};

static struct bugdemo_softc *bugdemo_sc;

static SYSCTL_NODE(_hw, OID_AUTO, bugdemo,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "bugdemo driver");

static int
bugdemo_sysctl_ioctls(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(bugdemo_sc->ioctls);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

static int
bugdemo_sysctl_errors(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(bugdemo_sc->errors);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

SYSCTL_PROC(_hw_bugdemo, OID_AUTO, ioctls,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, bugdemo_sysctl_ioctls, "QU",
    "Total number of ioctls served since load");

SYSCTL_PROC(_hw_bugdemo, OID_AUTO, errors,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, bugdemo_sysctl_errors, "QU",
    "Total number of errors since load");

static int
bugdemo_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return (0);
}

static int
bugdemo_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return (0);
}

static void
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{

	KASSERT(sc != NULL, ("bugdemo: softc missing"));
	KASSERT(cmd != NULL, ("bugdemo: cmd missing"));
	KASSERT(cmd->op < BUGDEMO_OP_MAX,
	    ("bugdemo: op %u out of range", cmd->op));
	MPASS(sc->state == BUGDEMO_STATE_READY);
	BUGDEMO_LOCK_ASSERT(sc);

	switch (cmd->op) {
	case BUGDEMO_OP_NOOP:
		break;
	case BUGDEMO_OP_HELLO:
		cmd->arg = 0x48454c4c4f;	/* "HELLO" */
		break;
	case BUGDEMO_OP_COUNT:
		cmd->arg = counter_u64_fetch(sc->ioctls);
		break;
	default:
		/*
		 * Unreachable because of the KASSERT above, but kept
		 * as an explicit fail-safe for release kernels.
		 */
		cmd->arg = 0;
		break;
	}
}

static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct bugdemo_softc *sc = dev->si_drv1;
	struct bugdemo_command *bcmd;

	KASSERT(sc != NULL, ("bugdemo_ioctl: no softc"));

	counter_u64_add(sc->ioctls, 1);

	switch (cmd) {
	case BUGDEMO_TRIGGER:
		bcmd = (struct bugdemo_command *)data;
		/*
		 * FORCE_BAD_OP deliberately produces an out-of-range
		 * op to demonstrate KASSERT firing. In a release build
		 * (no INVARIANTS) the KASSERT in bugdemo_process is
		 * compiled out and the default switch branch handles
		 * the bad op safely.
		 */
		if (bcmd->flags & BUGDEMO_FLAG_FORCE_BAD_OP)
			bcmd->op = 0xff;
		BUGDEMO_LOCK(sc);
		bugdemo_process(sc, bcmd);
		BUGDEMO_UNLOCK(sc);
		return (0);
	default:
		counter_u64_add(sc->errors, 1);
		return (ENOTTY);
	}
}

static int
bugdemo_modevent(module_t mod, int what, void *arg)
{
	struct bugdemo_softc *sc;
	int error = 0;

	switch (what) {
	case MOD_LOAD:
		sc = malloc(sizeof(*sc), M_BUGDEMO, M_WAITOK | M_ZERO);
		mtx_init(&sc->sc_mutex, "bugdemo_sc", NULL, MTX_DEF);
		sc->state = BUGDEMO_STATE_READY;
		sc->ioctls = counter_u64_alloc(M_WAITOK);
		sc->errors = counter_u64_alloc(M_WAITOK);
		sc->dev = make_dev(&bugdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "bugdemo");
		if (sc->dev == NULL) {
			counter_u64_free(sc->ioctls);
			counter_u64_free(sc->errors);
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			return (ENOMEM);
		}
		sc->dev->si_drv1 = sc;
		bugdemo_sc = sc;
		printf("bugdemo: lab01 loaded\n");
		break;
	case MOD_UNLOAD:
		if (bugdemo_sc != NULL) {
			sc = bugdemo_sc;
			BUGDEMO_LOCK(sc);
			sc->state = BUGDEMO_STATE_DEAD;
			BUGDEMO_UNLOCK(sc);
			if (sc->dev != NULL)
				destroy_dev(sc->dev);
			counter_u64_free(sc->ioctls);
			counter_u64_free(sc->errors);
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			bugdemo_sc = NULL;
		}
		printf("bugdemo: lab01 unloaded\n");
		break;
	default:
		error = EOPNOTSUPP;
	}
	return (error);
}

static moduledata_t bugdemo_mod = {
	"bugdemo",
	bugdemo_modevent,
	NULL
};

DECLARE_MODULE(bugdemo, bugdemo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(bugdemo, 1);
