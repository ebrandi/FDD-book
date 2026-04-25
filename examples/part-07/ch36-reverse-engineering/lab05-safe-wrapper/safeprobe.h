/*
 * safeprobe.h - Safe register-access wrappers for use during
 * reverse-engineering work.
 *
 * Companion to Chapter 36, Lab 5.
 *
 * The wrappers in this header combine four safety techniques that
 * the chapter recommends:
 *
 *   1. Logging of every access, so that the kernel log records
 *      what the driver did against the device. The log can be
 *      compared against captures of the vendor driver's behaviour.
 *
 *   2. Read-modify-write-verify for set-field operations. After
 *      every write that should change a value, the wrapper reads
 *      the value back and confirms that the change took effect.
 *
 *   3. Timeout-protected polling. Loops that wait for the device
 *      to reach a state include a hard deadline; the loop returns
 *      an error if the deadline expires.
 *
 *   4. Read-only mode. A flag in the safeprobe context can suppress
 *      all writes, turning the wrappers into pure-observation tools
 *      for use early in an investigation.
 *
 * The wrappers are not specific to any particular device. They are
 * meant to be used by any driver-style code that needs disciplined
 * access to a memory-mapped resource.
 */

#ifndef _SAFEPROBE_H_
#define	_SAFEPROBE_H_

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>

struct safeprobe_ctx {
	device_t	 sp_dev;
	struct resource	*sp_mem;
	struct mtx	 sp_mtx;
	int		 sp_log_reads;
	int		 sp_log_writes;
	int		 sp_read_only;
	uint64_t	 sp_n_reads;
	uint64_t	 sp_n_writes;
	uint64_t	 sp_n_failures;
};

void safeprobe_init(struct safeprobe_ctx *ctx, device_t dev,
    struct resource *res);
void safeprobe_destroy(struct safeprobe_ctx *ctx);

uint32_t safeprobe_read(struct safeprobe_ctx *ctx, bus_size_t off);
int safeprobe_write(struct safeprobe_ctx *ctx, bus_size_t off,
    uint32_t value);
int safeprobe_set_field(struct safeprobe_ctx *ctx, bus_size_t off,
    uint32_t mask, uint32_t value);
int safeprobe_wait(struct safeprobe_ctx *ctx, bus_size_t off,
    uint32_t mask, uint32_t value, int timeout_us);

#endif /* _KERNEL */

#endif /* _SAFEPROBE_H_ */
