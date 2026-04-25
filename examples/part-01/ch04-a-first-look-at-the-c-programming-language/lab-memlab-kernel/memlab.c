/*
 * memlab.c - simple kernel module demonstrating malloc(9) / free(9).
 *
 * Companion for "Mini-Lab 3: Memory Allocation in a Kernel Module"
 * in Chapter 4. Load the module and then check `vmstat -m | grep
 * memlab` to see the allocation recorded; unload it and check again
 * to confirm the memory is returned.
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

MALLOC_DEFINE(M_MEMLAB, "memlab", "Memory Lab Example");

static void *buffer = NULL;

static int
memlab_load(struct module *m, int event, void *arg)
{
	int error = 0;

	switch (event) {
	case MOD_LOAD:
		printf("memlab: Loading module\n");
		buffer = malloc(128, M_MEMLAB, M_WAITOK | M_ZERO);
		if (buffer == NULL) {
			printf("memlab: malloc failed!\n");
			error = ENOMEM;
		} else {
			printf("memlab: allocated 128 bytes\n");
		}
		break;

	case MOD_UNLOAD:
		printf("memlab: Unloading module\n");
		if (buffer != NULL) {
			free(buffer, M_MEMLAB);
			printf("memlab: memory freed\n");
		}
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}

static moduledata_t memlab_mod = {
	"memlab", memlab_load, NULL
};

DECLARE_MODULE(memlab, memlab_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(memlab, 1);
