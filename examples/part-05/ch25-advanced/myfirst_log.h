/*
 * myfirst_log.h - rate-limit state for Chapter 25's logging discipline.
 *
 * The struct is embedded by value in the softc.  myfirst_log.c does
 * the attach and detach bookkeeping.  DLOG_RL (defined in
 * myfirst_debug.h) is the macro that consumes the state.
 */

#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define MYF_RL_DEFAULT_PPS  10

#endif /* _MYFIRST_LOG_H_ */
