# DMA Subsystem

## Purpose

The DMA layer allows the driver to transfer data between host memory
and the device without CPU-per-byte involvement. It is used for every
transfer larger than a few words; smaller register reads and writes
still use MMIO directly.

## Public API

- `myfirst_dma_setup(sc)`: called from attach. Creates the tag,
  allocates the buffer, loads the map, populates `sc->dma_bus_addr`.
  Initialises the `dma_cv` condition variable.
- `myfirst_dma_teardown(sc)`: called from detach. Reverses setup.
  Destroys the `dma_cv`.
- `myfirst_dma_do_transfer(sc, dir, len)`: triggers one DMA transfer
  and polls for completion (legacy; kept for comparison).
- `myfirst_dma_do_transfer_intr(sc, dir, len)`: triggers one DMA
  transfer and sleeps on `dma_cv` for completion (preferred).
- `myfirst_dma_handle_complete(sc)`: called from the rx task when
  `MYFIRST_INTR_COMPLETE` was observed.
- `myfirst_dma_add_sysctls(sc)`: registers the DMA counters and test
  sysctls under `dev.myfirst.N.`.

## Register Layout

| Offset | Name             | R/W | Meaning                                       |
|--------|------------------|-----|-----------------------------------------------|
| 0x20   | `DMA_ADDR_LOW`   | RW  | Low 32 bits of the DMA bus address.           |
| 0x24   | `DMA_ADDR_HIGH`  | RW  | High 32 bits.                                 |
| 0x28   | `DMA_LEN`        | RW  | Transfer length in bytes.                     |
| 0x2C   | `DMA_DIR`        | RW  | 0 = host-to-device, 1 = device-to-host.       |
| 0x30   | `DMA_CTRL`       | RW  | 1 = start, 2 = abort, bit 31 = reset.         |
| 0x34   | `DMA_STATUS`     | RO  | bit 0 = DONE, bit 1 = ERR, bit 2 = RUNNING.   |

## Transfer Flow (Host-to-Device)

```
CPU fills dma_vaddr with data
bus_dmamap_sync(PREWRITE)
write DMA_ADDR_LOW/HIGH, DMA_LEN, DMA_DIR=0, DMA_CTRL=START
cv_timedwait on dma_cv
--- interrupt fires ---
filter: acknowledge INTR_COMPLETE, enqueue task
task: myfirst_dma_handle_complete -> bus_dmamap_sync(POSTWRITE) -> cv_broadcast
helper: wake, check status, return
```

## Transfer Flow (Device-to-Host)

```
bus_dmamap_sync(PREREAD)
write DMA_ADDR_LOW/HIGH, DMA_LEN, DMA_DIR=1, DMA_CTRL=START
cv_timedwait on dma_cv
--- interrupt fires ---
filter: acknowledge INTR_COMPLETE, enqueue task
task: myfirst_dma_handle_complete -> bus_dmamap_sync(POSTREAD) -> cv_broadcast
helper: wake, check status, return
CPU reads dma_vaddr
```

## Counters

- `dev.myfirst.N.dma_transfers_write`: successful host-to-device transfers.
- `dev.myfirst.N.dma_transfers_read`: successful device-to-host transfers.
- `dev.myfirst.N.dma_errors`: transfers that returned EIO.
- `dev.myfirst.N.dma_timeouts`: transfers that hit the 1-second timeout.
- `dev.myfirst.N.dma_complete_intrs`: completion-bit observations in the filter.
- `dev.myfirst.N.dma_complete_tasks`: completion processing in the task.
- `dev.myfirst.N.dma_bus_addr`: read-only view of the allocated bus address.

## Invariants

- `dma_complete_intrs == dma_complete_tasks` at all times.
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

The regression test verifies both.

## Testing

The sysctls `dma_test_write` and `dma_test_read` trigger transfers
from user space:

```sh
sysctl dev.myfirst.0.dma_test_write=0xAA
sysctl dev.myfirst.0.dma_test_read=1
```

The first fills the buffer with 0xAA and runs host-to-device; the
second runs device-to-host and logs the first eight bytes of the
buffer to `dmesg`.

## Locking

- `sc->mtx` protects every DMA field that is read or written outside
  attach/detach: `dma_in_flight`, `dma_last_direction`,
  `dma_last_status`.
- Per-transfer counters are updated with `atomic_add_64` so the
  filter can increment them without the mutex.
- `dma_cv` is waited on and broadcast under `sc->mtx`.

## Detach Safety

Detach order:
1. Mask `MYFIRST_INTR_COMPLETE` in the device register.
2. Drain the simulation's callout (`callout_drain`).
3. Drain the rx task (`taskqueue_drain`).
4. Wait for `dma_in_flight == false` under `sc->mtx`.
5. Call `myfirst_dma_teardown`.

## Known Limitations

- Single buffer, single transfer at a time.
- No descriptor-ring support.
- No per-NUMA-node allocation.
- No partial-transfer reporting.

These are natural extensions documented in the chapter text.

## See Also

- `bus_dma(9)`, `/usr/src/sys/sys/bus_dma.h`.
- `/usr/src/sys/dev/re/if_re.c` for a production descriptor-ring driver.
- `INTERRUPTS.md`, `MSIX.md` for the interrupt path the completion uses.
- The chapter text at `content/chapters/part4/chapter-21.md`.
