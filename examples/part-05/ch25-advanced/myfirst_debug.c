/*
 * myfirst_debug.c - debug-class enumeration.
 *
 * Today the debug macros are all inline in myfirst_debug.h.  This
 * .c file exists to keep the build graph stable if future versions
 * need an out-of-line helper for a complex class filter.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include "myfirst.h"
