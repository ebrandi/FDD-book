# Hardware to Driver Quick Reference

A short map of how a physical signal becomes a FreeBSD driver
callback, how a DMA transfer becomes bytes in a softc, and how a
PCI capability becomes an API call. Use this sheet to sanity-check
your understanding of a new device family before you write code.

Full teaching lives in Appendix C and in Chapters 16 through 21.
This sheet only helps you see the whole pipeline at once.

## From a device register to your C code

```
Device asserts a status bit
        |
        v
Bus fabric (PCI/PCIe/APB/etc.) routes the access
        |
        v
CPU MMU + cache attributes: "this region is uncached MMIO"
        |
        v
bus_space_handle_t resolves to virtual address in MMIO window
        |
        v
bus_space_read_4(tag, handle, offset)
        |
        v
Driver C code sees a uint32_t value
```

Reverse for writes. The `bus_space_barrier(9)` call lets you order
reads and writes when their order matters to the device.

## From a device interrupt to your driver's filter

```
Device raises an interrupt signal
        |
        v
Interrupt controller (IOAPIC / local APIC / GIC / etc.) maps to a
CPU vector
        |
        v
Kernel low-level entry saves minimal state, dispatches through
intr_event(9)
        |
        v
Your filter function runs
        |       (spin locks only, no sleeping, no malloc, very short)
        |
        +-- returns FILTER_HANDLED        -> controller EOIs, done
        |
        +-- returns FILTER_STRAY          -> not mine, controller
        |                                    checks other handlers
        |
        +-- returns FILTER_SCHEDULE_THREAD -> ithread wakes to run
                                             the heavy work
```

Per-vector counters (usually a `sysctl` under `dev.<driver>.<unit>`)
are the cheapest and most useful diagnostic.

## From DMA transfer setup to data in memory

```
attach:
  bus_dma_tag_create(...)     -> tag T
  bus_dmamem_alloc(T, ...)    -> vaddr V, map M

runtime:
  fill V with command data at the CPU virtual address
  bus_dmamap_sync(T, M, BUS_DMASYNC_PREWRITE)
  write ds_addr(M) into the device's descriptor register(s)
  write the "start" register
        |
        v
  device reads memory at ds_addr(M) via the bus (possibly IOMMU)
        |
        v
  device writes result (if applicable) at another ds_addr
        |
        v
  device raises an interrupt on completion

completion:
  filter recognises the event, optionally schedules the ithread
  bus_dmamap_sync(T, M, BUS_DMASYNC_POSTREAD)
  driver reads the result at V (the CPU virtual address)
  optionally bus_dmamap_unload(T, M)
```

The two sync calls are the only thing that guarantees cache
coherence between the CPU view (V) and the device view (ds_addr).
Write them every time.

## From PCI capabilities to API calls

```
PCI configuration space
  +--- Base header (vendor, device, class, BARs, INTx, ...)
  +--- Capability list (linked list starting at PCIR_CAP_PTR)
        |
        +--- PCIY_MSI       -> pci_alloc_msi(dev, &count)
        +--- PCIY_MSIX      -> pci_alloc_msix(dev, &count)
        +--- PCIY_PMG       -> power management via ACPI/PCI
        +--- PCIY_VENDOR    -> vendor-specific, read through
                               pci_read_config at the capability
                               offset
  +--- Extended capabilities (PCIe only, 4 KiB config window)
        |
        +--- PCIZ_AER       -> advanced error reporting
        +--- PCIZ_VC        -> virtual channels
        +--- ...
```

Lookup calls:
- `pci_find_cap(dev, PCIY_MSIX, &offset)` returns the offset of a
  standard capability.
- `pci_find_extcap(dev, PCIZ_AER, &offset)` returns the offset of an
  extended capability.
- Both are declared in `/usr/src/sys/dev/pci/pcivar.h` and defined in
  `/usr/src/sys/dev/pci/pci.c`.

## From probe to attached driver

```
Bus driver (pci, usb, iicbus, spibus) enumerates children at boot
        |
        v
For each child, kernel walks the list of registered drivers
        |
        v
Each driver's probe function runs:
  PCI probe:  match vendor/device IDs  -> BUS_PROBE_DEFAULT
  USB match:  match VID/PID + class    -> probe result
  I2C/SPI:    match FDT compatible or ACPI HID
        |
        v
Best-scoring driver wins
        |
        v
Driver's attach function runs:
  allocate resources, set up interrupts, build softc,
  make /dev nodes, expose sysctl, register with
  subsystems if any
        |
        v
Driver is live
```

## Checklist: do I understand this device family?

Answer yes to every line before writing code.

- [ ] I know which bus the device sits on (`pci`, `usb`, `iicbus`,
      `spibus`, `simplebus`, `acpi`, etc.).
- [ ] I know how the bus identifies devices (vendor/device ID,
      VID/PID, slave address, chip-select, FDT compatible).
- [ ] I know whether the device uses registers I need to access
      directly, or whether a framework handles transactions.
- [ ] I know whether the device uses interrupts and, if so, whether
      they are legacy INTx, MSI, or MSI-X.
- [ ] I know whether the device uses DMA, and if so, what its
      alignment and addressing limits are.
- [ ] I know which cache coherence mode applies on my target
      architecture, and whether my code depends on it.
- [ ] I know which /dev node the device will expose, if any.
- [ ] I know which `sysctl` tree will expose counters and tunables.

If any box is unchecked, the datasheet is where to look next.

## Cross-references

- Appendix C for every concept above.
- Chapter 16 (`bus_space`), Chapter 18 (PCI), Chapter 19 (interrupts),
  Chapter 20 (MSI and MSI-X), Chapter 21 (DMA).
- Appendix A for the API reference, Appendix E for the subsystem
  architecture.
