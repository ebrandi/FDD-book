/*
 * hello_world.c - Simple FreeBSD kernel module
 * Prints messages when loaded and unloaded
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

/*
 * Load handler - called when the module is loaded
 */
static int
hello_world_load(module_t mod, int cmd, void *arg)
{
    int error = 0;

    switch (cmd) {
    case MOD_LOAD:
        printf("Hello World! Kernel module loaded.\n");
        break;
    case MOD_UNLOAD:
        printf("Goodbye World! Kernel module unloaded.\n");
        break;
    default:
        error = EOPNOTSUPP;
        break;
    }

    return (error);
}

/*
 * Module declaration
 */
static moduledata_t hello_world_mod = {
    "hello_world",      /* module name */
    hello_world_load,   /* event handler */
    NULL                /* extra data */
};

/*
 * Register the module with the kernel
 * DECLARE_MODULE(name, data, sub-system, order)
 */
DECLARE_MODULE(hello_world, hello_world_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(hello_world, 1);
