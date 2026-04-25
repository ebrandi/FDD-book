#ifndef OPS_H
#define OPS_H

#include <stdio.h>

/* typedefs keep pointer-to-function types readable. */
typedef int         (*ops_init_t)(void);
typedef void        (*ops_start_t)(void);
typedef void        (*ops_stop_t)(void);
typedef const char *(*ops_status_t)(void);

/* A tiny vtable of operations for a device. */
typedef struct {
	const char   *name;
	ops_init_t    init;
	ops_start_t   start;
	ops_stop_t    stop;
	ops_status_t  status;
} dev_ops_t;

/* Two concrete implementations declared here, defined in .c files. */
extern const dev_ops_t console_ops;
extern const dev_ops_t uart_ops;

#endif
