/*
 * myfirst_ioctl.c - ioctl dispatch for the myfirst driver.
 *
 * The switch is the driver's entire public ioctl surface.  Adding a
 * command means adding a case here and the matching declaration in
 * myfirst_ioctl.h.  Retiring a command means removing its case here
 * and marking the number retired in the header.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	int error = 0;

	DPRINTF(sc, MYF_DBG_IOCTL, "ioctl: cmd=0x%lx\n", cmd);

	switch (cmd) {
	case MYFIRSTIOC_GETVER:
		*(int *)data = MYFIRST_IOCTL_VERSION;
		break;

	case MYFIRSTIOC_RESET:
		mtx_lock(&sc->sc_mtx);
		sc->sc_total_reads  = 0;
		sc->sc_total_writes = 0;
		mtx_unlock(&sc->sc_mtx);
		break;

	case MYFIRSTIOC_GETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
		mtx_unlock(&sc->sc_mtx);
		break;

	case MYFIRSTIOC_SETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy(sc->sc_msg, (const char *)data,
		    sizeof(sc->sc_msg));
		sc->sc_msglen = strlen(sc->sc_msg);
		mtx_unlock(&sc->sc_mtx);
		devctl_notify("myfirst", device_get_nameunit(sc->sc_dev),
		    "MSG_CHANGED", NULL);
		break;

	case MYFIRSTIOC_GETCAPS:
		*(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
		                    MYF_CAP_SETMSG;
		break;

	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}
