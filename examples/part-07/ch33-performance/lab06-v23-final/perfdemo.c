/*
 * perfdemo v2.3 - the shipped, optimised pseudo-device.
 *
 * This is the lab-6 final that combines the best techniques from
 * the earlier labs:
 *   - counter(9) for per-CPU counters (reads, bytes).
 *   - Latency histogram exposed via sysctl.
 *   - Minimum instrumentation on the hot path.
 *   - Versioned (MODULE_VERSION == 23).
 *   - Stable sysctl interface under hw.perfdemo.
 *   - Debug mode controllable at runtime.
 *
 * Companion example for Chapter 33.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/counter.h>
#include <sys/time.h>

#define PD_HIST_BUCKETS 8

static d_read_t  perfdemo_read;
static d_open_t  perfdemo_open;
static d_close_t perfdemo_close;

static struct cdevsw perfdemo_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "perfdemo",
	.d_open    = perfdemo_open,
	.d_close   = perfdemo_close,
	.d_read    = perfdemo_read,
};

static struct cdev *perfdemo_dev;

/* Latency-bucket upper bounds, in nanoseconds. */
static const uint64_t perfdemo_hist_bounds_ns[PD_HIST_BUCKETS] = {
	1000ULL,             /* <= 1us */
	10000ULL,            /* <= 10us */
	100000ULL,           /* <= 100us */
	1000000ULL,          /* <= 1ms */
	10000000ULL,         /* <= 10ms */
	100000000ULL,        /* <= 100ms */
	1000000000ULL,       /* <= 1s */
	UINT64_MAX,          /* > 1s */
};

static counter_u64_t perfdemo_reads;
static counter_u64_t perfdemo_bytes;
static counter_u64_t perfdemo_hist[PD_HIST_BUCKETS];

static int perfdemo_debug = 0;

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo driver");

SYSCTL_INT(_hw_perfdemo, OID_AUTO, debug,
    CTLFLAG_RWTUN, &perfdemo_debug, 0,
    "Enable debug printf from read path (0 = off)");

static int
perfdemo_sysctl_reads(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(perfdemo_reads);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

static int
perfdemo_sysctl_bytes(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(perfdemo_bytes);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

SYSCTL_PROC(_hw_perfdemo, OID_AUTO, reads,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_reads, "QU",
    "Total number of reads served");

SYSCTL_PROC(_hw_perfdemo, OID_AUTO, bytes,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_bytes, "QU",
    "Total bytes returned");

static int
perfdemo_sysctl_hist(SYSCTL_HANDLER_ARGS)
{
	uint64_t snap[PD_HIST_BUCKETS];
	int i;

	for (i = 0; i < PD_HIST_BUCKETS; i++)
		snap[i] = counter_u64_fetch(perfdemo_hist[i]);
	return (SYSCTL_OUT(req, snap, sizeof(snap)));
}

SYSCTL_PROC(_hw_perfdemo, OID_AUTO, latency_histogram,
    CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_hist, "",
    "Read latency histogram (8 x uint64 ns buckets)");

static int
perfdemo_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return (0);
}

static int
perfdemo_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	return (0);
}

static inline int
perfdemo_bucket(uint64_t ns)
{
	int i;

	for (i = 0; i < PD_HIST_BUCKETS - 1; i++) {
		if (ns <= perfdemo_hist_bounds_ns[i])
			return (i);
	}
	return (PD_HIST_BUCKETS - 1);
}

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	static const char zero_buf[4096] = { 0 };
	size_t amt;
	int error, bucket;
	struct bintime start, end;
	uint64_t elapsed_ns;

	amt = MIN(uio->uio_resid, sizeof(zero_buf));
	if (amt == 0)
		return (0);

	binuptime(&start);
	error = uiomove(__DECONST(void *, zero_buf), amt, uio);
	binuptime(&end);

	if (error == 0) {
		counter_u64_add(perfdemo_reads, 1);
		counter_u64_add(perfdemo_bytes, amt);

		bintime_sub(&end, &start);
		elapsed_ns = (uint64_t)end.sec * 1000000000ULL +
		    ((uint64_t)(end.frac >> 32) * 1000000000ULL) /
		    ((uint64_t)1 << 32);
		bucket = perfdemo_bucket(elapsed_ns);
		counter_u64_add(perfdemo_hist[bucket], 1);

		if (perfdemo_debug > 0) {
			log(LOG_DEBUG, "perfdemo: read %zu bytes in %llu ns\n",
			    amt, (unsigned long long)elapsed_ns);
		}
	}
	return (error);
}

static int
perfdemo_modevent(module_t mod, int what, void *arg)
{
	int i;

	switch (what) {
	case MOD_LOAD:
		perfdemo_reads = counter_u64_alloc(M_WAITOK);
		perfdemo_bytes = counter_u64_alloc(M_WAITOK);
		for (i = 0; i < PD_HIST_BUCKETS; i++)
			perfdemo_hist[i] = counter_u64_alloc(M_WAITOK);
		perfdemo_dev = make_dev(&perfdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "perfdemo");
		if (perfdemo_dev == NULL) {
			counter_u64_free(perfdemo_reads);
			counter_u64_free(perfdemo_bytes);
			for (i = 0; i < PD_HIST_BUCKETS; i++)
				counter_u64_free(perfdemo_hist[i]);
			return (ENOMEM);
		}
		printf("perfdemo v2.3: loaded\n");
		return (0);
	case MOD_UNLOAD:
		if (perfdemo_dev != NULL)
			destroy_dev(perfdemo_dev);
		counter_u64_free(perfdemo_reads);
		counter_u64_free(perfdemo_bytes);
		for (i = 0; i < PD_HIST_BUCKETS; i++)
			counter_u64_free(perfdemo_hist[i]);
		printf("perfdemo v2.3: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t perfdemo_mod = {
	"perfdemo",
	perfdemo_modevent,
	NULL
};

DECLARE_MODULE(perfdemo, perfdemo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo, 23);
