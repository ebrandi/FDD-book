/*
 * myfirst_ioctl.c - ioctl dispatcher for the myfirst driver.
 *
 * The d_ioctl callback in myfirst_cdevsw points at myfirst_ioctl.
 * Per-command argument layout is documented in myfirst_ioctl.h, which
 * is shared with user space.
 *
 * The dispatcher takes the softc mutex once at the top and releases
 * it once at the bottom, so every command is serialised against
 * read/write/close and against other ioctls.  Every command is short
 * enough that holding the mutex for the whole switch statement is
 * fine; a real driver that needed slow hardware operations would
 * release the mutex around them.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sdt.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

/*
 * The ioctl SDT probe is defined here rather than in myfirst_debug.c
 * because the probe arguments depend on the ioctl interface, which is
 * a Stage 2 addition.  The myfirst_debug.c file owns only the probes
 * that the Chapter 23 debug refactor introduced.
 */
SDT_PROBE_DEFINE3(myfirst, , , ioctl,
    "struct myfirst_softc *", "u_long", "int");

int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	int error = 0;

	SDT_PROBE3(myfirst, , , ioctl, sc, cmd, fflag);
	DPRINTF(sc, MYF_DBG_IOCTL, "ioctl: cmd=0x%08lx fflag=0x%x\n",
	    cmd, fflag);

	mtx_lock(&sc->sc_mtx);

	switch (cmd) {
	case MYFIRSTIOC_GETVER:
		*(uint32_t *)data = MYFIRST_IOCTL_VERSION;
		break;

	case MYFIRSTIOC_GETMSG:
		/*
		 * Copy the current message into the caller's buffer.
		 * The buffer is MYFIRST_MSG_MAX bytes; we always emit
		 * a NUL-terminated string.
		 */
		strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
		break;

	case MYFIRSTIOC_SETMSG:
		if ((fflag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		/*
		 * The kernel has copied MYFIRST_MSG_MAX bytes into
		 * data.  Take the prefix up to the first NUL.
		 */
		strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
		sc->sc_msglen = strlen(sc->sc_msg);
		DPRINTF(sc, MYF_DBG_IOCTL,
		    "SETMSG: new message is %zu bytes\n", sc->sc_msglen);
		break;

	case MYFIRSTIOC_RESET:
		if ((fflag & FWRITE) == 0) {
			error = EBADF;
			break;
		}
		sc->sc_open_count = 0;
		sc->sc_total_reads = 0;
		sc->sc_total_writes = 0;
		bzero(sc->sc_msg, sizeof(sc->sc_msg));
		sc->sc_msglen = 0;
		DPRINTF(sc, MYF_DBG_IOCTL,
		    "RESET: counters and message cleared\n");
		break;

	default:
		/*
		 * ENOIOCTL, never EINVAL.  Returning EINVAL would
		 * suppress the kernel's generic ioctl fallback for
		 * commands such as FIONBIO and FIOASYNC.
		 */
		error = ENOIOCTL;
		break;
	}

	mtx_unlock(&sc->sc_mtx);
	return (error);
}
