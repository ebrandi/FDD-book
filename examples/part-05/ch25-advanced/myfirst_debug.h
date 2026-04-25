/*
 * myfirst_debug.h - DPRINTF macros, debug class bits, DLOG_RL.
 *
 * The debug framework was introduced in Chapter 23.  Chapter 25 adds
 * the DLOG_RL macro that wraps ppsratecheck(9) for hot-path logging.
 */

#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/kernel.h>
#include <sys/time.h>

#include "myfirst.h"

/*
 * Debug class bits.  The chapter's class list is:
 *   INIT  - attach/detach lifecycle.
 *   OPEN  - open/close events.
 *   IO    - read/write hot path.
 *   IOCTL - ioctl dispatch.
 *   INTR  - interrupt paths (placeholder for a future hardware port).
 *   DMA   - DMA operations (placeholder).
 *   PWR   - power management (placeholder).
 *   MEM   - memory allocation (placeholder).
 */
#define MYF_DBG_INIT   0x01
#define MYF_DBG_OPEN   0x02
#define MYF_DBG_IO     0x04
#define MYF_DBG_IOCTL  0x08
#define MYF_DBG_INTR   0x10
#define MYF_DBG_DMA    0x20
#define MYF_DBG_PWR    0x40
#define MYF_DBG_MEM    0x80

/*
 * DPRINTF: emit a device_printf only when the matching class bit is
 * set in sc->sc_debug.  Disabled classes add only a branch.
 */
#define DPRINTF(sc, class, fmt, ...) do {                       \
	if (((sc)->sc_debug & (class)) != 0)                    \
		device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);\
} while (0)

/*
 * DLOG_RL: rate-limited device_printf.  pps is the cap on how often
 * the call actually fires.  rlp is a struct myfirst_ratelimit pointer
 * stored in the softc.
 */
#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
	if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps))  \
		device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)

#endif /* _MYFIRST_DEBUG_H_ */
