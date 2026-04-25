/*
 * myfirst.h - public types and constants for the myfirst driver.
 *
 * Types and prototypes declared here are visible to every .c file in
 * the driver.  Keep this header small.  Wire-format declarations live
 * in myfirst_ioctl.h.  Debug macros live in myfirst_debug.h.  Rate-
 * limit state lives in myfirst_log.h.
 *
 * Driver version at Chapter 25: 1.8-maintenance.
 */

#ifndef _MYFIRST_H_
#define _MYFIRST_H_

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/eventhandler.h>

#include "myfirst_log.h"

#define MYFIRST_MSG_MAX  256

struct myfirst_softc {
	device_t       sc_dev;
	struct mtx     sc_mtx;
	struct cdev   *sc_cdev;

	char           sc_msg[MYFIRST_MSG_MAX];
	size_t         sc_msglen;

	u_int          sc_open_count;
	u_int          sc_total_reads;
	u_int          sc_total_writes;

	u_int          sc_debug;
	u_int          sc_timeout_sec;
	u_int          sc_max_retries;
	u_int          sc_log_pps;

	struct myfirst_ratelimit sc_rl_generic;
	struct myfirst_ratelimit sc_rl_io;
	struct myfirst_ratelimit sc_rl_intr;

	eventhandler_tag sc_shutdown_tag;
};

/* Sysctl tree. */
void myfirst_sysctl_attach(struct myfirst_softc *);

/* Rate-limit state. */
int  myfirst_log_attach(struct myfirst_softc *);
void myfirst_log_detach(struct myfirst_softc *);

/* Ioctl dispatch. */
struct thread;
int  myfirst_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);

/* cdev callbacks. */
d_open_t  myfirst_open;
d_close_t myfirst_close;
d_read_t  myfirst_read;
d_write_t myfirst_write;

#endif /* _MYFIRST_H_ */
