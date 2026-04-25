/*
 * demo.c - Final template for Chapter 6, Lab 4.
 *
 * This version is the end state of Lab 4's experiments. It shows
 * the habits every production driver should follow:
 *
 *   - always check the return value of make_dev() and malloc()
 *   - always NULL-check before calling destroy_dev() or free()
 *   - always clear pointers after freeing them
 *   - tear down resources in the reverse order of acquisition
 *   - use a single "fail:" label to centralise error unwinding
 *
 * The driver creates /dev/demo, keeps a small kernel buffer, and
 * tears everything down cleanly on unload.
 *
 * Use this file as a reference skeleton when you move on to
 * Chapter 7.
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

/* Private memory type for this driver (shows up in `vmstat -m`). */
static MALLOC_DEFINE(M_DEMO, "demo", "Chapter 6 demo driver");

/*
 * Global state. In a real driver this would live in a per-device
 * softc allocated by Newbus. For the sake of Chapter 6 we keep it
 * simple.
 */
static struct cdev	*demo_dev = NULL;
static char		*demo_buffer = NULL;
#define DEMO_BUFSZ	128

/* --- cdev entry points ------------------------------------------- */

static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	size_t len;

	if (demo_buffer == NULL)
		return (ENODEV);

	len = MIN(uio->uio_resid, strlen(demo_buffer));
	return (uiomove(demo_buffer, len, uio));
}

static int
demo_write(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	size_t len;
	int error;

	if (demo_buffer == NULL)
		return (ENODEV);

	len = MIN(uio->uio_resid, DEMO_BUFSZ - 1);
	error = uiomove(demo_buffer, len, uio);
	if (error != 0)
		return (error);

	demo_buffer[len] = '\0';
	printf("demo: user wrote %zu bytes: \"%s\"\n", len, demo_buffer);
	return (0);
}

static struct cdevsw demo_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	demo_read,
	.d_write =	demo_write,
	.d_name =	"demo",
};

/* --- module event handler (with unwinding) ----------------------- */

static int
demo_modevent(module_t mod __unused, int event, void *arg __unused)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		/* Step 1: allocate a kernel buffer. */
		demo_buffer = malloc(DEMO_BUFSZ, M_DEMO,
		    M_WAITOK | M_ZERO);
		/*
		 * M_WAITOK will not return NULL on modern FreeBSD, but
		 * keeping the check makes the intent explicit and
		 * survives future API changes.
		 */
		if (demo_buffer == NULL) {
			error = ENOMEM;
			goto fail;
		}
		strlcpy(demo_buffer, "Hello from a disciplined driver!\n",
		    DEMO_BUFSZ);

		/* Step 2: create the device node. */
		demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0666, "demo");
		if (demo_dev == NULL) {
			printf("demo: make_dev failed\n");
			error = ENXIO;
			goto fail;
		}

		printf("demo: /dev/demo created\n");
		return (0);

	case MOD_UNLOAD:
		/*
		 * Tear down in reverse order of creation. Each step
		 * checks for NULL first so MOD_UNLOAD can recover
		 * cleanly from a partial MOD_LOAD (e.g. make_dev
		 * failed after malloc succeeded).
		 */
		if (demo_dev != NULL) {
			destroy_dev(demo_dev);
			demo_dev = NULL;
		}
		if (demo_buffer != NULL) {
			free(demo_buffer, M_DEMO);
			demo_buffer = NULL;
		}
		printf("demo: /dev/demo destroyed cleanly\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}

fail:
	/*
	 * Centralised error unwinding for MOD_LOAD. Every allocation
	 * above walks back through this block on the way out.
	 */
	if (demo_dev != NULL) {
		destroy_dev(demo_dev);
		demo_dev = NULL;
	}
	if (demo_buffer != NULL) {
		free(demo_buffer, M_DEMO);
		demo_buffer = NULL;
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
