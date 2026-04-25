/*
 * perfdemo - pseudo-device used to illustrate performance measurement
 *            and tuning techniques in FreeBSD device drivers.
 *
 * Lab 1: baseline. Implements a trivial read that returns zero bytes and
 *        counts reads and bytes with counter(9). Exposes totals via sysctl.
 *
 * This is a companion example for Chapter 33 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery."
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

static counter_u64_t perfdemo_reads;
static counter_u64_t perfdemo_bytes;

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo driver");

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
    "Total number of reads served since load");

SYSCTL_PROC(_hw_perfdemo, OID_AUTO, bytes,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_sysctl_bytes, "QU",
    "Total bytes returned to readers since load");

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

static int
perfdemo_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	size_t amt;
	static const char zero_buf[4096] = { 0 };
	int error;

	amt = MIN(uio->uio_resid, sizeof(zero_buf));
	if (amt == 0)
		return (0);

	error = uiomove(__DECONST(void *, zero_buf), amt, uio);
	if (error == 0) {
		counter_u64_add(perfdemo_reads, 1);
		counter_u64_add(perfdemo_bytes, amt);
	}
	return (error);
}

static int
perfdemo_modevent(module_t mod, int what, void *arg)
{
	int error = 0;

	switch (what) {
	case MOD_LOAD:
		perfdemo_reads = counter_u64_alloc(M_WAITOK);
		perfdemo_bytes = counter_u64_alloc(M_WAITOK);
		perfdemo_dev = make_dev(&perfdemo_cdevsw, 0,
		    UID_ROOT, GID_WHEEL, 0666, "perfdemo");
		if (perfdemo_dev == NULL) {
			counter_u64_free(perfdemo_reads);
			counter_u64_free(perfdemo_bytes);
			error = ENOMEM;
		} else {
			printf("perfdemo: loaded, device node created\n");
		}
		break;
	case MOD_UNLOAD:
		if (perfdemo_dev != NULL)
			destroy_dev(perfdemo_dev);
		counter_u64_free(perfdemo_reads);
		counter_u64_free(perfdemo_bytes);
		printf("perfdemo: unloaded\n");
		break;
	default:
		error = EOPNOTSUPP;
	}
	return (error);
}

static moduledata_t perfdemo_mod = {
	"perfdemo",
	perfdemo_modevent,
	NULL
};

DECLARE_MODULE(perfdemo, perfdemo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo, 1);
