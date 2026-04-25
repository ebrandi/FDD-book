/*
 * bugdemo.h - shared user/kernel header for the Lab 2 variant.
 *
 * Adds a subcommand that clears the driver's softc pointer and
 * triggers an immediate panic, so the reader can walk the resulting
 * crash dump with kgdb.
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

#define	BUGDEMO_OP_NOOP		0
#define	BUGDEMO_OP_HELLO	1
#define	BUGDEMO_OP_COUNT	2
#define	BUGDEMO_OP_MAX		3

#define	BUGDEMO_FLAG_FORCE_BAD_OP	0x0001
#define	BUGDEMO_FLAG_NULL_SOFTC		0x0002

#define	BUGDEMO_TRIGGER		_IOWR('B', 1, struct bugdemo_command)

#endif /* _BUGDEMO_H_ */
