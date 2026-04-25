#include "log.h"

/* Simple printf-style logger that prefixes level, file, and line. */
void
eb_log(const char *lvl, const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "[%s] %s:%d: ", lvl, file, line);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}
