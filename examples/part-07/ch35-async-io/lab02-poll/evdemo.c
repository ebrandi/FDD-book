/*
 * evdemo.c - Lab 2: adds poll() support to the evdemo driver.
 *
 * Changes from Lab 1:
 *   - Adds struct selinfo sc_rsel to the softc.
 *   - Initializes the selinfo's knlist in attach (cheap, and makes
 *     it compatible with the kqueue support added in Lab 3).
 *   - Adds d_poll callback that registers the caller on the
 *     selinfo if the queue is empty, reports POLLIN if non-empty.
 *   - Calls selwakeup() in the producer path after the lock is
 *     dropped.
 *   - Adds seldrain() and knlist_destroy() to the detach path.
 *   - Tracks a selwakeup counter for observability.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/event.h>
#include <sys/proc.h>

#include "evdemo.h"

#define	EVDEMO_QUEUE_SIZE	64

MALLOC_DEFINE(M_EVDEMO, "evdemo", "evdemo driver softc");

struct evdemo_softc {
	struct cdev		*sc_dev;
	struct mtx		 sc_mtx;
	struct cv		 sc_cv;
	struct callout		 sc_callout;
	struct selinfo		 sc_rsel;

	struct evdemo_event	 sc_queue[EVDEMO_QUEUE_SIZE];
	u_int			 sc_qhead;
	u_int			 sc_qtail;
	u_int			 sc_nevents;
	u_int			 sc_dropped;
	u_int			 sc_posted;
	u_int			 sc_consumed;
	u_int			 sc_selwakeups;

	bool			 sc_detaching;
};

static struct evdemo_softc	*evdemo_sc_global;

static d_open_t		evdemo_open;
static d_close_t	evdemo_close;
static d_read_t		evdemo_read;
static d_poll_t		evdemo_poll;

static struct cdevsw evdemo_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	evdemo_open,
	.d_close =	evdemo_close,
	.d_read =	evdemo_read,
	.d_poll =	evdemo_poll,
	.d_name =	"evdemo",
};

static void	evdemo_callout(void *arg);
static void	evdemo_post_event(struct evdemo_softc *sc,
		    struct evdemo_event *ev);
static int	evdemo_trigger_sysctl(SYSCTL_HANDLER_ARGS);

static SYSCTL_NODE(_dev, OID_AUTO, evdemo, CTLFLAG_RW, 0,
    "evdemo driver");
SYSCTL_PROC(_dev_evdemo, OID_AUTO, trigger,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, NULL, 0,
    evdemo_trigger_sysctl, "I", "Post a synthetic event");

static inline bool
evdemo_queue_empty(const struct evdemo_softc *sc)
{
	return (sc->sc_nevents == 0);
}

static inline bool
evdemo_queue_full(const struct evdemo_softc *sc)
{
	return (sc->sc_nevents == EVDEMO_QUEUE_SIZE);
}

static void
evdemo_enqueue(struct evdemo_softc *sc, const struct evdemo_event *ev)
{
	mtx_assert(&sc->sc_mtx, MA_OWNED);

	if (evdemo_queue_full(sc)) {
		sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
		sc->sc_nevents--;
		sc->sc_dropped++;
	}
	sc->sc_queue[sc->sc_qtail] = *ev;
	sc->sc_qtail = (sc->sc_qtail + 1) % EVDEMO_QUEUE_SIZE;
	sc->sc_nevents++;
	sc->sc_posted++;
}

static int
evdemo_dequeue(struct evdemo_softc *sc, struct evdemo_event *ev)
{
	mtx_assert(&sc->sc_mtx, MA_OWNED);

	if (evdemo_queue_empty(sc))
		return (-1);
	*ev = sc->sc_queue[sc->sc_qhead];
	sc->sc_qhead = (sc->sc_qhead + 1) % EVDEMO_QUEUE_SIZE;
	sc->sc_nevents--;
	sc->sc_consumed++;
	return (0);
}

static int
evdemo_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return (0);
}

static int
evdemo_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return (0);
}

static int
evdemo_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct evdemo_softc *sc = dev->si_drv1;
	struct evdemo_event ev;
	ssize_t start_resid = uio->uio_resid;
	int error = 0;

	while (uio->uio_resid >= (ssize_t)sizeof(ev)) {
		mtx_lock(&sc->sc_mtx);
		while (evdemo_queue_empty(sc) && !sc->sc_detaching) {
			if (flag & O_NONBLOCK) {
				mtx_unlock(&sc->sc_mtx);
				if (uio->uio_resid == start_resid)
					return (EAGAIN);
				return (0);
			}
			error = cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
			if (error != 0) {
				mtx_unlock(&sc->sc_mtx);
				return (error);
			}
		}
		if (sc->sc_detaching) {
			mtx_unlock(&sc->sc_mtx);
			return (0);
		}
		evdemo_dequeue(sc, &ev);
		mtx_unlock(&sc->sc_mtx);

		error = uiomove(&ev, sizeof(ev), uio);
		if (error != 0)
			return (error);
	}
	return (0);
}

static int
evdemo_poll(struct cdev *dev, int events, struct thread *td)
{
	struct evdemo_softc *sc = dev->si_drv1;
	int revents = 0;

	mtx_lock(&sc->sc_mtx);
	if (events & (POLLIN | POLLRDNORM)) {
		if (!evdemo_queue_empty(sc))
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &sc->sc_rsel);
	}
	if (events & (POLLOUT | POLLWRNORM))
		revents |= events & (POLLOUT | POLLWRNORM);
	mtx_unlock(&sc->sc_mtx);

	return (revents);
}

static void
evdemo_post_event(struct evdemo_softc *sc, struct evdemo_event *ev)
{
	mtx_lock(&sc->sc_mtx);
	evdemo_enqueue(sc, ev);
	cv_broadcast(&sc->sc_cv);
	sc->sc_selwakeups++;
	mtx_unlock(&sc->sc_mtx);

	selwakeup(&sc->sc_rsel);
}

static void
evdemo_callout(void *arg)
{
	struct evdemo_softc *sc = arg;
	struct evdemo_event ev;

	nanotime(&ev.ev_time);
	ev.ev_type = EVDEMO_EV_TYPE_TICK;
	ev.ev_code = 0;
	ev.ev_value = 0;
	evdemo_post_event(sc, &ev);

	callout_reset(&sc->sc_callout, hz, evdemo_callout, sc);
}

static int
evdemo_trigger_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct evdemo_softc *sc = evdemo_sc_global;
	struct evdemo_event ev;
	int value = 0;
	int error;

	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL || sc == NULL)
		return (error);

	nanotime(&ev.ev_time);
	ev.ev_type = EVDEMO_EV_TYPE_TRIGGER;
	ev.ev_code = 0;
	ev.ev_value = value;
	evdemo_post_event(sc, &ev);
	return (0);
}

static int
evdemo_modevent(module_t mod, int event, void *arg)
{
	struct evdemo_softc *sc;
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		sc = malloc(sizeof(*sc), M_EVDEMO, M_WAITOK | M_ZERO);
		mtx_init(&sc->sc_mtx, "evdemo", NULL, MTX_DEF);
		cv_init(&sc->sc_cv, "evdemo");
		knlist_init_mtx(&sc->sc_rsel.si_note, &sc->sc_mtx);
		callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

		sc->sc_dev = make_dev(&evdemo_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0600, "evdemo");
		sc->sc_dev->si_drv1 = sc;
		evdemo_sc_global = sc;

		mtx_lock(&sc->sc_mtx);
		callout_reset(&sc->sc_callout, hz, evdemo_callout, sc);
		mtx_unlock(&sc->sc_mtx);

		printf("evdemo: loaded (lab02-poll)\n");
		break;

	case MOD_UNLOAD:
		sc = evdemo_sc_global;
		if (sc == NULL)
			break;

		mtx_lock(&sc->sc_mtx);
		sc->sc_detaching = true;
		cv_broadcast(&sc->sc_cv);
		mtx_unlock(&sc->sc_mtx);
		selwakeup(&sc->sc_rsel);

		callout_drain(&sc->sc_callout);
		destroy_dev(sc->sc_dev);

		seldrain(&sc->sc_rsel);
		knlist_destroy(&sc->sc_rsel.si_note);

		cv_destroy(&sc->sc_cv);
		mtx_destroy(&sc->sc_mtx);

		free(sc, M_EVDEMO);
		evdemo_sc_global = NULL;
		printf("evdemo: unloaded\n");
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t evdemo_mod = {
	"evdemo",
	evdemo_modevent,
	NULL
};

DECLARE_MODULE(evdemo, evdemo_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(evdemo, 20);
