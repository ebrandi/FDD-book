/*
 * sync_bench.c - in-kernel microbenchmark for FreeBSD synchronization
 * primitives.
 *
 * Builds as a loadable kernel module. Exposes a small set of sysctls
 * under debug.sync_bench.* that trigger benchmarks and report
 * results.
 *
 * Writes trigger the benchmark; reads expose last measured nanosecond
 * average. Iterations are fixed per benchmark and reported in the
 * module's dmesg output.
 *
 * Companion to Appendix F of "FreeBSD Device Drivers: From First
 * Steps to Kernel Mastery".
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/condvar.h>
#include <sys/sema.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/time.h>

#define	SB_MTX_ITER	1000000ULL
#define	SB_SX_ITER	1000000ULL
#define	SB_CV_ITER	   10000ULL
#define	SB_SEMA_ITER	   10000ULL

static struct mtx	sb_mtx;
static struct sx	sb_sx;
static struct mtx	sb_cv_mtx;
static struct cv	sb_cv_ping;
static struct cv	sb_cv_pong;
static struct sema	sb_sema_ping;
static struct sema	sb_sema_pong;

static uint64_t		sb_last_ns_mtx;
static uint64_t		sb_last_ns_sx_slock;
static uint64_t		sb_last_ns_sx_xlock;
static uint64_t		sb_last_ns_cv;
static uint64_t		sb_last_ns_sema;

static volatile int	sb_cv_running;
static volatile int	sb_sema_running;
static struct proc	*sb_cv_worker;
static struct proc	*sb_sema_worker;

static uint64_t
sbt_to_ns(sbintime_t sbt)
{
	return ((uint64_t)sbttons(sbt));
}

static void
sb_run_mtx(void)
{
	sbintime_t start, end;
	uint64_t i;

	start = sbinuptime();
	for (i = 0; i < SB_MTX_ITER; i++) {
		mtx_lock(&sb_mtx);
		mtx_unlock(&sb_mtx);
	}
	end = sbinuptime();
	sb_last_ns_mtx = sbt_to_ns(end - start) / SB_MTX_ITER;
	printf("sync_bench: mtx uncontended avg=%ju ns (%ju iter)\n",
	    (uintmax_t)sb_last_ns_mtx, (uintmax_t)SB_MTX_ITER);
}

static void
sb_run_sx_slock(void)
{
	sbintime_t start, end;
	uint64_t i;

	start = sbinuptime();
	for (i = 0; i < SB_SX_ITER; i++) {
		sx_slock(&sb_sx);
		sx_sunlock(&sb_sx);
	}
	end = sbinuptime();
	sb_last_ns_sx_slock = sbt_to_ns(end - start) / SB_SX_ITER;
	printf("sync_bench: sx_slock uncontended avg=%ju ns (%ju iter)\n",
	    (uintmax_t)sb_last_ns_sx_slock, (uintmax_t)SB_SX_ITER);
}

static void
sb_run_sx_xlock(void)
{
	sbintime_t start, end;
	uint64_t i;

	start = sbinuptime();
	for (i = 0; i < SB_SX_ITER; i++) {
		sx_xlock(&sb_sx);
		sx_xunlock(&sb_sx);
	}
	end = sbinuptime();
	sb_last_ns_sx_xlock = sbt_to_ns(end - start) / SB_SX_ITER;
	printf("sync_bench: sx_xlock uncontended avg=%ju ns (%ju iter)\n",
	    (uintmax_t)sb_last_ns_sx_xlock, (uintmax_t)SB_SX_ITER);
}

static void
sb_cv_worker_loop(void *arg __unused)
{
	mtx_lock(&sb_cv_mtx);
	while (sb_cv_running) {
		cv_wait(&sb_cv_ping, &sb_cv_mtx);
		if (!sb_cv_running)
			break;
		cv_signal(&sb_cv_pong);
	}
	mtx_unlock(&sb_cv_mtx);
	kproc_exit(0);
}

static void
sb_run_cv(void)
{
	sbintime_t start, end;
	uint64_t i;
	int err;

	sb_cv_running = 1;
	err = kproc_create(sb_cv_worker_loop, NULL, &sb_cv_worker, 0, 0,
	    "sync_bench_cv");
	if (err != 0) {
		printf("sync_bench: kproc_create failed: %d\n", err);
		return;
	}

	/* Let the worker reach cv_wait. */
	pause("sbcvup", hz / 10);

	mtx_lock(&sb_cv_mtx);
	start = sbinuptime();
	for (i = 0; i < SB_CV_ITER; i++) {
		cv_signal(&sb_cv_ping);
		cv_wait(&sb_cv_pong, &sb_cv_mtx);
	}
	end = sbinuptime();

	sb_cv_running = 0;
	cv_broadcast(&sb_cv_ping);
	mtx_unlock(&sb_cv_mtx);

	/* Wait for the worker to exit. */
	pause("sbcvdn", hz / 10);

	sb_last_ns_cv = sbt_to_ns(end - start) / SB_CV_ITER;
	printf("sync_bench: cv round-trip avg=%ju ns (%ju iter)\n",
	    (uintmax_t)sb_last_ns_cv, (uintmax_t)SB_CV_ITER);
}

static void
sb_sema_worker_loop(void *arg __unused)
{
	while (sb_sema_running) {
		sema_wait(&sb_sema_ping);
		if (!sb_sema_running)
			break;
		sema_post(&sb_sema_pong);
	}
	kproc_exit(0);
}

static void
sb_run_sema(void)
{
	sbintime_t start, end;
	uint64_t i;
	int err;

	sb_sema_running = 1;
	err = kproc_create(sb_sema_worker_loop, NULL, &sb_sema_worker, 0, 0,
	    "sync_bench_sema");
	if (err != 0) {
		printf("sync_bench: kproc_create failed: %d\n", err);
		return;
	}

	/* Let the worker reach sema_wait. */
	pause("sbseup", hz / 10);

	start = sbinuptime();
	for (i = 0; i < SB_SEMA_ITER; i++) {
		sema_post(&sb_sema_ping);
		sema_wait(&sb_sema_pong);
	}
	end = sbinuptime();

	sb_sema_running = 0;
	sema_post(&sb_sema_ping);

	pause("sbsedn", hz / 10);

	sb_last_ns_sema = sbt_to_ns(end - start) / SB_SEMA_ITER;
	printf("sync_bench: sema round-trip avg=%ju ns (%ju iter)\n",
	    (uintmax_t)sb_last_ns_sema, (uintmax_t)SB_SEMA_ITER);
}

/* sysctl: writing non-zero triggers a run; reading returns 0. */
#define	SB_TRIGGER(name, runfn)						\
static int								\
sb_sysctl_run_##name(SYSCTL_HANDLER_ARGS)				\
{									\
	int val = 0;							\
	int error;							\
									\
	error = sysctl_handle_int(oidp, &val, 0, req);			\
	if (error != 0 || req->newptr == NULL)				\
		return (error);						\
	if (val != 0)							\
		runfn();						\
	return (0);							\
}

SB_TRIGGER(mtx, sb_run_mtx)
SB_TRIGGER(sx_slock, sb_run_sx_slock)
SB_TRIGGER(sx_xlock, sb_run_sx_xlock)
SB_TRIGGER(cv, sb_run_cv)
SB_TRIGGER(sema, sb_run_sema)

static SYSCTL_NODE(_debug, OID_AUTO, sync_bench,
    CTLFLAG_RD | CTLFLAG_MPSAFE, NULL,
    "Appendix F synchronization-primitive benchmarks");

SYSCTL_PROC(_debug_sync_bench, OID_AUTO, run_mtx,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, NULL, 0,
    sb_sysctl_run_mtx, "I", "Run mtx uncontended benchmark");
SYSCTL_PROC(_debug_sync_bench, OID_AUTO, run_sx_slock,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, NULL, 0,
    sb_sysctl_run_sx_slock, "I", "Run sx_slock uncontended benchmark");
SYSCTL_PROC(_debug_sync_bench, OID_AUTO, run_sx_xlock,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, NULL, 0,
    sb_sysctl_run_sx_xlock, "I", "Run sx_xlock uncontended benchmark");
SYSCTL_PROC(_debug_sync_bench, OID_AUTO, run_cv,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, NULL, 0,
    sb_sysctl_run_cv, "I", "Run cv round-trip benchmark");
SYSCTL_PROC(_debug_sync_bench, OID_AUTO, run_sema,
    CTLTYPE_INT | CTLFLAG_WR | CTLFLAG_MPSAFE, NULL, 0,
    sb_sysctl_run_sema, "I", "Run sema round-trip benchmark");

SYSCTL_QUAD(_debug_sync_bench, OID_AUTO, last_ns_mtx, CTLFLAG_RD,
    &sb_last_ns_mtx, 0, "Average ns per mtx_lock/unlock");
SYSCTL_QUAD(_debug_sync_bench, OID_AUTO, last_ns_sx_slock, CTLFLAG_RD,
    &sb_last_ns_sx_slock, 0, "Average ns per sx_slock/sunlock");
SYSCTL_QUAD(_debug_sync_bench, OID_AUTO, last_ns_sx_xlock, CTLFLAG_RD,
    &sb_last_ns_sx_xlock, 0, "Average ns per sx_xlock/xunlock");
SYSCTL_QUAD(_debug_sync_bench, OID_AUTO, last_ns_cv, CTLFLAG_RD,
    &sb_last_ns_cv, 0, "Average ns per cv round-trip");
SYSCTL_QUAD(_debug_sync_bench, OID_AUTO, last_ns_sema, CTLFLAG_RD,
    &sb_last_ns_sema, 0, "Average ns per sema round-trip");

static int
sync_bench_load(module_t m __unused, int what, void *arg __unused)
{
	switch (what) {
	case MOD_LOAD:
		mtx_init(&sb_mtx, "sync_bench_mtx", NULL, MTX_DEF);
		sx_init(&sb_sx, "sync_bench_sx");
		mtx_init(&sb_cv_mtx, "sync_bench_cv_mtx", NULL, MTX_DEF);
		cv_init(&sb_cv_ping, "sync_bench_cv_ping");
		cv_init(&sb_cv_pong, "sync_bench_cv_pong");
		sema_init(&sb_sema_ping, 0, "sync_bench_sema_ping");
		sema_init(&sb_sema_pong, 0, "sync_bench_sema_pong");
		printf("sync_bench: loaded\n");
		return (0);
	case MOD_UNLOAD:
		sema_destroy(&sb_sema_ping);
		sema_destroy(&sb_sema_pong);
		cv_destroy(&sb_cv_ping);
		cv_destroy(&sb_cv_pong);
		mtx_destroy(&sb_cv_mtx);
		sx_destroy(&sb_sx);
		mtx_destroy(&sb_mtx);
		printf("sync_bench: unloaded\n");
		return (0);
	}
	return (EOPNOTSUPP);
}

static moduledata_t sync_bench_mod = {
	"sync_bench",
	sync_bench_load,
	NULL,
};

DECLARE_MODULE(sync_bench, sync_bench_mod, SI_SUB_KLD, SI_ORDER_ANY);
MODULE_VERSION(sync_bench, 1);
