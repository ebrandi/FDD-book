/*
 * perfdemo_v3 - variant using the counter(9) API.
 *
 * This is the recommended shape. counter(9) handles per-CPU storage,
 * alignment, and the read-time sum; the driver simply calls
 * counter_u64_add() on the hot path. The API is also type-safe and
 * well-tested.
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
#include <sys/counter.h>

static d_read_t perfdemo_v3_read;

static struct cdevsw perfdemo_v3_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "perfdemo_v3",
	.d_read    = perfdemo_v3_read,
};

static struct cdev *perfdemo_v3_dev;

static counter_u64_t perfdemo_v3_reads;

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo_v3,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo_v3 driver");

static int
perfdemo_v3_sysctl_reads(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(perfdemo_v3_reads);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

SYSCTL_PROC(_hw_perfdemo_v3, OID_AUTO, reads,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_v3_sysctl_reads, "QU",
    "Total number of reads (counter(9))");

static int
perfdemo_v3_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	static const char zero_buf[4096] = { 0 };
	size_t amt;
	int error;

	amt = MIN(uio->uio_resid, sizeof(zero_buf));
	if (amt == 0)
		return (0);

	error = uiomove(__DECONST(void *, zero_buf), amt, uio);
	if (error == 0)
		counter_u64_add(perfdemo_v3_reads, 1);
	return (error);
}

static int
perfdemo_v3_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		perfdemo_v3_reads = counter_u64_alloc(M_WAITOK);
		perfdemo_v3_dev = make_dev(&perfdemo_v3_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "perfdemo_v3");
		if (perfdemo_v3_dev == NULL) {
			counter_u64_free(perfdemo_v3_reads);
			return (ENOMEM);
		}
		printf("perfdemo_v3: loaded (counter(9) variant)\n");
		return (0);
	case MOD_UNLOAD:
		if (perfdemo_v3_dev != NULL)
			destroy_dev(perfdemo_v3_dev);
		counter_u64_free(perfdemo_v3_reads);
		printf("perfdemo_v3: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t perfdemo_v3_mod = {
	"perfdemo_v3",
	perfdemo_v3_modevent,
	NULL
};

DECLARE_MODULE(perfdemo_v3, perfdemo_v3_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo_v3, 1);
