/*
 * atomic_demo.c - Atomic operations and inline helpers.
 *
 * Companion for the "Hands-On Lab: Atomic Operations and Performance"
 * section of Chapter 5.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <machine/atomic.h>

static volatile u_int shared_counter = 0;
static volatile u_int shared_flags = 0;

static __inline void
safe_increment(volatile u_int *counter)
{

	atomic_add_int(counter, 1);
}

static __inline void
set_flag_atomically(volatile u_int *flags, u_int flag)
{

	atomic_set_int(flags, flag);
}

static __inline void
clear_flag_atomically(volatile u_int *flags, u_int flag)
{

	atomic_clear_int(flags, flag);
}

static __inline bool
test_flag_atomically(volatile u_int *flags, u_int flag)
{

	return ((atomic_load_acq_int(flags) & flag) != 0);
}

static int
atomic_max_update(volatile u_int *current_max, u_int new_value)
{
	u_int old_value;

	do {
		old_value = *current_max;
		if (new_value <= old_value)
			return (0);
	} while (!atomic_cmpset_int(current_max, old_value, new_value));
	return (1);
}

static int
atomic_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	int i;

	switch (type) {
	case MOD_LOAD:
		printf("=== Atomic Operations Demo ===\n");

		atomic_store_rel_int(&shared_counter, 0);
		atomic_store_rel_int(&shared_flags, 0);

		for (i = 0; i < 10; i++)
			safe_increment(&shared_counter);
		printf("counter after 10 increments: %u\n",
		    atomic_load_acq_int(&shared_counter));

		set_flag_atomically(&shared_flags, 0x01);
		set_flag_atomically(&shared_flags, 0x04);
		set_flag_atomically(&shared_flags, 0x10);
		printf("flags after set 0x01/0x04/0x10: 0x%02x\n",
		    atomic_load_acq_int(&shared_flags));

		printf("flag 0x01 is %s\n",
		    test_flag_atomically(&shared_flags, 0x01) ? "set" : "clear");
		printf("flag 0x02 is %s\n",
		    test_flag_atomically(&shared_flags, 0x02) ? "set" : "clear");

		clear_flag_atomically(&shared_flags, 0x01);
		printf("flag 0x01 after clear: %s\n",
		    test_flag_atomically(&shared_flags, 0x01) ? "set" : "clear");

		printf("update max to 5: %s\n",
		    atomic_max_update(&shared_counter, 5) ? "success" : "skipped");
		printf("update max to 15: %s\n",
		    atomic_max_update(&shared_counter, 15) ? "success" : "skipped");
		printf("final counter: %u\n",
		    atomic_load_acq_int(&shared_counter));

		printf("atomic_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		printf("atomic_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t atomic_demo_mod = {
	"atomic_demo",
	atomic_demo_modevent,
	NULL
};

DECLARE_MODULE(atomic_demo, atomic_demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(atomic_demo, 1);
