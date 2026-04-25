/*
 * myfirst_ioctl.h - public ioctl interface for the myfirst driver.
 *
 * This header is included by both the kernel module and any user-space
 * program that talks to the driver.  Keep it self-contained: no kernel
 * headers, no kernel types, no inline functions that pull kernel state.
 *
 * The header is the contract between the driver and the outside world.
 * Bumping MYFIRST_IOCTL_VERSION is the signal that this contract has
 * changed in a way that may not be backward-compatible.
 */

#ifndef _MYFIRST_IOCTL_H_
#define _MYFIRST_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

/*
 * Maximum length of the in-driver message, including the trailing NUL.
 * The driver enforces this on SETMSG; user-space programs that build
 * larger buffers will see EINVAL.
 *
 * Well under IOCPARM_MAX (8192), so the kernel's automatic copyin /
 * copyout machinery will move the buffer without complaint.
 */
#define MYFIRST_MSG_MAX		256

/*
 * The interface version.  Bumped when this header changes in a way
 * that is not backward-compatible.  User-space programs should call
 * MYFIRSTIOC_GETVER first and refuse to operate on an unexpected
 * version.
 */
#define MYFIRST_IOCTL_VERSION	1

/*
 * MYFIRSTIOC_GETVER - return the driver's interface version.
 *
 *   uint32_t ver;
 *   ioctl(fd, MYFIRSTIOC_GETVER, &ver);   // ver == 1, 2, ...
 *
 * No FREAD or FWRITE flag is required.
 */
#define MYFIRSTIOC_GETVER	_IOR('M', 1, uint32_t)

/*
 * MYFIRSTIOC_GETMSG - copy the current in-driver message into the
 * caller's buffer.  The buffer must be MYFIRST_MSG_MAX bytes; the
 * message is NUL-terminated.
 */
#define MYFIRSTIOC_GETMSG	_IOR('M', 2, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_SETMSG - replace the in-driver message.  The buffer must
 * be MYFIRST_MSG_MAX bytes; the kernel takes the prefix up to the
 * first NUL or to MYFIRST_MSG_MAX - 1 bytes.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_SETMSG	_IOW('M', 3, char[MYFIRST_MSG_MAX])

/*
 * MYFIRSTIOC_RESET - reset all per-instance counters and clear the
 * message.  Returns 0 on success.
 *
 * Requires FWRITE on the file descriptor.
 */
#define MYFIRSTIOC_RESET	_IO('M', 4)

#endif /* _MYFIRST_IOCTL_H_ */
