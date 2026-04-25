/*
 * demo.c - DELIBERATELY BROKEN version for Chapter 6, Lab 4.
 *
 * This is Experiment 1A from the chapter: the MOD_UNLOAD handler
 * forgets to call destroy_dev(). That single missing line leaves
 * /dev/demo orphaned in the filesystem after the module is unloaded.
 *
 *   DO NOT USE THIS FILE AS A REFERENCE FOR REAL DRIVERS.
 *   It is included only so you can reproduce the failure the
 *   chapter describes, then contrast it with the fixed version
 *   in ../fixed/demo.c.
 *
 * Expected observation:
 *
 *   # make && sudo kldload ./demo.ko
 *   # ls -l /dev/demo              # present
 *   # sudo kldunload demo
 *   # ls -l /dev/demo              # STILL PRESENT -- bug!
 *   # cat /dev/demo                # undefined behaviour
 *
 * Reboot the lab VM when you are done observing the leak.
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/proc.h>

static struct cdev *demo_dev = NULL;

static int
demo_read(struct cdev *dev __unused, struct uio *uio, int ioflag __unused)
{
	char message[] = "Hello from broken demo!\n";
	size_t len = MIN(uio->uio_resid, sizeof(message) - 1);

	return (uiomove(message, len, uio));
}

static struct cdevsw demo_cdevsw = {
	.d_version =	D_VERSION,
	.d_read =	demo_read,
	.d_name =	"demo",
};

static int
demo_modevent(module_t mod __unused, int event, void *arg __unused)
{

	switch (event) {
	case MOD_LOAD:
		demo_dev = make_dev(&demo_cdevsw, 0, UID_ROOT, GID_WHEEL,
		    0666, "demo");
		if (demo_dev == NULL) {
			printf("demo: Failed to create /dev/demo\n");
			return (ENXIO);
		}
		printf("demo: /dev/demo created (broken example)\n");
		break;

	case MOD_UNLOAD:
		/*
		 * THE BUG: we clear the pointer and log a lie, but we
		 * never call destroy_dev(). The kernel still has a
		 * cdev registered, /dev/demo still exists, and nothing
		 * will clean up until the system reboots.
		 */
		if (demo_dev != NULL) {
			/* destroy_dev(demo_dev);  <-- missing */
			demo_dev = NULL;
			printf("demo: /dev/demo destroyed (lie!)\n");
		}
		break;

	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t demo_mod = {
	"demo",
	demo_modevent,
	NULL
};

DECLARE_MODULE(demo, demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(demo, 1);
