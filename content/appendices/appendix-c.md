---
title: "Hardware Concepts for Driver Developers"
description: "A concept-level field guide to the memory, buses, interrupts, and DMA ideas that recur throughout FreeBSD device-driver development."
appendix: "C"
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 30
---

# Appendix C: Hardware Concepts for Driver Developers

## How to Use This Appendix

The main chapters teach the driver-facing side of FreeBSD: how to write a softc, how to declare a `cdev`, how to set up an interrupt, how to walk a PCI BAR into a `bus_space` handle, how to tame a DMA engine with `bus_dma(9)`. Underneath every one of those topics there is a quiet layer of hardware knowledge that the book keeps using without always naming. What a physical address really is. Why the CPU cannot simply read a device register through an ordinary pointer. Why PCI and I2C feel like completely different worlds even though both carry bytes between a CPU and a peripheral. Why an interrupt is not a function call. Why DMA changes how you write a driver from top to bottom.

This appendix is that quiet layer, written down. It is a concept guide, not an electronics textbook. It is shaped by what a driver author actually needs to believe about the machine in order to write correct code, and deliberately stops short of full computer-architecture teaching. If you have ever written a `bus_space_read_4` call and wondered what the tag and the handle really represent, or set up an MSI-X vector and wondered what is being signalled across which wires, or debugged a DMA transfer that worked on your desk and broke on a different board, you are in the right place.

### What You Will Find Here

The appendix is organised by driver relevance, not by abstract hardware taxonomy. Each topic follows the same compact rhythm:

- **What it is.** One or two sentences of plain definition.
- **Why this matters to driver writers.** The concrete place where the concept shows up in your code.
- **How this shows up in FreeBSD.** The API, header, or convention that names the idea.
- **Common trap.** The misunderstanding that actually costs people time.
- **Where the book teaches this.** A pointer back to the chapter that uses it in context.
- **What to read next.** A manual page, a header, or a real driver you can open.

Not every topic uses every label; the shape is a guide, not a template.

### What This Appendix Is Not

It is not a first course in electronics. It assumes you can accept "a wire carries a signal" and move on. It is not a computer-architecture textbook either; there is no discussion of pipelines, caches beyond what a driver author must know, or branch prediction. It is not a replacement for the focused chapters: Chapter 16 teaches `bus_space(9)`, Chapter 18 teaches PCI, Chapter 19 teaches interrupts, Chapter 20 teaches MSI and MSI-X, and Chapter 21 teaches DMA in depth. This appendix is what you keep open beside those chapters when you want a mental-model anchor instead of a full walkthrough.

It also does not overlap with the other appendices. Appendix A is the API reference; Appendix B is the algorithmic-pattern field guide; Appendix D is the operating-systems-concepts companion; Appendix E is the kernel-subsystem reference. If the question you want answered is "what does this macro call do" or "what algorithm fits this problem" or "how does the scheduler schedule", you want a different appendix.

## Reader Guidance

Three ways to use this appendix, each calling for a different strategy.

If you are **learning the main chapters**, open the appendix alongside them. When Chapter 16 mentions memory barriers, flip to the memory section here. When Chapter 18 talks about BARs, flip to the PCI section. When Chapter 19 introduces edge versus level triggering, flip to the interrupt section. The appendix is designed to be a mental-model companion, not a linear read. Thirty minutes from start to finish is realistic for a first pass; a few minutes at a time is more common in daily use.

If you are **reading real driver code**, use the appendix as a translator for unfamiliar terminology. When the header comment says "coherent DMA memory" or "message-signalled interrupt" or "BAR at offset 0x10", find the concept here and keep going. Full understanding can come later; the first job is to form a mental picture of what the driver is doing against what kind of hardware.

If you are **designing a new driver**, read the sections that match your hardware. A PCI NIC will touch the memory model, PCI, interrupts (probably MSI-X), and DMA, so those four sections are your warm-up. An I2C sensor will touch the I2C section and the interrupt section and almost nothing else. An embedded driver on a system-on-chip will touch everything except PCI. Match the hardware to the sections, then skim only those.

A few conventions apply throughout:

- Source paths are shown in the book-facing form, `/usr/src/sys/...`, matching a standard FreeBSD system. You can open any of them on your lab machine.
- Manual pages are cited in the usual FreeBSD style. The kernel-facing pages live in section 9: `bus_space(9)`, `bus_dma(9)`, `pci(9)`, `malloc(9)`, `intr_event(9)`. Device overviews usually live in section 4: `pci(4)`, `usb(4)`, `iicbus(4)`.
- When an entry points to real source as reading material, the file is one a beginner can navigate in one sitting. Larger subsystems exist that also use the pattern; those are mentioned only when they are the canonical example.

With that in mind, we start with memory: the single concept that separates a working driver from a puzzling one.

## Memory in the Kernel

A driver that misunderstands memory will write perfect-looking code that crashes on real hardware. Memory in the kernel is not the same as memory in a C tutorial. There are at least two address spaces in play (physical and virtual), there are constraints the hardware enforces (DMA addressing limits, page boundaries), and there are regions where even an ordinary read has side effects (device registers). This section builds the mental model that every later topic in the appendix depends on.

### Physical vs Virtual Memory

**What it is.** A physical address is the number the memory controller uses to select a real location in RAM, or the number the I/O fabric uses to select a register on a peripheral. A virtual address is the number a running thread sees when it dereferences a pointer; the memory-management unit (MMU) translates it to a physical address on the fly, using the page tables that the kernel maintains.

**Why this matters to driver writers.** Every pointer in your driver is virtual. The CPU never sees a physical address when it executes your C code. The kernel and the `pmap(9)` layer keep the page tables so that kernel virtual addresses map to wherever in physical memory the kernel has decided to put the corresponding pages. A driver that "knows" a physical address cannot simply cast it to `(void *)` and dereference; the translation might not exist, the permissions might forbid it, or the attributes might be wrong for the kind of access you need.

Hardware is on the opposite side of the mirror. A peripheral device lives somewhere in a physical address space (its registers are physical addresses, and when it does DMA it emits physical addresses on the bus). A device never dereferences a virtual pointer. If your driver hands the device a kernel virtual address and expects it to read the data there, the device will read some unrelated physical location and you will spend a long afternoon figuring out why.

**How this shows up in FreeBSD.** Two places. First, `bus_space(9)` encodes the translation for register access: a tag describes the kind of physical address space, a handle describes the virtual mapping the kernel has set up, and the `bus_space_read_*` and `bus_space_write_*` accessors use the handle to reach the device through that mapping. Second, `bus_dma(9)` mediates DMA: the `bus_dmamap_load` call walks a buffer and produces the list of bus addresses the device should use, which are not necessarily the same as either the virtual addresses or the physical addresses, because an IOMMU may sit in between.

**Common trap.** Casting a physical address returned by firmware or a BAR register to a pointer and reading from it directly. On some architectures and some builds this compiles and may even return something, but it is never correct. Always map through `bus_space`, `pmap_mapdev`, or an established FreeBSD API.

**Where the book teaches this.** Chapter 16 establishes the distinction and shows why raw pointer arithmetic is not enough for device memory. Chapter 21 returns to it when DMA makes the physical side visible.

**What to read next.** `bus_space(9)`, `pmap(9)`, and the early parts of `/usr/src/sys/kern/subr_bus_dma.c`.

### The MMU, Page Tables, and Device Mappings

**What it is.** The MMU is the hardware unit that translates virtual addresses into physical addresses using a tree of page tables. Each page-table entry holds a physical page number and a small set of attribute bits: readable, writable, executable, cacheable, user-accessible. The same physical page can be mapped at several virtual addresses with different attributes; the same virtual address can point at different physical pages at different times.

**Why this matters to driver writers.** Device memory is not ordinary RAM, and the CPU must know that. A register may change on its own (a status bit that sets when the device finishes a transfer), it may have side effects on read (a read-to-clear flag), and it may require that writes become visible in a specific order. If the CPU caches a device register the way it caches an integer in RAM, the code reads a stale value forever. The MMU solves this by letting the kernel map device regions with different attributes: typically uncached, strongly ordered, and sometimes write-combining. A driver does not usually choose those attributes by hand. `bus_space` and `bus_alloc_resource` do it on your behalf.

**How this shows up in FreeBSD.** When `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)` returns, the kernel has already mapped the device region with the right attributes for MMIO. The `struct resource *` you get back carries a `bus_space_tag_t` and `bus_space_handle_t` pair that the accessors use. You should never manipulate the underlying page table yourself. In practice the access is two lines:

```c
uint32_t val = bus_space_read_4(rman_get_bustag(res),
    rman_get_bushandle(res), STATUS_REG_OFFSET);
```

Each `bus_space_read_4` / `bus_space_write_4` call goes through the MMIO mapping the kernel set up for you; no pointer dereference, no cached load.

**Common trap.** Mixing cacheable and uncacheable views of the same physical region. A DMA buffer allocated with `bus_dmamem_alloc` and then cast to a different virtual mapping will give you two incoherent views of the same bytes. Use the mapping the allocator returned, not a mapping you build on the side.

**Where the book teaches this.** Chapter 16 touches the MMU indirectly when it explains why the `bus_space` abstraction exists. Chapter 21 makes it explicit when DMA coherence becomes the topic.

**What to read next.** `pmap(9)`, `bus_space(9)`, and `/usr/src/sys/vm/vm_page.h` for the kernel's representation of a physical page (`vm_page_t`).

### Why Hardware Cannot Simply See Kernel Pointers

**What it is.** A short way of stating the most important consequence of the previous two subsections. The kernel lives in virtual memory. Peripherals live in physical (and sometimes I/O-virtual) memory. A kernel pointer has no meaning to a device.

**Why this matters to driver writers.** Every time you give a device a memory address, you must give it a bus address, not a virtual pointer. The bus address is what the device sees; it may be the physical address, or it may be an I/O-virtual address remapped by an IOMMU. You do not compute it yourself. The DMA infrastructure computes it for you when you load a map.

**How this shows up in FreeBSD.** `bus_dmamap_load` takes a virtual buffer and produces `bus_dma_segment_t` entries whose `ds_addr` is the address the device must use. You copy that value into the descriptor you are about to hand the hardware. The moment you find yourself wanting to pass the output of `vtophys` or a raw BAR value to the device, stop and reach for `bus_dma` instead.

**Common trap.** Writing `(uint64_t)buffer` into a device descriptor because it works in a toy test on one machine. It will fail on any machine where the kernel address is not the same as the bus address, which is most real machines.

**Where the book teaches this.** Chapter 21. The whole chapter is about honouring this separation.

**What to read next.** `bus_dma(9)`, and the DMA section below.

### Kernel Memory Allocation in One Page

The kernel provides three main ways to get memory, each with a different purpose. Drivers reach for them constantly, and picking the wrong one is a classic beginner mistake. The point here is the conceptual difference; the full API reference is in Appendix A.

- **`malloc(9)`** is the general-purpose allocator. It returns kernel-virtual memory that is perfectly fine for softc structures, lists, command buffers for your own bookkeeping, and anything else that stays inside the driver's data. It is not automatically physically contiguous, and it is not automatically suitable for DMA. Use it for control data.

- **`uma(9)`** is the zone allocator. It is a fast cache for fixed-size objects that the driver allocates and frees often, such as per-request structures or per-packet state. It is a specialisation of general-purpose allocation; it does not change the physical-vs-virtual story.

- **`contigmalloc(9)`** returns a physically contiguous range, with options for alignment and an upper address limit. A driver calls it directly only when it truly needs a single contiguous physical block and the `bus_dma` layer is not a better fit. In modern drivers, `bus_dma` almost always is the better fit. The call shape is telling on its own:

  ```c
  void *buf = contigmalloc(size, M_DEVBUF, M_WAITOK | M_ZERO,
      0, BUS_SPACE_MAXADDR, PAGE_SIZE, 0);
  ```

  The `low`, `high`, `alignment`, and `boundary` arguments are exactly the DMA constraints a `bus_dma_tag_create` call would express, which is why `bus_dma(9)` is almost always the better place to encode them.

- **`bus_dmamem_alloc(9)`** is the DMA-aware allocator. It returns a buffer that is usable for DMA under a given tag, properly aligned, optionally coherent, and paired with a `bus_dmamap_t` you can load. This is the call you want in almost every DMA scenario.

**Why this matters to driver writers.** The question to ask yourself is always "who is going to read or write this memory?". If only the CPU ever touches it, `malloc(9)` is fine. If the device needs to read or write it, you want `bus_dmamem_alloc(9)` under a `bus_dma(9)` tag that expresses the device's addressing constraints. Mixing the two is the source of an entire genus of driver bugs.

**Common trap.** Allocating a buffer with `malloc(9)`, handing its virtual address to the device, and wondering why the device reads garbage. The fix is rarely to patch around the symptoms; it is to rewrite the allocation through `bus_dma(9)`.

**Where the book teaches this.** Chapter 5 introduces `malloc(9)` in a kernel-C context, and the driver chapters from Chapter 7 onward use it repeatedly for softc and bookkeeping memory. Chapter 21 introduces `bus_dmamem_alloc`. Appendix A lists the full API and flags.

**What to read next.** `malloc(9)`, `contigmalloc(9)`, `uma(9)`, `bus_dma(9)`.

### IOMMUs and Bus Addresses

**What it is.** An IOMMU is a hardware unit (sometimes the `amdvi` or `intel-iommu` unit; on ARM, the SMMU) that sits between devices and main memory and translates the addresses devices emit into physical addresses. It is the device-side analogue of the MMU.

**Why this matters to driver writers.** When an IOMMU is present, the address the device uses on the bus (a bus address or I/O-virtual address) is not the same as the physical address of the underlying memory. The IOMMU lets the kernel give each device a restricted view of memory, which is good for safety and isolation, and it lets the kernel use scatter-gather where the device would otherwise demand contiguous physical pages.

**How this shows up in FreeBSD.** Transparently, for most drivers. When the kernel is built with the `IOMMU` option and one of the architecture backends (the Intel and AMD drivers under `/usr/src/sys/x86/iommu/` on amd64, the SMMU backend under `/usr/src/sys/arm64/iommu/` on arm64), the `busdma_iommu` layer in `/usr/src/sys/dev/iommu/` bridges `bus_dma(9)` and the IOMMU automatically, without changing the driver's code. You still allocate a tag, create a map, load the map, and read the `ds_addr` of each segment; the number you get is the address the device should use, IOMMU or not. You do not need to know whether an IOMMU is present.

**Common trap.** Assuming `ds_addr` is a physical address and logging it that way. On IOMMU systems the number is an I/O-virtual address and will not match anything you see in `/dev/mem`. The right mental model is "the address the device uses", not "the physical address".

**Where the book teaches this.** Chapter 21 mentions IOMMU integration briefly and sticks to the common case where `bus_dma` hides it.

**What to read next.** `bus_dma(9)`, `/usr/src/sys/dev/iommu/busdma_iommu.c` for the bridge code, and `/usr/src/sys/x86/iommu/` or `/usr/src/sys/arm64/iommu/` for the per-architecture backends.

### Memory Barriers in One Paragraph

On modern CPUs, memory operations may become visible to other agents (other CPUs, other devices) in an order different from the one in which the code issued them. For ordinary RAM the kernel's locking primitives hide this from you. For device registers, though, ordering matters: writing to a "start" register before writing to the "data" register produces a different outcome from the reverse. The kernel provides explicit barriers for both sides. For register access, `bus_space_barrier(tag, handle, offset, length, flags)` is the right tool; for DMA buffers, `bus_dmamap_sync(tag, map, op)` is the right tool. You will meet both again later in this appendix. For now, internalise the rule: when the order of two accesses matters to the hardware, say so with a barrier. Do not rely on the code order alone.

## Buses and Interfaces

A bus is a physical and logical pathway that carries commands, addresses, and data between a CPU and peripherals. FreeBSD treats every bus as a specialised tree in the Newbus framework: the bus has a driver that enumerates its children, each child is a `device_t`, and each child can in turn be a bus for further children. The concept of "a bus" is uniform at the Newbus level. The concepts of "the electrical, protocol, and enumeration rules of a particular bus" are not uniform at all, and a driver author must understand which one they are writing for.

This section gives the mental models for the four buses the book cares about: PCI (and PCIe), USB, I2C, and SPI. Their chapters are later; this is the shared vocabulary you need first.

### PCI and PCIe at a Driver-Relevant Level

**What it is.** PCI is the canonical bus for attaching peripherals to a CPU complex. Classical PCI was parallel and shared; modern PCI Express is a set of point-to-point serial lanes that a switch fabric organises into a tree. From a driver's perspective, the electrical difference is mostly invisible: you still see a configuration space, you still see Base Address Registers, you still see interrupts, and you still write probe-and-attach code. Unless stated otherwise, "PCI" in this book means "PCI and PCIe together".

A PCI device is identified by a Bus:Device:Function (B:D:F) tuple and by a configuration space that the firmware and the kernel populate. The configuration space holds the vendor and device identifiers, the class code that describes what kind of peripheral this is, the Base Address Registers (BARs) that describe the device's memory and I/O windows, and a linked list of capability structures that advertise optional features like MSI, MSI-X, and power management.

**Why this matters to driver writers.** Every PCI driver does roughly the same dance. Read `pci_get_vendor(dev)` and `pci_get_device(dev)` during `probe`, compare them to a table of devices the driver supports, return a `BUS_PROBE_*` value on a match. In `attach`, allocate each BAR with `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)`, pull the `bus_space_tag_t` and `bus_space_handle_t` out with `rman_get_bustag` and `rman_get_bushandle`, allocate the interrupt with `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)` (or `pci_alloc_msix`/`pci_alloc_msi` for message-signalled interrupts), wire the handler with `bus_setup_intr`, and you are in business.

Capabilities are where the PCI driver meets the features of the specific device family. `pci_find_cap(dev, PCIY_MSIX, &offset)` finds the MSI-X capability; `pci_find_extcap(dev, PCIZ_AER, &offset)` finds the extended capability for Advanced Error Reporting. A driver usually only walks the capabilities it cares about.

**How this shows up in FreeBSD.** The PCI bus driver lives under `/usr/src/sys/dev/pci/`. The key header is `/usr/src/sys/dev/pci/pcireg.h` for register constants and `/usr/src/sys/dev/pci/pcivar.h` for functions a client driver calls. Every PCI driver you will write starts with a `device_probe_t` and a `device_attach_t`, with `DRIVER_MODULE(name, pci, driver, 0, 0)` at the bottom of the file. The last two arguments are the per-module event handler and its argument, which are normally left as `0, 0` (or `NULL, NULL`); the separate `devclass_t` slot that older books mention was removed from the macro some releases ago.

**Common trap.** Forgetting to call `pci_enable_busmaster(dev)` before expecting the device to DMA. The chip can live in a quiet state where MMIO works and DMA silently does nothing. Enabling bus-mastering in `attach` is part of the usual sequence.

**Where the book teaches this.** Chapter 18 is the full chapter on PCI driver writing. Chapter 20 extends it with MSI-X.

**What to read next.** `pci(4)`, `pci(9)`, and a compact real example such as `/usr/src/sys/dev/uart/uart_bus_pci.c`. The PCI bus itself is implemented in `/usr/src/sys/dev/pci/pci.c`.

### USB at a Driver-Relevant Level

**What it is.** USB is a host-controlled tiered bus, in which a host controller talks to devices through a tree of hubs. Unlike PCI, the bus is strictly hierarchical and polling-like: the host schedules every transaction. A device does not "speak" on the bus without being asked. USB speed classes (low, full, high, super) share the same conceptual model; the protocol details change.

A USB device exposes a set of endpoints. Each endpoint is a one-way data channel with a specific transfer type: control, bulk, interrupt, or isochronous. Control is the setup channel every device has. Bulk is high-throughput with no timing guarantees. Interrupt is small, periodic, low-latency (mice, keyboards, sensors). Isochronous is real-time streaming (audio, video) that trades reliability for timing.

**Why this matters to driver writers.** A FreeBSD USB driver attaches to a `device_t` whose parent is a USB bus, not a PCI bus. The identification model is different: instead of vendor/device IDs and class codes in configuration space, a USB driver matches on a USB Vendor ID (VID), Product ID (PID), device class, subclass, and protocol. The FreeBSD USB stack gives you a structured probe against those fields. At run time, a USB driver opens specific endpoints through the USB framework and issues transfers whose lifetime is managed by the host controller; you do not write to registers the way a PCI driver does.

**How this shows up in FreeBSD.** The USB stack lives under `/usr/src/sys/dev/usb/`. A class driver uses `usbd_transfer_setup` to describe the endpoints it wants, `usbd_transfer_start` to initiate transfers, and callback functions to handle completion. Transfer-type constants live in `/usr/src/sys/dev/usb/usb.h` (`UE_CONTROL`, `UE_ISOCHRONOUS`, `UE_BULK`, `UE_INTERRUPT`).

**Common trap.** Thinking in terms of registers and interrupts. A USB device driver is event-driven around transfer completion callbacks, not around device-register reads. A register-oriented mindset leads to fighting the framework instead of using it.

**Where the book teaches this.** Chapter 26 is the full chapter on USB and serial drivers. This appendix is the concept-level primer; the chapter is where your first USB driver lives.

**What to read next.** `usb(4)`, `usbdi(9)`, and the headers under `/usr/src/sys/dev/usb/`.

### I2C at a Driver-Relevant Level

**What it is.** I2C (Inter-Integrated Circuit) is a slow, two-wire, master-slave bus designed to connect low-bandwidth peripherals on a board: temperature sensors, power management controllers, EEPROMs, small displays. Each slave has a seven-bit or ten-bit address. A master initiates every transaction by asserting a start condition, sending the address, sending or receiving bytes, and asserting a stop condition.

Speeds are typically 100 kHz (standard), 400 kHz (fast), 1 MHz (fast-plus), and higher variants on newer devices. The bus supports multiple masters, but most embedded systems use a single master; in that case the driver only has to worry about the master-to-slave interaction.

**Why this matters to driver writers.** An I2C driver in FreeBSD comes in two flavours. An I2C *controller* driver implements the master side of a specific controller chip and hangs under the Newbus tree as an I2C bus. An I2C *device* driver is a client that uses the `iicbus` layer to talk to a slave device without knowing the controller details. The second form is what most driver authors write. You ask `iicbus_transfer` to perform a short sequence of `struct iic_msg` operations, and the `iicbus` layer takes care of arbitration and timing.

There are no interrupts or BARs to allocate in the client driver. There is no DMA. Each transfer is short and blocking, typically a few bytes; long reads are broken into a series of transfers.

**How this shows up in FreeBSD.** The framework lives under `/usr/src/sys/dev/iicbus/`. The client interface centres on `iicbus_transfer(dev, msgs, nmsgs)` and the `struct iic_msg` type. FreeBSD discovers I2C buses through FDT on embedded platforms and ACPI on x86 laptops that expose an I2C controller.

**Common trap.** Writing an I2C driver that busy-waits. Even at 1 MHz, a transfer is several tens of microseconds; a driver that pins a CPU through a polling loop wastes resources. The `iicbus` layer handles the waiting for you.

**Where the book teaches this.** The book does not dedicate a full chapter to I2C; this appendix is the primary concept treatment, and the embedded chapter (Chapter 32) revisits it in the context of device-tree bindings. For a concrete client you can read front-to-back, open `/usr/src/sys/dev/iicbus/icee.c`.

**What to read next.** `iicbus(4)`, `/usr/src/sys/dev/iicbus/iicbus.h`, and an example client such as `/usr/src/sys/dev/iicbus/icee.c` (an EEPROM driver).

### SPI at a Driver-Relevant Level

**What it is.** SPI (Serial Peripheral Interface) is a simple, fast, full-duplex, master-slave bus. It has no addressing in the bus itself; each slave has a dedicated chip-select line, and the master asserts that line to begin a transaction. Four wires are typical: SCLK (clock), MOSI (master-out slave-in), MISO (master-in slave-out), and SS/CS (slave-select). Speeds commonly range from a few MHz to tens of MHz.

Unlike I2C, SPI has no protocol beyond the electrical one: the master shifts bits out on MOSI while simultaneously shifting bits in on MISO. What those bits mean is entirely up to the device.

**Why this matters to driver writers.** As with I2C, FreeBSD separates the controller from the client. A controller driver knows how the SPI master in a particular chip is programmed; a client driver uses the `spibus` interface to send and receive bytes without knowing the controller. The client calls `spibus_transfer` or `spibus_transfer_ext` with a `struct spi_command` that describes a single transaction, including the chip-select to assert and the buffers to shift.

**How this shows up in FreeBSD.** The framework lives under `/usr/src/sys/dev/spibus/`. The typical client driver hangs under an `spibus` parent in Newbus and uses the same probe/attach shape as any other device. `/usr/src/sys/dev/spibus/spigen.c` is a generic character-device client that exposes raw SPI to userland; reading it is a good way to see the client side of the framework.

**Common trap.** Expecting SPI to carry data with any built-in framing. It does not. The device datasheet tells you what bytes mean; the bus tells you nothing.

**Where the book teaches this.** The book does not dedicate a full chapter to SPI; this appendix is the primary concept treatment, and the embedded chapter (Chapter 32) revisits it in the context of device-tree bindings. For a concrete example you can read front-to-back, open `/usr/src/sys/dev/spibus/spigen.c`.

**What to read next.** `/usr/src/sys/dev/spibus/spibus.c`, `/usr/src/sys/dev/spibus/spigen.c`, and the device datasheet for whatever SPI peripheral you are driving.

### A Compact Comparison of the Four Buses

A small reference is often easier than a long description. This table compares the buses along the axes that actually affect driver design. It is not a full electrical comparison.

| Aspect | PCI / PCIe | USB | I2C | SPI |
| :-- | :-- | :-- | :-- | :-- |
| Topology | Tree (PCIe switches) | Strict host/hub tree | Multi-drop, addressed | Multi-drop, chip-select |
| Typical speed | Gigabytes/s per lane | Kilobytes/s to gigabytes/s | 100 kHz - 1 MHz | 1 MHz - tens of MHz |
| Identification | Vendor/Device + class | VID/PID + class | 7- or 10-bit slave address | None (chip-select) |
| Protocol framing | PCI TLPs, invisible | Packets + transfer types | Start/address/data/stop | Raw bits |
| Interrupts | INTx or MSI / MSI-X | Transfer callbacks | Rare; bus is polled | Rare; bus is polled |
| DMA | Yes, device-driven | Host controller does it | No | No |
| Driver role | Register-level | Framework client | `iicbus` client | `spibus` client |
| Enumeration | Firmware + `pci(4)` | Hub/device descriptors | FDT / ACPI / hand-wired | FDT / ACPI / hand-wired |

The pattern you should take away is that PCI and USB are complex enough to need their own enumeration stacks and transaction machinery, while I2C and SPI are simple enough that a short transaction struct is the whole API. A driver author's workload looks different on each side of that split.

## Interrupts

An interrupt is how a device tells the CPU that something has happened. Without interrupts, drivers would have to poll, burning CPU cycles to discover events the hardware already knows about. With interrupts, the CPU does other work until the hardware signals; then the kernel dispatches a handler in a narrow, well-defined context. This section explains what that signal actually is, what distinguishes the common forms, and why interrupt discipline is one of the most demanding skills in driver work.

### What an Interrupt Actually Is

**What it is.** At the hardware level, an interrupt is a signal the device raises to request CPU attention. On classical parallel buses, the signal is a line that the device pulls high or low. On modern PCIe, the signal is a memory-write transaction carrying a small payload that the interrupt controller recognises. In both cases, the interrupt controller maps the signal to a CPU vector, the CPU saves just enough state to switch context, and the kernel's low-level entry code decides which driver to call.

What the driver sees is a function call: the interrupt handler runs with a small stack, in a special scheduling context, usually with preemption disabled or highly constrained. The hardware model is "a device raised a line"; the software model is "the kernel ran your callback".

**Why this matters to driver writers.** Interrupts are not ordinary function calls. They happen asynchronously, possibly on a different CPU from the one your driver is running on, and they can arrive in the middle of almost any code path. That is why interrupt handlers must be short, must avoid sleeping, and must not touch shared state without a lock that is safe in that context. Most of the discipline a driver learns about locking, ordering, and deferred work exists because of this one fact.

**How this shows up in FreeBSD.** Interrupts reach drivers through `intr_event(9)`. A driver calls `bus_setup_intr(dev, irq_resource, flags, filter, ithread, arg, &cookie)` to register up to two callbacks: a *filter* that runs first, in interrupt context, with only spin locks available, and optionally an *ithread* (interrupt thread) that runs as a kernel thread and may use sleep locks. The filter's return value decides what happens next: `FILTER_HANDLED` (fully handled, acknowledge at the controller), `FILTER_STRAY` (not mine, do nothing), or `FILTER_SCHEDULE_THREAD` (the ithread should run now). Those constants live in `/usr/src/sys/sys/bus.h`.

**Common trap.** Doing real work in the filter. The filter is there to decide ownership, acknowledge the device quickly if possible, and either handle a trivially small amount of work or schedule the thread. Long processing belongs in the ithread or on a taskqueue, not in the filter.

**Where the book teaches this.** Chapter 19 is the dedicated chapter on interrupts. Chapter 20 adds MSI and MSI-X. Chapter 14 (taskqueues) explains how to defer heavy work out of the interrupt path.

**What to read next.** `intr_event(9)`, `bus_setup_intr(9)`, and the filter/ithread documentation in `/usr/src/sys/sys/bus.h` around the `FILTER_` and `INTR_` constants.

### Edge-Triggered vs Level-Triggered

**What it is.** Two ways the hardware signal can mean "there is something new".

- **Edge-triggered.** The device asserts a transition (a rising or falling edge) to signal an event. If the driver does not record that an interrupt happened, the event is lost. The edge is a moment, not a state.
- **Level-triggered.** The device holds the line (or the signal) active as long as the condition persists. The interrupt controller keeps firing until the driver acknowledges the device and the line deasserts. The level is a state, not a moment.

**Why this matters to driver writers.** Edge-triggered interrupts are unforgiving: if the driver has a race and a second edge arrives while the first is being processed without being recorded, the second event is forgotten. Level-triggered interrupts are self-healing: if the line is still asserted when you return, the controller will fire again. Legacy PCI INTx lines are level-triggered and shared, which is why driver handlers must read the device's status register and check whether "this interrupt" is really theirs before claiming it. MSI and MSI-X are effectively edge-like: each message is a discrete event.

**How this shows up in FreeBSD.** The distinction is mostly handled below your driver. Legacy INTx registration with `SYS_RES_IRQ` and rid `0` gets you level-triggered, shared interrupts; `pci_alloc_msi` and `pci_alloc_msix` give you message-signalled interrupts that are per-vector and not shared with unrelated drivers. Your filter still has to behave well; but the sharing concern goes away.

**Common trap.** Writing a filter that returns `FILTER_HANDLED` without actually checking the device. On level-triggered INTx, this hangs the system because the line stays asserted and the controller fires forever.

**Where the book teaches this.** Chapter 19 teaches the classical case; Chapter 20 teaches the message-signalled case.

**What to read next.** The legacy interrupt discussion in `/usr/src/sys/dev/pci/pci.c` around `pci_alloc_msi`, and the filter examples in Chapter 19.

### MSI and MSI-X Without the Acronyms

**What it is.** Message-Signalled Interrupts. Instead of asserting a shared interrupt line, a PCI Express device sends a small memory-write transaction to a special address that the interrupt controller watches. The payload of the write identifies which interrupt has fired. MSI supports a handful of vectors per device (typically up to 32); MSI-X supports thousands, each with its own address and data, and per-vector CPU affinity.

**Why this matters to driver writers.** Two things change when a driver uses MSI or MSI-X instead of legacy INTx. First, the interrupts are no longer shared, so the filter does not need to coexist with unrelated drivers (it still has to confirm the event came from its own hardware). Second, you can have more than one interrupt per device. A NIC can have one interrupt per receive queue, another per transmit queue, another for the management path, and you can bind each one to a different CPU. That changes how you structure the driver: each queue has its own softc substructure, its own lock, and its own handler.

**How this shows up in FreeBSD.** `pci_msi_count(dev)` and `pci_msix_count(dev)` tell you how many vectors are available. `pci_alloc_msi(dev, &count)` and `pci_alloc_msix(dev, &count)` reserve them; the call updates `count` with the number actually allocated. You then allocate each vector with `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)` using `rid = 1, 2, ..., count`. CPU affinity is set with `bus_bind_intr(dev, res, cpu_id)`. Teardown mirrors the sequence: per-vector `bus_teardown_intr` and `bus_release_resource`, then a single `pci_release_msi(dev)`.

**Common trap.** Requesting too many vectors and assuming the system will give them all. `pci_alloc_msix` may return fewer than requested, even on hardware that advertises plenty; the driver must adapt its queue structure to the number actually allocated. A fallback ladder (MSI-X, then MSI, then INTx) is the standard shape.

**Where the book teaches this.** Chapter 20.

**What to read next.** `pci(9)` for the function list, and `/usr/src/sys/dev/pci/pci.c` for the implementation.

### Why Interrupt Discipline Matters

**What it is.** The set of rules that keep a driver correct under interrupts. Most of it follows from three facts: the filter runs with very few primitives available, the filter can race with every other path in the driver, and late acknowledgement produces wedged systems.

**Core rules.**

- Keep the filter short and allocation-free. No `malloc`, no sleeping, no calls that might sleep, no complex logic.
- Acknowledge at the device before returning `FILTER_HANDLED`, or make sure the device continues to assert until your ithread runs.
- Use spin locks (`MTX_SPIN`) to protect state the filter and a CPU path share. Sleep locks cannot be taken in the filter.
- Defer real work to an ithread, a taskqueue, or a callout. The filter's job is to move data off the critical path.
- Count your interrupts. A `sysctl` counter per vector is cheap and often the first clue when the driver misbehaves.

**Common trap.** Holding a sleep lock across a path that could be re-entered from the filter. A thread that owns a sleep mutex cannot be preempted by an interrupt on the same CPU; the kernel's `witness(9)` diagnostic will catch many of these before production, but only if the scenario is actually exercised.

**Where the book teaches this.** Chapter 11 introduces the discipline in general; Chapter 19 makes it concrete for real hardware.

**What to read next.** The locking summary in Appendix A and the filter/ithread split in `/usr/src/sys/sys/bus.h`.

## DMA

DMA (Direct Memory Access) is how a peripheral reads or writes memory without the CPU copying every byte. It exists because the alternative (the CPU reading a register, placing the byte in RAM, and looping) is too slow for any modern device. DMA is also the most hardware-aware part of a driver, because it is where the driver must speak the bus's address language instead of the kernel's virtual-pointer language. A driver that is not disciplined about DMA will produce data corruption that is almost impossible to diagnose from user space.

### What DMA Is and Why It Exists

**What it is.** A transfer in which the device reads from or writes to system memory under its own control, using bus addresses the driver has handed it in advance. The CPU sets up the transfer and goes on with other work; the device signals completion, usually by raising an interrupt. There is no per-byte CPU involvement during the data movement itself.

**Why this matters to driver writers.** Everything about how you allocate, load, synchronise, and release memory changes the moment DMA enters the picture. You no longer own the buffer exclusively; the device owns it during the transfer. You must tell the kernel which parts of the buffer are being read or written and when, so that cache coherence, alignment, and address remapping can be handled correctly.

**How this shows up in FreeBSD.** `bus_dma(9)` is the whole machinery. The driver author's workflow is consistent across every DMA-capable device:

1. Create a `bus_dma_tag_t` in `attach`, describing the device's addressing constraints (alignment, boundary, `lowaddr`, `highaddr`, maximum segment size, number of segments, and so on). The tag captures the device's truth.
2. Allocate a DMA-capable buffer with `bus_dmamem_alloc(tag, &vaddr, flags, &map)`, or allocate an ordinary buffer and load it later.
3. Load the buffer with `bus_dmamap_load(tag, map, vaddr, length, callback, arg, flags)`. The callback receives one or more `bus_dma_segment_t` entries whose `ds_addr` fields are the bus addresses to hand the device.
4. Before the device reads, `bus_dmamap_sync(tag, map, BUS_DMASYNC_PREWRITE)`. Before the CPU reads what the device wrote, `bus_dmamap_sync(tag, map, BUS_DMASYNC_POSTREAD)`. The flags are defined in `/usr/src/sys/sys/bus_dma.h`.
5. When the transfer is complete, `bus_dmamap_unload(tag, map)` releases the mapping; `bus_dmamem_free(tag, vaddr, map)` releases the buffer; `bus_dma_tag_destroy(tag)` releases the tag (usually in `detach`).

**Common trap.** Forgetting the `PREWRITE` or `POSTREAD` sync. On coherent architectures (amd64 with IOMMU, for example), the driver often works without it; on non-coherent architectures (some ARM systems), it silently corrupts data. Write the sync calls every time, even when you are developing on a forgiving platform.

**Where the book teaches this.** Chapter 21 is the full-length dedicated treatment.

**What to read next.** `bus_dma(9)` and `/usr/src/sys/kern/subr_bus_dma.c`.

### The Mental Model: Tags, Maps, Segments, Sync

A short refresher in the shape of a diagram. The pieces fit together like this:

```text
+----------------------------------------------------------+
| Device constraints  ->  bus_dma_tag_t     (made once)    |
+----------------------------------------------------------+
         |
         v
+----------------------------------------------------------+
| Buffer in kernel memory  ->  virtual address             |
+----------------------------------------------------------+
         |
         v  bus_dmamap_load()
+----------------------------------------------------------+
| bus_dmamap_t  ->  one or more bus_dma_segment_t entries  |
|                    (ds_addr, ds_len)                     |
+----------------------------------------------------------+
         |
         v  bus_dmamap_sync(PREWRITE) / PREREAD
+----------------------------------------------------------+
| Device reads or writes memory at ds_addr                 |
+----------------------------------------------------------+
         |
         v  bus_dmamap_sync(POSTWRITE) / POSTREAD
+----------------------------------------------------------+
| CPU reads the buffer; kernel knows ordering is valid     |
+----------------------------------------------------------+
```

Each arrow is a responsibility. The tag expresses what the device can tolerate. The map expresses one particular set of segments the device will actually touch. The sync calls bracket the device's use of the buffer. Miss any one of them and the transfer is undefined.

### Why Synchronisation and Mapping Matter

**What it is.** Two concerns that together explain why `bus_dma` is not just an address translator.

- **Mapping.** The device needs a bus address, not a virtual one. On IOMMU systems, the mapping is a real I/O-virtual range; on non-IOMMU systems, it is the physical address, possibly through a bounce buffer if the device cannot reach where the memory actually lives. The driver does not choose which; `bus_dma` chooses based on the tag.
- **Synchronisation.** The CPU has caches. The device may have its own. The kernel has ordering rules. The sync calls translate the driver's intent ("I am about to hand this buffer to the device for reading") into whatever cache flushes, invalidations, or memory barriers the architecture requires.

**Why this matters to driver writers.** Skipping synchronisation is the single most common source of subtle data corruption in a DMA driver. The code appears to work because the CPU caches happen to be flushed by other activity; then a kernel update changes cache behaviour slightly and the driver begins to fail. Writing every sync call every time is the only reliable habit.

**How this shows up in FreeBSD.** The four sync flags are `BUS_DMASYNC_PREREAD`, `BUS_DMASYNC_PREWRITE`, `BUS_DMASYNC_POSTREAD`, and `BUS_DMASYNC_POSTWRITE` (values 1, 4, 2, 8 in `/usr/src/sys/sys/bus_dma.h`). "PRE" runs before the device touches the buffer; "POST" runs after. The "READ" and "WRITE" halves follow the wording in `bus_dma(9)`: `PREREAD` is "the device is about to update host memory, and the CPU will later read what was written"; `PREWRITE` is "the CPU has updated host memory, and the device is about to access it". In the pair of directions a driver usually thinks about, `PREWRITE` covers the CPU-writes-buffer-then-device-reads case, and `PREREAD` covers the device-writes-buffer-then-CPU-reads case.

**Common trap.** Reading the flag names as if they described the device's own action. If the device is about to write to memory, and the CPU will later read what the device wrote, you call `BUS_DMASYNC_PREREAD` first (because the CPU will later read this memory), not `BUS_DMASYNC_PREWRITE`. The flag pairs track the memory operation the CPU is part of, not the direction the device happens to move data on the wire.

**Where the book teaches this.** Chapter 21 spends substantial space on the sync semantics because they are easy to get wrong.

**What to read next.** `bus_dma(9)` and the header comments in `/usr/src/sys/sys/bus_dma.h`.

### DMA Buffers vs Control Buffers

**What it is.** A final distinction that keeps the memory section and the DMA section in step. Not every buffer in a DMA-capable driver is a DMA buffer.

- **DMA buffers** are the ones the device reads from or writes to. They go through `bus_dma(9)`: allocated with `bus_dmamem_alloc` or `malloc` plus `bus_dmamap_load`, loaded, synchronised, unloaded.
- **Control buffers** are the ones only the CPU touches: ring indices, per-request bookkeeping, command structures the driver inspects but the hardware does not. They live in ordinary `malloc(9)` memory.

**Why this matters to driver writers.** Both kinds of buffer usually exist in the same driver, often side by side inside the same softc. Keeping them straight makes the code much easier to review. A pointer that the device reads should be labelled and allocated unambiguously; a pointer the device never sees should be labelled and allocated just as unambiguously. Confusing the two usually produces hard-to-reproduce bugs.

**Where the book teaches this.** Chapter 21 revisits the split when it walks a complete descriptor-ring driver.

## Quick-Reference Tables

The compact tables below are meant for scanning. They do not replace the sections above; they help you point at the right section fast.

### Address Spaces at a Glance

| You have... | It lives in... | How you get it | Who can dereference it |
| :-- | :-- | :-- | :-- |
| A C pointer in kernel code | Kernel virtual memory | `malloc`, `uma_zalloc`, stack, softc | CPU |
| A device register | Physical I/O space | `bus_alloc_resource(SYS_RES_MEMORY,...)` + `bus_space_handle_t` | CPU via `bus_space_*` |
| A DMA buffer | Kernel virtual, plus a bus view | `bus_dmamem_alloc` | CPU via virtual, device via `ds_addr` |
| A device-side bus address | Bus address space (maybe IOMMU-mapped) | `bus_dmamap_load` callback | Device only |

### When to Use Which Allocator

| Purpose | Call |
| :-- | :-- |
| Softc, lists, control structures | `malloc(9)` with `M_DRIVER_NAME` |
| Frequent fixed-size objects | `uma(9)` zone |
| DMA buffer with tag constraints | `bus_dmamem_alloc(9)` under a `bus_dma` tag |
| Physically contiguous block, no DMA tag | `contigmalloc(9)` (rarely) |
| Stack temporary, small and bounded | Plain C stack |

### Interrupt vs Polling

| You should use... | When... |
| :-- | :-- |
| A hardware interrupt | The event rate is moderate and latency matters |
| Polling inside an ithread | The event rate is very high and interrupts would dominate |
| A taskqueue triggered by interrupts | The work is heavy and cannot run in the filter |
| A timer (callout) plus occasional interrupts | The device is slow and state is simple |

### Bus at a Glance (Driver-Writer View)

| Bus | Driver form | Identification | Main API call |
| :-- | :-- | :-- | :-- |
| PCI / PCIe | Full Newbus driver with registers | Vendor/Device ID + class | `bus_alloc_resource_any`, `bus_setup_intr` |
| USB | Framework class driver | VID/PID + class/subclass/protocol | `usbd_transfer_setup`, `usbd_transfer_start` |
| I2C | `iicbus` client | Slave address | `iicbus_transfer` |
| SPI | `spibus` client | Chip-select line | `spibus_transfer` |

### DMA Sync Quick Check

| You are about to... | Call this |
| :-- | :-- |
| Hand a buffer to the device for the device to *read* | `bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)` |
| Hand a buffer to the device for the device to *write* | `bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)` |
| Read from a buffer the device just wrote | `bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)` |
| Reuse a buffer after the device read it | `bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)` |

The "PRE" calls fence the CPU writes or cache lines before the device looks at the buffer. The "POST" calls fence the device's completion before the CPU looks at what changed.

## Wrapping Up: How to Keep Relating Hardware Concepts to Driver Code

The hardware concepts in this appendix are not a separate subject from driver writing. Every one of them shows up in code. The sequence the reader will use, over and over, is always the same:

1. The device exists in physical address space. Its registers are at physical addresses.
2. The kernel gives the driver a `bus_space` handle that maps those registers with the right attributes.
3. The device raises interrupts on events, and the driver handles them through a filter (fast, small, spin-lock safe) and optionally an ithread (slower, sleep-lock safe).
4. When the device needs to move data, the driver uses `bus_dma(9)` to produce bus addresses that the device can use, and it brackets every transfer with sync calls.
5. The driver never hands a kernel virtual pointer to the device, and never treats a `ds_addr` as if it were a physical address the CPU can read directly.

If you keep that sequence in mind, the hardware concepts here stop being a separate subject and become a running commentary on the code you are writing. When you are about to cast a pointer and hand it to hardware, stop and translate the action into the sequence: in which address space is this, who is going to read it, what attributes does that view need. The sequence never changes. The details change with every device family.

Three habits that reinforce the model.

The first is reading the header comment of a real driver before reading its code. Most FreeBSD drivers open with a block that explains the locking discipline, the MSI-X structure, or the ring layout. That comment is there because the code alone cannot communicate it. When the comment says "the receive ring uses the per-queue lock; the filter acknowledges the device and schedules a taskqueue", read it carefully. It is a condensed map of the hardware-to-software bridge for that driver.

The second is naming the bus every time. When you read an unfamiliar driver, find the `DRIVER_MODULE` line. The bus name in that line (`pci`, `usb`, `iicbus`, `spibus`, `acpi`, `simplebus`) tells you which hardware model applies before you read a single function. A driver attached to `pci` is register-oriented and interrupt-driven; a driver attached to `iicbus` is transaction-oriented and polled at the bus level. The same C code reads differently when you know which bus is behind it.

The third is keeping a short companion sheet with your driver notes. The files under `examples/appendices/appendix-c-hardware-concepts-for-driver-developers/` are meant for exactly this. A bus-comparison cheatsheet you can glance at when you open a new datasheet. A DMA mental-model checklist you can walk when you suspect a sync is missing. A physical-vs-virtual diagram you can annotate with the particular tag and handle pair you are working with today. The teaching is in this appendix; the application is in those sheets.

With that, the hardware side of the book has a consolidated home. The chapters keep teaching; the appendix keeps naming; the examples keep reminding. When a reader closes this appendix and opens a real driver, the vocabulary is ready.
