/*
 * restrictions_demo.c - Work around the rules kernel C enforces.
 *
 * Companion for the "Hands-On Lab: Understanding Restrictions" section
 * of Chapter 5. Shows fixed-point arithmetic, bounded recursion, and
 * atomic counters, all in one module.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <machine/atomic.h>

MALLOC_DEFINE(M_RESTRICT, "restrict", "Restriction demo");

static volatile u_int atomic_counter = 0;
static struct mtx demo_lock;

static int
safe_recursive_demo(int depth, int max_depth)
{

	if (depth >= max_depth)
		return (depth);
	return (safe_recursive_demo(depth + 1, max_depth) + 1);
}

static int
fixed_point_average(int *values, int count, int scale)
{
	long sum = 0;
	int i;

	if (count == 0)
		return (0);
	for (i = 0; i < count; i++)
		sum += values[i];
	return ((int)((sum * scale) / count));
}

static int
restrictions_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	int values[] = { 10, 20, 30, 40, 50 };
	void *buffer;

	switch (type) {
	case MOD_LOAD:
		printf("=== Kernel Restrictions Demo ===\n");

		mtx_init(&demo_lock, "demo_lock", NULL, MTX_DEF);

		printf("fixed-point average * 100 = %d\n",
		    fixed_point_average(values, 5, 100));

		printf("safe recursive depth=10 returned %d\n",
		    safe_recursive_demo(0, 10));

		atomic_add_int(&atomic_counter, 42);
		printf("atomic counter: %u\n",
		    atomic_load_acq_int(&atomic_counter));

		buffer = malloc(1024, M_RESTRICT, M_WAITOK);
		if (buffer != NULL) {
			printf("allocated 1KB in sleepable context\n");
			free(buffer, M_RESTRICT);
		}

		printf("restrictions_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		mtx_destroy(&demo_lock);
		printf("restrictions_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t restrictions_demo_mod = {
	"restrictions_demo",
	restrictions_demo_modevent,
	NULL
};

DECLARE_MODULE(restrictions_demo, restrictions_demo_mod,
    SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(restrictions_demo, 1);
