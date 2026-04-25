/*
 * error_demo.c - Error handling and diagnostics.
 *
 * Companion for the "Hands-On Lab: Error Handling and Diagnostics"
 * section of Chapter 5. Shows compiler attributes, debug levels,
 * statistics counters, and an error-context struct used on every
 * failure path.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <machine/atomic.h>

MALLOC_DEFINE(M_ERRTEST, "errtest", "Error handling test structures");

#define DEBUG_ERROR	1
#define DEBUG_WARN	2
#define DEBUG_INFO	3
#define DEBUG_VERBOSE	4

static int debug_level = DEBUG_INFO;

#define DPRINTF(level, fmt, ...)					\
	do {								\
		if ((level) <= debug_level)				\
			printf("[%s:%d] " fmt "\n",			\
			    __func__, __LINE__, ##__VA_ARGS__);		\
	} while (0)

struct error_context {
	int		error_code;
	const char	*operation;
	const char	*file;
	int		line;
	sbintime_t	timestamp;
};

#define SET_ERROR(ctx, code, op)					\
	do {								\
		if ((ctx) != NULL) {					\
			(ctx)->error_code = (code);			\
			(ctx)->operation = (op);			\
			(ctx)->file = __FILE__;				\
			(ctx)->line = __LINE__;				\
			(ctx)->timestamp = sbinuptime();		\
		}							\
	} while (0)

struct operation_stats {
	volatile u_long	total_attempts;
	volatile u_long	successes;
	volatile u_long	failures;
};

static struct operation_stats global_stats;

#define TEST_MAGIC	0xABCDEF00U
struct test_object {
	uint32_t	magic;
	int		id;
	size_t		size;
	void		*data;
};

static struct test_object *
test_object_alloc(int id, size_t size, struct error_context *err_ctx)
{
	struct test_object *obj;
	void *data;

	atomic_add_long(&global_stats.total_attempts, 1);

	if (id < 0 || size == 0 || size > 1024U * 1024U) {
		SET_ERROR(err_ctx, EINVAL, "parameter validation");
		atomic_add_long(&global_stats.failures, 1);
		return (NULL);
	}

	obj = malloc(sizeof(*obj), M_ERRTEST, M_NOWAIT | M_ZERO);
	if (obj == NULL) {
		SET_ERROR(err_ctx, ENOMEM, "structure allocation");
		atomic_add_long(&global_stats.failures, 1);
		return (NULL);
	}

	data = malloc(size, M_ERRTEST, M_NOWAIT);
	if (data == NULL) {
		free(obj, M_ERRTEST);
		SET_ERROR(err_ctx, ENOMEM, "data buffer allocation");
		atomic_add_long(&global_stats.failures, 1);
		return (NULL);
	}

	obj->magic = TEST_MAGIC;
	obj->id = id;
	obj->size = size;
	obj->data = data;

	atomic_add_long(&global_stats.successes, 1);
	DPRINTF(DEBUG_INFO, "allocated object id=%d size=%zu", id, size);
	return (obj);
}

static void
test_object_free(struct test_object *obj)
{

	if (obj == NULL)
		return;
	if (obj->magic != TEST_MAGIC) {
		DPRINTF(DEBUG_ERROR, "object bad magic 0x%x", obj->magic);
		return;
	}
	memset(obj->data, 0, obj->size);
	free(obj->data, M_ERRTEST);
	obj->magic = 0xDEADBEEFU;
	free(obj, M_ERRTEST);
}

static int
error_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	struct test_object *obj;
	struct error_context ec = { 0 };

	switch (type) {
	case MOD_LOAD:
		printf("=== Error Handling and Diagnostics Demo ===\n");

		obj = test_object_alloc(1, 1024, &ec);
		if (obj != NULL) {
			printf("allocated object 1\n");
			test_object_free(obj);
		}

		memset(&ec, 0, sizeof(ec));
		if (test_object_alloc(-1, 1024, &ec) == NULL)
			printf("rejected invalid id: error=%d op='%s'\n",
			    ec.error_code, ec.operation);

		memset(&ec, 0, sizeof(ec));
		if (test_object_alloc(2, 0, &ec) == NULL)
			printf("rejected invalid size: error=%d op='%s'\n",
			    ec.error_code, ec.operation);

		printf("stats: attempts=%lu ok=%lu err=%lu\n",
		    atomic_load_acq_long(&global_stats.total_attempts),
		    atomic_load_acq_long(&global_stats.successes),
		    atomic_load_acq_long(&global_stats.failures));
		printf("error_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		printf("error_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t error_demo_mod = {
	"error_demo",
	error_demo_modevent,
	NULL
};

DECLARE_MODULE(error_demo, error_demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(error_demo, 1);
