#include "flags.h"

/*
 * Print both the hex value and a friendly list of which flags are set.
 * This mirrors how driver debug output often looks in practice.
 *
 * The `first` variable tracks whether we are printing the first flag
 * (no leading comma) or a later one (comma separator).
 */
void
print_state(uint32_t st)
{
	int first = 1;

	printf("State: 0x%08x [", st);
	if (test_flag(st, DF_ENABLED))  { printf("%sENABLED",  first ? "" : ","); first = 0; }
	if (test_flag(st, DF_OPEN))     { printf("%sOPEN",     first ? "" : ","); first = 0; }
	if (test_flag(st, DF_ERROR))    { printf("%sERROR",    first ? "" : ","); first = 0; }
	if (test_flag(st, DF_TX_BUSY))  { printf("%sTX_BUSY",  first ? "" : ","); first = 0; }
	if (test_flag(st, DF_RX_READY)) { printf("%sRX_READY", first ? "" : ","); first = 0; }
	if (first)
		printf("none");
	printf("]\n");
}
