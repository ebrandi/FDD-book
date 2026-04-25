/*
 * bugdemo.h - Lab 5 shared header.
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

#define	BUGDEMO_OP_ALLOC		0
#define	BUGDEMO_OP_USE_AFTER_FREE	1
#define	BUGDEMO_OP_MAX			2

#define	BUGDEMO_TRIGGER		_IOWR('B', 1, struct bugdemo_command)

#endif /* _BUGDEMO_H_ */
