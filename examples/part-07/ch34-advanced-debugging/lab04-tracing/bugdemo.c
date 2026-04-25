/*
 * bugdemo - Lab 4 variant.
 *
 * Adds SDT probes at command start and completion so DTrace can
 * watch the driver in action. Also exposes an explicit error path
 * (BUGDEMO_OP_BAD) that returns EIO, useful for demonstrating how
 * DTrace can walk stacks on errors.
 *
 * Build with WITH_CTF=1 for DTrace to resolve struct fields by name:
 *     $ WITH_CTF=1 make
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
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sdt.h>
#include <sys/ioccom.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

SDT_PROVIDER_DEFINE(bugdemo);

SDT_PROBE_DEFINE2(bugdemo, , , cmd__start,
    "struct bugdemo_softc *", "int");
SDT_PROBE_DEFINE3(bugdemo, , , cmd__done,
    "struct bugdemo_softc *", "int", "int");
SDT_PROBE_DEFINE2(bugdemo, , , cmd__error,
    "struct bugdemo_softc *", "int");

struct bugdemo_softc {
	struct cdev	*dev;
	struct mtx	 sc_mutex;
};

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

static int
bugdemo_process(struct bugdemo_softc *sc, struct bugdemo_command *cmd)
{
	int error = 0;

	SDT_PROBE2(bugdemo, , , cmd__start, sc, cmd->op);

	switch (cmd->op) {
	case BUGDEMO_OP_NOOP:
		break;
	case BUGDEMO_OP_HELLO:
		cmd->arg = 0x48454c4c4f;
		break;
	case BUGDEMO_OP_COUNT:
		cmd->arg = 1;
		break;
	case BUGDEMO_OP_BAD:
		SDT_PROBE2(bugdemo, , , cmd__error, sc, cmd->op);
		error = EIO;
		break;
	default:
		error = EINVAL;
		break;
	}

	SDT_PROBE3(bugdemo, , , cmd__done, sc, cmd->op, error);
	return (error);
}

static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct bugdemo_softc *sc = dev->si_drv1;
	struct bugdemo_command *bcmd;
	int error;

	switch (cmd) {
	case BUGDEMO_TRIGGER:
		bcmd = (struct bugdemo_command *)data;
		mtx_lock(&sc->sc_mutex);
		error = bugdemo_process(sc, bcmd);
		mtx_unlock(&sc->sc_mutex);
		return (error);
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
		sc->dev = make_dev(&bugdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "bugdemo");
		if (sc->dev == NULL) {
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			return (ENOMEM);
		}
		sc->dev->si_drv1 = sc;
		bugdemo_sc = sc;
		printf("bugdemo: lab04 loaded\n");
		break;
	case MOD_UNLOAD:
		if (bugdemo_sc != NULL) {
			sc = bugdemo_sc;
			if (sc->dev != NULL)
				destroy_dev(sc->dev);
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			bugdemo_sc = NULL;
		}
		printf("bugdemo: lab04 unloaded\n");
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
