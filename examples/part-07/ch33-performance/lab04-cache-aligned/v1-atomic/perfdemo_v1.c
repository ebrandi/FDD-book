/*
 * perfdemo_v1 - variant using a shared atomic counter.
 *
 * This is the intentionally-bad baseline for Lab 4. A single
 * atomic_add_64 is updated on every read from every CPU, producing
 * cache-line ping-pong under concurrent load.
 *
 * Companion example for Chapter 33.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <machine/atomic.h>

static d_read_t perfdemo_v1_read;

static struct cdevsw perfdemo_v1_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "perfdemo_v1",
	.d_read    = perfdemo_v1_read,
};

static struct cdev *perfdemo_v1_dev;

static volatile uint64_t perfdemo_v1_reads;

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo_v1,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo_v1 driver");

SYSCTL_U64(_hw_perfdemo_v1, OID_AUTO, reads,
    CTLFLAG_RD, __DEVOLATILE(uint64_t *, &perfdemo_v1_reads), 0,
    "Total number of reads (shared atomic)");

static int
perfdemo_v1_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	static const char zero_buf[4096] = { 0 };
	size_t amt;
	int error;

	amt = MIN(uio->uio_resid, sizeof(zero_buf));
	if (amt == 0)
		return (0);

	error = uiomove(__DECONST(void *, zero_buf), amt, uio);
	if (error == 0)
		atomic_add_64(&perfdemo_v1_reads, 1);
	return (error);
}

static int
perfdemo_v1_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		perfdemo_v1_dev = make_dev(&perfdemo_v1_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "perfdemo_v1");
		if (perfdemo_v1_dev == NULL)
			return (ENOMEM);
		printf("perfdemo_v1: loaded (shared atomic variant)\n");
		return (0);
	case MOD_UNLOAD:
		if (perfdemo_v1_dev != NULL)
			destroy_dev(perfdemo_v1_dev);
		printf("perfdemo_v1: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t perfdemo_v1_mod = {
	"perfdemo_v1",
	perfdemo_v1_modevent,
	NULL
};

DECLARE_MODULE(perfdemo_v1, perfdemo_v1_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo_v1, 1);
