/*
 * perfdemo_tq2 - variant that defers heavy work to a taskqueue.
 *
 * The callout fires at 1 ms intervals and enqueues a task on a
 * dedicated taskqueue. The task performs the work in a kernel
 * thread context, which does not block the callout wheel.
 *
 * This is the recommended pattern for Lab 5: keep the callout or
 * interrupt handler light, move heavy work to a taskqueue.
 *
 * Companion example for Chapter 33.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/counter.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

static struct callout perfdemo_tq2_co;
static struct taskqueue *perfdemo_tq2_tq;
static struct task perfdemo_tq2_task;
static counter_u64_t perfdemo_tq2_ticks;
static counter_u64_t perfdemo_tq2_runs;
static counter_u64_t perfdemo_tq2_cycles;
static volatile uint64_t perfdemo_tq2_running;

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo_tq2,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo_tq2 driver");

static int
perfdemo_tq2_sysctl_ticks(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(perfdemo_tq2_ticks);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

SYSCTL_PROC(_hw_perfdemo_tq2, OID_AUTO, ticks,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_tq2_sysctl_ticks, "QU",
    "Callout ticks fired since load");

static int
perfdemo_tq2_sysctl_runs(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(perfdemo_tq2_runs);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

SYSCTL_PROC(_hw_perfdemo_tq2, OID_AUTO, runs,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_tq2_sysctl_runs, "QU",
    "Taskqueue runs since load");

static void
perfdemo_tq2_fake_work(void *arg, int pending)
{
	volatile uint64_t x;
	int i;

	counter_u64_add(perfdemo_tq2_runs, 1);
	for (i = 0; i < 20000; i++)
		x += i;
	counter_u64_add(perfdemo_tq2_cycles, i);
}

static void
perfdemo_tq2_callout(void *arg)
{
	counter_u64_add(perfdemo_tq2_ticks, 1);
	taskqueue_enqueue(perfdemo_tq2_tq, &perfdemo_tq2_task);
	if (perfdemo_tq2_running)
		callout_schedule(&perfdemo_tq2_co, 1);
}

static int
perfdemo_tq2_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		perfdemo_tq2_ticks = counter_u64_alloc(M_WAITOK);
		perfdemo_tq2_runs = counter_u64_alloc(M_WAITOK);
		perfdemo_tq2_cycles = counter_u64_alloc(M_WAITOK);
		perfdemo_tq2_tq = taskqueue_create("perfdemo_tq2",
		    M_WAITOK, taskqueue_thread_enqueue,
		    &perfdemo_tq2_tq);
		taskqueue_start_threads(&perfdemo_tq2_tq, 1,
		    PI_SOFT, "perfdemo_tq2");
		TASK_INIT(&perfdemo_tq2_task, 0,
		    perfdemo_tq2_fake_work, NULL);
		callout_init(&perfdemo_tq2_co, 1);
		perfdemo_tq2_running = 1;
		callout_reset(&perfdemo_tq2_co, 1,
		    perfdemo_tq2_callout, NULL);
		printf("perfdemo_tq2: loaded (taskqueue variant)\n");
		return (0);
	case MOD_UNLOAD:
		perfdemo_tq2_running = 0;
		callout_drain(&perfdemo_tq2_co);
		taskqueue_drain(perfdemo_tq2_tq, &perfdemo_tq2_task);
		taskqueue_free(perfdemo_tq2_tq);
		counter_u64_free(perfdemo_tq2_ticks);
		counter_u64_free(perfdemo_tq2_runs);
		counter_u64_free(perfdemo_tq2_cycles);
		printf("perfdemo_tq2: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t perfdemo_tq2_mod = {
	"perfdemo_tq2",
	perfdemo_tq2_modevent,
	NULL
};

DECLARE_MODULE(perfdemo_tq2, perfdemo_tq2_mod, SI_SUB_DRIVERS,
    SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo_tq2, 1);
