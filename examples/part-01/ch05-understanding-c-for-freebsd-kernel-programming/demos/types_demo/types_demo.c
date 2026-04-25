/*
 * types_demo.c - Print the sizes of common kernel types at load time.
 *
 * Companion for the "Hands-On Lab: Exploring Kernel Types" section of
 * Chapter 5.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/types.h>

static int
types_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{

	switch (type) {
	case MOD_LOAD:
		printf("=== FreeBSD Kernel Data Types Demo ===\n");
		printf("uint8_t size:   %zu bytes\n", sizeof(uint8_t));
		printf("uint16_t size:  %zu bytes\n", sizeof(uint16_t));
		printf("uint32_t size:  %zu bytes\n", sizeof(uint32_t));
		printf("uint64_t size:  %zu bytes\n", sizeof(uint64_t));
		printf("size_t size:    %zu bytes\n", sizeof(size_t));
		printf("off_t size:     %zu bytes\n", sizeof(off_t));
		printf("time_t size:    %zu bytes\n", sizeof(time_t));
		printf("uintptr_t size: %zu bytes\n", sizeof(uintptr_t));
		printf("void * size:    %zu bytes\n", sizeof(void *));
		printf("types_demo: module loaded\n");
		break;
	case MOD_UNLOAD:
		printf("types_demo: module unloaded\n");
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t types_demo_mod = {
	"types_demo",
	types_demo_modevent,
	NULL
};

DECLARE_MODULE(types_demo, types_demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(types_demo, 1);
