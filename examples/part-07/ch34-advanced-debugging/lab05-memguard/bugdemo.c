/*
 * bugdemo - Lab 5 variant.
 *
 * Contains a deliberate use-after-free: the ioctl allocates a
 * buffer, schedules a callout to read from it, then frees the
 * buffer. When the callout fires, it dereferences freed memory. On
 * a kernel with DEBUG_MEMGUARD and the driver's malloc type guarded,
 * this produces an immediate page fault.
 *
 * Usage:
 *     # kldload ./bugdemo.ko
 *     # sysctl vm.memguard.desc=bugdemo
 *     # ./bugdemo_test use-after-free
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
#include <sys/callout.h>
#include <sys/ioccom.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

struct bugdemo_softc {
	struct cdev		*dev;
	struct mtx		 sc_mutex;
	char			*buffer;
	size_t			 buflen;
	struct callout		 cb;
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
bugdemo_callout(void *arg)
{
	struct bugdemo_softc *sc = arg;
	volatile char c;

	/*
	 * Read from sc->buffer. In the use-after-free path, this
	 * buffer has already been freed by the ioctl. With memguard
	 * enabled for the bugdemo type, the page is unmapped and this
	 * access produces a page fault.
	 */
	c = sc->buffer[0];
	(void)c;
}

static void
bugdemo_trigger_use_after_free(struct bugdemo_softc *sc)
{
	sc->buflen = 128;
	sc->buffer = malloc(sc->buflen, M_BUGDEMO, M_WAITOK | M_ZERO);
	memcpy(sc->buffer, "bugdemo", 7);

	/* schedule the callout for 100ms out. */
	callout_init_mtx(&sc->cb, &sc->sc_mutex, 0);
	callout_reset(&sc->cb, hz / 10, bugdemo_callout, sc);

	/*
	 * Deliberate bug: free the buffer without cancelling the
	 * callout. The callout will fire with sc->buffer still pointing
	 * at the freed memory.
	 */
	free(sc->buffer, M_BUGDEMO);
	/* note: we do not set sc->buffer to NULL, also deliberately */
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
		mtx_lock(&sc->sc_mutex);
		if (bcmd->op == BUGDEMO_OP_USE_AFTER_FREE)
			bugdemo_trigger_use_after_free(sc);
		mtx_unlock(&sc->sc_mutex);
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
		sc->dev = make_dev(&bugdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "bugdemo");
		if (sc->dev == NULL) {
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			return (ENOMEM);
		}
		sc->dev->si_drv1 = sc;
		bugdemo_sc = sc;
		printf("bugdemo: lab05 loaded\n");
		break;
	case MOD_UNLOAD:
		if (bugdemo_sc != NULL) {
			sc = bugdemo_sc;
			callout_drain(&sc->cb);
			if (sc->dev != NULL)
				destroy_dev(sc->dev);
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			bugdemo_sc = NULL;
		}
		printf("bugdemo: lab05 unloaded\n");
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
