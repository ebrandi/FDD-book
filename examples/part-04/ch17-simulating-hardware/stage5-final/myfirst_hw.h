/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_hw.h -- Chapter 17 Stage 5 hardware interface.
 *
 * Extended from Chapter 16:
 *   - Register map grows with Chapter 17 additions (0x28..0x3c)
 *   - CTRL.GO bit added for command triggering
 *   - Fault-mode constants added
 */

#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>
#include <machine/bus.h>

/*
 * Register offsets. Total register block is 64 bytes.
 */
#define MYFIRST_REG_CTRL          0x00   /* 32-bit, R/W: control       */
#define MYFIRST_REG_STATUS        0x04   /* 32-bit, R/W: status        */
#define MYFIRST_REG_DATA_IN       0x08   /* 32-bit, R/W: input data    */
#define MYFIRST_REG_DATA_OUT      0x0c   /* 32-bit, R/W: output data   */
#define MYFIRST_REG_INTR_MASK     0x10   /* 32-bit, R/W: intr enables  */
#define MYFIRST_REG_INTR_STATUS   0x14   /* 32-bit, R/W: intr flags    */
#define MYFIRST_REG_DEVICE_ID     0x18   /* 32-bit, RO: device id      */
#define MYFIRST_REG_FIRMWARE_REV  0x1c   /* 32-bit, RO: fw rev         */
#define MYFIRST_REG_SCRATCH_A     0x20   /* 32-bit, R/W: scratch A     */
#define MYFIRST_REG_SCRATCH_B     0x24   /* 32-bit, R/W: scratch B     */

/* Chapter 17 additions. */
#define MYFIRST_REG_SENSOR        0x28   /* 32-bit, RO: sensor value   */
#define MYFIRST_REG_SENSOR_CONFIG 0x2c   /* 32-bit, R/W: sensor config */
#define MYFIRST_REG_DELAY_MS      0x30   /* 32-bit, R/W: cmd delay     */
#define MYFIRST_REG_FAULT_MASK    0x34   /* 32-bit, R/W: fault mask    */
#define MYFIRST_REG_FAULT_PROB    0x38   /* 32-bit, R/W: fault prob    */
#define MYFIRST_REG_OP_COUNTER    0x3c   /* 32-bit, RO: op counter     */

#define MYFIRST_REG_SIZE          0x40   /* total 64 bytes             */

/* CTRL register bits. */
#define MYFIRST_CTRL_ENABLE       0x00000001u
#define MYFIRST_CTRL_RESET        0x00000002u
#define MYFIRST_CTRL_MODE_MASK    0x000000f0u
#define MYFIRST_CTRL_MODE_SHIFT   4
#define MYFIRST_CTRL_LOOPBACK     0x00000100u
#define MYFIRST_CTRL_GO           0x00000200u   /* Ch17: start command  */

/* STATUS register bits. */
#define MYFIRST_STATUS_READY      0x00000001u
#define MYFIRST_STATUS_BUSY       0x00000002u
#define MYFIRST_STATUS_ERROR      0x00000004u
#define MYFIRST_STATUS_DATA_AV    0x00000008u

/* INTR_MASK / INTR_STATUS bits. */
#define MYFIRST_INTR_DATA_AV      0x00000001u
#define MYFIRST_INTR_ERROR        0x00000002u
#define MYFIRST_INTR_COMPLETE     0x00000004u

/* Fixed identifier values. */
#define MYFIRST_DEVICE_ID_VALUE   0x4D594649u
#define MYFIRST_FW_REV_MAJOR      1
#define MYFIRST_FW_REV_MINOR      0
#define MYFIRST_FW_REV_VALUE \
	((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

/* SENSOR_CONFIG register fields. */
#define MYFIRST_SCFG_INTERVAL_MASK   0x0000ffffu
#define MYFIRST_SCFG_INTERVAL_SHIFT  0
#define MYFIRST_SCFG_AMPLITUDE_MASK  0xffff0000u
#define MYFIRST_SCFG_AMPLITUDE_SHIFT 16

/* FAULT_MASK register bits. */
#define MYFIRST_FAULT_TIMEOUT     0x00000001u
#define MYFIRST_FAULT_READ_1S     0x00000002u
#define MYFIRST_FAULT_ERROR       0x00000004u
#define MYFIRST_FAULT_STUCK_BUSY  0x00000008u

/* Access log. */
#define MYFIRST_ACCESS_LOG_SIZE   64

struct myfirst_access_log_entry {
	uint64_t	timestamp_ns;
	uint32_t	value;
	bus_size_t	offset;
	uint8_t		is_write;
	uint8_t		width;
	uint8_t		context_tag;
	uint8_t		_pad;
};

/*
 * Hardware state, grouped. Pointed at from sc->hw.
 */
struct myfirst_hw {
	uint8_t			*regs_buf;
	size_t			 regs_size;
	bus_space_tag_t		 regs_tag;
	bus_space_handle_t	 regs_handle;

	struct task		 reg_ticker_task;
	int			 reg_ticker_enabled;

	struct myfirst_access_log_entry	access_log[MYFIRST_ACCESS_LOG_SIZE];
	unsigned int		 access_log_head;
	bool			 access_log_enabled;
};

struct myfirst_softc;

/* Hardware layer API. */
int  myfirst_hw_attach(struct myfirst_softc *sc);
void myfirst_hw_detach(struct myfirst_softc *sc);
void myfirst_hw_add_sysctls(struct myfirst_softc *sc);

/* Accessors. sc->mtx must be held. */
uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value);
void     myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t clear_mask, uint32_t set_mask);
void     myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value, int flags);

/* Side-effect helper for CTRL writes. Chapter 17 extends it. */
void     myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old,
             uint32_t new);

/* Shared sysctl write handler used by both hw and sim layers. */
int      myfirst_sysctl_reg_write(struct sysctl_oid *oidp, void *arg1,
             intmax_t arg2, struct sysctl_req *req);

/* CSR_* macros mirroring the idiom used in production FreeBSD drivers. */
#define CSR_READ_4(sc, off)        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val)  myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
	myfirst_reg_update((sc), (off), (clear), (set))

#endif /* _MYFIRST_HW_H_ */
