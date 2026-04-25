/*
 * secdev.c - Lab 6 of Chapter 31 (fixed).
 *
 * Safe detach.  The MOD_UNLOAD case closes doors in the right order:
 *
 *   1. destroy_dev         (no new d_* entries; in-flight d_* drain)
 *   2. callout_drain       (no pending or running callback)
 *   3. free                (memory can now be released safely)
 *   4. mtx_destroy         (lock is no longer held or waited on)
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>
#include <sys/priv.h>
#include <sys/ucred.h>
#include <sys/jail.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/callout.h>

#define	SECDEV_BUFSIZE	4096

#define	SECDEV_SLEEP	_IO('S', 2)

struct secdev_softc {
	struct mtx	sc_mtx;
	struct cdev	*sc_cdev;
	char		*sc_buf;
	size_t		sc_buflen;
	struct callout	sc_callout;
	uint64_t	sc_ticks;
};

static struct secdev_softc secdev_sc;

static d_open_t		secdev_open;
static d_ioctl_t	secdev_ioctl;

static struct cdevsw secdev_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	secdev_open,
	.d_ioctl =	secdev_ioctl,
	.d_name =	"secdev",
};

static void
secdev_timer(void *arg)
{
	struct secdev_softc *sc = arg;

	mtx_assert(&sc->sc_mtx, MA_OWNED);
	sc->sc_ticks++;
	callout_reset(&sc->sc_callout, hz, secdev_timer, sc);
}

static int
secdev_open(struct cdev *dev __unused, int oflags __unused,
    int devtype __unused, struct thread *td)
{
	int error;

	if (jailed(td->td_ucred))
		return (EPERM);
	error = priv_check(td, PRIV_DRIVER);
	if (error != 0)
		return (error);
	return (0);
}

static int
secdev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
    int fflag __unused, struct thread *td __unused)
{
	struct secdev_softc *sc = &secdev_sc;

	switch (cmd) {
	case SECDEV_SLEEP:
		pause("secdev", hz / 10);
		mtx_lock(&sc->sc_mtx);
		sc->sc_ticks++;
		mtx_unlock(&sc->sc_mtx);
		return (0);
	default:
		return (ENOTTY);
	}
}

static int
secdev_modevent(module_t mod __unused, int event, void *arg __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	struct make_dev_args args;
	int error;

	switch (event) {
	case MOD_LOAD:
		mtx_init(&sc->sc_mtx, "secdev", NULL, MTX_DEF);
		sc->sc_buf = malloc(SECDEV_BUFSIZE, M_TEMP, M_WAITOK | M_ZERO);
		sc->sc_buflen = 0;
		callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
		mtx_lock(&sc->sc_mtx);
		callout_reset(&sc->sc_callout, hz, secdev_timer, sc);
		mtx_unlock(&sc->sc_mtx);

		make_dev_args_init(&args);
		args.mda_devsw = &secdev_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_WHEEL;
		args.mda_mode = 0600;

		error = make_dev_s(&args, &sc->sc_cdev, "secdev");
		if (error != 0) {
			callout_drain(&sc->sc_callout);
			free(sc->sc_buf, M_TEMP);
			mtx_destroy(&sc->sc_mtx);
			return (error);
		}
		printf("secdev: loaded (Lab 6 fixed)\n");
		return (0);

	case MOD_UNLOAD:
		destroy_dev(sc->sc_cdev);
		callout_drain(&sc->sc_callout);
		free(sc->sc_buf, M_TEMP);
		mtx_destroy(&sc->sc_mtx);
		printf("secdev: unloaded\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t secdev_mod = {
	"secdev",
	secdev_modevent,
	NULL
};

DECLARE_MODULE(secdev, secdev_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(secdev, 1);
