/*
 * bugdemo.h - Lab 4 shared header.
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
#define	BUGDEMO_OP_BAD		3	/* deliberately returns EIO */
#define	BUGDEMO_OP_MAX		4

#define	BUGDEMO_TRIGGER		_IOWR('B', 1, struct bugdemo_command)

#endif /* _BUGDEMO_H_ */
