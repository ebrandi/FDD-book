#ifndef EB_LOG_H
#define EB_LOG_H

#include <stdio.h>
#include <stdarg.h>

/*
 * When compiled with -DDEBUG we enable extra logs.
 * This is common in kernel and driver code to keep fast paths quiet.
 */
#ifdef DEBUG
#define LOG_DEBUG(fmt, ...) \
	eb_log("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)	((void)0)	/* compiles away */
#endif

#define LOG_INFO(fmt, ...) \
	eb_log("INFO",  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) \
	eb_log("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* Single declaration for the underlying function. */
void eb_log(const char *lvl, const char *file, int line, const char *fmt, ...);

/*
 * Example compile-time assumption check:
 * The code should work on 32- or 64-bit. Fail early otherwise.
 */
_Static_assert(sizeof(void *) == 8 || sizeof(void *) == 4,
    "Unsupported pointer size");

#endif
