# PCI Support in the myfirst Driver

## Supported Devices

As of version `1.1-pci`, `myfirst` attaches to PCI devices matching
the following vendor/device ID pairs:

| Vendor | Device | Description                                 |
| ------ | ------ | ------------------------------------------- |
| 0x1af4 | 0x1005 | Red Hat/virtio-rnd (demo target; see README) |

The table is maintained in `myfirst_pci.c` as the static array
`myfirst_pci_ids[]`. Adding a new supported device requires:

1. Appending an entry to `myfirst_pci_ids[]` with the vendor and
   device IDs and a human-readable description.
2. Verifying that the driver's BAR layout and register map are
   compatible with the new device.
3. Running the Chapter 18 regression script against the new device.
4. Updating this file.

## Attach Behaviour

The driver's probe routine (`myfirst_pci_probe`) returns
`BUS_PROBE_DEFAULT` on a match against `myfirst_pci_ids[]` and
`ENXIO` otherwise. Its priority matches the base-system drivers for
the same IDs; readers must ensure no other driver (for example,
`virtio_random` for the demo target) is loaded before `myfirst` can
attach.

Attach (`myfirst_pci_attach`) follows this sequence:

1. Store the `device_t` and assigned unit number.
2. Initialise the softc (`myfirst_init_softc`): locks, condition
   variables, callouts, taskqueue, statistics.
3. Allocate BAR 0 as `SYS_RES_MEMORY` with `RF_ACTIVE`.
4. Walk the PCI capability list and PCIe extended capability list,
   logging the offsets of any recognised capabilities.
5. Attach the Chapter 16 hardware layer against the BAR
   (`myfirst_hw_attach_pci`), which stores the tag and handle so
   the `CSR_*` accessors operate on real silicon.
6. Create the per-instance cdev `/dev/myfirst<unit>`.
7. Read a diagnostic 32-bit value from BAR offset 0 and log it.

On any failure after step 2 the attach path unwinds through a goto
cascade, undoing exactly the steps that had succeeded.

## Detach Behaviour

Detach (`myfirst_pci_detach`) refuses to proceed if the driver is
busy (open file descriptors, in-flight commands) by returning
`EBUSY`. Otherwise it executes in the reverse of attach order:

1. Mark the driver as no longer PCI-attached.
2. Destroy the cdev.
3. Quiesce callouts and taskqueue tasks.
4. Detach the hardware layer (frees the wrapper struct; does not
   touch the BAR).
5. Release the BAR resource.
6. Deinit the softc.

## Module Dependencies

- `pci`, version 1: the kernel's PCI subsystem.

## Compile-Time Options

- `-DMYFIRST_SIMULATION_ONLY` disables the PCI attach layer. The
  driver builds as a simulation-only module, indistinguishable from
  the Chapter 17 Stage 5 driver.
- `-DMYFIRST_PCI_ONLY` disables the simulation fallback. The driver
  attaches only when a matching PCI device is present; `kldload`
  without a matching device produces no attach.

## Known Limitations

- The driver does not handle interrupts. A later chapter adds a
  real interrupt handler through `bus_setup_intr(9)`.
- The driver does not support DMA. A later chapter adds `bus_dma(9)`
  tags and descriptor rings.
- The Chapter 17 simulation backend is not attached on the PCI
  path; `sc->sim` remains NULL and the simulation's callouts do not
  run. A simulation-only build (`-DMYFIRST_SIMULATION_ONLY`) keeps
  the Chapter 17 behaviour for readers who load the driver without a
  matching PCI device.

## See Also

- `HARDWARE.md` for the register map.
- `SIMULATION.md` for the simulation backend.
- `LOCKING.md` for the lock discipline.
- `README.md` for build and test instructions.
