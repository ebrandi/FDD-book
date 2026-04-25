/*
 * evdemo.h - shared user/kernel definitions for the evdemo driver.
 *
 * Version 2.5-async reference implementation.
 */

#ifndef _EVDEMO_H_
#define	_EVDEMO_H_

#include <sys/types.h>
#include <sys/time.h>

struct evdemo_event {
	struct timespec	ev_time;
	uint32_t	ev_type;
	uint32_t	ev_code;
	int64_t		ev_value;
};

#define	EVDEMO_EV_TYPE_TICK	1
#define	EVDEMO_EV_TYPE_TRIGGER	2
#define	EVDEMO_EV_TYPE_BURST	3

#endif /* _EVDEMO_H_ */
