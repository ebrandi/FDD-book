/*
 * bugdemo - Challenge 2 (leaky-driver) variant.
 *
 * Each ALLOC ioctl allocates a small object. Each FREE ioctl is
 * supposed to free the most recently allocated object. The bug
 * is that one code path inside ALLOC returns without adding the
 * new object to the list, so FREE cannot find it. Over time the
 * orphaned objects accumulate.
 *
 * Your job in the challenge is to find the leak path.
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
#include <sys/queue.h>
#include <sys/ioccom.h>

#include "bugdemo.h"

static MALLOC_DEFINE(M_BUGDEMO, "bugdemo", "bugdemo driver buffers");

struct bugdemo_obj {
	TAILQ_ENTRY(bugdemo_obj)	 link;
	uint64_t			 id;
	char				 pad[56];
};

TAILQ_HEAD(bugdemo_objlist, bugdemo_obj);

struct bugdemo_softc {
	struct cdev		*dev;
	struct mtx		 sc_mutex;
	struct bugdemo_objlist	 objs;
	uint64_t		 next_id;
	uint64_t		 nallocs;
	uint64_t		 nfrees;
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
bugdemo_do_alloc(struct bugdemo_softc *sc, struct bugdemo_command *bcmd)
{
	struct bugdemo_obj *obj;

	obj = malloc(sizeof(*obj), M_BUGDEMO, M_WAITOK | M_ZERO);

	mtx_lock(&sc->sc_mutex);
	obj->id = ++sc->next_id;
	sc->nallocs++;

	/*
	 * Deliberate bug: when RETURN_EARLY is set, the driver
	 * increments the statistics counter and returns without
	 * linking the object into sc->objs. The caller gets
	 * success, but the object is unreachable from FREE.
	 */
	if (bcmd->flags & BUGDEMO_FLAG_RETURN_EARLY) {
		mtx_unlock(&sc->sc_mutex);
		return (0);
	}

	TAILQ_INSERT_HEAD(&sc->objs, obj, link);
	mtx_unlock(&sc->sc_mutex);
	return (0);
}

static int
bugdemo_do_free(struct bugdemo_softc *sc)
{
	struct bugdemo_obj *obj;

	mtx_lock(&sc->sc_mutex);
	obj = TAILQ_FIRST(&sc->objs);
	if (obj == NULL) {
		mtx_unlock(&sc->sc_mutex);
		return (ENOENT);
	}
	TAILQ_REMOVE(&sc->objs, obj, link);
	sc->nfrees++;
	mtx_unlock(&sc->sc_mutex);
	free(obj, M_BUGDEMO);
	return (0);
}

static int
bugdemo_do_stats(struct bugdemo_softc *sc, struct bugdemo_command *bcmd)
{
	mtx_lock(&sc->sc_mutex);
	bcmd->arg = sc->nallocs - sc->nfrees;
	mtx_unlock(&sc->sc_mutex);
	return (0);
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
		case BUGDEMO_OP_ALLOC:
			return (bugdemo_do_alloc(sc, bcmd));
		case BUGDEMO_OP_FREE:
			return (bugdemo_do_free(sc));
		case BUGDEMO_OP_STATS:
			return (bugdemo_do_stats(sc, bcmd));
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
	struct bugdemo_obj *obj, *tobj;
	int error = 0;

	switch (what) {
	case MOD_LOAD:
		sc = malloc(sizeof(*sc), M_BUGDEMO, M_WAITOK | M_ZERO);
		mtx_init(&sc->sc_mutex, "bugdemo_sc", NULL, MTX_DEF);
		TAILQ_INIT(&sc->objs);
		sc->dev = make_dev(&bugdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "bugdemo");
		if (sc->dev == NULL) {
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			return (ENOMEM);
		}
		sc->dev->si_drv1 = sc;
		bugdemo_sc = sc;
		printf("bugdemo: leaky-driver variant loaded\n");
		break;
	case MOD_UNLOAD:
		if (bugdemo_sc != NULL) {
			sc = bugdemo_sc;
			if (sc->dev != NULL)
				destroy_dev(sc->dev);
			TAILQ_FOREACH_SAFE(obj, &sc->objs, link, tobj) {
				TAILQ_REMOVE(&sc->objs, obj, link);
				free(obj, M_BUGDEMO);
			}
			mtx_destroy(&sc->sc_mutex);
			free(sc, M_BUGDEMO);
			bugdemo_sc = NULL;
		}
		printf("bugdemo: leaky-driver variant unloaded\n");
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
