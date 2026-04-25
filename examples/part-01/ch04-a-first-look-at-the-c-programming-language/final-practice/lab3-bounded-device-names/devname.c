#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "devname.h"

/* Safe build: check pointers and capacity; use snprintf to avoid overflow. */
int
build_devname(char *dst, size_t dstsz, const char *prefix, int unit)
{
	int n;

	if (!dst || !prefix || unit < 0)
		return (DN_EINVAL);

	n = snprintf(dst, dstsz, "%s%d", prefix, unit);

	/*
	 * snprintf returns the number of chars it *wanted* to write.
	 * If that does not fit in dst, we report DN_ERANGE.
	 */
	return (n >= 0 && (size_t)n < dstsz) ? DN_OK : DN_ERANGE;
}

/* Very small validator for practice. Adjust rules if you want stricter checks. */
int
is_valid_devname(const char *s)
{
	int saw_digit = 0;

	if (!s || !isalpha((unsigned char)s[0]))
		return (0);
	for (const char *p = s; *p; p++) {
		if (isdigit((unsigned char)*p))
			saw_digit = 1;
		else if (!isalpha((unsigned char)*p))
			return (0);
	}
	return (saw_digit);
}
