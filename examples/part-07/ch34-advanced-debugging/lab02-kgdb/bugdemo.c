/*
 * bugdemo - Lab 2 variant.
 *
 * Adds support for a subcommand that clears the softc pointer to
 * produce a predictable null-pointer panic whose dump can be walked
 * with kgdb.
 *
 * Companion example for Chapter 34 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/counter.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

struct bugdemo_softc {
	struct cdev		*dev;
	struct mtx		 sc_mutex;
	int			 state;
#define	BUGDEMO_STATE_READY	 1
	counter_u64_t		 ioctls;
};

#define	BUGDEMO_LOCK(sc)	mtx_lock(&(sc)->sc_mutex)
#define	BUGDEMO_UNLOCK(sc)	mtx_unlock(&(sc)->sc_mutex)

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

	/*
	 * The KASSERT fires on a debug kernel when sc is NULL, which
	 * is what the null-softc test path produces.
	 */
	KASSERT(sc != NULL, ("bugdemo: softc missing"));

	counter_u64_add(sc->ioctls, 1);

	switch (cmd->op) {
	case BUGDEMO_OP_NOOP:
		break;
	case BUGDEMO_OP_HELLO:
		cmd->arg = 0x48454c4c4f;
		break;
	case BUGDEMO_OP_COUNT:
		cmd->arg = counter_u64_fetch(sc->ioctls);
		break;
	default:
		cmd->arg = 0;
		break;
	}
}

static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct bugdemo_softc *sc;
	struct bugdemo_command *bcmd;

	switch (cmd) {
	case BUGDEMO_TRIGGER:
		bcmd = (struct bugdemo_command *)data;
		/*
		 * NULL_SOFTC sets the si_drv1 pointer to NULL before
		 * dereferencing it in bugdemo_process. This is a
		 * deliberate bug for demonstration.
		 */
		if (bcmd->flags & BUGDEMO_FLAG_NULL_SOFTC) {
			dev->si_drv1 = NULL;
			sc = NULL;
		} else {
			sc = dev->si_drv1;
		}
		BUGDEMO_LOCK(bugdemo_sc);
		bugdemo_process(sc, bcmd);
		BUGDEMO_UNLOCK(bugdemo_sc);
		return (0);
	default:
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
		sc->dev = make_dev(&bugdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "bugdemo");
		if (sc->dev == NULL) {
			counter_u64_free(sc->ioctls);
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			return (ENOMEM);
		}
		sc->dev->si_drv1 = sc;
		bugdemo_sc = sc;
		printf("bugdemo: lab02 loaded\n");
		break;
	case MOD_UNLOAD:
		if (bugdemo_sc != NULL) {
			sc = bugdemo_sc;
			if (sc->dev != NULL)
				destroy_dev(sc->dev);
			counter_u64_free(sc->ioctls);
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			bugdemo_sc = NULL;
		}
		printf("bugdemo: lab02 unloaded\n");
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
