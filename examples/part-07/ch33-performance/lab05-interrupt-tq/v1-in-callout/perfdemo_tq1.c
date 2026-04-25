/*
 * perfdemo_tq1 - variant that does heavy work inside a callout.
 *
 * The callout fires at 1 ms intervals and, for demonstration,
 * spends some cycles "processing" before returning. The problem:
 * the callout runs at a high scheduling priority and can delay
 * other callouts if its work expands.
 *
 * This is the anti-pattern variant for Lab 5.
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

static struct callout perfdemo_tq1_co;
static counter_u64_t perfdemo_tq1_ticks;
static counter_u64_t perfdemo_tq1_cycles;
static volatile uint64_t perfdemo_tq1_running;

static SYSCTL_NODE(_hw, OID_AUTO, perfdemo_tq1,
    CTLFLAG_RD | CTLFLAG_MPSAFE, 0, "perfdemo_tq1 driver");

static int
perfdemo_tq1_sysctl_ticks(SYSCTL_HANDLER_ARGS)
{
	uint64_t v = counter_u64_fetch(perfdemo_tq1_ticks);
	return (sysctl_handle_64(oidp, &v, 0, req));
}

SYSCTL_PROC(_hw_perfdemo_tq1, OID_AUTO, ticks,
    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
    NULL, 0, perfdemo_tq1_sysctl_ticks, "QU",
    "Callout ticks fired since load");

static void
perfdemo_tq1_fake_work(void)
{
	volatile uint64_t x;
	int i;

	/* Pretend to do work. */
	for (i = 0; i < 20000; i++)
		x += i;
	counter_u64_add(perfdemo_tq1_cycles, i);
}

static void
perfdemo_tq1_callout(void *arg)
{
	counter_u64_add(perfdemo_tq1_ticks, 1);
	perfdemo_tq1_fake_work();
	if (perfdemo_tq1_running)
		callout_schedule(&perfdemo_tq1_co, 1);
}

static int
perfdemo_tq1_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		perfdemo_tq1_ticks = counter_u64_alloc(M_WAITOK);
		perfdemo_tq1_cycles = counter_u64_alloc(M_WAITOK);
		callout_init(&perfdemo_tq1_co, 1);
		perfdemo_tq1_running = 1;
		callout_reset(&perfdemo_tq1_co, 1,
		    perfdemo_tq1_callout, NULL);
		printf("perfdemo_tq1: loaded (work-in-callout variant)\n");
		return (0);
	case MOD_UNLOAD:
		perfdemo_tq1_running = 0;
		callout_drain(&perfdemo_tq1_co);
		counter_u64_free(perfdemo_tq1_ticks);
		counter_u64_free(perfdemo_tq1_cycles);
		printf("perfdemo_tq1: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t perfdemo_tq1_mod = {
	"perfdemo_tq1",
	perfdemo_tq1_modevent,
	NULL
};

DECLARE_MODULE(perfdemo_tq1, perfdemo_tq1_mod, SI_SUB_DRIVERS,
    SI_ORDER_MIDDLE);
MODULE_VERSION(perfdemo_tq1, 1);
