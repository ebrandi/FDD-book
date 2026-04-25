/*
 * bugdemo.h - Challenge 3 (deadlock) shared header.
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

#define	BUGDEMO_OP_PATH_A	0
#define	BUGDEMO_OP_PATH_B	1
#define	BUGDEMO_OP_MAX		2

#define	BUGDEMO_TRIGGER		_IOWR('B', 1, struct bugdemo_command)

#endif /* _BUGDEMO_H_ */
