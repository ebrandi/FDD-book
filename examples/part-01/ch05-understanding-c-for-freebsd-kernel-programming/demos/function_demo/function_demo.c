/*
 * function_demo.c - Function design patterns for kernel C.
 *
 * Companion for the "Hands-On Lab: Function Design Patterns" section
 * of Chapter 5. Shows parameter validation, output parameters, and
 * the classic errno-style return convention.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/malloc.h>

MALLOC_DEFINE(M_FUNCDEMO, "funcdemo", "Function demo allocations");

static int
validate_buffer_params(size_t size, int flags)
{

	if (size == 0)
		return (EINVAL);
	if (size > 1024 * 1024)
		return (EFBIG);
	if ((flags & ~(M_WAITOK | M_NOWAIT | M_ZERO)) != 0)
		return (EINVAL);
	return (0);
}

static int
demo_buffer_alloc(char **bufp, size_t size, int flags)
{
	char *buffer;
	int error;

	if (bufp == NULL)
		return (EINVAL);
	*bufp = NULL;

	error = validate_buffer_params(size, flags);
	if (error != 0)
		return (error);

	buffer = malloc(size, M_FUNCDEMO, flags);
	if (buffer == NULL)
		return (ENOMEM);

	snprintf(buffer, size, "Demo buffer of %zu bytes", size);
	*bufp = buffer;
	return (0);
}

static void
demo_buffer_free(char *buffer)
{

	if (buffer != NULL)
		free(buffer, M_FUNCDEMO);
}

static ssize_t
demo_buffer_process(const char *buffer, size_t size, bool verbose)
{
	size_t len;

	if (buffer == NULL || size == 0)
		return (-EINVAL);

	len = strnlen(buffer, size);
	if (verbose)
		printf("Processing: '%.*s' (length %zu)\n",
		    (int)len, buffer, len);
	return ((ssize_t)len);
}

static int
function_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	char *buffer = NULL;
	ssize_t processed;
	int error;

	switch (type) {
	case MOD_LOAD:
		printf("=== Function Design Demo ===\n");

		error = demo_buffer_alloc(&buffer, 256, M_WAITOK | M_ZERO);
		if (error != 0) {
			printf("alloc failed: %d\n", error);
			return (error);
		}
		printf("Allocated buffer: %p\n", buffer);

		processed = demo_buffer_process(buffer, 256, true);
		if (processed < 0)
			printf("process failed: %zd\n", processed);
		else
			printf("processed %zd bytes\n", processed);

		demo_buffer_free(buffer);

		error = demo_buffer_alloc(&buffer, 0, M_WAITOK);
		if (error != 0)
			printf("validation rejected size=0 with %d (good)\n",
			    error);

		printf("function_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		printf("function_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t function_demo_mod = {
	"function_demo",
	function_demo_modevent,
	NULL
};

DECLARE_MODULE(function_demo, function_demo_mod,
    SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(function_demo, 1);
