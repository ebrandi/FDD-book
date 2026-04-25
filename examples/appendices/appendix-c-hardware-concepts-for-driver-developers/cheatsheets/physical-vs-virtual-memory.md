# Physical vs Virtual Memory Diagram Sheet

A mental-model reference for the address spaces a FreeBSD driver
operates in. Full teaching lives in Appendix C, section **Memory in
the Kernel**, and in Chapters 16 and 21. Use this sheet when you want
to ground your current driver in the underlying layout.

## The three address spaces a driver touches

Every driver operates across at least three address spaces.
Understanding which one you are in at each line of code is the single
most important habit in driver work.

```
+-------------------------------------------------------------+
|                    Kernel virtual memory                    |
|                                                             |
|   C pointers live here. malloc(9), uma_zalloc, stack,       |
|   softc. The MMU translates these to physical pages on the  |
|   fly via the kernel's page tables.                         |
|                                                             |
|   The CPU dereferences these. The device never sees them.   |
+-------------------------------------------------------------+
                           |
                           | MMU translation
                           v
+-------------------------------------------------------------+
|                      Physical memory                        |
|                                                             |
|   Real RAM pages, real MMIO register regions, real I/O      |
|   ports. A peripheral with no IOMMU sees memory this way    |
|   (subject to its own address bits).                        |
+-------------------------------------------------------------+
                           ^
                           | IOMMU translation (when present)
                           |
+-------------------------------------------------------------+
|                      Bus address space                      |
|                                                             |
|   The addresses a peripheral emits on the bus. Without an   |
|   IOMMU, these equal physical addresses (possibly through   |
|   a bounce buffer). With an IOMMU, these are I/O-virtual    |
|   addresses that the IOMMU translates to physical.          |
|                                                             |
|   The device dereferences these. The CPU cannot.            |
+-------------------------------------------------------------+
```

## How the driver API lines up

Fill in the blanks for your current driver to pin down the layout.

| Concept | Address space | How your driver obtains it | Example (your driver) |
| :-- | :-- | :-- | :-- |
| Softc pointer | Kernel virtual | `device_get_softc(dev)` | |
| DMA buffer (CPU view) | Kernel virtual | `bus_dmamem_alloc(...)` returns vaddr | |
| DMA buffer (device view) | Bus address | `ds_addr` from load callback | |
| Device register window | Physical (mapped) | `bus_alloc_resource(SYS_RES_MEMORY, ...)` | |
| Register access | Virtual, uncached | `bus_space_read_4(tag, handle, offset)` | |
| PCI BAR raw value | Physical | `pci_read_config(dev, PCIR_BAR(n), 4)` | |

## What each layer owns

- **CPU path (left side).** The kernel dereferences virtual pointers.
  The MMU translates them. Your C code, including every pointer
  dereference in the driver, lives here.
- **Device path (right side).** The device emits bus addresses. The
  IOMMU (if present) translates them. Your DMA setup, including every
  `ds_addr` you copy into a descriptor, lives here.

The `bus_space` and `bus_dma` families are the bridges between the
two sides. Anything that crosses from one side to the other must go
through one of them.

## MMIO access path (CPU to device register)

```
CPU code:  bus_space_read_4(tag, handle, offset)
              |
              v
  Tag decides address space (memory vs I/O port) and attributes
              |
              v
  Handle resolves to a virtual address in the region
  mapped by the kernel with uncached / strongly-ordered
  attributes.
              |
              v
  MMU translates to physical MMIO address, delivers the
  read, returns the value.
              |
              v
CPU code:  uses the value
```

## DMA access path (device to RAM)

```
Driver setup:
  bus_dma_tag_create(...) -> tag encodes device constraints
  bus_dmamem_alloc(...)   -> CPU-virtual + map
  bus_dmamap_load(...)    -> callback receives ds_addr
  hand ds_addr to device  -> device stores it in a descriptor

Runtime:
  bus_dmamap_sync(..., PREWRITE or PREREAD)
  device reads or writes memory at ds_addr
        (IOMMU translates ds_addr -> physical if present)
  device signals completion (interrupt)
  bus_dmamap_sync(..., POSTREAD or POSTWRITE)
  CPU reads the result through the virtual view
```

## Things to verify in your own driver

- [ ] Every virtual pointer stays on the CPU path.
- [ ] Every bus address reaches the device only through `ds_addr`
      or a raw PCI BAR on the device side.
- [ ] No line of code casts a `void *` to something the device will
      dereference.
- [ ] Register reads and writes go through `bus_space_read_*` /
      `bus_space_write_*`, not raw dereferences.
- [ ] DMA buffers and control buffers are clearly separated in the
      softc.

## Cross-references

- Appendix C, section **Memory in the Kernel**, for the full
  treatment.
- Chapter 16 for `bus_space(9)` and MMIO mapping.
- Chapter 21 for DMA and `bus_dma(9)`.
- `bus_space(9)`, `bus_dma(9)`, `pmap(9)`.
- `/usr/src/sys/sys/bus.h`, `/usr/src/sys/sys/bus_dma.h`, and
  `/usr/src/sys/vm/vm_page.h` for the underlying types.
