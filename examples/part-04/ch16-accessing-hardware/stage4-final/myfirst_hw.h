/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_hw.h -- Chapter 16 Stage 4 simulated hardware interface.
 *
 * Adds hardware-access layer to the Chapter 15 Stage 4 driver:
 *   - Register offset and bit-mask definitions.
 *   - A struct myfirst_hw grouping hardware state.
 *   - API for attach/detach of the hardware layer.
 *   - Accessor helpers (myfirst_reg_read, myfirst_reg_write, ...).
 *   - CSR_READ_4, CSR_WRITE_4, CSR_UPDATE_4 macros.
 */

#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/taskqueue.h>
#include <machine/bus.h>

/*
 * Register offsets for the simulated myfirst widget. Total register
 * block size is 64 bytes, though only offsets 0x00..0x27 are
 * currently defined; the remaining bytes are reserved for Chapter 17
 * expansion.
 */
#define MYFIRST_REG_CTRL         0x00   /* 32-bit, R/W: control       */
#define MYFIRST_REG_STATUS       0x04   /* 32-bit, R/W: status        */
#define MYFIRST_REG_DATA_IN      0x08   /* 32-bit, R/W: input data    */
#define MYFIRST_REG_DATA_OUT     0x0c   /* 32-bit, R/W: output data   */
#define MYFIRST_REG_INTR_MASK    0x10   /* 32-bit, R/W: intr enables  */
#define MYFIRST_REG_INTR_STATUS  0x14   /* 32-bit, R/W: intr flags    */
#define MYFIRST_REG_DEVICE_ID    0x18   /* 32-bit, R-only: device id  */
#define MYFIRST_REG_FIRMWARE_REV 0x1c   /* 32-bit, R-only: fw rev     */
#define MYFIRST_REG_SCRATCH_A    0x20   /* 32-bit, R/W: scratch A     */
#define MYFIRST_REG_SCRATCH_B    0x24   /* 32-bit, R/W: scratch B     */

#define MYFIRST_REG_SIZE         0x40   /* total 64 bytes             */

/* CTRL register bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u   /* bit 0: enable        */
#define MYFIRST_CTRL_RESET       0x00000002u   /* bit 1: reset         */
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u   /* bits 4..7: mode      */
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u   /* bit 8: loopback      */

/* STATUS register bits. */
#define MYFIRST_STATUS_READY     0x00000001u   /* bit 0: ready         */
#define MYFIRST_STATUS_BUSY      0x00000002u   /* bit 1: busy          */
#define MYFIRST_STATUS_ERROR     0x00000004u   /* bit 2: error latch   */
#define MYFIRST_STATUS_DATA_AV   0x00000008u   /* bit 3: data in out   */

/* INTR_MASK / INTR_STATUS bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u   /* bit 0: data avail.   */
#define MYFIRST_INTR_ERROR       0x00000002u   /* bit 1: error         */
#define MYFIRST_INTR_COMPLETE    0x00000004u   /* bit 2: op complete   */

/* Fixed identifier values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u   /* 'MYFI' LE            */
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE \
	((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

/* Access log. */
#define MYFIRST_ACCESS_LOG_SIZE  64

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
 * Hardware state, grouped. A single allocation per softc; sc->hw
 * points at this struct when the hardware layer is attached.
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

/* Forward declaration. */
struct myfirst_softc;

/* Attach/detach the hardware layer. Called from the main driver. */
int  myfirst_hw_attach(struct myfirst_softc *sc);
void myfirst_hw_detach(struct myfirst_softc *sc);

/* Register sysctls for the hardware layer. */
void myfirst_hw_add_sysctls(struct myfirst_softc *sc);

/* Accessor functions. Must be called with sc->mtx held. */
uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value);
void     myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t clear_mask, uint32_t set_mask);
void     myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value, int flags);

/*
 * CSR_* macros matching the idiom used in production FreeBSD drivers
 * such as /usr/src/sys/dev/ale/if_alevar.h. Short names at call sites,
 * standard semantics.
 */
#define CSR_READ_4(sc, off)        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val)  myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
	myfirst_reg_update((sc), (off), (clear), (set))

#endif /* _MYFIRST_HW_H_ */
