/*
 * bugdemo - Challenge 1 (silent-bug) variant.
 *
 * Contains a subtle bug: an atomic counter is incremented without
 * the memory barrier that callers implicitly expect. Under enough
 * concurrency the READ path can observe stale values. There is no
 * panic, no error, no log message. The only symptom is a wrong
 * number.
 *
 * Your job in the challenge is to reproduce the bug, locate it
 * with tracing, and fix it.
 *
 * Hint: look at the choice of atomic operation. FreeBSD's
 * atomic(9) distinguishes between plain atomics and atomics with
 * acquire/release semantics for a reason.
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

#include <machine/atomic.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

struct bugdemo_softc {
	struct cdev	*dev;
	struct mtx	 sc_mutex;
	volatile u_int	 counter;
	volatile u_int	 ready;
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
bugdemo_do_increment(struct bugdemo_softc *sc)
{
	/*
	 * Deliberate bug: plain add. The READ path depends on
	 * ready=1 implying the counter is visible, but without a
	 * release barrier on the writer side (and an acquire
	 * barrier on the reader side), the ready store can become
	 * visible before the counter store on weakly ordered
	 * architectures. The correct primitive here is
	 * atomic_store_rel_int() (or equivalent).
	 */
	atomic_add_int(&sc->counter, 1);
	atomic_store_int(&sc->ready, 1);
}

static u_int
bugdemo_do_read(struct bugdemo_softc *sc)
{
	/*
	 * Deliberate bug: plain load. Without acquire semantics on
	 * ready, the compiler or hardware can reorder the counter
	 * load above the ready check, yielding a stale value.
	 */
	if (atomic_load_int(&sc->ready) == 0)
		return (0);
	return (atomic_load_int(&sc->counter));
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
		case BUGDEMO_OP_INCREMENT:
			bugdemo_do_increment(sc);
			return (0);
		case BUGDEMO_OP_READ:
			bcmd->arg = bugdemo_do_read(sc);
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
		printf("bugdemo: silent-bug variant loaded\n");
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
		printf("bugdemo: silent-bug variant unloaded\n");
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
