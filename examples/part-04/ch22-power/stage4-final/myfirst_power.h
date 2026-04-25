/*-
 * Chapter 22 Stage 4: public power-management API for the
 * myfirst driver.
 *
 * The power subsystem implements DEVICE_SUSPEND, DEVICE_RESUME, and
 * DEVICE_SHUTDOWN by coordinating a quiesce (mask interrupts, drain
 * DMA, drain workers) and a restore (re-enable bus-master, restore
 * registers, clear suspended flag). It optionally implements runtime
 * power management when built with -DMYFIRST_ENABLE_RUNTIME_PM.
 */

#ifndef _MYFIRST_POWER_H_
#define _MYFIRST_POWER_H_

struct myfirst_softc;

/* Subsystem lifecycle. */
int	myfirst_power_setup(struct myfirst_softc *sc);
void	myfirst_power_teardown(struct myfirst_softc *sc);

/* Power-management method implementations. Called from myfirst_pci.c. */
int	myfirst_power_suspend(struct myfirst_softc *sc);
int	myfirst_power_resume(struct myfirst_softc *sc);
int	myfirst_power_shutdown(struct myfirst_softc *sc);

#ifdef MYFIRST_ENABLE_RUNTIME_PM
/* Runtime power management: idle-triggered D0 <-> D3 transitions. */
int	myfirst_power_runtime_suspend(struct myfirst_softc *sc);
int	myfirst_power_runtime_resume(struct myfirst_softc *sc);
void	myfirst_power_mark_active(struct myfirst_softc *sc);
#endif

/* Sysctl registration. */
void	myfirst_power_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_POWER_H_ */
