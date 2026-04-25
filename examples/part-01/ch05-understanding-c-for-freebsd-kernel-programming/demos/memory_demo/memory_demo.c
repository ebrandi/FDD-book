/*
 * memory_demo.c - malloc(9), M_NOWAIT vs M_WAITOK, and a UMA zone.
 *
 * Companion for the "Hands-On Lab: Kernel Memory Management" section
 * of Chapter 5.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <vm/uma.h>

MALLOC_DEFINE(M_DEMO, "demo", "Memory demo allocations");

static uma_zone_t demo_zone;

struct demo_object {
	int	id;
	char	name[32];
};

static int
memory_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	void *ptr1, *ptr2, *ptr3;
	struct demo_object *obj;

	switch (type) {
	case MOD_LOAD:
		printf("=== Kernel Memory Management Demo ===\n");

		ptr1 = malloc(1024, M_DEMO, M_WAITOK);
		printf("M_WAITOK allocation of 1024 bytes at %p\n", ptr1);

		ptr2 = malloc(512, M_DEMO, M_WAITOK | M_ZERO);
		printf("M_WAITOK|M_ZERO allocation of 512 bytes at %p\n", ptr2);

		ptr3 = malloc(2048, M_DEMO, M_NOWAIT);
		if (ptr3 != NULL)
			printf("M_NOWAIT allocation succeeded at %p\n", ptr3);
		else
			printf("M_NOWAIT allocation failed (memory pressure)\n");

		demo_zone = uma_zcreate("demo_objects", sizeof(*obj),
		    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
		if (demo_zone != NULL) {
			obj = uma_zalloc(demo_zone, M_WAITOK);
			obj->id = 42;
			strlcpy(obj->name, "demo_object", sizeof(obj->name));
			printf("UMA zone allocation: id=%d name='%s' at %p\n",
			    obj->id, obj->name, obj);
			uma_zfree(demo_zone, obj);
		}

		free(ptr1, M_DEMO);
		free(ptr2, M_DEMO);
		if (ptr3 != NULL)
			free(ptr3, M_DEMO);

		printf("memory_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		if (demo_zone != NULL)
			uma_zdestroy(demo_zone);
		printf("memory_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t memory_demo_mod = {
	"memory_demo",
	memory_demo_modevent,
	NULL
};

DECLARE_MODULE(memory_demo, memory_demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(memory_demo, 1);
