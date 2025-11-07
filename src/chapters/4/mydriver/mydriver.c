/*
 * FreeBSD device driver lifecycle (quick map)
 *
 * 1) Kernel enumerates devices on a bus
 *    The bus framework walks hardware and creates device_t objects.
 *
 * 2) probe()
 *    The kernel asks your driver if it supports a given device.
 *    You inspect IDs or capabilities and return a score.
 *      - Return ENXIO if this driver does not match.
 *      - Return BUS_PROBE_DEFAULT or a better score if it matches.
 *
 * 3) attach()
 *    Called after a successful probe to bring the device online.
 *    Typical work:
 *      - Allocate resources (memory, IRQ) with bus_alloc_resource_any()
 *      - Map registers and set up bus_space
 *      - Initialize hardware to a known state
 *      - Set up interrupts and handlers
 *    Return 0 on success, or an errno if something fails.
 *
 * 4) Runtime
 *    Your driver services requests. This may include:
 *      - Interrupt handlers
 *      - I/O paths invoked by upper layers or devfs interfaces
 *      - Periodic tasks, callouts, or taskqueues
 *
 * 5) detach()
 *    Called when the device is being removed or the module unloads.
 *    Cleanup tasks:
 *      - Quiesce hardware, stop DMA, disable interrupts
 *      - Tear down handlers and timers
 *      - Unmap registers and release resources with bus_release_resource()
 *    Return 0 on success, or an errno if detach must be denied.
 *
 * 6) Optional lifecycle events
 *      - suspend() and resume() during power management
 *      - shutdown() during system shutdown
 *
 * Files to remember
 *    - mydriver.h declares the entry points that the kernel and bus code will call
 *    - mydriver.c defines those functions and contains the implementation details
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/systm.h>   // device_printf
#include "mydriver.h"

/*
 * mydriver_probe()
 * Called early during device enumeration.
 * Purpose: decide if this driver matches the hardware represented by dev.
 * Return: BUS_PROBE_DEFAULT for a normal match, a better score for a strong match,
 *         or ENXIO if the device is not supported.
 */
int
mydriver_probe(device_t dev)
{
    device_printf(dev, "Probing device...\n");

    /*
     * Here you would usually check vendor and device IDs or use bus-specific
     * helper routines. If the device is not supported, return (ENXIO).
     */

    return (BUS_PROBE_DEFAULT);
}

/*
 * mydriver_attach()
 * Called after a successful probe when the kernel is ready to attach the device.
 * Purpose: allocate resources, map registers, initialise hardware, register interrupts,
 *          and make the device ready for use.
 * Return: 0 on success, or an errno value (like ENOMEM or EIO) on failure.
 */
int
mydriver_attach(device_t dev)
{
    device_printf(dev, "Attaching device and initializing resources...\n");

    /*
     * Typical steps you will add here:
     * 1) Allocate device resources (I/O memory, IRQs) with bus_alloc_resource_any().
     * 2) Map register space and set up bus_space tags and handles.
     * 3) Initialise hardware registers to a known state.
     * 4) Set up interrupt handlers if needed.
     * 5) Create device nodes or child devices if this driver exposes them.
     * On any failure, release what you allocated and return an errno.
     */

    return (0);
}

/*
 * mydriver_detach()
 * Called when the device is being detached or the module is unloading.
 * Purpose: stop the hardware, free resources, and leave the system clean.
 * Return: 0 on success, or an errno value if detach must be refused.
 */
int
mydriver_detach(device_t dev)
{
    device_printf(dev, "Detaching device and cleaning up...\n");

    /*
     * Typical steps you will add here:
     * 1) Disable interrupts and stop DMA or timers.
     * 2) Tear down interrupt handlers.
     * 3) Unmap register space and free bus resources with bus_release_resource().
     * 4) Destroy any device nodes or sysctl entries created at attach time.
     */

    return (0);
}
