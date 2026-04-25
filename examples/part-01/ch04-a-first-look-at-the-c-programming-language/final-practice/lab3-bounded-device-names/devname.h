#ifndef DEVNAME_H
#define DEVNAME_H

#include <stddef.h>

/* Tiny errno-like set for clarity when reading results. */
enum dn_err { DN_OK = 0, DN_EINVAL = 22, DN_ERANGE = 34 };

/*
 * Caller supplies dst and its size. We combine prefix + unit into dst.
 * This mirrors common kernel patterns: pointer plus explicit length.
 */
int build_devname(char *dst, size_t dstsz, const char *prefix, int unit);

/* Quick validator: letters then at least one digit (e.g., ttyu0). */
int is_valid_devname(const char *s);

#endif
