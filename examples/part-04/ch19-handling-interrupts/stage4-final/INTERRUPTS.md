# Interrupt Handling in the myfirst Driver

## Summary

The driver registers a single filter handler on the legacy PCI INTx
line through `bus_setup_intr(9)` with `INTR_TYPE_MISC | INTR_MPSAFE`.
The filter reads `INTR_STATUS`, handles each recognised bit
inline (atomically incrementing per-bit counters and writing back
to acknowledge), and enqueues a deferred task on a dedicated
taskqueue for `DATA_AV` events.

## Files

- `myfirst_intr.c`: the filter handler, the deferred task, the
  setup and teardown helpers, and the interrupt-related sysctls.
- `myfirst_intr.h`: the interface exported to `myfirst_pci.c`,
  plus the `ICSR_READ_4` / `ICSR_WRITE_4` interrupt-context
  accessor macros.

## Setup Sequence

`myfirst_intr_setup()`:

1. Initialise the deferred-work task (`TASK_INIT`).
2. Create a taskqueue named `"myfirst_intr"` with one worker at
   `PI_NET` priority.
3. Allocate an IRQ resource with `rid = 0`, `SYS_RES_IRQ`,
   `RF_SHAREABLE | RF_ACTIVE`.
4. Register the filter handler via `bus_setup_intr(9)`.
5. Describe the handler via `bus_describe_intr(9)` as "legacy".
6. Write the device's `INTR_MASK` to enable `DATA_AV | ERROR |
   COMPLETE`.

On any step failing, the function unwinds in reverse.

## Teardown Sequence

`myfirst_intr_teardown()`:

1. Clear the device's `INTR_MASK` (stop the device from asserting).
2. `bus_teardown_intr(9)` (stop the kernel from running the handler).
3. `taskqueue_drain(9)` on the data task (finish in-flight work).
4. `taskqueue_free(9)` (release the taskqueue).
5. `bus_release_resource(9)` on the IRQ (free the resource).

Safe to call at any stage of attach failure.

## Filter Behaviour

`myfirst_intr_filter(sc)`:

1. Read `INTR_STATUS`. Return `FILTER_STRAY` if zero.
2. Increment `intr_count` atomically.
3. For each of `DATA_AV`, `ERROR`, `COMPLETE`:
   - Increment the per-bit counter atomically.
   - Write the bit back to `INTR_STATUS` to acknowledge.
   - For `DATA_AV`, enqueue the deferred task.
4. Return `FILTER_HANDLED` if any bit was recognised, or
   `FILTER_STRAY` otherwise.

## Deferred Task

`myfirst_intr_data_task_fn(sc, npending)`:

1. Acquire `sc->mtx`.
2. If the driver is no longer attached, release the lock and return.
3. Read `DATA_OUT`, store in `sc->intr_last_data`.
4. Increment `intr_task_invocations`.
5. Broadcast `sc->data_cv` to wake any pending readers.
6. Release the lock.

## Sysctls

- `dev.myfirst.N.intr_count` (RD, uint64): total filter invocations.
- `dev.myfirst.N.intr_data_av_count` (RD, uint64): DATA_AV events.
- `dev.myfirst.N.intr_error_count` (RD, uint64): ERROR events.
- `dev.myfirst.N.intr_complete_count` (RD, uint64): COMPLETE events.
- `dev.myfirst.N.intr_task_invocations` (RD, uint64): task runs.
- `dev.myfirst.N.intr_last_data` (RD, uint): last `DATA_OUT` value.
- `dev.myfirst.N.intr_simulate` (WR, uint): write a bitmask to set
  those bits in `INTR_STATUS` and invoke the filter. Exercises the
  full pipeline without real IRQ events.

## Interrupt-Context Accessor Macros

The filter cannot take `sc->mtx` (a sleep mutex). Use
`ICSR_READ_4(sc, off)` and `ICSR_WRITE_4(sc, off, val)` from
`myfirst_intr.h` for BAR accesses inside the filter. Outside the
filter, use the usual `CSR_READ_4` and `CSR_WRITE_4` macros, which
assert the lock is held.

## Lock Ordering

The filter takes no locks (atomics only). The task takes `sc->mtx`.
The simulated-interrupt sysctl takes `sc->mtx` briefly to set state
and releases before invoking the filter. The existing Chapter 15
order (`sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`) is unchanged.

## Known Limitations

- Only the legacy PCI INTx line is handled. MSI and MSI-X are Chapter 20.
- The driver does not implement its own interrupt-storm mitigation.
  The kernel's `hw.intr_storm_threshold` machinery applies.
- The Chapter 17 register semantics are assumed. On the bhyve
  virtio-rnd target, the `INTR_MASK` write at offset 0x10 overlaps
  virtio's `queue_notify` + `device_status` + `isr_status` fields
  and acts as a harmless virtio device reset; it does not enable any
  real interrupt. Exercise the pipeline through the simulated-interrupt
  sysctl.

## See Also

- `HARDWARE.md` for the register map.
- `LOCKING.md` for the full lock discipline.
- `PCI.md` for the PCI attach behaviour.
- `SIMULATION.md` for the Chapter 17 simulation backend.
