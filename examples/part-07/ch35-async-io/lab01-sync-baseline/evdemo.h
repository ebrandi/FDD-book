/*
 * evdemo.h - shared user/kernel definitions for the evdemo driver.
 *
 * The evdemo pseudo-device exposes a queue of synthetic events. This
 * header defines the event record that userland reads from the
 * /dev/evdemo device node.
 */

#ifndef _EVDEMO_H_
#define	_EVDEMO_H_

#include <sys/types.h>
#include <sys/time.h>

/*
 * A single event record. The layout mirrors the evdev(4) event
 * convention: a timestamp plus a (type, code, value) triple.
 */
struct evdemo_event {
	struct timespec	ev_time;	/* kernel time at event */
	uint32_t	ev_type;	/* event type */
	uint32_t	ev_code;	/* event code */
	int64_t		ev_value;	/* event value */
};

/* Event types. Extend as needed. */
#define	EVDEMO_EV_TYPE_TICK	1	/* periodic tick from callout */
#define	EVDEMO_EV_TYPE_TRIGGER	2	/* synthetic trigger from sysctl */
#define	EVDEMO_EV_TYPE_BURST	3	/* event from a burst */

#endif /* _EVDEMO_H_ */
