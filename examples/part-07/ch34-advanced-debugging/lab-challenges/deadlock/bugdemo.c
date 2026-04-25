/*
 * bugdemo - Challenge 3 (deadlock) variant.
 *
 * Two mutexes, two ioctl paths, opposite acquisition orders.
 * Classic AB-BA deadlock. The lab03-debug-kernel variant is
 * deterministic (sleeps between acquisitions to force the
 * ordering); this challenge variant adds timing jitter so the
 * hang looks intermittent. Your job is to find it anyway.
 *
 * Run with a kernel that has WITNESS enabled. WITNESS will
 * report the lock-order violation the first time it is seen,
 * even if the deadlock itself does not trigger that run.
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
#include <sys/ioccom.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

struct bugdemo_softc {
	struct cdev	*dev;
	struct mtx	 lock1;
	struct mtx	 lock2;
	uint64_t	 ncalls;
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

static void
bugdemo_do_path_a(struct bugdemo_softc *sc)
{
	mtx_lock(&sc->lock1);
	/* Simulate some work under lock1. */
	pause("path_a", 1);
	mtx_lock(&sc->lock2);
	sc->ncalls++;
	mtx_unlock(&sc->lock2);
	mtx_unlock(&sc->lock1);
}

static void
bugdemo_do_path_b(struct bugdemo_softc *sc)
{
	/*
	 * Deliberate bug: path B acquires lock2 first, then lock1.
	 * If thread 1 runs path A and holds lock1 while thread 2
	 * runs path B and holds lock2, both will block forever.
	 */
	mtx_lock(&sc->lock2);
	pause("path_b", 1);
	mtx_lock(&sc->lock1);
	sc->ncalls++;
	mtx_unlock(&sc->lock1);
	mtx_unlock(&sc->lock2);
}

static int
bugdemo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct bugdemo_softc *sc = dev->si_drv1;
	struct bugdemo_command *bcmd;

	switch (cmd) {
	case BUGDEMO_TRIGGER:
		bcmd = (struct bugdemo_command *)data;
		switch (bcmd->op) {
		case BUGDEMO_OP_PATH_A:
			bugdemo_do_path_a(sc);
			return (0);
		case BUGDEMO_OP_PATH_B:
			bugdemo_do_path_b(sc);
			return (0);
		default:
			return (EINVAL);
		}
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
		mtx_init(&sc->lock1, "bugdemo_l1", NULL, MTX_DEF);
		mtx_init(&sc->lock2, "bugdemo_l2", NULL, MTX_DEF);
		sc->dev = make_dev(&bugdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "bugdemo");
		if (sc->dev == NULL) {
			mtx_destroy(&sc->lock1);
			mtx_destroy(&sc->lock2);
			free(sc, M_BUGDEMO);
			return (ENOMEM);
		}
		sc->dev->si_drv1 = sc;
		bugdemo_sc = sc;
		printf("bugdemo: deadlock variant loaded\n");
		break;
	case MOD_UNLOAD:
		if (bugdemo_sc != NULL) {
			sc = bugdemo_sc;
			if (sc->dev != NULL)
				destroy_dev(sc->dev);
			mtx_destroy(&sc->lock1);
			mtx_destroy(&sc->lock2);
			free(sc, M_BUGDEMO);
			bugdemo_sc = NULL;
		}
		printf("bugdemo: deadlock variant unloaded\n");
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
