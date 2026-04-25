/*
 * mockdev.h - Public interface of the mock device.
 *
 * Companion to Chapter 36, Lab 4.
 *
 * The mock device exposes a small "command and status" interface
 * via a character device at /dev/mockdev. The interface uses ioctl
 * to send commands and to read back status and result data.
 *
 * The simulated register layout, were the mock backed by real
 * hardware, would be:
 *
 *   offset 0x00 (CMD)    - write a command code; reads as 0
 *   offset 0x04 (STATUS) - status bits (BUSY, DONE, ERROR)
 *   offset 0x08 (DATA)   - result data after a successful command
 *   offset 0x0c (ID)     - constant chip identifier
 *
 * In this lab the registers are not memory-mapped onto a bus; the
 * mock implements them as fields in its softc and exposes them
 * through ioctl. The structure of the interface is the same as the
 * real device's would be, however, so that the test program can be
 * adapted to drive a real device with minimal changes.
 */

#ifndef _MOCKDEV_H_
#define	_MOCKDEV_H_

#include <sys/ioccom.h>

#define	MOCKDEV_DEVICE_NAME	"mockdev"

/* Command codes. The mock recognises only a few specific codes;
 * everything else is treated as an unknown command and reported as
 * an error in the status register. */
#define	MOCKDEV_CMD_NOP		0x00	/* no operation */
#define	MOCKDEV_CMD_RESET	0x01	/* reset internal state */
#define	MOCKDEV_CMD_READ_ID	0x02	/* read chip identifier */
#define	MOCKDEV_CMD_INCREMENT	0x03	/* increment internal counter */
#define	MOCKDEV_CMD_FAIL	0xfe	/* always reports failure */

/* Status register bits. */
#define	MOCKDEV_STATUS_BUSY	0x01
#define	MOCKDEV_STATUS_DONE	0x02
#define	MOCKDEV_STATUS_ERROR	0x04

/* Constant chip identifier returned by MOCKDEV_CMD_READ_ID. The
 * value is a magic number that tests can verify against. */
#define	MOCKDEV_CHIP_ID		0x4d4f434bU	/* "MOCK" */

/* ioctl interface. Each ioctl operates on a small command/status
 * structure that mirrors what a real driver would expose. */
struct mockdev_op {
	uint32_t	mo_cmd;		/* command code, input */
	uint32_t	mo_status;	/* status, output */
	uint32_t	mo_data;	/* result data, output */
};

#define	MOCKDEV_IOC_SUBMIT	_IOWR('M', 1, struct mockdev_op)
#define	MOCKDEV_IOC_READ_STATUS	_IOR('M', 2, struct mockdev_op)
#define	MOCKDEV_IOC_RESET	_IO('M', 3)

#endif /* _MOCKDEV_H_ */
