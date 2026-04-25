/*
 * perfdemo_v2 - variant using a manually-aligned per-CPU array.
 *
 * This variant uses an explicit per-CPU counter array with each
 * element aligned to a cache line to prevent false sharing between
 * counters on adjacent CPUs.
 *
 * The summing happens only on read (not on every update), so the
 * hot path has no cross-CPU coordination.
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
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/pcpu.h>

static d_read_t perfdemo_v2_read;

static struct cdevsw perfdemo_v2_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "perfdemo_v2",
	.d_read    = perfdemo_v2_read,
};

static struct cdev *perfdemo_v2_dev;

struct perfdemo_v2_pc {
	uint64_t reads;
} __aligned(CACHE_LINE_SIZE);

static struct perfdemo_v2_pc perfdemo_v2_pc[MAXCPU];

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo_v2,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo_v2 driver");

static int
perfdemo_v2_sysctl_reads(SYSCTL_HANDLER_ARGS)
{
	uint64_t sum = 0;
	int i;

	CPU_FOREACH(i) {
		sum += perfdemo_v2_pc[i].reads;
	}
	return (sysctl_handle_64(oidp, &sum, 0, req));
}

SYSCTL_PROC(_hw_perfdemo_v2, OID_AUTO, reads,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_v2_sysctl_reads, "QU",
    "Total number of reads (manually-aligned per-CPU)");

static int
perfdemo_v2_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	static const char zero_buf[4096] = { 0 };
	size_t amt;
	int error, cpu;

	amt = MIN(uio->uio_resid, sizeof(zero_buf));
	if (amt == 0)
		return (0);

	error = uiomove(__DECONST(void *, zero_buf), amt, uio);
	if (error == 0) {
		cpu = PCPU_GET(cpuid);
		perfdemo_v2_pc[cpu].reads++;
	}
	return (error);
}

static int
perfdemo_v2_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		memset(perfdemo_v2_pc, 0, sizeof(perfdemo_v2_pc));
		perfdemo_v2_dev = make_dev(&perfdemo_v2_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "perfdemo_v2");
		if (perfdemo_v2_dev == NULL)
			return (ENOMEM);
		printf("perfdemo_v2: loaded (aligned per-CPU variant)\n");
		return (0);
	case MOD_UNLOAD:
		if (perfdemo_v2_dev != NULL)
			destroy_dev(perfdemo_v2_dev);
		printf("perfdemo_v2: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t perfdemo_v2_mod = {
	"perfdemo_v2",
	perfdemo_v2_modevent,
	NULL
};

DECLARE_MODULE(perfdemo_v2, perfdemo_v2_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo_v2, 1);
