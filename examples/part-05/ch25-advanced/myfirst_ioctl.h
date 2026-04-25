/*
 * myfirst_ioctl.h - ioctl command numbers and payload structures.
 *
 * Chapter 25 adds MYFIRSTIOC_GETCAPS (command 5) to the set inherited
 * from Chapter 22.  Command 4 was reserved during draft work and
 * retired before release; do not reuse the number.
 *
 * Wire format version is 1.  Increment MYFIRST_IOCTL_VERSION only for
 * genuine breaking changes (removed command, changed payload size or
 * layout, changed semantics).  Pure additions leave it alone.
 */

#ifndef _MYFIRST_IOCTL_H_
#define _MYFIRST_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

#define MYFIRST_IOCTL_VERSION  1

#define MYFIRST_MSG_MAX        256

#define MYFIRSTIOC_GETVER   _IOR('M', 0, int)
#define MYFIRSTIOC_RESET    _IO ('M', 1)
#define MYFIRSTIOC_GETMSG   _IOR('M', 2, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_SETMSG   _IOW('M', 3, char[MYFIRST_MSG_MAX])
/*
 * Command 4 retired; do not reuse.
 */
#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)

/* Capability bits returned by MYFIRSTIOC_GETCAPS. */
#define MYF_CAP_RESET      (1U << 0)
#define MYF_CAP_GETMSG     (1U << 1)
#define MYF_CAP_SETMSG     (1U << 2)
#define MYF_CAP_TIMEOUT    (1U << 3)   /* reserved; not set by 1.8 */
#define MYF_CAP_STATS      (1U << 4)   /* reserved for Challenge 8 */

#endif /* _MYFIRST_IOCTL_H_ */
