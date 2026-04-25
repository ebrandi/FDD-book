# Bus Comparison Cheatsheet

Quick reference for the four buses a driver author is most likely to
meet in FreeBSD: PCI/PCIe, USB, I2C, and SPI. Full teaching lives in
Appendix C and in the dedicated chapters. This sheet only helps you
point at the right mental model fast.

## The question to ask first

What role is your driver in this bus?

- **Register-level driver.** You allocate resources and touch device
  registers directly. PCI/PCIe fits here.
- **Framework client.** You describe what you need and let the bus
  framework run transactions for you. USB, I2C, and SPI fit here.

Everything else follows from this distinction.

## Buses at a glance

| Aspect | PCI / PCIe | USB | I2C | SPI |
| :-- | :-- | :-- | :-- | :-- |
| Topology | Tree (PCIe switches) | Strict host/hub tree | Multi-drop, addressed | Multi-drop, chip-select |
| Typical speed | GB/s per lane | KB/s to GB/s | 100 kHz - 1 MHz | 1 MHz - tens of MHz |
| Identification | Vendor/Device + class | VID/PID + class/subclass/protocol | 7- or 10-bit slave address | None (chip-select) |
| Protocol framing | PCI TLPs, invisible | Packets + transfer types | Start/address/data/stop | Raw bits |
| Interrupts | INTx or MSI / MSI-X | Transfer callbacks | Rare; bus is polled | Rare; bus is polled |
| DMA | Yes, device-driven | Host controller does it | No | No |
| FreeBSD parent bus name | `pci` | `uhub` or similar | `iicbus` | `spibus` |
| Main API call | `bus_alloc_resource_any`, `bus_setup_intr` | `usbd_transfer_setup`, `usbd_transfer_start` | `iicbus_transfer` | `spibus_transfer` |
| Manual pages | `pci(4)`, `pci(9)` | `usb(4)`, `usbdi(9)` | `iicbus(4)` | - (see source) |

## Decision hints

Reach for **PCI/PCIe** when the device is on a motherboard slot, a
PCIe switch, or an x86 desktop machine's internal fabric. Expect
configuration space, BARs, MSI or MSI-X, and full DMA.

Reach for **USB** when the device is external, hot-pluggable, or uses
transfer endpoints (control, bulk, interrupt, isochronous). Expect
a framework that runs transactions for you on callback.

Reach for **I2C** when the peripheral is small, low-bandwidth, and
sits on a two-wire bus with a slave address. Sensors, EEPROMs, PMICs,
small displays. Transfers are short and blocking.

Reach for **SPI** when the peripheral is slightly faster, has a
chip-select line, and you need full-duplex bit shifting. Expect raw
bytes with meaning defined entirely by the device datasheet.

## What the driver's top-level shape looks like

### PCI driver skeleton

```
probe:    pci_get_vendor() / pci_get_device() match -> BUS_PROBE_DEFAULT
attach:   bus_alloc_resource_any(SYS_RES_MEMORY, ...)       // BAR
          rman_get_bustag() / rman_get_bushandle()           // tag/handle
          pci_enable_busmaster() if DMA capable
          bus_alloc_resource_any(SYS_RES_IRQ, ...)           // IRQ
          bus_setup_intr(filter, ithread, ...)               // handler
          bus_dma_tag_create(...)                            // DMA tag
          bus_dmamem_alloc(...) / bus_dmamap_load(...)       // DMA buffer
detach:   reverse of attach, in exact reverse order
```

### USB client skeleton

```
match:    VID/PID + class/subclass/protocol
attach:   usbd_transfer_setup(xfer_config[], ...)            // endpoints
          usbd_transfer_start(xfer)                          // begin
callback: process completed transfer, restart if needed
detach:   usbd_transfer_drain(), usbd_transfer_unsetup()
```

### I2C client skeleton

```
probe:    FDT / ACPI match on compatible string or hardware ID
attach:   record parent iicbus, slave address, callout for periodic work
use:      iicbus_transfer(dev, msgs, nmsgs) for each transaction
detach:   stop callouts, release softc resources
```

### SPI client skeleton

```
probe:    FDT / ACPI match
attach:   record parent spibus, chip-select index
use:      spibus_transfer(dev, &cmd) per transaction
detach:   release resources
```

## Anti-patterns to avoid

Do not write a USB driver as if it were a PCI driver. Endpoints are
not registers, and transfers are not interrupts. Fight the framework
and you will lose.

Do not write an I2C or SPI client that busy-waits inside the
transaction. The bus framework already knows how to wait properly.

Do not skip `pci_enable_busmaster` in a PCI driver that does DMA. The
chip will look alive and move no data.

Do not copy PCI resource allocation into an I2C or SPI client. There
are no resources to allocate there; the bus already owns the
controller.

## Cross-references

- Appendix C, section **Buses and Interfaces**, for the full mental model.
- Chapter 18 for PCI, a later chapter for USB, a later chapter for
  I2C, and a later chapter for SPI.
- Real drivers worth reading:
  - PCI: `/usr/src/sys/dev/uart/uart_bus_pci.c`
  - USB stack: `/usr/src/sys/dev/usb/`
  - I2C client: `/usr/src/sys/dev/iicbus/icee.c`
  - SPI client: `/usr/src/sys/dev/spibus/spigen.c`
