/*
 * secdev.c - Lab 3 of Chapter 31 (fixed).
 *
 * Information-leak fix: the SECDEV_GET_INFO ioctl zeros its output
 * structure before populating it and uses strlcpy for the name.
 * Padding bytes are always zero, so the ioctl no longer carries
 * kernel memory contents to user space.
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
	uint64_t	flags;
	uint16_t	id;
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
    int devtype __unused, struct thread *td __unused)
{

	return (0);
}

static int
secdev_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	struct secdev_softc *sc = &secdev_sc;
	size_t available;
	int error;

	mtx_lock(&sc->sc_mtx);
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
	if (error == 0)
		sc->sc_buflen = amount;
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
		bzero(info, sizeof(*info));
		info->version = 1;
		info->flags = 0;
		info->id = 42;
		strlcpy(info->name, "secdev", sizeof(info->name));
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

		make_dev_args_init(&args);
		args.mda_devsw = &secdev_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_WHEEL;
		args.mda_mode = 0666;

		error = make_dev_s(&args, &sc->sc_cdev, "secdev");
		if (error != 0) {
			free(sc->sc_buf, M_TEMP);
			mtx_destroy(&sc->sc_mtx);
			return (error);
		}
		printf("secdev: loaded (Lab 3 fixed)\n");
		return (0);

	case MOD_UNLOAD:
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
