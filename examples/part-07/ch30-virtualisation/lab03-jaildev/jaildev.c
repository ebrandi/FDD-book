/*
 * jaildev.c - A minimal character-device driver for jail visibility testing.
 *
 * Chapter 30, Lab 3. Creates /dev/jaildev that returns a fixed greeting on
 * read. Exposes an ioctl that requires PRIV_DRIVER, so the same operation
 * succeeds on the host's root and fails inside a jail (where PRIV_DRIVER
 * is not granted by default).
 *
 * The driver illustrates three ideas:
 *   1. make_dev(9) with a well-known name for devfs ruleset control.
 *   2. d_read handling that is safe for any caller.
 *   3. priv_check(9) as the correct primitive for gated ioctls.
 *
 * Build:
 *   make clean && make
 * Load:
 *   sudo kldload ./jaildev.ko
 * Test:
 *   cat /dev/jaildev
 * Unload:
 *   sudo kldunload jaildev
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/ioccom.h>

#define	JAILDEV_GREETING	"hello from jaildev\n"

#define	JAILDEV_IOC_PRIVOP	_IO('J', 1)

static d_read_t		jaildev_read;
static d_ioctl_t	jaildev_ioctl;

static struct cdevsw jaildev_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	jaildev_read,
	.d_ioctl =	jaildev_ioctl,
	.d_name =	"jaildev",
};

static struct cdev *jaildev_dev;

static int
jaildev_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	const char *msg = JAILDEV_GREETING;
	size_t len = sizeof(JAILDEV_GREETING) - 1;
	size_t toread;

	if (uio->uio_offset >= (off_t)len)
		return (0);

	toread = len - uio->uio_offset;
	if (toread > uio->uio_resid)
		toread = uio->uio_resid;

	return (uiomove(__DECONST(void *, msg + uio->uio_offset), toread, uio));
}

static int
jaildev_ioctl(struct cdev *dev __unused, u_long cmd, caddr_t data __unused,
    int fflag __unused, struct thread *td)
{
	int error;

	switch (cmd) {
	case JAILDEV_IOC_PRIVOP:
		error = priv_check(td, PRIV_DRIVER);
		if (error != 0)
			return (error);
		printf("jaildev: privileged ioctl executed by pid %d\n",
		    td->td_proc->p_pid);
		return (0);
	default:
		return (ENOTTY);
	}
}

static int
jaildev_modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		jaildev_dev = make_dev(&jaildev_cdevsw, 0, UID_ROOT,
		    GID_WHEEL, 0644, "jaildev");
		if (jaildev_dev == NULL)
			return (ENOMEM);
		printf("jaildev: created /dev/jaildev\n");
		break;
	case MOD_UNLOAD:
		if (jaildev_dev != NULL)
			destroy_dev(jaildev_dev);
		printf("jaildev: goodbye\n");
		break;
	default:
		error = EOPNOTSUPP;
	}
	return (error);
}

static moduledata_t jaildev_mod = {
	"jaildev",
	jaildev_modevent,
	NULL
};

DECLARE_MODULE(jaildev, jaildev_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(jaildev, 1);
