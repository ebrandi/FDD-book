/*
 * secdev.c - Lab 7 of Chapter 31 (fully secure reference).
 *
 * Consolidation of every defence introduced in Labs 1 through 6 plus
 * a handful of additional measures that round out the driver's
 * defensive posture:
 *
 *   - Driver-private MALLOC_DEFINE tag so allocations appear in
 *     vmstat -m under a recognisable name.
 *   - make_dev_s with mode 0600, UID_ROOT, GID_WHEEL.
 *   - priv_check(PRIV_DRIVER) and jailed() refusal in d_open.
 *   - Bounds checks on every read, write, and ioctl path.
 *   - bzero before every copyout; strlcpy for strings.
 *   - M_ZERO on every allocation.
 *   - explicit_bzero on sensitive buffer before free.
 *   - ppsratecheck-gated log message on the unknown-ioctl path.
 *   - destroy_dev -> callout_drain -> free -> mtx_destroy on unload.
 *   - sysctl-controlled debug flag (secdev_debug).
 *   - KASSERT statements documenting internal invariants.
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
#include <sys/sysctl.h>
#include <sys/libkern.h>

#define	SECDEV_BUFSIZE	4096

static MALLOC_DEFINE(M_SECDEV, "secdev", "Chapter 31 secdev driver");

struct secdev_info {
	uint32_t	version;
	uint64_t	flags;
	uint16_t	id;
	char		name[32];
};

#define	SECDEV_GET_INFO	_IOR('S', 1, struct secdev_info)
#define	SECDEV_SLEEP	_IO('S', 2)
#define	SECDEV_WIPE	_IO('S', 3)

struct secdev_softc {
	struct mtx	sc_mtx;
	struct cdev	*sc_cdev;
	char		*sc_buf;
	size_t		sc_buflen;
	struct callout	sc_callout;
	uint64_t	sc_ticks;
};

static struct secdev_softc secdev_sc;

static struct timeval	secdev_log_last;
static int		secdev_log_pps;
static int		secdev_debug = 0;

static SYSCTL_NODE(_debug, OID_AUTO, secdev,
    CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "secdev teaching driver");
SYSCTL_INT(_debug_secdev, OID_AUTO, debug,
    CTLFLAG_RWTUN, &secdev_debug, 0,
    "Enable verbose diagnostic logging (0 = off, 1 = on)");

static d_open_t		secdev_open;
static d_read_t		secdev_read;
static d_write_t	secdev_write;
static d_ioctl_t	secdev_ioctl;

static struct cdevsw secdev_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	secdev_open,
	.d_read =	secdev_read,
	.d_write =	secdev_write,
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
	if (secdev_debug) {
		printf("secdev: open from uid %u\n",
		    td->td_ucred->cr_uid);
	}
	return (0);
}

static int
secdev_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	size_t available;
	int error;

	KASSERT(sc->sc_buf != NULL, ("secdev_read: sc_buf is NULL"));

	mtx_lock(&sc->sc_mtx);
	KASSERT(sc->sc_buflen <= SECDEV_BUFSIZE,
	    ("secdev_read: sc_buflen %zu exceeds SECDEV_BUFSIZE",
	    sc->sc_buflen));
	if ((size_t)uio->uio_offset >= sc->sc_buflen) {
		mtx_unlock(&sc->sc_mtx);
		return (0);
	}
	available = sc->sc_buflen - (size_t)uio->uio_offset;
	error = uiomove(sc->sc_buf + uio->uio_offset, available, uio);
	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
secdev_write(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	size_t amount;
	int error;

	mtx_lock(&sc->sc_mtx);
	amount = MIN((size_t)uio->uio_resid, SECDEV_BUFSIZE);
	error = uiomove(sc->sc_buf, amount, uio);
	if (error == 0) {
		sc->sc_buflen = amount;
		KASSERT(sc->sc_buflen <= SECDEV_BUFSIZE,
		    ("secdev_write: sc_buflen %zu exceeds SECDEV_BUFSIZE",
		    sc->sc_buflen));
	}
	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
secdev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int fflag __unused, struct thread *td)
{
	struct secdev_softc *sc = &secdev_sc;
	struct secdev_info *info;
	int error;

	switch (cmd) {
	case SECDEV_GET_INFO:
		info = (struct secdev_info *)data;
		bzero(info, sizeof(*info));
		info->version = 1;
		info->flags = 0;
		info->id = 42;
		strlcpy(info->name, "secdev", sizeof(info->name));
		return (0);

	case SECDEV_SLEEP:
		pause("secdev", hz / 10);
		mtx_lock(&sc->sc_mtx);
		sc->sc_ticks++;
		mtx_unlock(&sc->sc_mtx);
		return (0);

	case SECDEV_WIPE:
		error = priv_check(td, PRIV_DRIVER);
		if (error != 0)
			return (error);
		mtx_lock(&sc->sc_mtx);
		explicit_bzero(sc->sc_buf, SECDEV_BUFSIZE);
		sc->sc_buflen = 0;
		mtx_unlock(&sc->sc_mtx);
		return (0);

	default:
		if (ppsratecheck(&secdev_log_last, &secdev_log_pps, 5)) {
			printf("secdev: unknown ioctl 0x%lx from uid %u\n",
			    cmd, td->td_ucred->cr_uid);
		}
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
		sc->sc_buf = malloc(SECDEV_BUFSIZE, M_SECDEV,
		    M_WAITOK | M_ZERO);
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
			explicit_bzero(sc->sc_buf, SECDEV_BUFSIZE);
			free(sc->sc_buf, M_SECDEV);
			mtx_destroy(&sc->sc_mtx);
			return (error);
		}
		printf("secdev: loaded (Lab 7 fixed)\n");
		return (0);

	case MOD_UNLOAD:
		destroy_dev(sc->sc_cdev);
		callout_drain(&sc->sc_callout);
		explicit_bzero(sc->sc_buf, SECDEV_BUFSIZE);
		free(sc->sc_buf, M_SECDEV);
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
