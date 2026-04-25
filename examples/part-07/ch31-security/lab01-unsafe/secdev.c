/*
 * secdev.c - Lab 1 of Chapter 31 (Security Best Practices).
 *
 * This module is intentionally INSECURE.  It exists only as a teaching
 * artefact.  Do NOT use this code as a starting point for any real
 * driver.  Every "TODO: security issue" comment marks a specific bug
 * that later labs will fix.
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

#define	SECDEV_BUFSIZE	4096

struct secdev_info {
	uint32_t	version;
	/* 4 bytes of implicit padding on 64-bit builds. */
	uint64_t	flags;
	uint16_t	id;
	/* 6 bytes of implicit padding. */
	char		name[32];
};

#define	SECDEV_GET_INFO	_IOR('S', 1, struct secdev_info)

struct secdev_softc {
	struct mtx	sc_mtx;
	struct cdev	*sc_cdev;
	char		*sc_buf;
	size_t		sc_buflen;
};

static struct secdev_softc secdev_sc;

static d_open_t		secdev_open;
static d_close_t	secdev_close;
static d_read_t		secdev_read;
static d_write_t	secdev_write;
static d_ioctl_t	secdev_ioctl;

static struct cdevsw secdev_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	secdev_open,
	.d_close =	secdev_close,
	.d_read =	secdev_read,
	.d_write =	secdev_write,
	.d_ioctl =	secdev_ioctl,
	.d_name =	"secdev",
};

static int
secdev_open(struct cdev *dev __unused, int oflags __unused,
    int devtype __unused, struct thread *td __unused)
{

	/*
	 * TODO: security issue (missing privilege check).
	 * A real driver that exposes privileged functionality should call
	 * priv_check(td, PRIV_DRIVER) here and refuse non-privileged opens.
	 */
	return (0);
}

static int
secdev_close(struct cdev *dev __unused, int fflag __unused,
    int devtype __unused, struct thread *td __unused)
{

	return (0);
}

static int
secdev_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	int error;

	mtx_lock(&sc->sc_mtx);
	/*
	 * TODO: security issue (unchecked read length).
	 * This copies sc_buflen bytes regardless of caller buffer size and
	 * regardless of whether sc_buflen reflects valid data.
	 */
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
	 * TODO: security issue (buffer overflow).
	 * uiomove is asked to move uio_resid bytes into a buffer of only
	 * SECDEV_BUFSIZE bytes.  A write larger than SECDEV_BUFSIZE
	 * overflows kernel memory.
	 */
	error = uiomove(sc->sc_buf, uio->uio_resid, uio);
	sc->sc_buflen = SECDEV_BUFSIZE - uio->uio_resid;
	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
secdev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int fflag __unused, struct thread *td __unused)
{
	struct secdev_info *info;

	switch (cmd) {
	case SECDEV_GET_INFO:
		info = (struct secdev_info *)data;
		/*
		 * TODO: security issue (information leak).
		 * info is not zeroed before fields are filled, so the padding
		 * bytes between fields carry whatever was on the kernel stack
		 * back to user space.
		 */
		info->version = 1;
		info->flags = 0;
		info->id = 42;
		strncpy(info->name, "secdev", sizeof(info->name));
		return (0);
	default:
		/*
		 * TODO: security issue (unbounded log on bad input).
		 * Without rate limiting, a user loop of malformed ioctls can
		 * flood the kernel log.
		 */
		printf("secdev: unknown ioctl 0x%lx\n", cmd);
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

		make_dev_args_init(&args);
		args.mda_devsw = &secdev_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_WHEEL;
		/*
		 * TODO: security issue (world-writable device node).
		 * 0666 lets every user on the system read and write the
		 * device.  For a driver with any privileged surface this
		 * should be 0600 or 0660 with a trusted group.
		 */
		args.mda_mode = 0666;

		error = make_dev_s(&args, &sc->sc_cdev, "secdev");
		if (error != 0) {
			free(sc->sc_buf, M_TEMP);
			mtx_destroy(&sc->sc_mtx);
			return (error);
		}
		printf("secdev: loaded (intentionally unsafe, Lab 1)\n");
		return (0);

	case MOD_UNLOAD:
		/*
		 * TODO: security issue (unsafe detach order).
		 * Freeing the buffer before destroying the cdev races with
		 * in-flight reads and writes.
		 */
		free(sc->sc_buf, M_TEMP);
		destroy_dev(sc->sc_cdev);
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
