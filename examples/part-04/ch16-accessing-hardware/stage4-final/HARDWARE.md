# myfirst Hardware Interface

## Version

`0.9-mmio`. Chapter 16 complete.

## Overview

The `myfirst` driver exposes a simulated 64-byte register block
through the FreeBSD `bus_space(9)` API. On x86 the block is backed
by kernel memory allocated with `malloc(9)`; the `bus_space_tag_t`
is `X86_BUS_SPACE_MEM` and the `bus_space_handle_t` is the kernel
virtual address of the allocation.

All register access is lock-protected by the driver's main mutex
`sc->mtx`. The accessor helpers assert lock ownership, so bugs
that skip locking are caught on debug kernels via `WITNESS`.

## Register Map

| Offset | Width  | Name            | Direction | Reset value           |
|--------|--------|-----------------|-----------|-----------------------|
| 0x00   | 32 bit | CTRL            | R/W       | 0x00000000            |
| 0x04   | 32 bit | STATUS          | R/W       | 0x00000001 (READY)    |
| 0x08   | 32 bit | DATA_IN         | R/W       | 0x00000000            |
| 0x0c   | 32 bit | DATA_OUT        | R/W       | 0x00000000            |
| 0x10   | 32 bit | INTR_MASK       | R/W       | 0x00000000            |
| 0x14   | 32 bit | INTR_STATUS     | R/W       | 0x00000000            |
| 0x18   | 32 bit | DEVICE_ID       | R         | 0x4D594649 ('MYFI')   |
| 0x1c   | 32 bit | FIRMWARE_REV    | R         | 0x00010000 (1.0)      |
| 0x20   | 32 bit | SCRATCH_A       | R/W       | 0x00000000            |
| 0x24   | 32 bit | SCRATCH_B       | R/W       | 0x00000000            |

Offsets 0x28 through 0x3f are reserved. Reads return 0; writes are
accepted but ignored by driver policy.

## Bit Fields

### CTRL (0x00)

| Bit  | Name             | Description                           |
|------|------------------|---------------------------------------|
| 0    | ENABLE           | Device enabled                        |
| 1    | RESET            | Reset (Chapter 17)                    |
| 4..7 | MODE             | Operating mode                        |
| 8    | LOOPBACK         | Route DATA_IN to DATA_OUT             |

### STATUS (0x04)

| Bit  | Name             | Description                           |
|------|------------------|---------------------------------------|
| 0    | READY            | Device attached and operational       |
| 1    | BUSY             | Operation in progress (Chapter 17)    |
| 2    | ERROR            | Error latch (Chapter 17)              |
| 3    | DATA_AV          | DATA_OUT has data                     |

### INTR_MASK and INTR_STATUS (0x10, 0x14)

| Bit  | Name             | Description                           |
|------|------------------|---------------------------------------|
| 0    | DATA_AV          | Data available                        |
| 1    | ERROR            | Error condition                       |
| 2    | COMPLETE         | Operation complete                    |

Chapter 19 wires interrupts to these bits. Chapter 16 leaves them
static.

## Per-Register Owners

| Register       | Who writes it                                     |
|----------------|---------------------------------------------------|
| CTRL           | sysctl writer; `myfirst_ctrl_update` side effect  |
| STATUS         | driver (via write/read paths)                     |
| DATA_IN        | syscall write path                                |
| DATA_OUT       | syscall read path                                 |
| INTR_MASK      | sysctl writer (Chapter 19 will add attach path)   |
| INTR_STATUS    | sysctl writer (Chapter 17 will add write-1-clear) |
| DEVICE_ID      | attach only                                       |
| FIRMWARE_REV   | attach only                                       |
| SCRATCH_A      | reg_ticker task; sysctl writer                    |
| SCRATCH_B      | sysctl writer                                     |

## API

All register access goes through macros defined in `myfirst_hw.h`:

```c
uint32_t v = CSR_READ_4(sc, MYFIRST_REG_STATUS);
CSR_WRITE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE);
CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);
```

Every call must be made with `sc->mtx` held. The accessors assert
this with `MYFIRST_ASSERT`.

For barrier-aware writes:

```c
myfirst_reg_write_barrier(sc, MYFIRST_REG_CTRL, value,
    BUS_SPACE_BARRIER_WRITE);
```

## Observability

| Sysctl                                     | Purpose                                   |
|--------------------------------------------|-------------------------------------------|
| `dev.myfirst.0.reg_*`                      | Read each register                        |
| `dev.myfirst.0.reg_ctrl_set`               | Write CTRL (Stage 1 test aid)             |
| `dev.myfirst.0.reg_ticker_enabled`         | Toggle the SCRATCH_A ticker               |
| `dev.myfirst.0.access_log_enabled`         | Start/stop access logging                 |
| `dev.myfirst.0.access_log`                 | Dump the access ring buffer               |

DTrace probes (if the driver is built with `MYFIRST_DEBUG_REG_TRACE`):

```text
# dtrace -n 'fbt::myfirst_reg_write:entry {
    printf("off=%#x val=%#x", arg1, arg2);
}'
```

## Architecture Portability

The simulation shortcut (tag = `X86_BUS_SPACE_MEM`, handle = kernel
VA) is x86-only. On other architectures the build fails loudly
(`#error`). Chapter 17 introduces a portable simulation; Chapter 18
replaces the simulation with real PCI hardware, which is portable
by design.

## Lock Order

Hardware access does not introduce a new lock. Chapter 15's order
remains:

```text
sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx
```

All register access happens under `sc->mtx`.

## Detach Ordering (hardware-relevant entries)

The full detach ordering is documented in `LOCKING.md`. The
hardware-relevant steps are:

1. Clear `is_attached`.
2. Drain callouts (heartbeat, watchdog, tick_source).
3. Drain tasks, including `reg_ticker_task`.
4. `myfirst_hw_detach(sc)`: frees `regs_buf`.
5. Destroy cvs, sx, semaphore, mutex.
