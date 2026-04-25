/*
 * detectmod.c - Hypervisor detection kernel module
 *
 * Chapter 30, Lab 2. Reads the global vm_guest variable on module load
 * and prints a descriptive message naming the hypervisor environment.
 * Demonstrates the simplest form of environment-aware driver behaviour.
 *
 * Build:
 *   make clean && make
 * Load:
 *   sudo kldload ./detectmod.ko
 * Unload:
 *   sudo kldunload detectmod
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

static const char *
vm_guest_name(int guest)
{
	switch (guest) {
	case VM_GUEST_NO:		return ("bare metal");
	case VM_GUEST_VM:		return ("generic VM");
	case VM_GUEST_XEN:		return ("Xen");
	case VM_GUEST_HV:		return ("Microsoft Hyper-V");
	case VM_GUEST_VMWARE:		return ("VMware");
	case VM_GUEST_KVM:		return ("KVM");
	case VM_GUEST_BHYVE:		return ("bhyve");
	case VM_GUEST_VBOX:		return ("VirtualBox");
	case VM_GUEST_PARALLELS:	return ("Parallels");
	case VM_GUEST_NVMM:		return ("NVMM");
	default:			return ("unknown");
	}
}

static int
detectmod_modevent(module_t mod, int event, void *arg)
{
	switch (event) {
	case MOD_LOAD:
		printf("detectmod: running on %s (vm_guest=%d)\n",
		    vm_guest_name(vm_guest), vm_guest);
		return (0);
	case MOD_UNLOAD:
		printf("detectmod: goodbye\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t detectmod_mod = {
	"detectmod",
	detectmod_modevent,
	NULL
};

DECLARE_MODULE(detectmod, detectmod_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(detectmod, 1);
