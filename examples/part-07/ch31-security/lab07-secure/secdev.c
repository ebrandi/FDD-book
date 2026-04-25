/*
 * secdev.c - Lab 7 of Chapter 31 (skeleton).
 *
 * This is the consolidation lab.  Fill in each TODO(lab7) marker,
 * applying every lesson from Labs 1 through 6 plus any additional
 * defensive measures you think the driver warrants.  When you are
 * finished the driver should satisfy every item on the Security
 * Checklist at the end of the chapter.
 *
 * The reference solution is in ../lab07-fixed/.  Try to complete this
 * skeleton without peeking; your version may diverge from the
 * reference in details, and that is fine as long as it is secure.
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

#define	SECDEV_BUFSIZE	4096

/*
 * TODO(lab7): define a MALLOC_DECLARE / MALLOC_DEFINE for this
 * driver's own memory tag so that vmstat -m makes the allocations
 * visible to the operator.  Free it in MOD_UNLOAD if you use
 * MALLOC_DEFINE.
 */

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
};

static struct secdev_softc secdev_sc;

/*
 * TODO(lab7): declare rate-limit state for the unknown-ioctl log and
 * a sysctl-controlled debug flag that gates verbose diagnostic
 * messages.  An OID per driver is common; see CTLFLAG_RWTUN.
 */

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

static int
secdev_open(struct cdev *dev __unused, int oflags __unused,
    int devtype __unused, struct thread *td)
{

	/*
	 * TODO(lab7): refuse opens from jailed credentials, refuse at
	 * elevated securelevel where appropriate, and call priv_check
	 * (or priv_check_cred) to enforce the in-kernel privilege
	 * requirement.
	 */
	(void)td;
	return (0);
}

static int
secdev_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	size_t available;
	int error;

	mtx_lock(&sc->sc_mtx);
	/*
	 * TODO(lab7): honour uio_offset so partial reads work.  Copy
	 * out only as many bytes as are valid in sc_buf.
	 */
	(void)available;
	error = uiomove(sc->sc_buf, sc->sc_buflen, uio);
	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
secdev_write(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	int error;

	mtx_lock(&sc->sc_mtx);
	/*
	 * TODO(lab7): clamp the copy length to SECDEV_BUFSIZE and
	 * update sc_buflen accordingly.  Consider returning EFBIG if
	 * the caller asked for more than the driver can hold; the
	 * silent-truncate behaviour is not always what you want.
	 */
	error = uiomove(sc->sc_buf, uio->uio_resid, uio);
	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
secdev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int fflag __unused, struct thread *td)
{
	struct secdev_softc *sc = &secdev_sc;
	struct secdev_info *info;

	switch (cmd) {
	case SECDEV_GET_INFO:
		info = (struct secdev_info *)data;
		/*
		 * TODO(lab7): zero the structure before filling it, then
		 * use strlcpy for any string field.  Re-check sign of id
		 * if the field type changes.
		 */
		info->version = 1;
		info->flags = 0;
		info->id = 42;
		strncpy(info->name, "secdev", sizeof(info->name));
		return (0);
	case SECDEV_SLEEP:
		pause("secdev", hz / 10);
		return (0);
	case SECDEV_WIPE:
		/*
		 * TODO(lab7): require PRIV_DRIVER (or a more specific
		 * privilege) with priv_check.  Zero the buffer with
		 * explicit_bzero so the optimiser cannot elide the clear.
		 */
		mtx_lock(&sc->sc_mtx);
		bzero(sc->sc_buf, SECDEV_BUFSIZE);
		sc->sc_buflen = 0;
		mtx_unlock(&sc->sc_mtx);
		return (0);
	default:
		/*
		 * TODO(lab7): log the unknown-ioctl event through a
		 * ppsratecheck gate so that a flood of malformed ioctls
		 * cannot flood dmesg.  Include the uid from
		 * td->td_ucred->cr_uid in the message.
		 */
		(void)td;
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
		/*
		 * TODO(lab7): replace M_TEMP with your private
		 * MALLOC_DEFINE tag and always include M_ZERO when the
		 * buffer may be copied to user space.
		 */
		sc->sc_buf = malloc(SECDEV_BUFSIZE, M_TEMP, M_WAITOK | M_ZERO);
		sc->sc_buflen = 0;
		callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

		make_dev_args_init(&args);
		args.mda_devsw = &secdev_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_WHEEL;
		/*
		 * TODO(lab7): use 0600 by default; think carefully before
		 * picking anything looser.  If the device should be
		 * accessible to a specific group, set that group here and
		 * document the trust model in the README.
		 */
		args.mda_mode = 0600;

		error = make_dev_s(&args, &sc->sc_cdev, "secdev");
		if (error != 0) {
			free(sc->sc_buf, M_TEMP);
			mtx_destroy(&sc->sc_mtx);
			return (error);
		}
		printf("secdev: loaded (Lab 7 skeleton)\n");
		return (0);

	case MOD_UNLOAD:
		/*
		 * TODO(lab7): close doors in the right order:
		 *   destroy_dev -> callout_drain -> explicit_bzero of the
		 *   sensitive buffer -> free -> mtx_destroy.
		 */
		destroy_dev(sc->sc_cdev);
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
