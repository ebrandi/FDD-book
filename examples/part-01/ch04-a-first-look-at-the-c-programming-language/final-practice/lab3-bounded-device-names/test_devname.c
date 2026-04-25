#include <stdio.h>
#include "devname.h"

/*
 * Table-driven tests: we try several cases and print both code and result.
 * Practise reading return codes and not assuming success.
 */
struct case_ { const char *pref; int unit; size_t cap; };

int
main(void)
{
	char buf[8];	/* small to force you to think about capacity */
	struct case_ cases[] = {
		{"ttyu", 0, sizeof(buf)},	/* fits */
		{"ttyv", 12, sizeof(buf)},	/* just fits or close */
		{"",     1, sizeof(buf)},	/* invalid prefix */
	};

	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int rc = build_devname(buf, cases[i].cap,
		    cases[i].pref, cases[i].unit);
		printf("case %u -> rc=%d, buf='%s'\n",
		    i, rc, (rc == DN_OK) ? buf : "<invalid>");
	}

	printf("valid? 'ttyu0'=%d, 'u0tty'=%d\n",
	    is_valid_devname("ttyu0"),
	    is_valid_devname("u0tty"));

	return (0);
}
