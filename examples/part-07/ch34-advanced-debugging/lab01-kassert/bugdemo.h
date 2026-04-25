/*
 * bugdemo.h - shared user-kernel definitions for the bugdemo driver.
 *
 * This header defines the ioctl interface used by the bugdemo
 * pseudo-device and its companion user-space test program.
 */

#ifndef _BUGDEMO_H_
#define	_BUGDEMO_H_

#include <sys/ioccom.h>
#include <sys/types.h>

/*
 * bugdemo_command - the argument struct for BUGDEMO_TRIGGER.
 *
 * The layout is fixed-width so user-space and kernel-space see the
 * same bytes regardless of compiler options. CTASSERT in the driver
 * verifies the size.
 */
struct bugdemo_command {
	uint32_t	op;
	uint32_t	flags;
	uint64_t	arg;
};

/* Operation codes. Add new ones at the end and update BUGDEMO_OP_MAX. */
#define	BUGDEMO_OP_NOOP		0
#define	BUGDEMO_OP_HELLO	1
#define	BUGDEMO_OP_COUNT	2
#define	BUGDEMO_OP_MAX		3

/* Flags passed with BUGDEMO_TRIGGER. */
#define	BUGDEMO_FLAG_FORCE_BAD_OP	0x0001
#define	BUGDEMO_FLAG_NULL_SOFTC		0x0002

/* ioctl commands */
#define	BUGDEMO_TRIGGER		_IOWR('B', 1, struct bugdemo_command)

#endif /* _BUGDEMO_H_ */
