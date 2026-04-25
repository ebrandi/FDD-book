/*
 * hello.c - Minimal FreeBSD kernel module for Chapter 6, Lab 2.
 *
 * This module does nothing except print messages when it is loaded
 * and unloaded. Its only job is to confirm that your build tools are
 * healthy and that you can move a .ko into and out of the kernel
 * safely.
 *
 * Compatible with FreeBSD 14.3.
 */

#include <sys/param.h>		/* system parameter definitions */
#include <sys/module.h>		/* kernel module definitions */
#include <sys/kernel.h>		/* kernel types and macros */
#include <sys/systm.h>		/* printf and friends */

/*
 * Module event handler.
 *
 * The kernel calls this function with a `type` argument describing
 * the event:
 *
 *   MOD_LOAD     - module is being loaded into the kernel
 *   MOD_UNLOAD   - module is being removed from the kernel
 *   MOD_SHUTDOWN - system is shutting down (rare, usually ignored)
 *   MOD_QUIESCE  - pre-unload check (advanced, not used here)
 *
 * Return 0 for success or an errno for failure. Returning a non-zero
 * value for MOD_LOAD aborts the load; for MOD_UNLOAD it refuses the
 * unload.
 */
static int
hello_modevent(module_t mod __unused, int type, void *arg __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		printf("Hello: Module loaded successfully!\n");
		printf("Hello: This message appears in dmesg\n");
		printf("Hello: Module address: %p\n", (void *)&hello_modevent);
		break;

	case MOD_UNLOAD:
		printf("Hello: Module unloaded. Goodbye!\n");
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/*
 * moduledata_t ties the module name to its event handler. The
 * `NULL` slot is for optional private data (unused here).
 */
static moduledata_t hello_mod = {
	"hello",		/* module name */
	hello_modevent,		/* event handler */
	NULL			/* no private data */
};

/*
 * DECLARE_MODULE registers the module with the kernel.
 *
 *   name:          "hello" (must match the string above)
 *   moduledata_t:  the struct just above
 *   subsystem:     SI_SUB_DRIVERS is a safe default
 *   order:         SI_ORDER_MIDDLE is a safe default
 */
DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(hello, 1);
