/*
 * defensive_demo.c - Defensive programming in the kernel.
 *
 * Companion for the "Hands-On Lab: Building Defensive Kernel Code"
 * section of Chapter 5. Demonstrates full parameter validation,
 * magic-number structure checks, bounded buffer writes, and safe
 * reference counting.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <machine/atomic.h>

MALLOC_DEFINE(M_DEFTEST, "deftest", "Defensive programming test");

#define MAX_BUFFER_SIZE		4096
#define MAX_NAME_LENGTH		64
#define DEMO_MAGIC		0x12345678U

struct demo_buffer {
	uint32_t	db_magic;
	size_t		db_size;
	size_t		db_used;
	char		db_name[MAX_NAME_LENGTH];
	void		*db_data;
	volatile u_int	db_refcount;
};

static struct demo_buffer *
demo_buffer_alloc(const char *name, size_t size)
{
	struct demo_buffer *db;
	size_t name_len;

	if (name == NULL)
		return (NULL);
	name_len = strnlen(name, MAX_NAME_LENGTH);
	if (name_len == 0 || name_len >= MAX_NAME_LENGTH)
		return (NULL);
	if (size == 0 || size > MAX_BUFFER_SIZE)
		return (NULL);
	if (SIZE_MAX - sizeof(*db) < size)
		return (NULL);

	db = malloc(sizeof(*db), M_DEFTEST, M_WAITOK | M_ZERO);
	db->db_data = malloc(size, M_DEFTEST, M_WAITOK);
	if (db->db_data == NULL) {
		free(db, M_DEFTEST);
		return (NULL);
	}

	db->db_magic = DEMO_MAGIC;
	db->db_size = size;
	db->db_refcount = 1;
	strlcpy(db->db_name, name, sizeof(db->db_name));
	return (db);
}

static void
demo_buffer_free(struct demo_buffer *db)
{

	if (db == NULL)
		return;
	if (db->db_magic != DEMO_MAGIC) {
		printf("demo_buffer_free: bad magic\n");
		return;
	}
	if (db->db_refcount != 0) {
		printf("demo_buffer_free: non-zero refcount %u\n",
		    db->db_refcount);
		return;
	}

	memset(db->db_data, 0, db->db_size);
	free(db->db_data, M_DEFTEST);
	db->db_data = NULL;
	db->db_magic = 0xDEADBEEFU;
	free(db, M_DEFTEST);
}

static int
demo_buffer_write(struct demo_buffer *db, const void *data, size_t len,
    size_t offset)
{

	if (db == NULL || data == NULL)
		return (EINVAL);
	if (db->db_magic != DEMO_MAGIC)
		return (EINVAL);
	if (len == 0)
		return (0);
	if (offset > db->db_size || len > db->db_size - offset)
		return (EOVERFLOW);

	memcpy((char *)db->db_data + offset, data, len);
	if (offset + len > db->db_used)
		db->db_used = offset + len;
	return (0);
}

static int
defensive_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	struct demo_buffer *db;
	const char test[] = "Hello, defensive kernel world!";
	int error;

	switch (type) {
	case MOD_LOAD:
		printf("=== Defensive Programming Demo ===\n");

		db = demo_buffer_alloc("test", 256);
		if (db == NULL)
			return (ENOMEM);
		printf("allocated '%s' size=%zu\n", db->db_name, db->db_size);

		error = demo_buffer_write(db, test, sizeof(test) - 1, 0);
		printf("write returned %d\n", error);

		/* Negative tests (should all return NULL or an error). */
		if (demo_buffer_alloc(NULL, 100) == NULL)
			printf("rejected NULL name (good)\n");
		if (demo_buffer_alloc("t", 0) == NULL)
			printf("rejected size=0 (good)\n");
		if (demo_buffer_alloc("t", MAX_BUFFER_SIZE + 1) == NULL)
			printf("rejected oversized (good)\n");
		if (demo_buffer_write(db, test, 10000, 0) != 0)
			printf("rejected oversized write (good)\n");

		/* Final release. */
		atomic_subtract_int(&db->db_refcount, 1);
		demo_buffer_free(db);

		printf("defensive_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		printf("defensive_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t defensive_demo_mod = {
	"defensive_demo",
	defensive_demo_modevent,
	NULL
};

DECLARE_MODULE(defensive_demo, defensive_demo_mod,
    SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(defensive_demo, 1);
