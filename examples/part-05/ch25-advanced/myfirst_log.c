/*
 * myfirst_log.c - attach/detach helpers for the rate-limit state.
 *
 * Today myfirst_log_attach does not allocate anything; the rate-limit
 * state is embedded by value in the softc, so the helper just zeroes
 * the fields.  The helper exists as a named label for the attach
 * chain and as a seam for future growth: if a later version of the
 * driver grows a per-class counter array, the allocation fits here
 * without changing the attach function's shape.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"

int
myfirst_log_attach(struct myfirst_softc *sc)
{

	bzero(&sc->sc_rl_generic, sizeof(sc->sc_rl_generic));
	bzero(&sc->sc_rl_io,      sizeof(sc->sc_rl_io));
	bzero(&sc->sc_rl_intr,    sizeof(sc->sc_rl_intr));
	return (0);
}

void
myfirst_log_detach(struct myfirst_softc *sc)
{

	(void)sc;
}
