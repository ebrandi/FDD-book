/*
 * bugdemo.h - Lab 3 shared header.
 *
 * Exposes two ioctl paths with deliberate lock ordering disagreement
 * to trigger WITNESS warnings on a debug kernel.
 */

#ifndef _BUGDEMO_H_
#define	_BUGDEMO_H_

#include <sys/ioccom.h>
#include <sys/types.h>

struct bugdemo_command {
	uint32_t	op;
	uint32_t	flags;
	uint64_t	arg;
};

/* Lock order paths. */
#define	BUGDEMO_OP_LOCK_A	0	/* lock1 -> lock2 */
#define	BUGDEMO_OP_LOCK_B	1	/* lock2 -> lock1 */
#define	BUGDEMO_OP_MAX		2

#define	BUGDEMO_TRIGGER		_IOWR('B', 1, struct bugdemo_command)

#endif /* _BUGDEMO_H_ */
