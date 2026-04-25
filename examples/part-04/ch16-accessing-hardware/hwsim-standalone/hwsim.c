/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * hwsim.c -- Chapter 16 stand-alone bus_space(9) practice module.
 *
 * Demonstrates the bus_space(9) API on a simulated register block
 * allocated with malloc(9). Exposes two 32-bit registers through
 * sysctl so the reader can read and write them from user space.
 *
 * This module is x86-specific. On x86, bus_space_tag_t is an integer
 * and the memory-space bus_space_*_4 functions reduce to a volatile
 * dereference of (handle + offset), which lets us back them with
 * plain kernel memory. On other architectures the tag has different
 * semantics and the shortcut does not apply.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <machine/bus.h>

MALLOC_DEFINE(M_HWSIM, "hwsim", "hwsim test module");

#define HWSIM_SIZE  0x40

static uint8_t            *hwsim_buf;
static bus_space_tag_t     hwsim_tag;
static bus_space_handle_t  hwsim_handle;

static SYSCTL_NODE(_dev, OID_AUTO, hwsim,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, "hwsim practice module");

static int
hwsim_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
	bus_size_t offset = arg2;
	uint32_t value;
	int error;

	if (hwsim_buf == NULL)
		return (ENODEV);

	value = bus_space_read_4(hwsim_tag, hwsim_handle, offset);
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	bus_space_write_4(hwsim_tag, hwsim_handle, offset, value);
	return (0);
}

static int
hwsim_modevent(module_t mod, int event, void *arg)
{
	switch (event) {
	case MOD_LOAD:
#if !defined(__amd64__) && !defined(__i386__)
		printf("hwsim: unsupported architecture\n");
		return (EOPNOTSUPP);
#else
		hwsim_buf = malloc(HWSIM_SIZE, M_HWSIM, M_WAITOK | M_ZERO);
		hwsim_tag = X86_BUS_SPACE_MEM;
		hwsim_handle = (bus_space_handle_t)(uintptr_t)hwsim_buf;

		SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
		    OID_AUTO, "reg0",
		    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    NULL, 0x00, hwsim_sysctl_reg, "IU",
		    "Register at offset 0x00 (32-bit)");
		SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
		    OID_AUTO, "reg4",
		    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    NULL, 0x04, hwsim_sysctl_reg, "IU",
		    "Register at offset 0x04 (32-bit)");
		SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
		    OID_AUTO, "reg8",
		    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    NULL, 0x08, hwsim_sysctl_reg, "IU",
		    "Register at offset 0x08 (32-bit)");
		SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
		    OID_AUTO, "regc",
		    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
		    NULL, 0x0c, hwsim_sysctl_reg, "IU",
		    "Register at offset 0x0c (32-bit)");

		printf("hwsim: loaded, %d bytes at %p\n",
		    HWSIM_SIZE, hwsim_buf);
		return (0);
#endif

	case MOD_UNLOAD:
		if (hwsim_buf != NULL) {
			free(hwsim_buf, M_HWSIM);
			hwsim_buf = NULL;
		}
		printf("hwsim: unloaded\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t hwsim_mod = {
	"hwsim",
	hwsim_modevent,
	NULL
};

DECLARE_MODULE(hwsim, hwsim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(hwsim, 1);
