/*
 * logging_safe.c - Kernel logging practices for Chapter 5, Lab 3.
 *
 * This module demonstrates the three most common logging primitives
 * (printf(9), device_printf(9), uprintf(9)) and the discipline that
 * goes with them: include context, avoid spam, and rate-limit
 * repetitive messages. Because this module is not attached to a real
 * device_t, we simulate a softc and log via plain printf() with a
 * device-style prefix.
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bus.h>

MALLOC_DEFINE(M_LOGLAB, "log_lab", "Logging Lab Allocations");

struct log_lab_softc {
	char	device_name[32];
	int	message_count;
	int	error_count;
};

static struct log_lab_softc	*lab_softc = NULL;
static struct callout		 lab_timer;

static void
demo_printf_variants(struct log_lab_softc *sc)
{

	printf("Log Lab: General kernel message (printf)\n");
	printf("Log Lab: [%s] Simulated device_printf message\n",
	    sc->device_name);
	printf("Log Lab: [%s] Device message count: %d\n",
	    sc->device_name, ++sc->message_count);

	printf("Log Lab: INFO - Normal operation message\n");
	printf("Log Lab: WARNING - Something unusual happened\n");
	printf("Log Lab: ERROR - Operation failed, count=%d\n",
	    ++sc->error_count);

	printf("Log Lab: [%s] status: messages=%d errors=%d ticks=%d\n",
	    sc->device_name, sc->message_count, sc->error_count, ticks);
}

static void
demo_logging_safety(struct log_lab_softc *sc)
{
	static int call_count = 0;

	call_count++;

	/* Rate-limit chatter so the kernel log doesn't fill up. */
	if (call_count <= 5 || (call_count % 100) == 0) {
		printf("Log Lab: [%s] Safety demo call #%d\n",
		    sc->device_name, call_count);
	}

	if (sc->error_count > 3) {
		printf("Log Lab: [%s] ERROR threshold exceeded: %d errors\n",
		    sc->device_name, sc->error_count);
	}

	if ((call_count % 10) == 0) {
		printf("Log Lab: [%s] Operational status: %d operations completed\n",
		    sc->device_name, call_count);
	}
}

static void
lab_timer_cb(void *arg)
{
	struct log_lab_softc *sc = arg;

	if (sc != NULL) {
		printf("Log Lab: [%s] Timer tick\n", sc->device_name);
		demo_printf_variants(sc);
		demo_logging_safety(sc);

		/* Re-arm for another 5 seconds. */
		callout_reset(&lab_timer, hz * 5, lab_timer_cb, sc);
	}
}

static int
logging_safe_handler(module_t mod __unused, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
		printf("Log Lab: Module loading\n");

		lab_softc = malloc(sizeof(*lab_softc), M_LOGLAB,
		    M_WAITOK | M_ZERO);
		strlcpy(lab_softc->device_name, "loglab0",
		    sizeof(lab_softc->device_name));

		demo_printf_variants(lab_softc);

		callout_init(&lab_timer, 0);
		callout_reset(&lab_timer, hz * 5, lab_timer_cb, lab_softc);

		printf("Log Lab: [%s] Module loaded; timer started\n",
		    lab_softc->device_name);
		return (0);

	case MOD_UNLOAD:
		printf("Log Lab: Module unloading\n");

		/* Stop the timer and wait for any in-flight callback. */
		callout_drain(&lab_timer);

		if (lab_softc != NULL) {
			printf("Log Lab: [%s] Final stats: messages=%d errors=%d\n",
			    lab_softc->device_name,
			    lab_softc->message_count,
			    lab_softc->error_count);
			free(lab_softc, M_LOGLAB);
			lab_softc = NULL;
		}

		printf("Log Lab: Module unloaded successfully\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t logging_safe_mod = {
	"logging_safe",
	logging_safe_handler,
	NULL
};

DECLARE_MODULE(logging_safe, logging_safe_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(logging_safe, 1);
