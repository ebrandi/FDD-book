/*
 * style_demo.c - KNF-style module with refcounted objects.
 *
 * Companion for the "Hands-On Lab: Implementing Kernel Coding
 * Patterns" section of Chapter 5. Demonstrates KNF layout, magic-
 * numbered structures, refcounting, and a global TAILQ protected by
 * a mutex.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <machine/atomic.h>

MALLOC_DEFINE(M_STYLEDEMO, "styledemo", "Style demo structures");

#define DEMO_ITEM_MAGIC	0xDEADBEEFU

struct demo_item {
	TAILQ_ENTRY(demo_item)	di_link;
	uint32_t		di_magic;
	int			di_id;
	char			di_name[32];
	int			di_refcount;
};

TAILQ_HEAD(demo_item_list, demo_item);

static struct demo_item_list	item_list = TAILQ_HEAD_INITIALIZER(item_list);
static struct mtx		item_list_lock;
static int			next_item_id = 1;

static void
demo_item_ref(struct demo_item *item)
{

	KASSERT(item != NULL, ("demo_item_ref: null"));
	KASSERT(item->di_magic == DEMO_ITEM_MAGIC,
	    ("demo_item_ref: bad magic"));
	atomic_add_int(&item->di_refcount, 1);
}

static void
demo_item_unref(struct demo_item *item)
{
	int old;

	if (item == NULL)
		return;
	KASSERT(item->di_magic == DEMO_ITEM_MAGIC,
	    ("demo_item_unref: bad magic"));

	old = atomic_fetchadd_int(&item->di_refcount, -1);
	if (old != 1)
		return;

	mtx_lock(&item_list_lock);
	TAILQ_REMOVE(&item_list, item, di_link);
	mtx_unlock(&item_list_lock);

	item->di_magic = 0;	/* poison */
	free(item, M_STYLEDEMO);
}

static struct demo_item *
demo_item_alloc(const char *name)
{
	struct demo_item *item;

	if (name == NULL)
		return (NULL);
	if (strnlen(name, sizeof(item->di_name)) >= sizeof(item->di_name))
		return (NULL);

	item = malloc(sizeof(*item), M_STYLEDEMO, M_WAITOK | M_ZERO);
	item->di_magic = DEMO_ITEM_MAGIC;
	item->di_refcount = 1;
	strlcpy(item->di_name, name, sizeof(item->di_name));

	mtx_lock(&item_list_lock);
	item->di_id = next_item_id++;
	TAILQ_INSERT_TAIL(&item_list, item, di_link);
	mtx_unlock(&item_list_lock);

	return (item);
}

static int
style_demo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	struct demo_item *i1, *i2;

	switch (type) {
	case MOD_LOAD:
		printf("=== Kernel Style Demo ===\n");
		mtx_init(&item_list_lock, "style_demo_items", NULL, MTX_DEF);

		i1 = demo_item_alloc("first");
		i2 = demo_item_alloc("second");
		if (i1 != NULL)
			printf("created item %d '%s'\n", i1->di_id, i1->di_name);
		if (i2 != NULL)
			printf("created item %d '%s'\n", i2->di_id, i2->di_name);

		if (i1 != NULL)
			demo_item_unref(i1);
		if (i2 != NULL)
			demo_item_unref(i2);

		printf("style_demo: module loaded\n");
		break;

	case MOD_UNLOAD:
		mtx_lock(&item_list_lock);
		if (!TAILQ_EMPTY(&item_list))
			printf("warning: item list not empty at unload\n");
		mtx_unlock(&item_list_lock);
		mtx_destroy(&item_list_lock);
		printf("style_demo: module unloaded\n");
		break;

	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t style_demo_mod = {
	"style_demo",
	style_demo_modevent,
	NULL
};

DECLARE_MODULE(style_demo, style_demo_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(style_demo, 1);
