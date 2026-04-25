# MSI and MSI-X Support in the myfirst Driver

## Summary

The driver probes the device's MSI-X, MSI, and legacy INTx capabilities
in that order, and uses the first one that allocates successfully.

## Fallback Ladder

1. **MSI-X** with `MYFIRST_MAX_VECTORS` (3) vectors. On success:
   - Per-vector IRQ resources at rid=1, 2, 3.
   - Distinct filter function per vector.
   - `bus_describe_intr` with per-vector name ("admin", "rx", "tx").
   - `bus_bind_intr` round-robin across CPUs.
2. **MSI** with 1 vector (single-handler fallback). MSI requires a
   power-of-two vector count (PCI spec; `pci_alloc_msi_method` returns
   `EINVAL` for count=3), so the driver requests 1 MSI vector and uses
   the Chapter 19 `myfirst_intr_filter` at rid=1. This matches the
   fallback used in `sys/dev/virtio/pci/virtio_pci.c`.
3. **Legacy INTx** with a single handler at rid=0 using the Chapter 19
   `myfirst_intr_filter`.

## Per-Vector Assignment

| Vector | ID | Purpose | Handles                   | Inline/Deferred |
|--------|----|---------|---------------------------|-----------------|
| 0      | admin | admin/error  | INTR_STATUS.ERROR     | Inline          |
| 1      | rx    | receive      | INTR_STATUS.DATA_AV   | Deferred (task) |
| 2      | tx    | transmit     | INTR_STATUS.COMPLETE  | Inline          |

## sysctls

- `dev.myfirst.N.intr_mode`: 0=legacy, 1=MSI, 2=MSI-X.
- `dev.myfirst.N.vec{0,1,2}_fire_count`: per-vector fire counts.
- `dev.myfirst.N.vec{0,1,2}_stray_count`: per-vector stray counts.
- `dev.myfirst.N.intr_simulate_admin|rx|tx`: simulated interrupts.

## Teardown Sequence

1. Disable interrupts at the device (clear INTR_MASK).
2. Per-vector in reverse: `bus_teardown_intr`, `bus_release_resource`.
3. Drain each per-vector task, then `taskqueue_free`.
4. `pci_release_msi(dev)` if mode is MSI or MSI-X.

## dmesg Summary

On attach one of:
- `interrupt mode: MSI-X, 3 vectors`
- `interrupt mode: MSI, 3 vectors`
- `interrupt mode: legacy INTx (1 handler for all events)`

## Known Limitations

- `MYFIRST_MAX_VECTORS` is hardcoded at 3. Dynamic adaptation is a
  challenge exercise.
- CPU binding is round-robin. NUMA-aware binding via `bus_get_cpus` is
  a challenge exercise.
- DMA is Chapter 21.
- iflib integration is out of scope.

## Test Environments

- **bhyve with virtio-rnd**: legacy INTx fallback. The device does
  not expose MSI-X in this configuration.
- **QEMU with `-device virtio-rng-pci`**: MSI-X path.

## See Also

- `INTERRUPTS.md`: Chapter 19 single-vector details.
- `HARDWARE.md`: register map.
- `LOCKING.md`: lock discipline.
- `PCI.md`: PCI attach.
