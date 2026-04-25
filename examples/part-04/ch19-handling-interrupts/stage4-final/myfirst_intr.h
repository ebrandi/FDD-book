/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_intr.h -- Chapter 19 interrupt-handler interface.
 *
 * Exposes the three functions the PCI layer calls to bring up and
 * tear down the interrupt path, the sysctl-registration helper, and
 * a pair of interrupt-context accessor macros that do not assert
 * sc->mtx is held (so they are safe inside the filter).
 */

#ifndef _MYFIRST_INTR_H_
#define _MYFIRST_INTR_H_

#include <sys/types.h>
#include <sys/taskqueue.h>

struct myfirst_softc;
struct resource;

/*
 * Allocate the IRQ resource, register the filter handler, create
 * the deferred-work taskqueue, and enable the device's interrupts
 * (by writing to INTR_MASK). Returns 0 on success, errno on failure.
 * On failure, the function undoes any partial setup.
 */
int  myfirst_intr_setup(struct myfirst_softc *sc);

/*
 * Disable the device's interrupts, tear down the handler, drain
 * and destroy the taskqueue, and release the IRQ resource. Safe
 * to call against a partially-set-up state.
 */
void myfirst_intr_teardown(struct myfirst_softc *sc);

/*
 * Register the interrupt-related sysctls (counters and the
 * intr_simulate sysctl). Call after the softc's sysctl tree is
 * established.
 */
void myfirst_intr_add_sysctls(struct myfirst_softc *sc);

/*
 * Interrupt-context accessor macros. These use bus_read_4 /
 * bus_write_4 directly against the resource without asserting
 * any lock. Safe in the filter; use CSR_READ_4 / CSR_WRITE_4 in
 * every other context.
 */
#define ICSR_READ_4(sc, off) \
	bus_read_4((sc)->bar_res, (off))
#define ICSR_WRITE_4(sc, off, val) \
	bus_write_4((sc)->bar_res, (off), (val))

/*
 * The filter handler is exported so the simulated-interrupt
 * sysctl can invoke it directly.
 */
int myfirst_intr_filter(void *arg);

#endif /* _MYFIRST_INTR_H_ */
