/*
 * demo.c - A simple character device for Chapter 6, Lab 3.
 *
 * This driver creates /dev/demo on load and destroys it on unload.
 * It implements the four classic cdevsw entry points:
 *
 *   open   - log who opened the device
 *   close  - log who closed it
 *   read   - return a short greeting message
 *   write  - accept (and log) up to 128 bytes from userspace
 *
 * Everything is deliberately minimal. Real drivers allocate per-open
 * state, validate input carefully, and keep much less noisy logs.
 * See Chapter 7 onward for that discipline.
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/proc.h>

/*
 * Global handle to the /dev/demo node. NULL until MOD_LOAD creates
 * it, cleared back to NULL after MOD_UNLOAD destroys it.
 */
static struct cdev *demo_dev = NULL;

/* --- entry points ------------------------------------------------ */

static int
demo_open(struct cdev *dev __unused, int oflags __unused,
    int devtype __unused, struct thread *td)
{

	printf("demo: Device opened (pid=%d, comm=%s)\n",
	    td->td_proc->p_pid, td->td_proc->p_comm);
	return (0);
}

static int
demo_close(struct cdev *dev __unused, int fflag __unused,
    int devtype __unused, struct thread *td)
{

	printf("demo: Device closed (pid=%d)\n", td->td_proc->p_pid);
	return (0);
}

static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	char message[] = "Hello from demo driver!\n";
	size_t len;
	int error;

	printf("demo: Read called, uio_resid=%zd bytes requested\n",
	    uio->uio_resid);

	/*
	 * Transfer the smaller of "what the user asked for" and
	 * "what we have available". The message terminator is kept
	 * out of the transferred count: devices are not strings.
	 */
	len = MIN(uio->uio_resid, sizeof(message) - 1);
	error = uiomove(message, len, uio);
	if (error != 0) {
		printf("demo: Read failed, error=%d\n", error);
		return (error);
	}

	printf("demo: Read completed, transferred %zu bytes\n", len);
	return (0);
}

static int
demo_write(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	char buffer[128];
	size_t len;
	int error;

	len = MIN(uio->uio_resid, sizeof(buffer) - 1);
	error = uiomove(buffer, len, uio);
	if (error != 0) {
		printf("demo: Write failed during uiomove, error=%d\n", error);
		return (error);
	}

	buffer[len] = '\0';
	printf("demo: User wrote %zu bytes: \"%s\"\n", len, buffer);
	return (0);
}

/* --- cdevsw (routing table) -------------------------------------- */

static struct cdevsw demo_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	demo_open,
	.d_close =	demo_close,
	.d_read =	demo_read,
	.d_write =	demo_write,
	.d_name =	"demo",
};

/* --- module event handler ---------------------------------------- */

static int
demo_modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		demo_dev = make_dev(&demo_cdevsw,
		    0,			/* unit */
		    UID_ROOT,
		    GID_WHEEL,
		    0666,		/* rw-rw-rw-, lab use only */
		    "demo");
		if (demo_dev == NULL) {
			printf("demo: Failed to create device node\n");
			return (ENXIO);
		}
		printf("demo: /dev/demo created\n");
		break;

	case MOD_UNLOAD:
		if (demo_dev != NULL) {
			/*
			 * destroy_dev() removes the node from /dev and
			 * waits for any in-flight cdev methods to
			 * return before it comes back. After this
			 * returns, none of our handlers will ever run
			 * again for the old cdev.
			 */
			destroy_dev(demo_dev);
			demo_dev = NULL;
			printf("demo: /dev/demo destroyed\n");
		}
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static moduledata_t demo_mod = {
	"demo",
	demo_modevent,
	NULL
};

DECLARE_MODULE(demo, demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(demo, 1);
