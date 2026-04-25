/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_msix.h -- Chapter 20 multi-vector interrupt interface.
 *
 * Adds a three-tier fallback ladder (MSI-X, MSI, legacy INTx) to the
 * Chapter 19 single-vector filter+task design. On MSI-X or MSI, the
 * driver registers three per-vector filter handlers (admin, rx, tx)
 * with per-vector state and per-vector CPU binding. On legacy INTx,
 * the driver falls back to the Chapter 19 single-filter path.
 */

#ifndef _MYFIRST_MSIX_H_
#define _MYFIRST_MSIX_H_

#include <sys/types.h>
#include <sys/taskqueue.h>

struct myfirst_softc;

enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX = 1,
	MYFIRST_VECTOR_TX = 2,
	MYFIRST_MAX_VECTORS = 3
};

/*
 * Per-vector state. One instance per allocated vector, stored in an
 * array in the softc. On MSI-X or MSI each vector's filter takes a
 * pointer to its own instance as the argument to bus_setup_intr.
 */
struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

/*
 * Public API. Called from myfirst_pci.c during attach and detach.
 */
int  myfirst_msix_setup(struct myfirst_softc *sc);
void myfirst_msix_teardown(struct myfirst_softc *sc);
void myfirst_msix_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_MSIX_H_ */
