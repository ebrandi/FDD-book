/*
 * memory_safe.c - Safe kernel memory management for Chapter 5, Lab 1.
 *
 * This module demonstrates the kernel C dialect of memory management:
 *   - malloc(9) with an explicit malloc_type
 *   - M_WAITOK versus M_NOWAIT allocation strategies
 *   - Mandatory cleanup on MOD_UNLOAD
 *   - Clearing sensitive data before freeing it
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/malloc.h>		/* kernel memory allocation */

/*
 * Define a memory type for debugging and statistics. `vmstat -m` will
 * later show allocations under the name "memory_lab".
 */
MALLOC_DEFINE(M_MEMLAB, "memory_lab", "Memory Lab Example Allocations");

static void	*test_buffer = NULL;
static size_t	 buffer_size = 1024;

/*
 * safe_allocate - defensive memory allocation.
 *
 *   1. Validate the requested size.
 *   2. Check we are not leaking an existing allocation.
 *   3. Request memory with M_WAITOK | M_ZERO.
 *   4. Check the return value anyway (a habit worth building).
 *   5. Touch the memory so the printout is meaningful.
 */
static int
safe_allocate(size_t size)
{

	if (size == 0 || size > (1024 * 1024)) {
		printf("Memory Lab: Invalid size %zu (must be 1-%d bytes)\n",
		    size, 1024 * 1024);
		return (EINVAL);
	}

	if (test_buffer != NULL) {
		printf("Memory Lab: Memory already allocated\n");
		return (EBUSY);
	}

	test_buffer = malloc(size, M_MEMLAB, M_WAITOK | M_ZERO);
	if (test_buffer == NULL) {
		/*
		 * M_WAITOK should not return NULL, but checking is cheap
		 * and keeps the code defensible if someone later changes
		 * the flags to M_NOWAIT.
		 */
		printf("Memory Lab: Allocation failed for %zu bytes\n", size);
		return (ENOMEM);
	}

	buffer_size = size;
	printf("Memory Lab: Successfully allocated %zu bytes at %p\n",
	    size, test_buffer);

	snprintf((char *)test_buffer, size, "Allocated at ticks=%d", ticks);
	printf("Memory Lab: Test data: '%s'\n", (char *)test_buffer);

	return (0);
}

/*
 * safe_deallocate - clean up the allocation.
 *
 * The kernel C rule: every malloc() must have a matching free(),
 * especially during module unload. We also clear the buffer before
 * freeing it so that whatever the allocator does with the memory next
 * does not leak old contents.
 */
static void
safe_deallocate(void)
{

	if (test_buffer != NULL) {
		printf("Memory Lab: Freeing %zu bytes at %p\n",
		    buffer_size, test_buffer);

		explicit_bzero(test_buffer, buffer_size);

		free(test_buffer, M_MEMLAB);
		test_buffer = NULL;
		buffer_size = 0;

		printf("Memory Lab: Memory safely deallocated\n");
	}
}

static int
memory_safe_handler(module_t mod __unused, int what, void *arg __unused)
{
	int error = 0;

	switch (what) {
	case MOD_LOAD:
		printf("Memory Lab: Module loading\n");
		error = safe_allocate(1024);
		if (error != 0) {
			printf("Memory Lab: Failed to allocate memory: %d\n",
			    error);
			return (error);
		}
		printf("Memory Lab: Module loaded successfully\n");
		break;

	case MOD_UNLOAD:
		printf("Memory Lab: Module unloading\n");
		safe_deallocate();
		printf("Memory Lab: Module unloaded safely\n");
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static moduledata_t memory_safe_mod = {
	"memory_safe",
	memory_safe_handler,
	NULL
};

DECLARE_MODULE(memory_safe, memory_safe_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(memory_safe, 1);
