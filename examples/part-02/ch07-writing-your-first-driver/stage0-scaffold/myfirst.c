/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Your Name
 * All rights reserved.
 *
 * Stage 0 of Chapter 7: minimal kernel module scaffold.
 *
 * This version does not register a device, does not allocate any
 * resources, and does not create /dev nodes. Its only job is to prove
 * that the build system works and that kldload/kldunload can call into
 * our module event handler.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

/*
 * Module load/unload event handler.
 *
 * Called with what == MOD_LOAD when the module is loaded and with
 * what == MOD_UNLOAD when it is unloaded.
 */
static int
myfirst_loader(module_t mod, int what, void *arg)
{
	int error = 0;

	switch (what) {
	case MOD_LOAD:
		printf("myfirst: driver loaded\n");
		break;
	case MOD_UNLOAD:
		printf("myfirst: driver unloaded\n");
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

static moduledata_t myfirst_mod = {
	"myfirst",		/* module name */
	myfirst_loader,		/* event handler */
	NULL			/* extra arg (unused here) */
};

DECLARE_MODULE(myfirst, myfirst_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myfirst, 1);
