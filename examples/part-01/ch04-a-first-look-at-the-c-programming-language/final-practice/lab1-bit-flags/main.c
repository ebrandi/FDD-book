#include <string.h>
#include <stdlib.h>
#include "flags.h"

/* Map small words to flags to keep the CLI simple. */
static uint32_t
flag_from_name(const char *s)
{
	if (!strcmp(s, "enable"))  return (DF_ENABLED);
	if (!strcmp(s, "open"))    return (DF_OPEN);
	if (!strcmp(s, "error"))   return (DF_ERROR);
	if (!strcmp(s, "txbusy"))  return (DF_TX_BUSY);
	if (!strcmp(s, "rxready")) return (DF_RX_READY);
	return (0);
}

/*
 * Usage example:
 *	./devflags set enable set rxready toggle open
 * Try different sequences and confirm your mental model of bits changing.
 */
int
main(int argc, char **argv)
{
	uint32_t st = 0;

	for (int i = 1; i + 1 < argc; i += 2) {
		const char *op = argv[i], *name = argv[i + 1];
		uint32_t f = flag_from_name(name);
		if (f == 0) {
			printf("Unknown flag '%s'\n", name);
			return (64);
		}

		if (!strcmp(op, "set"))          set_flag(&st, f);
		else if (!strcmp(op, "clear"))   clear_flag(&st, f);
		else if (!strcmp(op, "toggle"))  toggle_flag(&st, f);
		else {
			printf("Unknown op '%s'\n", op);
			return (64);
		}
	}

	print_state(st);
	return (0);
}
