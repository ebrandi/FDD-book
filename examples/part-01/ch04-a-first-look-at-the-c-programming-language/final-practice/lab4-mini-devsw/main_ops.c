#include <string.h>
#include "ops.h"

/* Choose one implementation based on argv[1]. */
static const dev_ops_t *
pick(const char *name)
{
	if (!name)
		return (NULL);
	if (!strcmp(name, "console"))
		return (&console_ops);
	if (!strcmp(name, "uart"))
		return (&uart_ops);
	return (NULL);
}

int
main(int argc, char **argv)
{
	const dev_ops_t *ops;

	if (argc < 2) {
		fprintf(stderr, "usage: %s {console|uart}\n", argv[0]);
		return (64);
	}

	ops = pick(argv[1]);
	if (!ops) {
		fprintf(stderr, "unknown ops\n");
		return (64);
	}

	/* The call sites do not care which backend we picked. */
	if (ops->init() != 0) {
		fprintf(stderr, "init failed\n");
		return (1);
	}
	ops->start();
	printf("[%s] status: %s\n", ops->name, ops->status());
	ops->stop();
	printf("[%s] status: %s\n", ops->name, ops->status());
	return (0);
}
