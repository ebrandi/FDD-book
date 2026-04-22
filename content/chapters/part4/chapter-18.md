---
title: "Writing a PCI Driver"
description: "Chapter 18 turns the simulated myfirst driver into a real PCI driver. It teaches the PCI topology, how FreeBSD enumerates PCI and PCIe devices, how a driver probes and attaches by vendor and device ID, how BARs become bus_space tags and handles through bus_alloc_resource_any, how attach-time initialisation proceeds against a real BAR, how the Chapter 17 simulation is kept inactive on the PCI path, and how a clean detach path tears the whole attachment down in reverse order. The driver grows from 1.0-simulated to 1.1-pci, gains a new pci-specific file, and leaves Chapter 18 ready to host a real interrupt handler in Chapter 19."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 18
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 225
---

# Writing a PCI Driver

## Reader Guidance & Outcomes

Chapter 17 ended with a driver that looked like a real device from the outside and behaved like one from the inside. The `myfirst` module at version `1.0-simulated` carries a register block, a `bus_space(9)`-based accessor layer, a simulation backend with callouts that produce autonomous state changes, a fault-injection framework, a command-response protocol, statistics counters, and three living documentation files (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`). Every register access in the driver still goes through `CSR_READ_4(sc, off)`, `CSR_WRITE_4(sc, off, val)`, and `CSR_UPDATE_4(sc, off, clear, set)`. The hardware layer (`myfirst_hw.c` and `myfirst_hw.h`) is a thin wrapper that produces a tag and a handle, and the simulation layer (`myfirst_sim.c` and `myfirst_sim.h`) is what makes those registers come alive. The driver itself does not know whether the register block is real silicon or a `malloc(9)` allocation.

That ambiguity is the gift Chapter 16 and Chapter 17 gave us, and Chapter 18 is where we finally cash it in. The driver will now meet real PCI hardware. The register block will not come from the kernel's heap any more; it will come from a device's Base Address Register, assigned at boot by the firmware, mapped by the kernel into a virtual address range with device-memory attributes, and handed to the driver as a `struct resource *`. The accessor layer does not change. A compile-time switch keeps the Chapter 17 simulation available as a separate build for readers without a PCI test environment; on the PCI build the simulation callouts do not run, so they cannot accidentally write into the real device's registers. What changes is the point at which the tag and handle originate: instead of being produced by `malloc(9)`, they will be produced by `bus_alloc_resource_any(9)` against a PCI child in the newbus tree.

Chapter 18's scope is precisely this transition. It teaches what PCI is, how FreeBSD represents a PCI device in the newbus tree, how a driver matches a device by vendor and device ID, how BARs appear in configuration space and become `bus_space` resources, how attach-time initialisation proceeds against a real BAR without disturbing the device, and how detach unwinds everything in reverse order. It covers the configuration-space accessors `pci_read_config(9)` and `pci_write_config(9)`, the capability walkers `pci_find_cap(9)` and `pci_find_extcap(9)`, and a brief introduction to PCIe Advanced Error Reporting so the reader knows where it lives without being asked to handle it yet. It ends with a small but significant refactor that splits the new PCI-specific code into its own file, versions the driver as `1.1-pci`, and runs the full regression pass against both the simulation build and the real PCI build.

Chapter 18 deliberately narrows itself to the probe-attach dance and what feeds it. Real interrupt handlers through `bus_setup_intr(9)`, filter plus ithread composition, and the rules for what a handler may and may not do are Chapter 19. MSI and MSI-X, along with the richer PCIe capabilities they expose, are Chapter 20. Descriptor rings, scatter-gather DMA, cache coherence around device writes, and the full `bus_dma(9)` story are Chapters 20 and 21. Configuration-space quirks on specific chipsets, power-management state machines during suspend and resume, and SR-IOV are later chapters. The chapter stays inside the ground it can cover well and hands off explicitly when a topic deserves its own chapter.

The layers of Part 4 stack. Chapter 16 taught the vocabulary of register access; Chapter 17 taught how to think like a device; Chapter 18 teaches how to meet a real one. Chapter 19 will teach you how to react to what the device says, and Chapters 20 and 21 will teach you how to let the device reach directly into RAM. Each layer depends on the one before it. Chapter 18 is your first encounter with the newbus tree as something more than an abstract diagram, and the disciplines Part 3 built are what keep the encounter honest.

### Why the PCI Subsystem Earns a Chapter of Its Own

At this point you may be asking why the PCI subsystem needs a chapter of its own. The simulation already gave us registers; the real hardware will give us the same registers. Why not simply say "call `bus_alloc_resource_any`, pass the returned handle to `bus_read_4`, and proceed"?

Two reasons.

The first is that the PCI subsystem is the most widely used bus in modern FreeBSD, and the newbus conventions around it are the conventions every other bus driver imitates. A reader who understands the PCI probe-attach dance can read the ACPI attach dance, the USB attach dance, the SD card attach dance, and the virtio attach dance without retraining. The patterns differ in detail, but the shape is PCI's. Spending a whole chapter on the canonical bus is spending a chapter on the pattern every bus borrows from.

The second is that PCI introduces concepts no earlier chapter prepared you for. Configuration space is a second address space per device, separate from the BARs themselves, where the device advertises what it is and what it needs. Vendor and device IDs are a sixteen-bit plus sixteen-bit tuple that the driver matches against a table of supported devices. Subvendor and subsystem IDs are a second-level tuple that disambiguates cards built around a common chipset by different vendors. Class codes let a driver match broad categories (any USB host controller, any UART) when a device-specific table would be too narrow. BARs exist in configuration space as thirty-two-bit or sixty-four-bit addresses the driver never directly dereferences. PCI capabilities are a linked list of extra metadata the driver reads at attach time. Each of these is new vocabulary; each of them is the reason Chapter 18 is not a single section bolted onto Chapter 17.

The chapter also earns its place by being the chapter where the `myfirst` driver gains its first real bus child. Until now, the driver has lived as a kernel module with a single implicit instance, attached by hand through `kldload` and detached by `kldunload`. After Chapter 18, the driver will be a proper PCI bus child, enumerated by the kernel's newbus code, attached automatically when a matching device is present, detached automatically when the device goes away, and visible in `devinfo -v` as a device with a parent (`pci0`), a unit (`myfirst0`, `myfirst1`), and a set of claimed resources. That change is the change from "a module that happens to exist" to "a driver for a device that the kernel knows about". Every later Part 4 chapter assumes you have made it.

### Where Chapter 17 Left the Driver

A few prerequisites to verify before starting. Chapter 18 extends the driver produced at the end of Chapter 17 Stage 5, tagged as version `1.0-simulated`. If any of the items below feels uncertain, return to Chapter 17 before starting this chapter.

- Your driver compiles cleanly and identifies itself as `1.0-simulated` in `kldstat -v`.
- The softc carries `sc->hw` (a `struct myfirst_hw *` from Chapter 16) and `sc->sim` (a `struct myfirst_sim *` from Chapter 17). Every register access goes through `sc->hw`; every simulated behaviour lives under `sc->sim`.
- The register map of sixteen 32-bit registers spans offsets `0x00` through `0x3c`, with the Chapter 17 additions (`SENSOR`, `SENSOR_CONFIG`, `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB`, `OP_COUNTER`) in place.
- `CSR_READ_4`, `CSR_WRITE_4`, and `CSR_UPDATE_4` wrap `bus_space_read_4`, `bus_space_write_4`, and a read-modify-write helper. Every access asserts `sc->mtx` is held on debug kernels.
- The sensor callout runs once per second on a ten-second cadence and oscillates `SENSOR`. The command callout fires per command with a configurable delay. The fault-injection framework is live.
- The module depends on nothing outside the base kernel; it is a `kldload`-able standalone driver.
- `HARDWARE.md`, `LOCKING.md`, and `SIMULATION.md` are current.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel.

That driver is what Chapter 18 extends. The additions are again modest in volume: one new file (`myfirst_pci.c`), one new header (`myfirst_pci.h`), one new set of probe and attach routines, a small change to `myfirst_hw_attach` to accept a resource rather than allocate a buffer, an expanded detach ordering, a bump to `1.1-pci`, a new `PCI.md` document, and an updated regression script. The mental-model change is larger than the line count suggests.

### What You Will Learn

When you close this chapter you should be able to:

- Describe what PCI and PCIe are in one paragraph, in a way a beginner could follow, with the key vocabulary of bus, device, function, BAR, configuration space, vendor ID, and device ID clearly placed.
- Read the output of `pciconf -lv` and `devinfo -v` and locate the information a driver author cares about: the B:D:F tuple, the vendor and device IDs, the class, the subclass, the interface, the claimed resources, and the parent bus.
- Write a minimal PCI driver that registers a probe routine against the `pci` bus, matches a specific vendor and device ID, returns a meaningful `BUS_PROBE_*` priority, prints the matched device through `device_printf`, and unloads cleanly.
- Attach and detach a PCI driver, matching the expected lifecycle of newbus: probe runs first (sometimes twice), attach runs once per matched device, detach runs once when the kernel removes the device or the driver, and the softc is freed in the correct order.
- Use `DRIVER_MODULE(9)` and `MODULE_DEPEND(9)` correctly, naming the bus (`pci`) and the module dependency (`pci`) so the kernel's module loader and newbus enumerator understand the relationship.
- Explain what BARs are, how the firmware assigns them at boot, how the kernel discovers them, and why the driver does not choose the address.
- Claim a PCI memory BAR with `bus_alloc_resource_any(9)`, extract its bus_space tag and handle with `rman_get_bustag(9)` and `rman_get_bushandle(9)`, and hand them to the Chapter 16 accessor layer without any change to the CSR macros.
- Recognise when a BAR is a 64-bit BAR that spans two configuration-space slots, how `PCIR_BAR(index)` works, and why counting BARs by simple integer increments is not always safe on 64-bit BARs.
- Use `pci_read_config(9)` and `pci_write_config(9)` to read device-specific configuration-space fields that the generic accessors do not cover, and understand the width argument (1, 2, or 4) and the side-effect contract.
- Walk a device's PCI capability list with `pci_find_cap(9)` to locate standard capabilities (Power Management, MSI, MSI-X, PCI Express), and walk the PCIe extended capability list with `pci_find_extcap(9)` to reach modern capabilities such as Advanced Error Reporting.
- Call `pci_enable_busmaster(9)` when the device will initiate DMA later, recognise why the MEMEN and PORTEN bits of the command register are usually already set by the bus driver by attach time, and know when a quirky device needs them asserted manually.
- Write an attach-time initialisation sequence that keeps the Chapter 17 simulation backend inactive on the PCI path while preserving a simulation-only build (via a compile-time switch) for readers without a PCI test environment.
- Write a detach path that releases resources in strictly the reverse order of attach, never leaking a resource or double-freeing one, even in the presence of partial attach failure.
- Exercise the driver against a real PCI device in a bhyve or QEMU guest, using a vendor and device ID that does not collide with a base-system driver, and observe the full attach, operate, detach, and unload cycle.
- Split the PCI-specific code into its own file, update the module's `SRCS` line, tag the driver as `1.1-pci`, and produce a short `PCI.md` that documents the vendor and device IDs the driver supports.
- Describe at a high level where MSI, MSI-X, and PCIe AER fit into the PCI picture, and know which later chapter picks each topic up.

The list is long; each item is narrow. The point of the chapter is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 18 stays focused.

- **Real interrupt handlers.** `bus_alloc_resource_any(9)` for `SYS_RES_IRQ`, `bus_setup_intr(9)`, the split between a filter handler and an ithread handler, `INTR_TYPE_*` flags, `INTR_MPSAFE`, and the rules for what may and may not happen inside a handler belong to Chapter 19. Chapter 18's driver still polls through user-space writes and the Chapter 17 callouts; it never takes a real interrupt.
- **MSI and MSI-X.** `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, vector allocation, per-queue interrupt routing, and the MSI-X table layout are Chapter 20. Chapter 18 only mentions these as future work when it lists PCI capabilities.
- **DMA.** `bus_dma(9)` tags, `bus_dmamap_create(9)`, `bus_dmamap_load(9)`, scatter-gather lists, bounce buffers, and cache-coherent descriptor rings are Chapters 20 and 21. Chapter 18 treats the BAR as a set of memory-mapped registers and nothing else.
- **PCIe AER handling.** The existence of Advanced Error Reporting is introduced because the reader should know the topic exists. Implementing a fault handler that subscribes to AER events, decodes the uncorrectable error register, and participates in system-wide recovery is a later-chapter topic.
- **Hot plug, device removal, and live runtime suspend.** A PCI device arriving or departing at runtime triggers a specific newbus sequence that a driver must respect; most drivers do respect it simply by having a correct detach path. Chapter 18 demonstrates the correct detach path and leaves runtime power management for Chapter 22 and hotplug for Part 7 (Chapter 32 on embedded platforms and Chapter 35 on asynchronous I/O and event handling).
- **Passthrough to virtual machines.** `bhyve(8)` and `vmm(4)` can pass a real PCI device through to a guest, and this is a useful technique for testing. Chapter 18 mentions it in passing. A deeper treatment belongs to the chapter where it serves the topic.
- **SR-IOV and virtual functions.** A PCIe capability where a single device advertises multiple virtual functions, each with its own configuration space, is out of scope for a beginner-level chapter.
- **Specific chipset quirks.** Real drivers often carry a long list of errata and workarounds for specific revisions of specific silicon. Chapter 18 aims at the common case; the book's later troubleshooting chapters cover how to reason about quirks when you meet them.

Staying inside those lines keeps Chapter 18 a chapter about the PCI subsystem and the driver's place in it. The vocabulary is what transfers; the specific chapters that follow apply the vocabulary to interrupts, DMA, and power.

### Estimated Time Investment

- **Reading only**: four to five hours. The PCI topology and the newbus sequence are small in concept but dense in detail, and each piece rewards a slow read.
- **Reading plus typing the worked examples**: ten to twelve hours over two or three sessions. The driver evolves in four stages; each stage is a small but real refactor on top of the Chapter 17 codebase.
- **Reading plus all labs and challenges**: sixteen to twenty hours over four or five sessions, including standing up a bhyve or QEMU lab, reading `uart_bus_pci.c` and `virtio_pci_modern.c` in the real FreeBSD tree, and running the regression pass against both simulation and real PCI.

Sections 2, 3, and 5 are the densest. If the probe-attach sequence or the BAR allocation path feels unfamiliar on first pass, that is normal. Stop, re-read Section 3's diagram of how a BAR becomes a tag and handle, and continue when the picture has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 17 Stage 5 (`1.0-simulated`). The starting point assumes the Chapter 16 hardware layer, the Chapter 17 simulation backend, the complete `CSR_*` accessor family, the sync header, and every primitive introduced in Part 3.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- `bhyve(8)` or `qemu-system-x86_64` is available on your lab host, and you can start a FreeBSD guest that boots your debug kernel. A bhyve guest is the canonical choice in this book; QEMU will work equivalently for every lab in this chapter.
- The `devinfo(8)` and `pciconf(8)` tools are in your path. Both live in the base system.

If any item above is shaky, fix it now rather than pushing through Chapter 18 and trying to reason from a moving foundation. PCI code is less forgiving than simulation code because a mismatch between your driver's expectations and the real bus's behaviour will usually surface as a failed probe, a failed attach, or a kernel page fault.

### How to Get the Most Out of This Chapter

Four habits will pay off quickly.

First, keep `/usr/src/sys/dev/pci/pcireg.h` and `/usr/src/sys/dev/pci/pcivar.h` bookmarked. The first file is the authoritative register map of PCI and PCIe configuration space; every macro starting with `PCIR_`, `PCIM_`, or `PCIZ_` is defined there. The second file is the authoritative list of PCI accessor functions (`pci_get_vendor`, `pci_read_config`, `pci_find_cap`, and so on), along with their documentation comments. Reading both files once takes about an hour and removes most of the guesswork the rest of the chapter might otherwise require.

> **A note on line numbers.** The declarations we will lean on later, such as `pci_read_config`, `pci_find_cap`, and the `PCIR_*` register-offset macros, sit in `pcivar.h` and `pcireg.h` under stable names. Whenever the chapter hands you a landmark in those files, the landmark is the symbol. Line numbers drift release to release; the names do not. Grep for the symbol and trust what your editor reports.

Second, run `pciconf -lv` on your lab host and your guest, and keep the output open in a terminal as you read. Every vocabulary item in the chapter (vendor, device, class, subclass, capabilities, resources) appears verbatim in that output. A reader who has read `pciconf -lv` for their own hardware finds the PCI subsystem much less abstract than one who has not.

Third, type the changes by hand and run each stage. PCI code is where small typos become silent mismatches. Mistyping `0x1af4` as `0x1af5` does not produce a compile error; it produces a driver that compiles cleanly and never attaches. Typing the values character by character, checking them against your test target, and confirming that `kldstat -v` shows the driver claiming the expected device are the habits that prevent a day of confused debugging.

Fourth, read `/usr/src/sys/dev/uart/uart_bus_pci.c` after Section 2 and `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` after Section 5. The first file is a straightforward example of the pattern Chapter 18 teaches, written at a level a beginner can follow. The second file is a slightly richer example that shows how a real modern driver composes the pattern with additional machinery. Neither needs to be understood line by line; both repay a careful first reading.

### Roadmap Through the Chapter

The sections in order are:

1. **What Is PCI and Why It Matters.** The bus, the topology, the B:D:F tuple, how FreeBSD represents PCI through the `pci(4)` subsystem, and how a driver author perceives all of this through `pciconf -lv` and `devinfo -v`. The conceptual foundation.
2. **Probing and Attaching a PCI Device.** The newbus dance from the driver's side: `device_method_t`, probe, attach, detach, resume, suspend, `DRIVER_MODULE(9)`, `MODULE_DEPEND(9)`, vendor and device ID matching, and the first stage of the Chapter 18 driver (`1.1-pci-stage1`).
3. **Understanding and Claiming PCI Resources.** What a BAR is, how the firmware assigns it, how `bus_alloc_resource_any(9)` claims it, and how the returned `struct resource *` becomes a `bus_space_tag_t` and `bus_space_handle_t`. The second stage (`1.1-pci-stage2`).
4. **Accessing Device Registers via `bus_space(9)`.** How the Chapter 16 accessor layer snaps onto the new resource without modification, how the CSR macros carry through, and how the driver's first real PCI read happens.
5. **Driver Attach-Time Initialisation.** `pci_enable_busmaster(9)`, `pci_read_config(9)`, `pci_find_cap(9)`, `pci_find_extcap(9)`, and the small set of configuration-space operations the driver performs at attach. Keeping the Chapter 17 simulation inactive on the PCI path. The third stage (`1.1-pci-stage3`).
6. **Supporting Detach and Resource Cleanup.** Releasing resources in reverse order, handling partial attach failure, `device_delete_child`, and the detach regression script.
7. **Testing PCI Driver Behavior.** Standing up a bhyve or QEMU guest that exposes a device the driver recognises, observing attach, exercising the driver against the real BAR, reading and writing configuration space from user space with `pciconf -r` and `pciconf -w`, and using `devinfo -v` and `dmesg` to trace the driver's view of the world.
8. **Refactoring and Versioning Your PCI Driver.** The final split into `myfirst_pci.c`, the new `PCI.md`, the version bump to `1.1-pci`, and the regression pass.

After the eight sections come hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 18's story and opens Chapter 19's, and a bridge to Chapter 19 with a forward pointer to Chapter 20. The reference-and-cheat-sheet material at the end of the chapter is meant to be re-read as you work through later Part 4 chapters; Chapter 18's vocabulary is the vocabulary every later PCI-family chapter reuses.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3 and 5 stand alone and make good single-sitting reads.



## Section 1: What Is PCI and Why It Matters

A reader who has come this far in the book has built a full driver around a simulated register block. The accessor layer, the command-response protocol, the locking discipline, the fault-injection framework, and the detach ordering are all in place. The one concession the driver makes to unreality is the origin of its registers: they come from a `malloc(9)` allocation in kernel memory, not from a piece of silicon on the far side of a bus. Section 1 introduces the subsystem that will change that. PCI is the most widespread peripheral bus in modern computing. It is also the canonical newbus child for a FreeBSD driver. Understanding what PCI is, how it got here, and how FreeBSD represents it is the foundation every remaining section in this chapter rests on.

### A Short History, and Why It Matters

PCI stands for Peripheral Component Interconnect. It was introduced by Intel in the early 1990s as a replacement for the previous generation of PC expansion buses (ISA, EISA, VESA local bus, and others), none of which scaled to the speeds and widths that modern peripherals would soon demand. The original PCI specification described a parallel, shared, clocked bus that carried thirty-two bits of data, was clocked at 33 MHz, and let a single device request and hold the bus for a transaction. A handful of revisions increased the width to sixty-four bits, raised the clock to 66 MHz, and introduced signalling variants for server platforms (PCI-X), but the basic shape remained that of a shared parallel bus.

PCI Express, called PCIe, is the modern successor. It keeps the software-visible model of PCI almost unchanged, but it replaces the physical bus with a collection of point-to-point serial links. Where PCI had many devices sharing a single set of wires, PCIe has each device connected to the chipset's root complex through its own lane (or set of lanes, up to sixteen in common use and thirty-two in some high-end cards). The bandwidth per lane has climbed through successive generations, from 2.5 Gb/s in PCIe Gen 1 to 32 Gb/s in PCIe Gen 5 and beyond.

Why does this history matter to a driver author? Because the software model did not change across the transition. From the driver's point of view, a PCIe device still has a configuration space, still has BARs, still has a vendor and device ID, still has capabilities, and still follows the probe-attach-detach lifecycle that PCI established. The physical layer changed; the software vocabulary did not. This is one of the few places in computing where a thirty-year-old interface is still the thing you read about in the FreeBSD source, and the continuity of the software model is what makes that possible. Code written for PCI in 1995 can, with minor updates for new capabilities, drive a PCIe Gen 5 device in 2026.

There is one important practical consequence of this continuity. When the book refers to "PCI", it almost always means "PCI or PCIe". The kernel's `pci(4)` subsystem handles both. When a distinction matters, such as when a PCIe-only feature like MSI-X or AER appears, the book will call that out. Everywhere else, "PCI" and "PCIe" are the same thing at the driver level.

### Where PCI Devices Live on a Modern Machine

Open almost any laptop, desktop, or server manufactured in the last twenty years, and you will find PCIe devices. The obvious ones are the add-in cards: a network adapter in a slot, a graphics card in another slot, an NVMe drive on the motherboard's M.2 socket, a Wi-Fi module on a Mini-PCIe daughtercard. The less obvious ones are integrated: the storage controller that talks to the SATA ports is a PCI device; the USB host controllers are PCI devices; the onboard Ethernet is a PCI device; the audio codec is a PCI device; the platform's integrated graphics is a PCI device. Everything that appears on the chipset-to-device interconnect between the CPU and the outside world is almost certainly a PCI device.

The kernel enumerates these devices at boot. Firmware (the system BIOS or UEFI) walks the bus, reads each device's configuration space, assigns BARs, and hands control to the OS. The OS re-walks the bus, builds its own representation, and attaches drivers. FreeBSD's `pci(4)` driver is what performs this walk. By the time the system is multi-user, every PCI device in the machine has been enumerated, every BAR has been assigned a kernel virtual address, and every device that matched a driver has been attached.

A practical demonstration: run `pciconf -lv` on any FreeBSD system. Each entry shows a device with its B:D:F (bus, device, function) address, its vendor and device IDs, its subvendor and subsystem IDs, its class and subclass, its current driver binding (if any), and a human-readable description of what it is. The entries are what the kernel saw; the descriptions are what `pciconf` looked up from its internal database. Running this command on your lab host is the best quick introduction to what your machine's PCI topology looks like.

### The Bus-Device-Function Tuple

The address of a PCI device has three components. Together they are called the **bus-device-function tuple**, or B:D:F, or sometimes just "PCI address".

The **bus number** is which physical or logical PCI bus the device is on. A machine usually has one primary bus (bus 0), plus additional buses behind PCI-to-PCI bridges. A laptop might have buses 0, 2, 3, and 4; a server might have dozens. Each bus is eight bits wide, so the system supports up to 256 buses in the original PCI spec. PCIe extended this to 16 bits (65,536 buses) through the Enhanced Configuration Access Mechanism, ECAM.

The **device number** is the slot on the bus. Each bus can hold up to 32 devices. On PCIe, the point-to-point nature of the physical link means each bridge has one device on each of its downstream buses; the device number is essentially always 0 in that case. On legacy PCI, several devices share a bus and each gets its own device number.

The **function number** is which function of a multi-function device is being addressed. A single physical device can expose up to 8 functions, each with its own configuration space, each presenting itself as an independent PCI device. Multi-function devices are common: a typical x86 chipset presents its USB host controllers as multiple functions of a single physical unit; a storage controller might present SATA, IDE, and AHCI on separate functions. Single-function devices (the common case) use function 0.

The combined tuple is written in FreeBSD's `pciconf` output as `pciN:D:F`, where `N` is the domain-plus-bus value. On the author's test machine, `pci0:0:2:0` refers to domain 0, bus 0, device 2, function 0, which on an Intel platform is typically the integrated graphics. This notation is stable across FreeBSD versions; you will see it in the kernel's boot messages, in `dmesg`, in `devinfo -v`, and in the bus documentation.

A driver rarely cares about the B:D:F value directly. The newbus subsystem hides it behind a `device_t` handle. But a driver author cares, because two things use B:D:F: system administrators (who match a B:D:F against a slot or a physical device when installing or troubleshooting), and kernel messages (which print it in `dmesg` when a device attaches, detaches, or misbehaves). When you see `pci0:3:0:0: <Intel Corporation Ethernet Controller ...>` in your boot log, you are reading a B:D:F.

### Configuration Space and What Lives There

PCI distinguishes two address spaces per device. The first is the set of BARs, which map the device's registers into the host memory (or I/O port) space; this is what Chapter 16 called "MMIO" and what Chapter 18's Section 3 and Section 4 will explore. The second is **configuration space**, which is a small, structured block of memory per device that describes the device itself.

Configuration space is where the vendor ID, device ID, class code, revision, BAR addresses, capability list pointer, and a number of other metadata fields live. It is 256 bytes on legacy PCI, expanded to 4096 bytes on PCIe. The layout of the first sixty-four bytes is standardised across every PCI device; the remaining space is used for capabilities and extended capabilities.

The driver reaches configuration space through the `pci_read_config(9)` and `pci_write_config(9)` interfaces. These two functions take a device handle, a byte offset into configuration space, and a width (1, 2, or 4 bytes), and return or accept a `uint32_t` value. The width argument lets the driver read or write a byte, a sixteen-bit field, or a thirty-two-bit field; the kernel translates this into the right underlying access primitive for the platform.

Most of what a driver needs to know about configuration space is already extracted by the newbus layer and cached in the device's ivars. That is why the driver can call `pci_get_vendor(dev)`, `pci_get_device(dev)`, `pci_get_class(dev)`, and `pci_get_subclass(dev)` without reading configuration space by hand. These accessors are defined in `/usr/src/sys/dev/pci/pcivar.h` and expanded through the `PCI_ACCESSOR` macro to inline functions that read a cached value. The values are read once, at enumeration time, and kept in the device's ivars thereafter.

For everything the common accessors do not cover, `pci_read_config(9)` and `pci_write_config(9)` are the fallback. An example: if a device's datasheet says "the firmware revision is at offset 0x48 in configuration space, as a 32-bit little-endian integer", the driver reads that value by calling `pci_read_config(dev, 0x48, 4)`. The kernel arranges the access so that the return value is the little-endian value the datasheet specified, on every supported architecture.

### Vendor ID and Device ID: How Matches Happen

The core of PCI device identification is the pair of sixteen-bit values called vendor ID and device ID.

The **vendor ID** is assigned by the PCI Special Interest Group (PCI-SIG) to a company that manufactures PCI devices. Intel has 0x8086. Broadcom has several (originally 0x14e4 and others by acquisition). Red Hat and the Linux community's virtualisation project share 0x1af4 for virtio devices. Every PCI device carries its vendor's ID in the `VENDOR` field of configuration space.

The **device ID** is assigned by the vendor to each specific product. Intel's 0x10D3 is the 82574L gigabit Ethernet controller. Broadcom's 0x165F is a specific NetXtreme BCM5719 variant. Red Hat's 0x1001 in the virtio range is virtio-block. Vendors maintain their own device ID allocations.

A **subvendor ID** and **subsystem ID** together form a second-level tuple. They identify the board that a chipset is built into, as opposed to the chipset itself. The same Intel 82574L Ethernet chip might appear on a Dell server with subvendor 0x1028, on an HP server with subvendor 0x103c, and on a generic OEM board with subvendor 0x8086. Drivers can use the subvendor or subsystem to apply specific quirks, to print a more useful identification string, or to select between slightly different board-level behaviour.

A driver's probe routine matches against these IDs. In the simplest case, the driver has a static table listing every supported vendor-and-device pair; the probe walks the table and returns `BUS_PROBE_DEFAULT` on a match or `ENXIO` on no match. In more complex cases, the driver also checks subvendor and subsystem, or walks a broader class-based match, or uses both. The `uart_bus_pci.c` file in `/usr/src/sys/dev/uart/` shows this pattern at a readable scale.

Chapter 18's driver will use the simple tabular form. The table will hold one or two entries. The vendor and device IDs we target are the ones a bhyve or QEMU guest will expose for a synthetic test device, and the teaching path will unload the base-system driver that would otherwise claim the same ID before loading `myfirst`.

### How FreeBSD Enumerates PCI Devices

The steps from "the firmware has set up the bus" to "a driver's attach routine runs" are worth understanding in outline, because understanding them makes the probe-and-attach sequence feel inevitable rather than mysterious.

First, the platform's bus-enumeration code runs. On x86 this lives under `/usr/src/sys/dev/pci/` and is driven by platform-specific attach code (x86 uses ACPI bridges and legacy host bridges; arm64 uses device-tree-based host bridges). The enumeration walks the bus, reads each device's vendor and device IDs, reads each device's BARs, and records what it finds.

Second, the kernel's newbus layer creates a `device_t` for each discovered device and adds it as a child of the PCI bus device (`pci0`, `pci1`, and so on). Each child has a device method table placeholder; the newbus code does not yet know which driver will bind. The child has ivars: the vendor, device, subvendor, subsystem, class, subclass, interface, revision, B:D:F, and resource descriptors are all cached in the ivars for later access.

Third, the kernel invites every registered driver to probe each device. Each driver's `probe` method is called in priority order. A driver inspects the device's vendor, device, and whatever else it needs, and returns one of a small set of values:

- A negative number: "I match this device and want it". Values closer to zero mean higher priority. The standard tier for a vendor-and-device-ID match is `BUS_PROBE_DEFAULT`, which is `-20`. `BUS_PROBE_VENDOR` is `-10` and wins over it; `BUS_PROBE_GENERIC` is `-100` and loses to it. Section 2 lists the full tier set.
- `0`: "I match this device with absolute priority". The `BUS_PROBE_SPECIFIC` tier. No other driver can outbid it.
- A positive errno (commonly `ENXIO`): "I do not match this device".

The kernel picks the driver that returned the numerically smallest value and attaches it. If two drivers return the same value, the one that registered first wins. The tiered priority lets a generic driver coexist with a device-specific driver: the generic driver returns `BUS_PROBE_GENERIC`, the specific driver returns `BUS_PROBE_DEFAULT`, and the specific driver wins because `-20` is closer to zero than `-100`.

Fourth, the kernel calls the winning driver's `attach` method. The driver allocates its softc (usually pre-allocated by newbus), claims resources with `bus_alloc_resource_any(9)`, sets up interrupts, and registers a character device or network interface or whatever the device exposes to user space. If `attach` returns 0, the device is live. If `attach` returns an errno, the kernel detaches the driver (calling `detach` is not strictly required on attach failure in modern newbus; the driver is expected to unwind cleanly before returning the error).

Fifth, the kernel moves to the next device. The process repeats until every PCI device has been probed and every device that found a match has been attached.

Detach is the inverse: the kernel calls each driver's `detach` method when the device is removed (through `devctl detach` or at module unload), and the driver releases everything it claimed in attach, in reverse order.

This is the newbus dance Chapter 18 teaches the driver how to follow. Section 2 writes the first version of it; Sections 3 through 6 add each additional capability; Section 8 consolidates it into a clean module.

### The pci(4) Subsystem from the Driver's Perspective

The driver does not see bus enumeration. It sees a device handle (`device_t dev`) and a set of accessor calls. The `/usr/src/sys/dev/pci/pcivar.h` header defines the accessors. The core ones are:

- `pci_get_vendor(dev)` returns the vendor ID as `uint16_t`.
- `pci_get_device(dev)` returns the device ID as `uint16_t`.
- `pci_get_subvendor(dev)` and `pci_get_subdevice(dev)` return the subvendor and subsystem.
- `pci_get_class(dev)`, `pci_get_subclass(dev)`, and `pci_get_progif(dev)` return the class code fields.
- `pci_get_revid(dev)` returns the revision.
- `pci_read_config(dev, reg, width)` reads from configuration space.
- `pci_write_config(dev, reg, val, width)` writes to configuration space.
- `pci_find_cap(dev, cap, &capreg)` finds a standard PCI capability; returns 0 on success, ENOENT on absence.
- `pci_find_extcap(dev, cap, &capreg)` finds a PCIe extended capability; same return convention.
- `pci_enable_busmaster(dev)` sets the Bus Master Enable bit in the command register.
- `pci_disable_busmaster(dev)` clears it.

This is the vocabulary of Chapter 18. Every section in the chapter uses one or more of these accessors. A reader who is comfortable with the shape of the list is ready to start writing PCI code.

### Common PCI Devices in the Real World

Before moving on, a brief tour of the devices PCI presents.

**Network interface controllers.** NICs are nearly all PCI devices. The Intel `em(4)` driver for the 8254x family, the Intel `ix(4)` driver for the 82599 family, the Intel `ixl(4)` driver for the X710 family, and the Broadcom `bge(4)` / `bnxt(4)` drivers for the NetXtreme family all live under `/usr/src/sys/dev/e1000/` or `/usr/src/sys/dev/ixl/` or `/usr/src/sys/dev/bge/`. They are large, production-grade drivers that exercise essentially every topic in Part 4.

**Storage controllers.** AHCI SATA controllers, NVMe drives, SAS HBAs, and RAID controllers are all PCI. `ahci(4)`, `nvme(4)`, `mpr(4)`, `mpi3mr(4)`, and others live under `/usr/src/sys/dev/`. These are among the best-maintained drivers in the tree.

**USB host controllers.** xHCI, EHCI, and OHCI controllers are PCI. The generic host-controller driver attaches to each, and the USB subsystem handles everything above it. `xhci(4)` is the canonical one for modern systems.

**Graphics cards and integrated graphics.** GPU drivers in FreeBSD are mostly maintained out of tree (DRM drivers from the drm-kmod ports), but the bus attach for them is standard PCI.

**Audio controllers.** HDA codecs, older AC'97 bridges, and various USB-attached audio devices all reach the system through PCI in some way. `snd_hda(4)` is the usual attach point.

**Virtio devices in virtual machines.** When a FreeBSD guest runs under bhyve, KVM, VMware, or Hyper-V, the paravirtualised devices appear as PCI. Virtio-network, virtio-block, virtio-entropy, and virtio-console all look like PCI devices to the guest. The `virtio_pci(4)` driver attaches first and publishes child nodes for each of the transport-specific virtio drivers.

**The machine's own chipset components.** The platform's LPC bridge, SMBus controller, thermal sensor interface, and various miscellaneous control functions are PCI.

If you have ever wondered why a FreeBSD source tree is as large as it is, the PCI device ecosystem is most of the answer. Every device in the list above needs a driver. The driver you are building in Chapter 18 is small and does very little; the drivers in the examples are large because they implement real protocols over the PCI bus. But the shape of every one of them is what Chapter 18 teaches.

### Simulated PCI Devices: bhyve and QEMU

A reader who owns a complete set of test hardware can skip this subsection. Everyone else depends on virtualisation to provide the PCI devices they will drive.

FreeBSD's `bhyve(8)` hypervisor, built into the base system, can present a guest with a set of emulated PCI devices. The common ones are `virtio-net`, `virtio-blk`, `virtio-rnd`, `virtio-console`, `ahci-hd`, `ahci-cd`, `e1000`, `xhci`, and a framebuffer device. Each has a well-known vendor and device ID; the guest's PCI enumerator sees them as real PCI devices; the guest's drivers attach to them as they would to real hardware. Running a FreeBSD guest under bhyve is the canonical way, for this book, to have a PCI device the reader's driver can attach to.

QEMU with KVM (on Linux hosts) or with the HVF accelerator (on macOS hosts) provides a superset of bhyve's emulated devices, plus a few specifically designed for testing. The `pci-testdev` device (vendor 0x1b36, device 0x0005) is a deliberately minimal PCI device intended for kernel test code; it has two BARs (one memory, one I/O), and writes to specific offsets trigger specific behaviours. Chapter 18's Section 7 can use either a virtio-rnd device under bhyve or a pci-testdev under QEMU as the target.

For the teaching path, the book targets a virtio-rnd device under bhyve. The reason is that bhyve ships with every FreeBSD installation, whereas QEMU requires additional packages. The cost is small: the virtio-rnd device has a real driver in the base system (`virtio_random(4)`), and the chapter will show how to prevent that driver from claiming the device so that `myfirst` can claim it instead.

An important note about the teaching path's choice. The `myfirst` driver is not a real virtio-rnd driver. It has no idea how to speak the virtio-rnd protocol; it treats the BAR as a set of opaque registers and reads and writes them for demonstration. This is fine for the chapter's purpose (proving that the driver can attach, read, write, and detach), but it is not fine for production use. Chapter 18 is a hands-on introduction to the PCI attach sequence, not a tutorial on how to write a virtio driver. When you finish the chapter, the driver you have is still the `myfirst` educational driver, now capable of attaching to a PCI bus rather than only to a kldload path.

### Placing Chapter 18 in the Driver's Evolution

A quick map of where the `myfirst` driver has been and where it is going.

- **Version 0.1 through 0.8** (Part 1 through Part 3): the driver learned the driver lifecycle, the cdev machinery, concurrency primitives, and coordination.
- **Version 0.9-coordination** (end of Chapter 15): full lock discipline, condition variables, sx locks, callouts, taskqueue, counting semaphore.
- **Version 0.9-mmio** (end of Chapter 16): `bus_space(9)`-backed register block, CSR macros, access log, hardware layer in `myfirst_hw.c`.
- **Version 1.0-simulated** (end of Chapter 17): dynamic register behaviour, callouts that change state, command-response protocol, fault injection, simulation layer in `myfirst_sim.c`.
- **Version 1.1-pci** (end of Chapter 18, our target): the simulation is switchable, and when the driver attaches to a real PCI device, the BAR becomes the register block, `myfirst_hw_attach` uses `bus_alloc_resource_any` instead of `malloc`, and the Chapter 16 accessor layer points at real silicon.
- **Version 1.2-intr** (Chapter 19): a real interrupt handler registered through `bus_setup_intr(9)`, so the driver can react to the device's own state changes rather than polling.
- **Version 1.3-msi** (Chapter 20): MSI and MSI-X, giving the driver a richer interrupt-routing story.
- **Version 1.4-dma** (Chapters 20 and 21): a `bus_dma(9)` tag, descriptor rings, and the first real DMA transfers.

Each version is a layer on the one before it. Chapter 18 is one layer, small enough to teach clearly, large enough to matter.

### Exercise: Read Your Own PCI Topology

Before Section 2, a short exercise to make the vocabulary concrete.

On your lab host, run:

```sh
sudo pciconf -lv
```

You will see a list of every PCI device the kernel enumerated. Each entry looks roughly like:

```text
em0@pci0:0:25:0:        class=0x020000 rev=0x03 hdr=0x00 vendor=0x8086 device=0x15ba subvendor=0x8086 subdevice=0x2000
    vendor     = 'Intel Corporation'
    device     = 'Ethernet Connection (2) I219-LM'
    class      = network
    subclass   = ethernet
```

Pick three devices from the list. For each, identify:

- The device's logical name in FreeBSD (the leading `name@pciN:B:D:F` string).
- The vendor and device IDs.
- The class and subclass (the meaningful English categories, not just the hex codes).
- Whether the device has a driver bound to it (`em0`, for example, is bound to `em(4)`; an entry with just `none0@...` has no driver).

Keep this output in a terminal while you read the rest of the chapter. Every vocabulary item introduced in Sections 2 through 5 refers to fields you can find here. The exercise is to anchor the abstract vocabulary to a concrete set of devices on your machine.

If you are reading the book without a FreeBSD machine available, the following snippet is the output of `pciconf -lv` on the author's lab host, truncated to the first three devices:

```text
hostb0@pci0:0:0:0:      class=0x060000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e31
    vendor     = 'Intel Corporation'
    device     = '8th Gen Core Processor Host Bridge/DRAM Registers'
    class      = bridge
    subclass   = HOST-PCI
pcib0@pci0:0:1:0:       class=0x060400 rev=0x00 hdr=0x01 vendor=0x8086 device=0x1901
    vendor     = 'Intel Corporation'
    device     = '6th-10th Gen Core Processor PCIe Controller (x16)'
    class      = bridge
    subclass   = PCI-PCI
vgapci0@pci0:0:2:0:     class=0x030000 rev=0x00 hdr=0x00 vendor=0x8086 device=0x3e9b
    vendor     = 'Intel Corporation'
    device     = 'CoffeeLake-H GT2 [UHD Graphics 630]'
    class      = display
    subclass   = VGA
```

Three devices, three drivers, three class codes. The host bridge (`hostb0`) is the PCI-to-memory-bus bridge; the PCI bridge (`pcib0`) is a PCI-to-PCI bridge leading to the GPU slot; the VGA-class device (`vgapci0`) is the integrated graphics on a Coffee Lake chipset. Every one of them follows the probe-attach-detach dance Chapter 18 teaches. The driver is what changes. The bus dance does not.

### Wrapping Up Section 1

PCI is the canonical peripheral bus of modern systems and the canonical newbus child of FreeBSD. It is shared by PCI and PCIe, which differ at the physical layer but present the same software-visible model. Every PCI device has a B:D:F address, a configuration space, a set of BARs, a vendor ID, a device ID, and a place in the kernel's newbus tree. A driver's job is to match one or more devices by their IDs, claim their BARs, and expose their behaviour through some user-space interface. FreeBSD's `pci(4)` subsystem does the enumeration; the driver does the attachment.

The vocabulary of Section 1 is the vocabulary the rest of the chapter uses. B:D:F, configuration space, BARs, vendor and device IDs, class codes, capabilities, and the newbus probe-attach-detach sequence. If any one of these feels unfamiliar, re-read the relevant subsection before continuing. Section 2 takes the vocabulary and builds the first version of the driver.



## Section 2: Probing and Attaching a PCI Device

Section 1 established what PCI is and how FreeBSD represents it. Section 2 is where the driver finally uses that vocabulary. The goal here is to stand up the minimum viable PCI driver: a driver that registers itself as a candidate for the PCI bus, matches a specific vendor and device ID, prints a banner in `dmesg` when the match succeeds, and unloads cleanly. No BAR claim yet. No register access yet. Just the skeleton.

The skeleton is important. It introduces the probe-attach-detach sequence in isolation, before BARs and resources and configuration-space walks crowd the picture. A reader who writes this skeleton once, by hand, and then types `kldload ./myfirst.ko`, sees `dmesg` report the driver probing and attaching, and types `kldunload myfirst` to see the detach side fire cleanly, has built the right mental model for everything that follows. Every later Part 4 chapter assumes this mental model.

### The Probe-Attach-Detach Contract

Every newbus driver has three methods at the heart of its lifecycle. `probe` asks "is this device something I know how to drive?" `attach` says "yes I want it, and here is how I claim it". `detach` says "release this device, I am going away".

**Probe**. Called by the kernel once per device that the bus has enumerated, for each driver that has registered an interest in that bus. The driver reads the device's vendor and device IDs (and whatever else it needs to decide), returns a priority value if it wants the device, and returns `ENXIO` if it does not. The priority system is what lets a specific driver win over a generic one: a driver that returns `BUS_PROBE_DEFAULT` wins over one that returns `BUS_PROBE_GENERIC` when both want the same device. If no driver returns a match, the device stays unclaimed (you will see this as `nonea@pci0:...` entries in `devinfo -v`).

A subtle point: **probe may be called more than once for a given device**. The newbus reprobe machinery exists to handle devices that appear at runtime (hotplug) or that come back from suspend. A good probe is idempotent: it reads the same state, makes the same decision, returns the same value. Probe must not allocate resources, set up timers, register interrupts, or do anything that would need to be undone. It only inspects and decides.

A second subtle point: **probe runs before attach, but after the kernel has assigned the device's resources**. The BARs, the IRQ, and the configuration space are all accessible from probe. This means probe can read device-specific registers through `pci_read_config` to distinguish variants of a chipset by revision or silicon ID if needed. Real drivers occasionally do this. Chapter 18's driver does not need to; vendor and device IDs are enough.

**Attach**. Called once per device, after probe has selected a winner. The driver's attach routine is where real work happens: softc initialisation, resource allocation, register mapping, character-device creation, and any configuration the device needs at bring-up. If attach returns 0, the device is live; the kernel considers the driver bound to the device and moves on. If attach returns a non-zero value, the kernel treats the attach as having failed. The driver must clean up anything it allocated before returning the error; modern newbus does not call detach in that case (older convention did, so older drivers still structure their error paths to handle it).

**Detach**. Called when the driver is being unbound from the device. The call is the mirror of attach: everything attach allocated, detach releases. Everything attach set up, detach tears down. Everything attach registered, detach unregisters. The ordering is strict: detach must undo in the reverse of the order attach did. A mistake here produces kernel panics at unload time, leaked resources at the best, or subtle use-after-free bugs at the worst.

**Resume** and **suspend** are optional methods. They are called when the system suspends and resumes, and they give the driver a chance to save and restore device state across the power event. Chapter 18's driver does not implement either method in the first stage; we will add resume in a later chapter when the topic serves the material.

There are other methods (`shutdown`, `quiesce`, `identify`) that less commonly matter for a basic PCI driver. The Chapter 18 skeleton registers only the three core methods plus `DEVMETHOD_END`.

### The Device Method Table

FreeBSD's newbus machinery reaches driver methods through a table. The table is an array of `device_method_t` entries, each entry mapping a method name to the C function that implements it. The table ends with `DEVMETHOD_END`, which is simply a zeroed entry that tells newbus "no more methods here".

The table is declared at file scope, like this, in the driver's source:

```c
static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};
```

Each `DEVMETHOD(name, func)` expands to a `{ name, func }` initialiser. The newbus layer reaches a driver's method by looking up the name in this table. If a method is not registered (for example, `device_resume` is not in this table), the newbus layer uses a default implementation; for `resume` the default is a no-op, for `probe` the default is `ENXIO`.

The method names are defined in `/usr/src/sys/sys/bus.h` and expanded by the newbus build system. Each one corresponds to a function prototype the driver must match. For example, the `device_probe` method's prototype is:

```c
int probe(device_t dev);
```

A driver implementation must have that exact signature. Type mismatches produce compile errors, not runtime mysteries; if your probe's signature is wrong, the build will fail.

### The Driver Structure

Alongside the method table, the driver declares a `driver_t`. This structure ties together the method table, the softc size, and a short name:

```c
static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};
```

The name (`"myfirst"`) is what newbus will use when numbering unit instances. The first attached device becomes `myfirst0`, the second `myfirst1`, and so on. This name is what `devinfo -v` shows and what user-space tools (like `/dev/myfirst0` if the driver creates a cdev with that name) expose.

The softc size tells newbus how many bytes to allocate for each device's softc. The allocation is automatic: by the time attach runs, `device_get_softc(dev)` returns a pointer to a zeroed block of the requested size. The driver does not call `malloc` for the softc itself; it uses what newbus gave it. This is a convenience that the `myfirst` driver has already been using since Chapter 10; it becomes more important with PCI because every unit has its own softc and newbus manages the lifetime.

### DRIVER_MODULE and MODULE_DEPEND

The driver is glued to the PCI bus through two macros. The first is `DRIVER_MODULE(9)`:

```c
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
```

The expansion of this macro performs several things. It registers the driver as a child candidate of the `pci` bus, wrapping the `driver_t` in a kernel module descriptor. It schedules the driver to participate in probe for every device the `pci` bus enumerates. It provides hooks for optional module event handlers (the two `NULL`s are for module init and cleanup, respectively; we leave them empty for now).

The first argument is the module's name, which must match the name in the `driver_t`. The second argument is the bus's name; `pci` is the newbus name of the PCI bus driver. The third argument is the driver itself. The remaining arguments are for optional callbacks.

The macro has a subtle consequence: the driver will participate in probe on every PCI bus in the system. If you have multiple PCI domains, the driver will be offered every device on every domain. Probe's job is to say yes only to the devices the driver actually supports; the kernel's job is to ask.

The second macro is `MODULE_DEPEND(9)`:

```c
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
```

This tells the module loader that `myfirst.ko` depends on the `pci` kernel module. The three numbers are minimum, preferred, and maximum version. A zero-to-one dependency on version 1 is the common case. The loader uses this information to refuse to load `myfirst.ko` if the kernel's PCI subsystem is not present (which is essentially never false on a real system, but the check is good hygiene).

Without `MODULE_DEPEND`, the loader might load `myfirst.ko` before the PCI subsystem is available at early boot, leading to a panic when `DRIVER_MODULE` tries to register against a bus that does not exist yet. With it, the loader serialises the load correctly.

### Matching by Vendor and Device ID

The probe routine is where the vendor-and-device match happens. The pattern is a static table and a loop. Consider a minimal version:

```c
static const struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
} myfirst_pci_ids[] = {
	{ 0x1af4, 0x1005, "Red Hat / Virtio entropy source (demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}
```

A few things to notice. The table is small and static, one entry per supported device. `pci_get_vendor` and `pci_get_device` read the cached ivars, so the calls are cheap. The comparison is a plain loop; the table is short enough not to warrant a hash. `device_set_desc` installs a human-readable description that `pciconf -lv` and `dmesg` will print when the device attaches. `BUS_PROBE_DEFAULT` is the standard priority for a vendor-specific match; it wins over generic class-based drivers but loses to any driver that explicitly returns a more negative value.

A subtle but important point: this probe routine targets the virtio-rnd (entropy) device that the base-system `virtio_random(4)` driver normally claims. If both drivers are loaded, the system's priority rules decide the winner. `virtio_random` registers `BUS_PROBE_DEFAULT`, as does `myfirst`. The tiebreaker is registration order, which varies. The reliable way to guarantee that `myfirst` attaches is to unload `virtio_random` before loading `myfirst`. Section 7 will show how.

A second note: the vendor and device IDs in the example above target a virtio device. Real PCI drivers for real hardware would target chips whose IDs are not already claimed by base-system drivers. For a production driver, the list would include every supported variant of the target chipset, often with descriptive strings that identify the silicon revision. `uart_bus_pci.c` has sixty-plus entries; `ix(4)` has over a hundred.

### The Probe Priority Tiers

FreeBSD defines several probe priority tiers, defined in `/usr/src/sys/sys/bus.h`:

- `BUS_PROBE_SPECIFIC` = 0. The driver matches the device precisely. No other driver can outbid this.
- `BUS_PROBE_VENDOR` = -10. The driver is vendor-provided and should win over anything generic.
- `BUS_PROBE_DEFAULT` = -20. The standard tier for a vendor-and-device-ID match.
- `BUS_PROBE_LOW_PRIORITY` = -40. A lower-priority match, often for drivers that want to be the default only if nothing else claims the device.
- `BUS_PROBE_GENERIC` = -100. A generic driver that attaches to a class of devices if nothing more specific exists.
- `BUS_PROBE_HOOVER` = -1000000. Absolute last resort; a driver that wants devices no other driver claimed.
- `BUS_PROBE_NOWILDCARD` = -2000000000. Special-case marker used by the newbus identify machinery.

Most drivers you will write or read use `BUS_PROBE_DEFAULT`. Some use `BUS_PROBE_VENDOR` if they expect to coexist with generic drivers. A handful use `BUS_PROBE_GENERIC` or lower for their fallback mode. Chapter 18's driver uses `BUS_PROBE_DEFAULT` throughout.

The priority values are negative by convention so that the numerically lowest value wins. A more specific driver has a more negative value. This is counter-intuitive on first reading; a mental model is "distance from perfection, measured downward". `BUS_PROBE_SPECIFIC` is zero distance. `BUS_PROBE_GENERIC` is a hundred units worse.

### Writing a Minimal PCI Driver

Putting it all together, here is the Chapter 18 Stage 1 driver, presented as a single self-contained file that grows out of the Chapter 17 skeleton. The file name is `myfirst_pci.c`; it is new in Chapter 18 and lives alongside the existing `myfirst.c`, `myfirst_hw.c`, and `myfirst_sim.c`.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.c -- Chapter 18 Stage 1 PCI probe/attach skeleton.
 *
 * At this stage the driver only probes, attaches, and detaches.
 * It does not yet claim BARs or touch device registers. Section 3
 * adds resource allocation. Section 5 wires the accessor layer to
 * the claimed BAR.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_pci.h"

static const struct myfirst_pci_id myfirst_pci_ids[] = {
	{ MYFIRST_VENDOR_REDHAT, MYFIRST_DEVICE_VIRTIO_RNG,
	    "Red Hat Virtio entropy source (myfirst demo target)" },
	{ 0, 0, NULL }
};

static int
myfirst_pci_probe(device_t dev)
{
	uint16_t vendor = pci_get_vendor(dev);
	uint16_t device = pci_get_device(dev);
	const struct myfirst_pci_id *id;

	for (id = myfirst_pci_ids; id->desc != NULL; id++) {
		if (id->vendor == vendor && id->device == device) {
			device_set_desc(dev, id->desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	device_printf(dev,
	    "attaching: vendor=0x%04x device=0x%04x revid=0x%02x\n",
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_revid(dev));
	device_printf(dev,
	    "           subvendor=0x%04x subdevice=0x%04x class=0x%02x\n",
	    pci_get_subvendor(dev), pci_get_subdevice(dev),
	    pci_get_class(dev));

	/*
	 * Stage 1 has no resources to claim and nothing to initialise
	 * beyond the softc pointer. Stage 2 will add the BAR allocation.
	 */
	return (0);
}

static int
myfirst_pci_detach(device_t dev)
{
	device_printf(dev, "detaching\n");
	return (0);
}

static device_method_t myfirst_pci_methods[] = {
	DEVMETHOD(device_probe,		myfirst_pci_probe),
	DEVMETHOD(device_attach,	myfirst_pci_attach),
	DEVMETHOD(device_detach,	myfirst_pci_detach),
	DEVMETHOD_END
};

static driver_t myfirst_pci_driver = {
	"myfirst",
	myfirst_pci_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
MODULE_VERSION(myfirst, 1);
```

The companion header, `myfirst_pci.h`:

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_pci.h -- Chapter 18 PCI interface for the myfirst driver.
 */

#ifndef _MYFIRST_PCI_H_
#define _MYFIRST_PCI_H_

#include <sys/types.h>

/* Target vendor and device IDs for the Chapter 18 demo. */
#define MYFIRST_VENDOR_REDHAT		0x1af4
#define MYFIRST_DEVICE_VIRTIO_RNG	0x1005

/* A single entry in the supported-device table. */
struct myfirst_pci_id {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
};

#endif /* _MYFIRST_PCI_H_ */
```

And the `Makefile` needs a small update:

```makefile
# Makefile for the Chapter 18 Stage 1 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci-stage1\"

.include <bsd.kmod.mk>
```

Three things have changed from Chapter 17 Stage 5. `myfirst_pci.c` is added to `SRCS`. The version string is bumped to `1.1-pci-stage1`. Nothing else needs to change.

### What Happens When the Driver Loads

Walking through the loading sequence makes the skeleton concrete.

The reader invokes `kldload ./myfirst.ko`. The kernel's module loader reads the module's metadata. It sees the `MODULE_DEPEND(myfirst, pci, ...)` declaration and verifies that the `pci` module is loaded. (It always is on a running kernel, so this check passes.) It sees the `DRIVER_MODULE(myfirst, pci, ...)` declaration and registers the driver as a probe candidate for the PCI bus.

The kernel then iterates over every PCI device in the system and calls `myfirst_pci_probe` for each. Most probes return `ENXIO` because the vendor and device IDs do not match. One probe, against the virtio-rnd device in the guest, returns `BUS_PROBE_DEFAULT`. The kernel picks `myfirst` as the driver for that device.

If the virtio-rnd device is already attached to `virtio_random`, the new driver's probe result competes with the existing binding. The kernel does not re-bind a device automatically just because a new driver appeared; instead, `myfirst` will not attach. To force the rebind, the reader must first detach the existing driver: `devctl detach virtio_random0`, or `kldunload virtio_random`. Section 7 walks through this.

Once the kernel decides `myfirst` wins, it allocates a new softc (the `sizeof(struct myfirst_softc)` block requested in the `driver_t`), clears it to zero, and calls `myfirst_pci_attach`. The attach routine runs. It prints a short banner. It returns 0. The kernel marks the device as attached.

`dmesg` shows the sequence:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> port 0x6040-0x605f mem 0xc1000000-0xc100001f irq 19 at device 5.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`devinfo -v` shows the device with its parent, its resources, and its driver binding. `pciconf -lv` shows it with `myfirst0` as its bound name.

On unload, the inverse happens. `kldunload myfirst` calls `myfirst_pci_detach` on every attached device. The detach prints its own banner, returns 0, and the kernel releases the softc. `DRIVER_MODULE` unregisters the driver from the PCI bus. The module loader removes the `myfirst.ko` image from memory.

### device_printf and Why It Matters

One small detail worth emphasising. The skeleton uses `device_printf(dev, ...)` instead of `printf(...)`. The difference is small but important.

`printf` prints to the kernel log without any prefix. A line that says "attaching" is hard to associate with a specific device; the log is full of messages from every driver in the system. `device_printf(dev, ...)` prefixes the message with the driver's name and unit number: "myfirst0: attaching". The prefix makes the log readable even when multiple instances of the driver are attached at once (`myfirst0`, `myfirst1`, and so on).

The convention is strict in the FreeBSD source tree: every driver uses `device_printf` in code paths that have a `device_t` handy, and falls back to `printf` only in very early module init or module unload, where the handle is not available. A reader who habitually uses `device_printf` produces logs that are easy to read and diagnose; a reader who uses `printf` everywhere produces logs that other contributors will ask to have corrected.

### The Softc and device_get_softc

The Chapter 17 driver already had a softc structure. Chapter 18's attach routine simply reuses it, with one addition: the PCI layer stores the `device_t` in `sc->dev` so later code (including the Chapter 16 accessors and the Chapter 17 simulation) can reach it.

A reminder: `device_get_softc(dev)` returns a pointer to the softc that newbus pre-allocated. The softc is zeroed before attach runs, so every field starts at zero or NULL or false. The softc is freed automatically by newbus after detach returns; the driver does not call `free` on it.

This is worth calling out because it differs from the `malloc`-based softc pattern in older FreeBSD drivers and in some Linux drivers. In newbus, the bus owns the softc lifetime. In older patterns, the driver owned it and had to remember to allocate and free. Forgetting to allocate in the old model causes a null dereference in attach; forgetting to free in the old model causes a memory leak in detach. Neither failure mode exists in modern newbus because the bus handles both operations.

### Probe-Attach-Detach Order in the Face of Unload

An important detail for a driver author is what happens when `kldunload myfirst` runs while one or more `myfirst` devices are attached.

The module loader's unload path first tries to detach every device bound to the driver. For each device, it calls the driver's `detach` method. If `detach` returns 0, the device is considered unbound; the softc is freed. If `detach` returns non-zero (usually `EBUSY`), the module loader aborts the unload: the module stays loaded, the device stays attached, and the unload returns an error. This is how a driver refuses to go away while it has work in progress.

The `myfirst` driver's detach should normally succeed, because the driver's user-facing state is idle by the time the user asks to unload it. But a driver that is actively serving requests (for example, a disk driver with open file descriptors on its cdev) returns `EBUSY` from detach and forces the user to close the descriptors first.

For Chapter 18 Stage 1, detach is a one-liner: print a banner and return 0. In later stages, detach will acquire the lock, cancel callouts, release resources, and finally return 0 once everything is torn down.

### Stage 1 Output: What Success Looks Like

The reader loads the driver. On a bhyve guest with a virtio-rnd device attached, and with `virtio_random` unloaded first, `dmesg` should print something like:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
```

`kldstat -v | grep myfirst` shows the driver loaded. `devinfo -v | grep myfirst` shows the device attached to `pci0`. `pciconf -lv | grep myfirst` confirms the match.

On unload:

```text
myfirst0: detaching
```

And the device returns to unclaimed status. (Or to `virtio_random` if that module is reloaded.)

If the virtio-rnd device is absent, no attach happens; the driver loads, but no `myfirst0` appears in `devinfo`. If the driver is loaded on a host without the device, the same happens: probe runs for every PCI device in the system, probe returns `ENXIO` for each, and no attach occurs. This is the correct and expected behaviour; the driver is patient.

### Common Mistakes at This Stage

A short list of traps the author has seen beginners fall into.

**Forgetting `MODULE_DEPEND`.** The driver loads, but early in boot it panics because the PCI module has not been initialised yet. Adding the declaration fixes it. The symptom is easy to recognise once you know to look for it.

**Wrong name in `DRIVER_MODULE`.** The name must match the `"name"` string in the `driver_t`. Mismatches produce subtle errors where the driver loads but never probes a device. The fix is to make the two match; the convention is that both use the driver's short name.

**Returning the wrong value from probe.** A beginner sometimes returns 0 from probe thinking "zero means success". Zero is `BUS_PROBE_SPECIFIC`, which is the strongest possible match; the driver will win over every other driver that wants the same device. This is almost never what you meant. Return `BUS_PROBE_DEFAULT` for the standard match.

**Returning a positive error code.** The newbus convention is that probe returns a negative priority value or a positive errno. Returning the wrong sign is a common typo. `ENXIO` is the right "I do not match" return.

**Leaving resources allocated in probe.** Probe must be side-effect-free. If probe allocates a resource, it must release it before returning. The cleanest approach is to never allocate from probe; do everything in attach.

**Confusing `pci_get_vendor` with `pci_read_config`**. The two are different. `pci_get_vendor` reads a cached ivar. `pci_read_config(dev, PCIR_VENDOR, 2)` reads the live configuration space. Both produce the same value for this field, but one is a cheap inline function and the other is a bus transaction. Use the accessor.

**Forgetting to include the right headers.** `dev/pci/pcireg.h` defines the `PCIR_*` constants. `dev/pci/pcivar.h` defines `pci_get_vendor` and friends. Both need to be included. The compiler error is usually "undefined identifier" for `pci_get_vendor`; the fix is the missing include.

**Name collision with `MODULE_VERSION`.** The first argument must match the driver name. `MODULE_VERSION(myfirst, 1)` is fine. `MODULE_VERSION(myfirst_pci, 1)` is not, because `myfirst_pci` is a file name, not a module name. The module loader looks up modules by the name registered in `DRIVER_MODULE`.

Each of these is recoverable. The debug kernel catches some of them (the loaded-before-pci case produces a panic the debug kernel pretty-prints). The others produce subtle wrong behaviour that is most easily caught by carefully testing the load-attach-detach-unload cycle after every change.

### Checkpoint: Stage 1 Working

Before moving to Section 3, confirm that the Stage 1 driver works end-to-end.

On the bhyve or QEMU guest:

- `kldload virtio_pci` (if not already loaded).
- `kldunload virtio_random` (if loaded; fails gracefully if it isn't).
- `kldload ./myfirst.ko`.
- `kldstat -v | grep myfirst` should show the module loaded.
- `devinfo -v | grep myfirst` should show `myfirst0` attached under `pci0`.
- `dmesg` should show the attach banner.
- `kldunload myfirst`.
- `dmesg` should show the detach banner.
- `devinfo -v | grep myfirst` should show nothing.

If all of these pass, you have a working Stage 1 driver. The next step is to claim the BAR.

### Wrapping Up Section 2

The probe-attach-detach sequence is the skeleton of every PCI driver. Section 2 built it in its smallest possible form: a probe that matches one vendor-and-device pair, an attach that prints a banner, a detach that prints another banner, and enough glue (`DRIVER_MODULE`, `MODULE_DEPEND`, `MODULE_VERSION`) to make the kernel's module loader and newbus enumerator accept it.

What the Stage 1 skeleton does not yet do: claim a BAR, read a register, enable bus mastering, walk the capability list, create a cdev, or coordinate the PCI build with the Chapter 17 simulation build. Every one of these is a topic of a later section in this chapter. The skeleton is important because every later topic plugs into it without reshaping it. Attach grows from a two-line function to a twenty-line function as the chapter proceeds; probe stays exactly as it is.

Section 3 introduces BARs. It explains what they are, how they are assigned, and how a driver claims the memory range a BAR describes. By the end of Section 3, the driver will hold a `struct resource *` for its BAR and a tag-and-handle pair ready to hand to the Chapter 16 accessor layer.



## Section 3: Understanding and Claiming PCI Resources

With a probe and a trivial attach in place, the driver knows when it has found the device it wants to drive. What it does not yet know is how to reach that device's registers. Section 3 closes the gap. It starts with what a BAR is in the PCI specification, walks through how the firmware and the kernel set one up, and finishes with the driver code that claims a BAR and turns it into a `bus_space` tag and handle that the Chapter 16 accessors can use unchanged.

The point of this section is to make the word "BAR" concrete. A reader who finishes Section 3 should be able to answer, in a sentence: a BAR is a configuration-space field where a device says "here is how much memory (or how many I/O ports) I need, and here is how you can reach it once the firmware has mapped it into the host's address space". Everything else in the section builds on that sentence.

### What a BAR Is, Precisely

Every PCI device advertises the resources it needs through Base Address Registers. A standard PCI device header (the non-bridge type) has six BARs, each four bytes wide, at configuration-space offsets `0x10`, `0x14`, `0x18`, `0x1c`, `0x20`, and `0x24`. In FreeBSD's `/usr/src/sys/dev/pci/pcireg.h`, these offsets are produced by the macro `PCIR_BAR(n)`, where `n` ranges from 0 to 5.

Each BAR describes one range of addresses. A BAR's low bit tells the software whether the range is in memory space or in I/O port space. If the low bit is zero, the range is memory-mapped; if it is one, the range is in the I/O port address space. Everything above the low few bits is an address; the exact field layout depends on the BAR type.

For a memory-mapped BAR, the layout is:

- Bit 0: `0` for memory.
- Bits 2-1: type. `0b00` for 32-bit, `0b10` for 64-bit, `0b01` reserved (formerly "below 1 MB").
- Bit 3: prefetchable. `1` if the device promises that reads have no side effects, so the CPU may prefetch and merge accesses.
- Bits 31-4 (or 63-4 for 64-bit): the address.

A 64-bit BAR occupies two consecutive BAR slots. The low slot holds the lower 32 bits of the address (with the type bits); the high slot holds the upper 32 bits. A driver walking the BAR list must recognise when it has encountered a 64-bit BAR and skip the consumed upper slot.

For an I/O port BAR:

- Bit 0: `1` for I/O.
- Bit 1: reserved.
- Bits 31-2: the port address.

I/O port BARs are less common on modern devices. Most modern PCIe devices use memory-mapped BARs exclusively. Chapter 18 focuses on memory-mapped BARs.

### How a BAR Gets an Address

A BAR is written in two passes. The first pass is what the silicon designer specified: a read from a BAR returns the device's requirements. The low bit type field is read-only. The address field is read-write, but with a catch: writing all ones to the address field and reading it back tells the firmware how large the range is. The device returns a value where the low bits (those below the size) are zero and the high bits (those that the device does not implement) return what was written. The firmware interprets the readback as a size mask.

The second pass assigns the actual address. The firmware (BIOS or UEFI) walks every BAR on every PCI device, notes the size each requires, partitions the host's address space to satisfy all of them, and writes the assigned address back into each BAR. By the time the OS boots, every BAR has a real address that the OS can use to reach the device.

The OS can optionally re-do the assignment if it wants to (for hot-plug support or if the firmware did a poor job). FreeBSD mostly accepts the firmware's assignment; the `hw.pci.realloc_bars` sysctl and the `bus_generic_probe` logic handle the uncommon case where reassignment is needed.

From the driver's point of view, all of this is done by the time attach runs. The BAR has an address, the address is mapped into the kernel's virtual space, and the driver needs only to ask for the resource by number.

### The rid Argument and PCIR_BAR

The driver claims a BAR by calling `bus_alloc_resource_any(9)` with a resource ID (usually called `rid`) that identifies which BAR to allocate. For a memory-mapped BAR, the `rid` is the configuration-space offset of that BAR, produced by the macro `PCIR_BAR(n)`:

- `PCIR_BAR(0)` = `0x10` (BAR 0)
- `PCIR_BAR(1)` = `0x14` (BAR 1)
- ...
- `PCIR_BAR(5)` = `0x24` (BAR 5)

Passing `PCIR_BAR(0)` to `bus_alloc_resource_any` requests BAR 0. Passing `PCIR_BAR(1)` requests BAR 1. The macro is one line in `pcireg.h`:

```c
#define	PCIR_BAR(x)	(PCIR_BARS + (x) * 4)
```

where `PCIR_BARS` is `0x10`.

Beginners sometimes pass `0` or `1` as the `rid` and are surprised when the allocation fails. The `rid` is not a BAR index; it is the offset. Use `PCIR_BAR(index)` unless you have a specific reason to pass a raw offset.

### The Resource Type: SYS_RES_MEMORY vs SYS_RES_IOPORT

`bus_alloc_resource_any` takes a type argument that tells the kernel what kind of resource the driver wants. For a memory BAR, the type is `SYS_RES_MEMORY`. For an I/O port BAR, the type is `SYS_RES_IOPORT`. For an interrupt, it is `SYS_RES_IRQ`. The small set of resource types is defined in `/usr/src/sys/arm64/include/resource.h` (and the per-architecture equivalents); memory, I/O port, and IRQ are the three a PCI driver normally uses.

PCI configuration space itself is not claimed through `bus_alloc_resource_any`. The driver reaches it through `pci_read_config(9)` and `pci_write_config(9)`, which route the access through the PCI bus driver without needing a resource handle.

A driver that does not know whether its BAR is memory or I/O can inspect the low bit of the BAR in configuration space to find out. A driver that knows (because the datasheet says so, or because the device has always been MMIO in this chapter) simply passes the right type.

Most PCIe devices have their main interface in memory space and an optional compatibility window in I/O port space. A driver usually asks for the memory BAR first and, if that fails, falls back to the I/O port BAR. Chapter 18's driver asks for memory only; the virtio-rnd device it targets exposes its registers in a memory BAR.

### The RF_ACTIVE Flag

`bus_alloc_resource_any` also takes a flags argument. The two most commonly set flags are:

- `RF_ACTIVE`: activate the resource as part of allocation. Without this, the allocation reserves the resource but does not map it; the driver must call `bus_activate_resource(9)` separately. With it, the resource is allocated and activated in one step.
- `RF_SHAREABLE`: the resource can be shared with other drivers. This matters for interrupts (IRQs shared between multiple devices); it matters less for memory BARs.

For a memory BAR, the common case is `RF_ACTIVE` alone. For an IRQ that might be shared on a legacy system, it is `RF_ACTIVE | RF_SHAREABLE`. Chapter 18 uses `RF_ACTIVE` only.

### bus_alloc_resource_any in Detail

The function signature is:

```c
struct resource *bus_alloc_resource_any(device_t dev, int type,
    int *rid, u_int flags);
```

Three arguments, plus a return value.

`dev` is the device handle. `type` is `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, or `SYS_RES_IRQ`. `rid` is a pointer to an integer holding the resource ID; the kernel may update it (for example, to tell the driver which slot was actually used when the driver passed a wildcard). `flags` is the bit mask described above.

The return value is a `struct resource *`. Non-NULL on success; NULL on failure. The resource handle is what every subsequent operation (reads, writes, release) uses.

A typical call looks like:

```c
int rid = PCIR_BAR(0);
struct resource *bar;

bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (bar == NULL) {
	device_printf(dev, "cannot allocate BAR0\n");
	return (ENXIO);
}
```

After the call, `bar` points to an allocated and activated resource; `rid` may have been updated by the kernel if it chose a different slot than the driver asked for (for wildcard allocations, this is where the chosen slot becomes visible).

### From Resource to Tag and Handle

The resource handle is the driver's connection to the BAR, but the Chapter 16 accessor layer expects a `bus_space_tag_t` and a `bus_space_handle_t`, not a `struct resource *`. Two helpers turn one into the other:

- `rman_get_bustag(res)` returns the `bus_space_tag_t`.
- `rman_get_bushandle(res)` returns the `bus_space_handle_t`.

Both are inline accessor functions defined in `/usr/src/sys/sys/rman.h`. The resource stores the tag and handle internally; the accessors return the stored values. The driver then stores the tag and handle in its own state (in Chapter 18, in the hardware layer's `struct myfirst_hw`) so that the Chapter 16 accessors can use them.

The pattern is short:

```c
sc->hw->regs_tag = rman_get_bustag(bar);
sc->hw->regs_handle = rman_get_bushandle(bar);
```

After these two lines, `CSR_READ_4(sc, off)` and `CSR_WRITE_4(sc, off, val)` work against the real BAR. No other code in the driver needs to know that the backend changed.

### rman_get_size and rman_get_start

Two additional helpers extract the range of addresses the resource covers:

- `rman_get_size(res)` returns the number of bytes.
- `rman_get_start(res)` returns the physical or bus start address.

The driver uses `rman_get_size` to sanity-check that the BAR is large enough for the registers the driver expects. A device whose BAR is smaller than the chapter expects is either mis-identified (wrong device behind the ID pair) or a variant the driver does not support. Either way, a failed sanity check in attach is better than a corrupted access at run time.

`rman_get_start` is useful mostly for diagnostic logging. The physical address of the BAR is not something the driver dereferences directly (the kernel's mapping is what the tag and handle wrap), but printing it helps when debugging because it connects the `pciconf -lv` output to the driver's view.

### Releasing the BAR

The counterpart to `bus_alloc_resource_any` is `bus_release_resource(9)`. The signature is:

```c
int bus_release_resource(device_t dev, int type, int rid, struct resource *res);
```

`dev`, `type`, and `rid` match the allocation call; `res` is the handle returned by the allocation. On success, the function returns 0; on failure, it returns an errno. Failures are rare because the resource was just allocated by this driver, but defensive drivers check the return value and log on failure.

The driver should always release every resource it allocated, in the reverse order of allocation. Chapter 18's driver at Stage 2 allocates one BAR; it will release that one BAR in detach. Later stages, after interrupts and DMA enter the picture in Chapters 19 through 21, will allocate more.

### Partial Failure in Attach

A subtle point about attach. If the driver claims the BAR successfully but then fails at a later step (for example, the device's expected `DEVICE_ID` register does not match), the driver must release the BAR before returning the error. Forgetting to release is a resource leak: the kernel's resource manager still thinks the BAR is allocated by this driver, even though the driver has returned. The next attempt to attach will fail.

The idiom is the familiar goto-based cleanup pattern:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int rid, error;

	sc->dev = dev;
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail;
	}

	error = myfirst_hw_attach_pci(sc);
	if (error != 0)
		goto fail_release;

	/* ... */
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail:
	return (error);
}
```

The `goto` cascade is an idiom, not a smell. It keeps cleanup code in one place and makes the allocation-and-release pairing symmetric. The pattern was introduced in Chapter 15 for mutex-and-callout cleanup; here it extends to resource cleanup. Chapter 18's final attach uses a longer version of this cascade to handle the softc init, BAR allocation, hardware-layer attach, and cdev creation as four staged allocations.

### What Lives in the Softc

Chapter 18 adds a few fields to the softc. They are declared in `myfirst.h` (the main driver header, not `myfirst_pci.h`, because the softc is shared across all the layers).

```c
struct myfirst_softc {
	device_t dev;
	/* ... Chapter 10 through 17 fields ... */

	/* Chapter 18 PCI fields. */
	struct resource	*bar_res;
	int		 bar_rid;
	bool		 pci_attached;
};
```

`bar_res` is the handle to the claimed BAR. `bar_rid` is the resource ID used to allocate it (stored so detach can pass the right value to `bus_release_resource`). `pci_attached` is a flag that later code uses to distinguish the real-PCI attach path from the simulated attach path.

A single BAR is enough for the Chapter 18 driver. Drivers for more complex devices would have `bar0_res`, `bar0_rid`, `bar1_res`, `bar1_rid`, and so on, each pair matching one BAR. The virtio-rnd device has only one BAR, so the driver has only one pair.

### The Stage 2 Attach

Putting the allocation into Stage 2's attach routine:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error = 0;

	sc->dev = dev;

	/* Allocate BAR0 as a memory resource. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		return (ENXIO);
	}

	device_printf(dev, "BAR0 allocated: %#jx bytes at %#jx\n",
	    (uintmax_t)rman_get_size(sc->bar_res),
	    (uintmax_t)rman_get_start(sc->bar_res));

	sc->pci_attached = true;
	return (error);
}
```

A successful Stage 2 attach prints a line like:

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

The size and address depend on the guest's layout. The important parts are that the allocation succeeded, the size is what the driver expected (the virtio-rnd device exposes at least 32 bytes of registers), and the detach path releases the resource.

### The Stage 2 Detach

The Stage 2 detach needs to release what attach allocated:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	sc->pci_attached = false;
	device_printf(dev, "detaching\n");
	return (0);
}
```

The `if` guard is defensive: in principle `sc->bar_res` is non-NULL whenever detach is called after a successful attach, but adding the check costs nothing and makes the detach robust against partial-failure cases that might arise in future refactors. Setting `bar_res` to NULL after release prevents a double-free if something later calls detach again.

### What Stage 2 Does Not Yet Do

At the end of Stage 2, the driver allocates a BAR but does nothing with it. The tag and handle are available but not yet wired to the Chapter 16 accessors. The simulation from Chapter 17 still runs, but it runs against the `malloc(9)`-allocated register block, not against the real BAR.

Section 4 bridges the gap. It takes the tag and handle from Stage 2 and hands them to `myfirst_hw_attach` so that `CSR_READ_4` and `CSR_WRITE_4` operate on real silicon. After Section 4, the Chapter 17 simulation becomes a runtime option rather than the only backend.

### Verifying Stage 2

Before moving to Section 4, confirm that Stage 2 works end-to-end.

```sh
# In the bhyve guest:
sudo kldunload virtio_random  # may not be loaded
sudo kldload ./myfirst.ko
sudo dmesg | grep myfirst | tail -5
```

The output should look like:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
```

`devinfo -v | grep -A 2 myfirst0` should show the resource claim:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
```

The memory range printed by `devinfo -v` matches the range printed by the driver. This confirms that the allocation succeeded and that the kernel sees the BAR as claimed by `myfirst`.

Unload and verify the cleanup:

```sh
sudo kldunload myfirst
sudo devinfo -v | grep myfirst  # should return nothing
```

No lingering devices, no leaked resources. Stage 2 is complete.

### Common Mistakes in BAR Allocation

A short list of the typical traps.

**Passing `0` as the rid for BAR 0.** The rid is `PCIR_BAR(0)` = `0x10`, not `0`. Passing `0` asks for a resource at offset 0, which is the `PCIR_VENDOR` field; the allocation fails or produces surprising results. Always use `PCIR_BAR(index)`.

**Forgetting `RF_ACTIVE`.** Without this flag, `bus_alloc_resource_any` allocates but does not activate. Reading from the tag and handle at that point is undefined behaviour. The symptom is usually a page fault or garbage values. The fix is to pass `RF_ACTIVE`.

**Using the wrong resource type.** Passing `SYS_RES_IOPORT` for a memory BAR produces an immediate allocation failure. Passing `SYS_RES_MEMORY` for an I/O port BAR does the same. The type must match the BAR's actual type. If the driver does not know in advance (a generic driver that supports both memory and I/O variants), it reads `PCIR_BAR(index)` from configuration space and checks the low bit.

**Not releasing on partial failure.** A common beginner mistake: attach claims the BAR, a subsequent step fails, the function returns the error, and the BAR is never released. The resource leaks. The next attach attempt fails because the BAR is still claimed.

**Releasing the BAR before the accessor layer is done with it.** The reverse mistake: detach releases the BAR too early, before draining callouts or tasks that might still be reading from it. The symptom is a page fault inside a callout shortly after `kldunload`. The fix is to drain everything that might access the BAR, then release.

**Confusing `rman_get_size` with `rman_get_end`.** `rman_get_size(res)` returns the number of bytes. `rman_get_end(res)` returns the last byte's address (start plus size minus one). Use `rman_get_size` for sanity checks on BAR size; use `rman_get_start` and `rman_get_end` for diagnostic printing.

**Assuming BARs are in any particular order.** The driver must name the BAR it wants explicitly (by passing `PCIR_BAR(n)`). Some devices put their main BAR at index 0; some put it at index 2. The datasheet (or the `pciconf -lv` output for the specific device) says where it is. Assuming BAR 0 without verifying is a common mistake.

### A Note on 64-bit BARs

The virtio-rnd device used in Chapter 18 has a 32-bit BAR, so the allocation shown here works without special handling. For devices with 64-bit BARs, there are two important details:

First, the BAR occupies two consecutive slots in the configuration-space BAR table. BAR 0 (at offset `0x10`) holds the lower 32 bits; BAR 1 (at offset `0x14`) holds the upper 32 bits. A driver walking the BAR table by simple integer increments would mistakenly treat BAR 1 as a separate BAR. The right walk reads each BAR's type bits and skips the next slot if the current slot is a 64-bit BAR.

Second, the `rid` passed to `bus_alloc_resource_any` is the lower slot's offset. The kernel recognises the 64-bit type and treats the pair as a single resource. The driver does not need to allocate two resources for a 64-bit BAR; one allocation with `rid = PCIR_BAR(0)` handles both slots.

For Chapter 18's driver, this is academic; the target device has 32-bit BARs. But a reader who later works with a device that has a 64-bit BAR will need the details. `/usr/src/sys/dev/pci/pcireg.h` defines `PCIM_BAR_MEM_TYPE`, `PCIM_BAR_MEM_32`, and `PCIM_BAR_MEM_64` to help with BAR inspection.

### Prefetchable vs Non-Prefetchable BARs

A related detail. A BAR is prefetchable if its bit 3 is set. Prefetchable means "reads from this range have no side effects, so the CPU is allowed to cache, prefetch, and merge accesses as it would for normal RAM". Non-prefetchable means "reads have side effects, so every access must hit the device; the CPU must not cache, prefetch, or merge".

Device registers are almost always non-prefetchable. A read from a status register may clear the flags; a prefetched read would be a catastrophic bug. Device memory (a frame buffer on a graphics card, or a ring buffer on a NIC) is usually prefetchable.

The driver does not directly control the prefetch attribute; the BAR declares what it is, and the kernel sets up the mapping accordingly. The driver's job is to use `bus_space_read_*` and `bus_space_write_*` correctly. The `bus_space` layer handles the ordering and caching details. A driver that tries to be clever by bypassing `bus_space` and directly dereferencing a pointer might accidentally get a cached mapping on a non-prefetchable BAR and produce a driver that works in ideal conditions and fails mysteriously under load.

Chapter 16 made the case for `bus_space` in general; Section 3 of Chapter 18 confirms that the case extends to real PCI devices. There is no shortcut.

### Wrapping Up Section 3

A BAR is a range of addresses where the device exposes its registers. The firmware assigns BAR addresses at boot; the kernel reads them during PCI enumeration; the driver claims them in attach through `bus_alloc_resource_any(9)` with the right type, `rid`, and flags. The returned `struct resource *` carries a `bus_space_tag_t` and `bus_space_handle_t` that `rman_get_bustag(9)` and `rman_get_bushandle(9)` extract. Detach must release every allocated resource in reverse order.

The Stage 2 driver allocates BAR 0 but does not yet use it. Section 4 wires the tag and handle to the Chapter 16 accessor layer, so `CSR_READ_4` and `CSR_WRITE_4` finally operate on the real BAR rather than on a `malloc(9)` block.



## Section 4: Accessing Device Registers via bus_space(9)

Section 3 ended with a tag and a handle in the driver's hand. The tag and handle point at the real BAR; the Chapter 16 accessors expect exactly that pair. Section 4 makes the connection. It teaches how to hand the PCI-allocated tag and handle to the hardware layer, confirms that the Chapter 16 `CSR_*` macros work unchanged against a real PCI BAR, reads the driver's first real register through `bus_space_read_4(9)`, and discusses the access patterns (`bus_space_read_multi`, `bus_space_read_region`, barriers) that Chapter 16 introduced and that the PCI path will reuse.

The theme of Section 4 is continuity. The reader has been writing register-access code for two chapters. Chapter 18 does not change that code. What changes is where the tag and handle come from. The accessors are exactly the accessors; the wrapping macros are exactly the wrapping macros; the lock discipline is exactly the lock discipline. This is the payoff for the `bus_space(9)` abstraction. The driver's upper layers do not know, and should not know, that the register block's origin has changed.

### Revisiting the Chapter 16 Accessors

A short reminder. `myfirst_hw.c` defines three public functions that the rest of the driver calls:

- `myfirst_reg_read(sc, off)` returns a 32-bit value from the register at the given offset.
- `myfirst_reg_write(sc, off, val)` writes a 32-bit value to the register at the given offset.
- `myfirst_reg_update(sc, off, clear, set)` performs a read-modify-write: read, clear the given bits, set the given bits, write.

All three are wrapped by the `CSR_*` macros defined in `myfirst_hw.h`:

- `CSR_READ_4(sc, off)` expands to `myfirst_reg_read(sc, off)`.
- `CSR_WRITE_4(sc, off, val)` expands to `myfirst_reg_write(sc, off, val)`.
- `CSR_UPDATE_4(sc, off, clear, set)` expands to `myfirst_reg_update(sc, off, clear, set)`.

The accessors reach `bus_space` through two fields in `struct myfirst_hw`:

- `hw->regs_tag` of type `bus_space_tag_t`
- `hw->regs_handle` of type `bus_space_handle_t`

The actual call inside `myfirst_reg_read` is:

```c
value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);
```

and inside `myfirst_reg_write`:

```c
bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);
```

These lines do not know about PCI. They do not know about `malloc`. They do not know whether `hw->regs_tag` came from a simulated pmap setup in Chapter 16 or from a `rman_get_bustag(9)` call in Chapter 18. Their contract is unchanged.

### The Two Origins of a Tag and Handle

Chapter 16 used a trick to produce a tag and handle from a `malloc(9)` allocation on x86. The trick was simple: the x86 `bus_space` implementation uses `x86_bus_space_mem` as the tag for memory-mapped accesses, and a handle is just a virtual address. A `malloc`-allocated buffer has a virtual address, so casting the buffer pointer to `bus_space_handle_t` produces a usable handle. The trick is x86-specific; on other architectures, a simulated block would need a different approach.

Chapter 18 uses the proper route: `bus_alloc_resource_any(9)` allocates a BAR as a resource, and `rman_get_bustag(9)` and `rman_get_bushandle(9)` extract the tag and handle that the kernel set up. The driver does not see the physical address; it does not see the virtual mapping; it sees an opaque tag and handle that the kernel's platform code set up correctly. The accessors use them, and the register read hits the real device.

This is the fundamental shape of the PCI integration. Two different origins of the tag and handle. One set of accessors that uses them. The driver picks which origin is live at attach time, and the accessors do not need to know which one was picked.

### Extending myfirst_hw_attach

Chapter 16's `myfirst_hw_attach` allocates a `malloc(9)` buffer and synthesises a tag and handle. Chapter 18 needs a second code path that takes an existing tag and handle (from the PCI BAR) and stores them directly. The simplest way is to rename the Chapter 16 version and introduce a new version for the PCI path.

The new header, adjusted for Chapter 18:

```c
/* Chapter 16 behaviour: allocate a malloc-backed register block. */
int myfirst_hw_attach_sim(struct myfirst_softc *sc);

/* Chapter 18 behaviour: use an already-allocated resource. */
int myfirst_hw_attach_pci(struct myfirst_softc *sc,
    struct resource *bar, bus_size_t bar_size);

/* Shared teardown; safe with either backend. */
void myfirst_hw_detach(struct myfirst_softc *sc);
```

The PCI-path attach stores the tag and handle directly:

```c
int
myfirst_hw_attach_pci(struct myfirst_softc *sc, struct resource *bar,
    bus_size_t bar_size)
{
	struct myfirst_hw *hw;

	if (bar_size < MYFIRST_REG_SIZE) {
		device_printf(sc->dev,
		    "BAR is too small: %ju bytes, need at least %u\n",
		    (uintmax_t)bar_size, (unsigned)MYFIRST_REG_SIZE);
		return (ENXIO);
	}

	hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

	hw->regs_buf = NULL;			/* no malloc block */
	hw->regs_size = (size_t)bar_size;
	hw->regs_tag = rman_get_bustag(bar);
	hw->regs_handle = rman_get_bushandle(bar);
	hw->access_log_enabled = true;
	hw->access_log_head = 0;

	sc->hw = hw;

	device_printf(sc->dev,
	    "hardware layer attached to BAR: %zu bytes "
	    "(tag=%p handle=%p)\n",
	    hw->regs_size, (void *)hw->regs_tag,
	    (void *)hw->regs_handle);
	return (0);
}
```

A few things to notice. `hw->regs_buf` is NULL because there is no `malloc` allocation backing the registers this time; the BAR's kernel mapping is what the tag and handle point at. `hw->regs_size` is the BAR's size, sanity-checked against the minimum size the driver expects. The tag and handle come from the `struct resource *` that the PCI attach path allocated. Everything else in `myfirst_hw` is unchanged.

The shared detach is where the two backends converge:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
	struct myfirst_hw *hw;

	if (sc->hw == NULL)
		return;

	hw = sc->hw;
	sc->hw = NULL;

	/*
	 * Free the simulated backing buffer only if the simulation
	 * attach produced one. The PCI path sets regs_buf to NULL and
	 * leaves regs_size as the BAR size; the BAR itself is released
	 * by the PCI layer (see myfirst_pci_detach).
	 */
	if (hw->regs_buf != NULL) {
		free(hw->regs_buf, M_MYFIRST);
		hw->regs_buf = NULL;
	}
	free(hw, M_MYFIRST);
}
```

The split is clean. The hardware layer knows how to tear down the Chapter 16 backing buffer or do nothing, depending on how it was attached. The BAR itself is not the hardware layer's responsibility; the PCI layer owns it. This separation is what lets Chapter 18 reuse the Chapter 16 code without rewriting the Chapter 16 teardown.

### The First Real Register Read

With the hardware layer wired up, the driver's first real read becomes possible. At Chapter 17 the first read was of the fixed `DEVICE_ID` register, which the simulation pre-populated with `0x4D594649` ("MYFI" in ASCII, for "MY FIrst"). The virtio-rnd device does not expose a `DEVICE_ID` register at that offset; its configuration space at the BAR's offset 0 is a virtio-specific layout that begins with a device features register.

For the Chapter 18 teaching path, we do not need to speak the virtio-rnd protocol. The driver reads the first 32-bit word of the BAR and logs the value. The value is whatever the virtio-rnd device's first register is (the first 32 bits of the virtio legacy device configuration, which for a running virtio-rnd device has no particular meaning to our driver). The point of the read is to prove that the BAR access works.

The code that does this (in `myfirst_pci_attach`, after the BAR allocation and the hardware layer attach):

```c
uint32_t first_word;

MYFIRST_LOCK(sc);
first_word = CSR_READ_4(sc, 0x00);
MYFIRST_UNLOCK(sc);

device_printf(dev, "first register read: 0x%08x\n", first_word);
```

The lock-and-unlock wrapping is the Chapter 16 discipline. The read goes through `bus_space_read_4` under the hood. The output line appears in `dmesg` at attach time:

```text
myfirst0: first register read: 0x10010000
```

The exact value depends on the virtio-rnd device's current state. A reader who sees any value (rather than a page fault or a garbage read that crashes the guest) has confirmed that the BAR allocation worked, the tag and handle are correct, and the accessor layer is operating on real silicon.

### The Full Accessor Family

The Chapter 16 driver used `bus_space_read_4` and `bus_space_write_4` exclusively because the register map is all 32-bit registers. Real PCI devices sometimes need 8-bit, 16-bit, or 64-bit reads, and sometimes need block operations that read or write many contiguous registers at once. The `bus_space` family covers all of these:

- `bus_space_read_1`, `_2`, `_4`, `_8`: single-byte, 16-bit, 32-bit, or 64-bit read.
- `bus_space_write_1`, `_2`, `_4`, `_8`: single-byte, 16-bit, 32-bit, or 64-bit write.
- `bus_space_read_multi_*`: read multiple values from the same register offset (useful for FIFO reads).
- `bus_space_write_multi_*`: write multiple values to the same register offset.
- `bus_space_read_region_*`: read a range of registers into a memory buffer.
- `bus_space_write_region_*`: write a memory buffer to a range of registers.
- `bus_space_set_multi_*`: write the same value to the same register many times.
- `bus_space_set_region_*`: write the same value to a range of registers.
- `bus_space_barrier`: enforce ordering between accesses.

Each variant has the width suffix as a separate entry. The family is symmetrical and predictable once you have seen it.

For Chapter 18's driver, only `_4` is needed. The register map is 32-bit throughout. If a later driver uses a device with 16-bit registers, the reader simply changes `_4` to `_2`. The `CSR_*` macros can be extended to cover multiple widths if needed:

```c
#define CSR_READ_1(sc, off)       myfirst_reg_read_1((sc), (off))
#define CSR_READ_2(sc, off)       myfirst_reg_read_2((sc), (off))
#define CSR_WRITE_1(sc, off, val) myfirst_reg_write_1((sc), (off), (val))
#define CSR_WRITE_2(sc, off, val) myfirst_reg_write_2((sc), (off), (val))
```

with corresponding accessor functions in `myfirst_hw.c`. Chapter 18 does not need these, but a driver author should know they exist.

### bus_space_read_multi vs bus_space_read_region

Two of the block operations deserve a second look because the naming is easy to confuse.

`bus_space_read_multi_4(tag, handle, offset, buf, count)` reads `count` 32-bit values, all from the same offset in the BAR, into `buf`. This is the correct operation for a FIFO: the register at a fixed offset is the FIFO's read port, and each read consumes one entry. Writing a similar loop by hand with `bus_space_read_4` would work, but the block version is often faster and is clearer in intent.

`bus_space_read_region_4(tag, handle, offset, buf, count)` reads `count` 32-bit values from consecutive offsets starting at `offset`, into `buf`. This is the correct operation for a block of registers: the driver wants to snapshot a range of the register map into a local buffer. Writing a loop with `bus_space_read_4` and incrementing the offset would work equivalently; the block version expresses the intent more clearly.

The difference is whether the offset in the BAR advances. `_multi` keeps the offset fixed. `_region` advances it. Writing `_multi` when you meant `_region` reads the same register four times, not four different registers. This is a classic confusion, and the way to avoid it is to read the variant name carefully and to remember "multi = one port, many accesses" vs "region = a range of ports, one access each".

### When Barriers Matter

Chapter 16 introduced `bus_space_barrier(9)` as a guard against CPU reordering and compiler reordering around register accesses. The rule is: when the driver has an ordering requirement between two accesses (a write that must precede a read, for example, or a write that must precede another write), insert a barrier.

For Chapter 18's driver, the accessor layer already wraps a barrier around writes that have side effects (inside `myfirst_reg_write_barrier`, defined in Chapter 16). The simulation backend in Chapter 17 does not require additional barriers because the accesses are to RAM. The PCI backend may require barriers in more places than the simulation did, because real device memory has weaker ordering semantics than RAM on some architectures.

The common case on x86: `bus_space_write_4` to a memory-mapped BAR is strongly ordered with respect to other writes to the same BAR; no explicit barrier is needed. On arm64 with device-memory attributes, writes to the same BAR are also ordered. On other architectures with weaker memory models, explicit barriers may be required. The `bus_space(9)` manual page specifies the default ordering guarantees per architecture; drivers that care about portability include barriers even where x86 would not require them.

Chapter 18's driver lives on x86 for teaching purposes and uses barriers the same way Chapter 16 did: after a CTRL write that has side effects (starts a command, triggers a state change, clears an interrupt). The Chapter 17 `myfirst_reg_write_barrier` helper is still the right entry point.

### The Access Log on a Real BAR

Chapter 16's access log is a ring buffer that records every register access with a timestamp, an offset, a value, and a context tag. On the simulation backend the log shows patterns like "user-space write to CTRL, followed by a callout read of STATUS". On the real PCI backend the log shows the same shape: any access the driver makes to the BAR passes through the accessors, and every accessor writes an entry.

This continuity is a quiet but important feature. A developer who debugs a simulation issue can consult the access log; a developer who debugs a real-hardware issue can consult the same access log. The technique transfers. The code does not change. Section 7's testing discipline depends on this continuity.

One note about the access log and real BARs: if the device sometimes takes side effects on reads (clearing a latched status bit, advancing a FIFO pointer, triggering a posted write completion), the log will record the read's value and the driver's subsequent actions. Reading the log can reveal timing problems that are not otherwise visible. A bug where the driver reads STATUS twice in rapid succession and the second read sees different bits because the first read's side effect interfered will show up clearly in the log. For Chapter 18 this does not matter yet; for Chapter 19 and later chapters it matters a great deal.

### A Small Subtlety: the CSR Macros Do Not Know About PCI

Worth calling out. The `CSR_*` macros do not take a tag or a handle. They take only the softc and the offset. Everything else is inside the accessor functions.

This means: when the driver changes from the Chapter 17 simulation to the Chapter 18 real BAR, not one call site in the driver changes. `CSR_READ_4(sc, MYFIRST_REG_STATUS)` does the right thing before and after the transition. The same is true for `CSR_WRITE_4` and `CSR_UPDATE_4`.

The payoff is concrete. Chapter 17's driver has probably thirty or forty call sites that read or write registers through the CSR macros. If those macros took a tag and a handle, Chapter 18 would need to update every one. Because they take only the softc, Chapter 18 needs to change only the hardware layer's attach routine. The discipline of hiding the low-level details inside the accessors, introduced in Chapter 16 and maintained in Chapter 17, pays its largest dividend here.

This is a pattern worth remembering. When you write a driver, define a small set of accessor functions that hide everything above the register level: the tag, the handle, the lock, the log, the barrier. Expose to the rest of the driver only the softc and the offset. The code that uses the accessors then does not care whether the registers are simulated, real PCI, real USB, real I2C, or real anything else. The abstraction holds across a wide range of transports. Part 7 of the book will return to this theme when it discusses refactoring drivers for portability; Chapter 18 is where the reader first sees the dividend paid.

### Stage 2 to Stage 3: Wiring It Together

The Stage 2 attach allocated the BAR but did not hand it to the hardware layer. Stage 3's attach does both. The relevant code is the full attach:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	error = myfirst_init_softc(sc);	/* Ch10-15: locks, softc fields */
	if (error != 0)
		return (error);

	/* Allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Hand the BAR to the hardware layer. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* Read a diagnostic word from the BAR. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

And the matching detach:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	sc->pci_attached = false;
	myfirst_hw_detach(sc);
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}
	myfirst_deinit_softc(sc);
	device_printf(dev, "detaching\n");
	return (0);
}
```

The attach sequence is strict: init the softc (locks, fields), allocate the BAR, attach the hardware layer against the BAR, perform any register reads that attach needs, mark the driver as attached. The detach undoes each step in reverse: mark as unattached, detach the hardware layer (which frees its wrapper struct), release the BAR, deinit the softc.

Section 5 will extend the attach with additional PCI-specific steps: enabling bus mastering, walking the capability list, reading a subvendor-specific field from configuration space. The shape of the attach stays the same; the middle grows.

### Common Mistakes in the bus_space-on-PCI Transition

A short list of traps.

**Casting the resource pointer instead of using `rman_get_bustag` / `rman_get_bushandle`.** A beginner sometimes writes `hw->regs_tag = (bus_space_tag_t)bar`. This does not compile on most architectures and compiles to nonsense on the rest. Use the accessors.

**Confusing the resource handle with the tag.** The tag is the bus identity (memory or I/O); the handle is the address. `rman_get_bustag` returns the tag; `rman_get_bushandle` returns the handle. Swapping them produces immediate crashes or silent wrong reads. Read the function names carefully.

**Not zeroing the hardware state on PCI attach.** `malloc(9)` with `M_ZERO` clears the structure. Without `M_ZERO`, fields like `access_log_head` start with garbage. The ring buffer wraps to an arbitrary index and the log is unreadable.

**Not releasing the hardware state in detach.** A symmetry mistake: the PCI detach releases the BAR but forgets to call `myfirst_hw_detach`. The hardware wrapper struct leaks. `vmstat -m` shows the leak over time.

**Reading the BAR before the lock is held.** The Chapter 16 discipline is: every CSR access is under `sc->mtx`. Reading at attach time without the lock violates the invariant that every later access assumes. Even if it happens to work on a single CPU, `WITNESS` on a debug kernel will complain. Take the lock even for the attach-time reads.

**Accidentally writing to a read-only register.** On the simulation backend, writes to a read-only register just update the `malloc`-allocated buffer (the simulation's read side ignores the write and returns the fixed value). On real PCI, writes to a read-only register are either silently ignored or cause some device-specific side effect. Neither case is what the driver author expects. Read the datasheet and write to writeable registers only.

**Calling `bus_space_read_multi_4` when the driver meant `_region_4`.** The two functions have identical signatures and very different semantics. Reading a range of registers with `_multi` fills the buffer with the same value (the fixed offset's current value) repeated `count` times. Reading a range with `_region` fills the buffer with the consecutive register values. The bug is silent until the values are inspected.

### Wrapping Up Section 4

The Chapter 16 accessor layer is unchanged by the transition from simulated to real PCI registers. The only change is in `myfirst_hw_attach_pci`, which replaces the `malloc(9)` backing buffer with a tag and handle produced by `rman_get_bustag(9)` and `rman_get_bushandle(9)` on the PCI-allocated resource. The `CSR_*` macros, the access log, the lock discipline, the ticker task, and every other part of the Chapter 16 and Chapter 17 code base continue to work without modification.

The driver's first real PCI register read happens at attach time. The value read is not meaningful in the virtio-rnd protocol sense; it is proof that the BAR mapping is live and the accessors are reading real silicon. Section 5 takes the attach sequence further: it introduces `pci_enable_busmaster(9)` (for future DMA use), walks the PCI capability list with `pci_find_cap(9)` and `pci_find_extcap(9)`, explains when a driver reads configuration-space fields directly, and shows how the Chapter 17 simulation is kept inactive on the PCI path so its callouts do not write arbitrary values into the real device's registers.



## Section 5: Driver Attach-Time Initialisation

Sections 2 through 4 built the attach routine from nothing to a fully wired PCI attach that claims a BAR, hands it to the hardware layer, and performs its first register access. Section 5 finishes the attach story. It introduces the handful of configuration-space operations a PCI driver typically performs at attach, explains when and why each is needed, walks the PCI capability list to discover the device's optional features, shows how the Chapter 17 simulation is kept inactive on the PCI path (so its callouts do not write into the real device), and creates the cdev that the Chapter 10 driver already exposed.

By the end of Section 5, the driver is complete as a PCI driver. It attaches to the real device, brings the device to a state where the driver can use it, exposes the same user-space interface the Chapter 10 through 17 iterations exposed, and is ready to be extended in Chapter 19 with a real interrupt handler.

### The Attach-Time Checklist

A working PCI driver's attach routine typically performs, in roughly this order:

1. **Initialise the softc.** Set `sc->dev`, initialise locks, initialise conditions and callouts, clear counters.
2. **Allocate resources.** Claim the BAR (or BARs), claim the IRQ resource (in Chapter 19), and any other bus resources.
3. **Activate device features.** Enable bus mastering if the driver will use DMA. Set up configuration-space bits the device needs.
4. **Walk capabilities.** Find PCI capabilities the driver supports and record their register offsets.
5. **Attach the hardware layer.** Hand the BAR to the accessor layer.
6. **Initialise the device.** Perform the device-specific bring-up sequence: reset, feature negotiation, queue setup. This is whatever the device's datasheet says is required to make the device usable.
7. **Register user-space interfaces.** Create cdevs, network interfaces, or whatever the driver exposes.
8. **Enable interrupts.** Register the interrupt handler (Chapter 19) and unmask interrupts in the device's INTR_MASK register.
9. **Mark the driver as attached.** Set a flag that other code can check.

Not every driver does every step. A driver for a passive device (no DMA, no interrupts, just reads and writes) skips bus mastering and interrupt setup. A driver for a device that does not need a user-space interface skips the cdev creation. But the order is stable: resources first, device features second, hardware layer third, device bring-up fourth, user-space fifth, interrupts last. Doing this out of order produces race conditions where an interrupt arrives before the driver is ready to handle it, or where a user-space access reaches a partially-initialised driver.

Chapter 18's driver performs steps 1 through 7. Step 8 is Chapter 19. Step 9 is a detail the driver already handled in Chapter 10.

### pci_enable_busmaster and the Command Register

The PCI command register lives at configuration-space offset `PCIR_COMMAND` (`0x04`), as a 16-bit field. Three bits in that register matter for most drivers:

- `PCIM_CMD_MEMEN` (`0x0002`): enable the device's memory BARs. Must be set before the driver can read or write any memory BAR.
- `PCIM_CMD_PORTEN` (`0x0001`): enable the device's I/O port BARs. Must be set before the driver can read or write any I/O port BAR.
- `PCIM_CMD_BUSMASTEREN` (`0x0004`): enable the device to initiate DMA as a bus master. Must be set before the device can read or write RAM on its own.

The PCI bus driver sets `MEMEN` and `PORTEN` automatically when it activates a BAR. A driver that has successfully called `bus_alloc_resource_any` with `RF_ACTIVE` and has received a non-NULL result does not need to set these bits by hand; the bus driver has already done so.

`BUSMASTEREN` is different. The bus driver does not set it automatically, because not every driver needs DMA. A driver that will program its device to read or write system RAM (a NIC, a storage controller, a GPU) must set `BUSMASTEREN` explicitly. A driver that only reads and writes the device's own BARs (no DMA) does not need to set it.

The helper `pci_enable_busmaster(dev)` sets the bit. Its inverse, `pci_disable_busmaster(dev)`, clears it. Chapter 18's driver does not use DMA and does not call `pci_enable_busmaster`. Chapter 20 and Chapter 21 will.

A note about reading the command register directly. The driver can always read the command register with `pci_read_config(dev, PCIR_COMMAND, 2)` and inspect individual bits. For most drivers this is unnecessary; the kernel has already configured the relevant bits. For diagnostic purposes (a driver that wants to log the device's command register state at attach), it is fine.

### Reading Configuration-Space Fields

Most drivers need to read at least a handful of configuration-space fields that the generic accessors do not cover. Examples include:

- Specific firmware revision numbers at vendor-specific offsets.
- PCIe link status fields inside the PCIe capability structure.
- Vendor-specific capability data.
- Subsystem-specific identification fields for multi-function devices.

The primitive is `pci_read_config(dev, offset, width)`. Offset is a byte offset into configuration space. Width is 1, 2, or 4 bytes. The return value is a `uint32_t` (narrower widths are right-aligned).

A concrete example. The PCI class code occupies bytes `0x09` through `0x0b` in configuration space:

- Byte `0x09`: programming interface (progIF).
- Byte `0x0a`: subclass.
- Byte `0x0b`: class.

Reading all three at once as a 32-bit value gives the class, subclass, and progIF in the top three bytes (the low byte is the revision ID). The cached accessors `pci_get_class`, `pci_get_subclass`, `pci_get_progif`, and `pci_get_revid` extract each field individually; the driver rarely needs to do this by hand.

For vendor-specific fields the driver must read by hand. The pattern is:

```c
uint32_t fw_rev = pci_read_config(dev, 0x48, 4);
device_printf(dev, "firmware revision 0x%08x\n", fw_rev);
```

The offset `0x48` is a placeholder; the real offset is whatever the device's datasheet specifies. Reading from an offset the device does not implement returns `0xffffffff` or a device-specific default; `0xffffffff` is the classic "no device" value on PCI.

### pci_write_config and the Side-Effect Contract

The counterpart is `pci_write_config(dev, offset, value, width)`. It writes `value` to the configuration-space field at `offset`, truncated to `width` bytes.

A critical point about configuration-space writes: some fields are read-only. Writing to a read-only field is either silently ignored (the common case) or causes a device-specific error. The driver must know, from the PCI specification or from the device's datasheet, which fields are writeable before issuing a write.

A second critical point: some fields have side effects on read or write. The command register, for example, has side effects: setting `MEMEN` enables memory BARs; clearing it disables them. Reading the command register has no side effects. The driver must understand the semantics of each field it touches.

The helper `pci_enable_busmaster` uses `pci_write_config` under the hood to set one bit. The driver can always use `pci_read_config` and `pci_write_config` directly to manipulate a field when a specific helper does not exist.

### pci_find_cap: Walking the Capability List

PCI devices advertise optional features through a linked list of capabilities. Each capability is a small block in configuration space, beginning with a one-byte capability ID and a one-byte "next pointer". The list starts at the offset stored in the device's `PCIR_CAP_PTR` field (configuration-space offset `0x34`) and follows the `next` pointers until a `0` terminates the chain.

Standard capabilities a driver might find include:

- `PCIY_PMG` (`0x01`): Power Management.
- `PCIY_MSI` (`0x05`): Message Signaled Interrupts.
- `PCIY_EXPRESS` (`0x10`): PCI Express. Any PCIe device has this.
- `PCIY_MSIX` (`0x11`): MSI-X. A richer interrupt routing mechanism than MSI.
- `PCIY_VENDOR` (`0x09`): vendor-specific capability.

The driver walks the list through `pci_find_cap(9)`:

```c
int capreg;

if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0) {
	device_printf(dev, "PCIe capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0) {
	device_printf(dev, "MSI capability at offset 0x%x\n", capreg);
}
if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0) {
	device_printf(dev, "MSI-X capability at offset 0x%x\n", capreg);
}
```

The function returns 0 on success and stores the capability's offset in `*capreg`. On failure (the capability is not present) it returns `ENOENT` and does not modify `*capreg`.

The offset returned is the byte offset into configuration space where the capability's first register lives. That register is usually the capability ID itself; the driver can confirm by reading it back and checking against the expected ID. Subsequent bytes in the capability define the feature-specific fields.

Chapter 18's driver walks the capability list at attach time and logs which capabilities are present. The list gives the driver author a sense of what the device offers. MSI and MSI-X are relevant for Chapter 20; Power Management is relevant for Chapter 22. At Chapter 18, the driver simply records the presence and the offsets.

### pci_find_extcap: PCIe Extended Capabilities

PCIe introduces a second list, called extended capabilities, that lives above offset `0x100` in configuration space. This is where modern features like Advanced Error Reporting, Virtual Channel, Access Control Services, and SR-IOV live. The list is structurally similar to the legacy capability list but uses 16-bit IDs and 4-byte offsets.

The walker is `pci_find_extcap(9)`. The signature is identical to `pci_find_cap`:

```c
int capreg;

if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0) {
	device_printf(dev, "AER capability at offset 0x%x\n", capreg);
}
```

Extended capability IDs are defined in `/usr/src/sys/dev/pci/pcireg.h` under names that start with `PCIZ_` (as opposed to `PCIY_` for standard capabilities). The prefix is a mnemonic: `PCIY` for "PCI capabilitY" (the older list), `PCIZ` for "PCI eXtended" (Z comes after Y).

Chapter 18's driver does not subscribe to AER or any other extended capability. It walks the extended list at attach time and logs what it finds, the same way it walks the standard list. This serves two purposes: it gives the reader a view of what PCIe capabilities look like in the wild, and it exercises `pci_find_extcap` so the reader has seen both walkers.

### PCIe AER: An Introduction

Advanced Error Reporting (AER) is a PCIe extended capability that lets the system detect and report certain classes of PCI-level errors: uncorrectable transaction errors, correctable errors, malformed TLPs, completion timeouts, and others. The capability is optional; not every PCIe device implements it.

On FreeBSD the PCI bus driver (`pci(4)`, implemented in `/usr/src/sys/dev/pci/pci.c`) walks each device's extended capability list during probe, locates the AER capability when present, and uses it for system-level error logging. A driver does not normally register its own AER callback; the bus handles AER centrally and logs correctable and uncorrectable errors into the kernel message buffer. A driver that wants custom handling reads the AER status registers through `pci_read_config(9)` at the offset returned by `pci_find_extcap(dev, PCIZ_AER, &offset)` and decodes them against the bit layouts in `/usr/src/sys/dev/pci/pcireg.h`.

For Chapter 18's driver, AER is mentioned to complete the PCIe capability picture. The driver does not subscribe to AER events. Chapter 20 picks the topic up again, in its "PCIe AER recovery through MSI-X vectors" discussion, to explain where a driver-owned AER handler would hook into the MSI-X plumbing the interrupt chapters build. A full, end-to-end AER recovery implementation is beyond the scope of this book; readers who want to follow the bus-centric side all the way through can study `pci_add_child_clear_aer` and `pcie_apei_error` in `/usr/src/sys/dev/pci/pci.c`, together with the `PCIR_AER_*` and `PCIM_AER_*` bit layouts in `/usr/src/sys/dev/pci/pcireg.h`.

A short aside on the naming: "AER" is pronounced letter by letter ("ay-ee-ar") in most FreeBSD conversations. The capability ID in the pcireg header is `PCIZ_AER` = `0x0001`.

### Composing the Simulation with the Real PCI Backend

The driver at Chapter 17 Stage 5 attached to the kernel as a standalone module (`kldload myfirst` triggered the attach). Chapter 18's driver attaches to a PCI device. Both attach paths need to set up the same upper-layer state (softc, cdev, some per-instance fields). The question is how to compose them.

Chapter 18's driver solves this with a single compile-time switch that selects which attach paths are active, and by having the Chapter 17 simulation's callouts **not** run when the driver is bound to a real PCI device. The logic is straightforward:

- If `MYFIRST_SIMULATION_ONLY` is defined at build time, the driver omits `DRIVER_MODULE` entirely. There is no PCI attach; the module behaves exactly like the Chapter 17 driver, and `kldload` spawns one simulated instance through the Chapter 17 module-event handler.
- If `MYFIRST_SIMULATION_ONLY` is **not** defined (the default for Chapter 18), the driver declares `DRIVER_MODULE(myfirst, pci, ...)`. The module is loadable. When a matching PCI device exists, `myfirst_pci_attach` runs. The Chapter 17 simulation callouts are not started on the PCI path; the accessor layer points at the real BAR, and the simulation backend stays idle. A reader who wants simulation re-activates it explicitly through a sysctl or by building with `MYFIRST_SIMULATION_ONLY`.

The compile-time guard in `myfirst_pci.c` is short:

```c
#ifndef MYFIRST_SIMULATION_ONLY
DRIVER_MODULE(myfirst, pci, myfirst_pci_driver, NULL, NULL);
MODULE_DEPEND(myfirst, pci, 1, 1, 1);
#endif
```

And `myfirst_pci_attach` deliberately skips `myfirst_sim_enable(sc)`. The Chapter 17 sensor callout, command callout, and fault-injection machinery stay asleep. They are present in the code but never scheduled when the backend is a real PCI BAR; that keeps writes from the simulated `CTRL.GO` bit out of the real device's registers.

A reader on a host without a matching PCI device still has the option of running the Chapter 17 simulation directly: build with `MYFIRST_SIMULATION_ONLY=1`, `kldload`, and the driver behaves exactly as it did at the end of Chapter 17. The two builds share every file; the selection happens at compile time.

An alternative the reader may choose: split the driver into two modules. `myfirst_core.ko` holds the hardware layer, the simulation, the cdev, and the locks. `myfirst_pci.ko` holds the PCI attach. `myfirst_core.ko` is always loadable and provides the simulation. `myfirst_pci.ko` depends on `myfirst_core.ko` and adds PCI support on top.

This is the approach real FreeBSD drivers use when a chipset has multiple transport variants. The `uart(4)` driver has `uart.ko` as the core and `uart_bus_pci.ko` as the PCI attach; `virtio(4)` has `virtio.ko` as the core and `virtio_pci.ko` as the PCI transport. The book's later chapter on multi-transport drivers returns to this pattern.

For Chapter 18 the simpler approach (one module with a compile-time switch) is enough. A reader who wants to practise the split can try it as a challenge at the end of the chapter.

### Why the Simulation Callouts Stay Silent on Real PCI

A note worth making explicit. When the Chapter 18 driver attaches to a real virtio-rnd device, the BAR does not hold the Chapter 17 register map. Offset `0x00` is the virtio legacy device-features register, not `CTRL`. Offset `0x12` is the virtio `device_status` register, not the Chapter 17 `INTR_STATUS`. Letting the Chapter 17 sensor callout write to `SENSOR_CONFIG` (at Chapter 17 offset `0x2c`) or letting the command callout write to `CTRL` at `0x00` would poke arbitrary bytes into the virtio device's registers.

On a bhyve guest this is not catastrophic (the guest is disposable), but it is poor discipline. The correct behaviour is: the simulation callouts run only when the accessor layer is backed by the simulated buffer. When the accessor layer is backed by a real BAR, the simulation stays off. Chapter 18's `myfirst_pci_attach` enforces this by never calling `myfirst_sim_enable`. The cdev still works, `CSR_READ_4` still reads the real BAR, and the rest of the driver operates normally. The callouts simply do not fire.

This is a small design decision with a real consequence: the driver is safe to attach against a real PCI device without corrupting the device's state. A reader who later adapts the driver to a different device (whose register map the Chapter 17 simulation does match) can re-enable the callouts with a sysctl and watch them drive the real silicon. For the virtio-rnd teaching target, the callouts stay asleep.

### Creating the cdev in a PCI Driver

The Chapter 10 driver created a cdev with `make_dev(9)` at module-load time. In the Chapter 18 PCI driver, `make_dev(9)` runs at attach time, once per PCI device. The name of the cdev incorporates the unit number: `/dev/myfirst0`, `/dev/myfirst1`, and so on.

The code is familiar:

```c
sc->cdev = make_dev(&myfirst_cdevsw, device_get_unit(dev), UID_ROOT,
    GID_WHEEL, 0600, "myfirst%d", device_get_unit(dev));
if (sc->cdev == NULL) {
	error = ENXIO;
	goto fail_hw;
}
sc->cdev->si_drv1 = sc;
```

`device_get_unit(dev)` returns the unit number newbus assigned. `"myfirst%d"` with that unit number as the argument produces the per-instance device name. The `si_drv1` assignment lets the cdev's `open`, `close`, `read`, `write`, and `ioctl` entry points recover the softc from the cdev.

The detach path destroys the cdev with `destroy_dev(9)`:

```c
if (sc->cdev != NULL) {
	destroy_dev(sc->cdev);
	sc->cdev = NULL;
}
```

This code is entirely the Chapter 10 pattern; nothing about it is new. The point of including it here is that it fits naturally into the PCI attach ordering: softc, BAR, hardware layer, cdev, and (later in Chapter 19) interrupts. Reverse the order in detach. Done.

### A Full Stage 3 Attach

Combining every piece of Section 5, the Stage 3 attach:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error, capreg;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: walk PCI capabilities (informational). */
	if (pci_find_cap(dev, PCIY_EXPRESS, &capreg) == 0)
		device_printf(dev, "PCIe capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSI, &capreg) == 0)
		device_printf(dev, "MSI capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_MSIX, &capreg) == 0)
		device_printf(dev, "MSI-X capability at 0x%x\n", capreg);
	if (pci_find_cap(dev, PCIY_PMG, &capreg) == 0)
		device_printf(dev, "Power Management capability at 0x%x\n",
		    capreg);
	if (pci_find_extcap(dev, PCIZ_AER, &capreg) == 0)
		device_printf(dev, "PCIe AER extended capability at 0x%x\n",
		    capreg);

	/* Step 3: attach the hardware layer against the BAR. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release;

	/* Step 4: create the cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_hw;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 5: read a diagnostic word. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_hw:
	myfirst_hw_detach(sc);
fail_release:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

The structure is exactly the attach-time checklist from the start of this section, with labels (`Step 1`, `Step 2`, etc.) making the order explicit. The goto cascade handles partial failure cleanly. Each fail label undoes the step that succeeded most recently, chaining down to the one before it.

A reader who has seen this pattern before (in Chapter 15's complex attach with multiple primitives, or in any FreeBSD driver that allocates more than one resource) will recognise it immediately. A reader who is seeing it for the first time may benefit from tracing through a hypothetical failure at each step and verifying that the right amount of cleanup happens.

### Verifying Stage 3

The expected `dmesg` output at Stage 3 attach:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x98
myfirst0: MSI-X capability at 0xa0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=0x... handle=0x...)
myfirst0: BAR[0x00] = 0x10010000
```

The exact capability offsets depend on the guest's virtio implementation; the values shown are indicative. A reader who sees all four lines (attach, capabilities walked, hardware attached, BAR read) has confirmed that Stage 3 is complete.

`ls /dev/myfirst*` should show `/dev/myfirst0`. A user-space program that opens that device, writes a byte, and reads a byte should see the Chapter 17 simulation path in action (the command-response protocol still runs under the surface, even though the BAR is now real; Chapter 17 and Chapter 18 do not interact at the data-path level yet, they only share the accessor layer).

Detach verifies the reverse:

```text
myfirst0: detaching
```

And `/dev/myfirst0` disappears. The BAR is released. The softc is freed. No leaks, no warnings, no stuck state.

### Wrapping Up Section 5

Attach-time initialisation is the composition of many small steps. Each step allocates or configures one thing. The steps go in a strict order that builds the driver's state from the device outward: resources first, then features, then device-specific bring-up, then user-space interfaces, then (in Chapter 19) interrupts. The detach path undoes each step in reverse.

The PCI-specific pieces Chapter 18 adds to this pattern are `pci_enable_busmaster` (not needed for our driver, reserved for Chapters 20 and 21), the capability walkers `pci_find_cap(9)` and `pci_find_extcap(9)`, configuration-space reads and writes via `pci_read_config(9)` and `pci_write_config(9)`, and a brief introduction to PCIe AER that the reader will return to in later chapters.

Section 6 covers the detach side in depth. The broad strokes are familiar, but the details (handling partial attach failure, the order of teardown, interactions with callouts and tasks that might still be running) deserve their own section.



## Section 6: Supporting Detach and Resource Cleanup

Attach brings the driver up. Detach takes it down. The two paths are mirrors, but not perfectly symmetric mirrors. Detach has a few concerns attach does not: the possibility that other code is still running (callouts, tasks, file descriptors, interrupt handlers), the need to refuse detach when the driver has work the caller has not quiesced, and the care to avoid use-after-free between the last live access and the freeing of the softc. Section 6 is about getting those concerns right.

The goal of Section 6 is a detach routine that is strict, complete, and easy to audit. It releases every resource attach claimed. It drains every callout and task that might still be running. It destroys the cdev before freeing the hardware layer. It releases the BAR after the hardware layer no longer needs it. And it does all of this in a way that the book's readers can read and understand one step at a time.

### The Core Rule: Reverse Order

The single most important discipline for detach is reverse order. Every step attach took, detach undoes in the inverse sequence. If attach allocated A, then B, then C, then detach releases C, then B, then A.

This rule sounds trivial. In practice, forgetting it or getting the order slightly wrong is one of the most common causes of kernel panics in new drivers. The typical symptom: a callout fires during detach, reads from a softc field, and the field is already freed. Or: the cdev still exists when the BAR is released, and a user-space process that has the cdev open triggers a read that dereferences an unmapped address.

The Chapter 15 detach pattern is the right model for Chapter 18. Attach built state outward from the device; detach tears it down inward toward the device. Nothing the detach frees can still be in use by anything else.

### The Chapter 18 Detach Ordering

The detach order, matching Stage 3's attach:

1. Mark the driver as no longer attached (`sc->pci_attached = false`).
2. Cancel any user-space access paths: destroy the cdev so no new `open` or `ioctl` can start, accept no new requests.
3. Drain callouts and tasks that might be running (`myfirst_quiesce`).
4. Detach the simulation backend if it was attached (frees `sc->sim`). On the PCI path this is a no-op because the simulation was not attached.
5. Detach the hardware layer (frees `sc->hw`; does not release the BAR).
6. Release the BAR resource through `bus_release_resource`.
7. Tear down the softc state: destroy locks, destroy condition variables, free any allocated memory.
8. (Chapter 19's addition, mentioned for completeness) Release the IRQ resource.

For Chapter 18 the detach is:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Refuse detach if something is still using the device. */
	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Tear down the cdev so no new user-space accesses start. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Drain callouts and tasks. Safe whether or not the simulation
	 * was ever enabled on this instance. */
	myfirst_quiesce(sc);

	/* Release the simulation backend if it was attached. The PCI
	 * path leaves sc->sim == NULL, so this is a no-op. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer (frees the wrapper struct). */
	myfirst_hw_detach(sc);

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	/* Tear down the softc state. */
	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

The code is longer than the Stage 2 detach because each step is its own concern. The structure is easy to read: each line or block releases one thing, in reverse order of attach. A reader auditing the detach can check each step against the attach and confirm the symmetry.

### myfirst_is_busy: When to Refuse Detach

A driver that has an open cdev, an in-flight command, or any other in-progress work cannot safely detach. Returning `EBUSY` from detach tells the kernel's module loader to leave the driver alone.

The Chapter 10 through 15 driver had a simple busy check: are there any open file descriptors on the cdev? Chapter 17 extended it to include in-flight simulated commands. Chapter 18 reuses the same check:

```c
static bool
myfirst_is_busy(struct myfirst_softc *sc)
{
	bool busy;

	MYFIRST_LOCK(sc);
	busy = (sc->open_count > 0) || sc->command_in_flight;
	MYFIRST_UNLOCK(sc);
	return (busy);
}
```

The check is under the lock because `open_count` and `command_in_flight` can be modified by other code paths (the cdev's `open` and `close` entry points, the Chapter 17 command callout). Without the lock the check might see an inconsistent view, and the decision to refuse or allow detach would race against an in-progress open or close. The exact field names come from the Chapter 10 softc (`open_count`) and the Chapter 17 additions (`command_in_flight`); a reader whose softc uses different names substitutes the local names here.

Returning `EBUSY` from detach produces a visible error on `kldunload`:

```text
# kldunload myfirst
kldunload: can't unload file: Device busy
```

The user then closes the open file descriptor, cancels the in-flight command, or does whatever else is needed to drain the busy state, and retries. This is the expected behaviour; a driver that never refuses detach is a driver that can be torn out from under its users.

### Quiescing Callouts and Tasks

The Chapter 17 simulation runs a sensor callout every second; a command callout fires per command; a busy-recovery callout fires occasionally. The Chapter 16 hardware layer runs a ticker task through a taskqueue. On the PCI backend the simulation callouts are not enabled (as Section 5 explained), so their `callout_drain` is a safe no-op if they never ran. The hardware-layer ticker task is still active and must be drained.

The right primitive for callouts is `callout_drain(9)`. It waits until the callout is not running and prevents any future firings. The right primitive for tasks is `taskqueue_drain(9)`. It waits until the task has finished running and prevents any further enqueues.

The Chapter 17 API exposes two functions that wrap the callout lifecycle for the simulation: `myfirst_sim_disable(sc)` stops scheduling new firings (it requires `sc->mtx` held), and `myfirst_sim_detach(sc)` drains every callout and frees the simulation state (it must not hold `sc->mtx`). A single `myfirst_quiesce` helper in the PCI driver composes them safely:

```c
static void
myfirst_quiesce(struct myfirst_softc *sc)
{
	if (sc->sim != NULL) {
		MYFIRST_LOCK(sc);
		myfirst_sim_disable(sc);
		MYFIRST_UNLOCK(sc);
	}

	if (sc->tq != NULL && sc->hw != NULL)
		taskqueue_drain(sc->tq, &sc->hw->reg_ticker_task);
}
```

On the PCI path `sc->sim` is NULL (the simulation backend is not attached), so the first block is skipped entirely. On a simulation-only build where the simulation is attached, `myfirst_sim_disable` stops the callouts under the lock, and the subsequent `myfirst_sim_detach` (called later in the detach sequence) drains them without the lock.

The split matters because `callout_drain` must be called **without** `sc->mtx` held: the callout body itself may try to acquire the mutex, and holding it would deadlock. Chapter 13 taught this discipline; Chapter 18 respects it by routing the drain through `myfirst_sim_detach`, which acquires no locks.

After `myfirst_quiesce` returns, nothing else is running against the softc except the detach path itself. The subsequent teardown steps can touch `sc->hw` and the BAR without fear.

### Releasing the BAR After the Hardware Layer

The order matters. `myfirst_hw_detach` is called before `bus_release_resource` because `myfirst_hw_detach` still needs the tag and handle to be valid (for example, if there were any last-chance reads during the hardware teardown; the Chapter 18 version does not do such reads, but defensive code keeps the order in case a later extension adds them).

After `myfirst_hw_detach` returns, `sc->hw` is NULL. The tag and handle stored in the (now-freed) `myfirst_hw` struct are gone. No code in the driver can read or write the BAR from this point onward. The BAR can then be released safely.

If the order were reversed (release BAR first, then `myfirst_hw_detach`), the hardware teardown code would hold a stale tag and handle; any access would be a use-after-free. On x86 the bug might be silent; on architectures with stricter memory permissions the access would page-fault.

### Failures During Detach

Unlike attach, detach is generally expected to succeed. The kernel's unload path calls detach; if detach returns non-zero, the unload aborts, but detach itself should not leave resources in an inconsistent state. The convention is that detach returns 0 (success) or `EBUSY` (refuse the unload because the driver is in use). Returning any other error is unusual and typically indicates a driver bug.

If a resource release fails (for example, `bus_release_resource` returns an error), the driver should log the failure but continue the detach. Leaving a partially-freed state is worse than logging and continuing; the kernel will complain about the leaked resource at shutdown, but the driver will not have crashed. Chapter 18's driver does not check the return value of `bus_release_resource` for this reason; the release either succeeds or leaves an unrecoverable kernel state, neither of which the driver can do anything about.

### Detach vs Module Unload vs Device Removal

Three different events can trigger detach.

**Module unload** (`kldunload myfirst`): the user asks to remove the module. The kernel's unload path calls detach on every device bound to the module, one at a time. If every detach returns 0, the module is unloaded. If any detach returns non-zero, the module stays loaded and the unload returns an error.

**Device removal by user** (`devctl detach myfirst0`): the user asks to detach a specific device from its driver, without unloading the module. The driver's detach runs for that one device; the module stays loaded and can still attach to other devices.

**Device removal by hardware** (hotplug, such as removing a PCIe card from a hot-plug-capable slot, or the hypervisor removing a virtual device): the PCI bus detects the change and calls detach on the device. The driver's detach runs. If the device is later re-inserted, the driver's probe and attach run again.

All three paths run the same `myfirst_pci_detach` function. The driver does not need to distinguish them. The code is the same because the obligations are the same: release everything attach allocated.

### Partial-Attach Failure and the Detach Path

A subtle case worth explaining. If attach fails partway through and returns an error, the kernel does not (in modern newbus) call detach on the partially-attached driver. The driver's own goto cascade handles the cleanup.

The attach code from Section 5 has a goto cascade that undoes exactly the steps that succeeded. If the cdev creation fails after the hardware layer attach, the cascade releases the hardware layer and the BAR before returning. If the hardware layer attach fails after the BAR allocation, the cascade releases the BAR before returning. Each fail label undoes one step.

A common beginner mistake is to write a goto cascade that skips steps. For example:

```c
fail_hw:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

This skips the `myfirst_hw_detach` step. If the hardware layer attach succeeded but the cdev creation failed, the cascade frees the BAR without tearing down the hardware layer, leaking the hardware layer's wrapper struct. The right cascade calls every unwind step the successful attach would have needed to unwind.

A technique some drivers use: arrange the attach as a sequence of `myfirst_init_*` helpers and the detach as a matching sequence of `myfirst_uninit_*` helpers, and have a single `myfirst_fail` function that walks the unwind list based on how far attach got. This is cleaner for very complex drivers; for Chapter 18's driver the goto cascade is simpler and more readable.

### A Concrete Walk Through the Cascade

Let's trace what happens if the cdev creation fails at Stage 3. The attach has:

1. Initialised the softc (success).
2. Allocated BAR0 (success).
3. Walked capabilities (always succeeds; it's just reads).
4. Attached the hardware layer (success).
5. Tried to create the cdev: fails because of some error (disk full? unlikely; in tests the reader can simulate this by returning NULL from a mocked `make_dev`).

The cascade runs `fail_hw`, which calls `myfirst_hw_detach` (undoes step 4), then `fail_release`, which releases the BAR (undoes step 2), then `fail_softc`, which deinit the softc (undoes step 1). Step 3's "undo" is the empty action (capability walks allocate nothing). The attach returns the error.

If the reader traces this by hand, the cleanup is clearly complete: the softc is deinit, the BAR is released, the hardware layer is detached. No leaks. No partial state. The test is the same as the test for a full attach followed by a full detach: `vmstat -m | grep myfirst` should show zero allocations after the failed attach returns.

### Detach vs Resume: A Preview

For completeness: the suspend-and-resume paths, which Chapter 18 does not implement, look similar to detach-and-attach but preserve more state. Suspend quiesces the driver (drains callouts, stops user-space access), records the device's state in the softc, and lets the system power down. Resume re-initialises the device from the saved state, restarts callouts, re-enables user-space access, and returns.

A driver that implements only attach and detach cannot suspend cleanly; the kernel will refuse to suspend a system with a non-suspend-aware driver attached. The `myfirst` driver is small enough that Chapter 18 does not worry about this; Chapter 22 on power management revisits the topic.

### Wrapping Up Section 6

Detach is attach, played in reverse, with a check for `EBUSY` at the top and a quiesce step before any teardown. The rule is simple; the discipline is in doing it consistently. Every resource attach allocates, detach releases. Every state attach sets up, detach tears down. Every callout attach starts, detach drains. The order is the reverse of attach.

For Chapter 18's driver, the detach at Stage 3 is six steps: refuse if busy, destroy the cdev, quiesce the simulation and hardware callouts, detach the hardware layer, release the BAR, deinit the softc. Each step is its own concern. Each step is auditable against the attach. Each step is testable in isolation.

Section 7 is the testing section. It stands up the bhyve or QEMU lab, walks through the attach-detach cycle on real PCI silicon-that-is-emulated, and teaches the reader to verify the driver's behaviour with `pciconf`, `devinfo`, `dmesg`, and a couple of small user-space programs.



## Section 7: Testing PCI Driver Behaviour

A driver exists to talk to a device. Chapter 18's driver is written and compiled, but it has not yet run against any device. Section 7 closes the loop. It walks the reader through standing up a FreeBSD guest in bhyve or QEMU, exposing a virtio-rnd PCI device to the guest, loading the driver, observing the full attach-operate-detach-unload cycle, exercising the cdev from user space, reading and writing configuration space with `pciconf -r` and `pciconf -w`, and confirming through `devinfo -v` and `dmesg` that the driver's view of the world matches the kernel's.

Testing is where all of Chapters 2 through 17 finally pay off. Every habit the book has built (typing code by hand, reading the FreeBSD source, running regressions after every stage, keeping a lab logbook) serves Chapter 18's testing discipline. The section is long because real PCI testing has real moving parts; each part is worth walking through carefully.

### The Test Environment

The canonical test environment for Chapter 18 is a FreeBSD 14.3 guest running under `bhyve(8)` on a FreeBSD 14.3 host. The guest receives an emulated virtio-rnd device through bhyve's `virtio-rnd` passthrough. The guest runs the reader's debug kernel. The `myfirst` driver is compiled and loaded inside the guest; it attaches to the virtio-rnd device, and the reader exercises it from inside the guest.

An equivalent environment uses `qemu-system-x86_64` on Linux or macOS as the host, with a FreeBSD 14.3 guest running a debug kernel. QEMU's `-device virtio-rng-pci` does the same job as bhyve's virtio-rnd. Everything else is identical.

The rest of this section assumes bhyve unless noted otherwise. Readers on QEMU substitute the equivalent commands; the concepts transfer directly.

### Preparing the bhyve Guest

The author's lab script looks roughly like this, edited for clarity:

```sh
#!/bin/sh
set -eu

# Load bhyve's kernel modules.
kldload -n vmm nmdm if_bridge if_tap

# Prepare a network bridge.
# ifconfig bridge0 create 2>/dev/null || true
# ifconfig bridge0 addm em0 addm tap0
# ifconfig tap0 up
# ifconfig bridge0 up

# Launch the guest.
bhyve -c 2 -m 2048 -H -w \
    -s 0:0,hostbridge \
    -s 1:0,lpc \
    -s 2:0,virtio-net,tap0 \
    -s 3:0,virtio-blk,/dev/zvol/zroot/vm/freebsd143/disk0 \
    -s 4:0,virtio-rnd \
    -l com1,/dev/nmdm0A \
    -l bootrom,/usr/local/share/uefi-firmware/BHYVE_UEFI.fd \
    vm:fbsd-14.3-lab
```

The key line is `-s 4:0,virtio-rnd`. It attaches a virtio-rnd device on PCI slot 4, function 0. The guest's PCI enumerator will see a device at `pci0:0:4:0` with vendor 0x1af4 and device 0x1005, which is exactly the ID pair the Chapter 18 driver's probe table matches.

Other slots carry the hostbridge, LPC, network (tap-bridged), and storage (zvol-backed block). The overall guest has everything it needs to boot and run multiuser, plus one PCI device for our driver.

A shorter form for readers who prefer `vm(8)` (the FreeBSD utility from the `vm-bhyve` port):

```sh
vm create -t freebsd-14.3 fbsd-lab
vm configure fbsd-lab  # edit vm.conf and add:
#   passthru0="0/0/0"        # if using passthrough, not needed here
#   virtio_rnd="1"            # add a virtio-rnd device
vm start fbsd-lab
```

`vm-bhyve` hides the detail of the bhyve command line. Either form produces an equivalent lab environment.

Readers on QEMU use:

```sh
qemu-system-x86_64 -cpu host -m 2048 -smp 2 \
    -drive file=freebsd-14.3-lab.img,if=virtio \
    -netdev tap,id=net0,ifname=tap0 -device virtio-net,netdev=net0 \
    -device virtio-rng-pci \
    -bios /usr/share/qemu/OVMF_CODE.fd \
    -serial stdio
```

The `-device virtio-rng-pci` line does the bhyve-equivalent job.

### Verifying the Guest Sees the Device

Inside the guest, after the first boot, the virtio-rnd device should be visible:

```sh
pciconf -lv
```

Look for an entry like:

```text
virtio_random0@pci0:0:4:0: class=0x00ff00 rev=0x00 hdr=0x00 vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004
    vendor     = 'Red Hat, Inc.'
    device     = 'Virtio entropy'
    class      = old
```

The entry tells you three things. First, the guest's PCI enumerator found the device. Second, the base-system `virtio_random(4)` driver has claimed it (the leading name `virtio_random0` is the clue). Third, the B:D:F is `0:0:4:0`, matching the bhyve `-s 4:0,virtio-rnd` configuration.

If the entry is missing, either the bhyve command line did not include `virtio-rnd` or the guest booted without loading `virtio_pci.ko`. Both are fixable: review the bhyve command, reboot the guest, or `kldload virtio_pci` by hand.

### Preparing the Guest for myfirst

On a stock FreeBSD 14.3 `GENERIC` kernel, `virtio_random` is not compiled in; it ships as a loadable module (`virtio_random.ko`). Whether it has claimed the device by the time you want to load `myfirst` depends on the platform. On a modern system, `devmatch(8)` may auto-load `virtio_random.ko` shortly after boot when it sees a matching PCI device. On a freshly booted guest where `devmatch` has not fired yet, the virtio-rnd device may still be unclaimed.

Check first:

```sh
kldstat | grep virtio_random
pciconf -lv | grep -B 1 virtio_random
```

If neither command shows `virtio_random`, the device is unclaimed and you can skip the next step.

If `virtio_random` has claimed the device, unload it:

```sh
sudo kldunload virtio_random
```

If the module is unloadable (not pinned, not in use), this succeeds and the virtio-rnd device becomes unclaimed. `devinfo -v` now shows it under `pci0` with no driver binding.

If you want a stable setup that never auto-loads `virtio_random` across reboots, add to `/boot/loader.conf`:

```text
hint.virtio_random.0.disabled="1"
```

This prevents the binding at boot time without removing the module image from the system. Alternatively, adding an entry under `/etc/devd.conf` or `devmatch.blocklist` (or using the `dev.virtio_random.0.%driver` sysctl at runtime) blocks the driver from attaching. For Chapter 18's teaching path, a simple `kldunload` once per test session is enough.

Chapter 18's testing uses the first approach during development (quick iteration) and the second approach when the reader wants a stable setup for repeated tests.

A third approach worth mentioning: the kernel's `dev.NAME.UNIT.%parent` and `dev.NAME.UNIT.%driver` sysctls describe bindings but do not change them. To force a rebind, use `devctl detach` and `devctl set driver`:

```sh
sudo devctl detach virtio_random0
sudo devctl set driver -f pci0:0:4:0 myfirst
```

The `-f` flag forces the set even if another driver has claimed the device. This is the precise command to use in a scripted test where the reader wants to switch drivers without reloading modules.

### Loading myfirst and Watching the Attach

With `virtio_random` out of the way, load `myfirst`:

```sh
sudo kldload ./myfirst.ko
```

Watch `dmesg` for the attach:

```sh
sudo dmesg | tail -20
```

The expected output (for Stage 3):

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> mem 0xc1000000-0xc100001f at device 4.0 on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0:            subvendor=0x1af4 subdevice=0x0004 class=0xff
myfirst0: PCIe capability at 0x0
myfirst0: MSI-X capability at 0x0
myfirst0: hardware layer attached to BAR: 32 bytes (tag=... handle=...)
myfirst0: BAR[0x00] = 0x10010000
```

(The capability offsets for the virtio legacy device are 0 because the bhyve emulation does not expose a PCIe capability; a reader who tests with QEMU's virtio-rng-pci may see non-zero offsets.)

The cdev `/dev/myfirst0` exists:

```sh
ls -l /dev/myfirst*
```

and `devinfo -v` shows the device:

```sh
devinfo -v | grep -B 1 -A 4 myfirst
```

```text
pci0
    myfirst0
        pnpinfo vendor=0x1af4 device=0x1005 ...
        resources:
            memory: 0xc1000000-0xc100001f
```

This is the driver attached to the device, visible to user space, ready to be exercised.

### Exercising the cdev

The `myfirst` driver's cdev path is the Chapter 10 through 17 interface. It accepts `open`, `close`, `read`, and `write` system calls. In the Chapter 17 simulation-only build, reads pulled from the command-response ring buffer that the simulation's callouts populated. In the Chapter 18 PCI build the simulation is not attached; the cdev still responds to `open`, `read`, `write`, and `close`, but the data path has no active callouts feeding it. Reads return what the underlying Chapter 10 circular buffer contains (typically empty at start); writes queue data into the same buffer.

This is expected behaviour for Chapter 18. The point of the chapter's tests is to prove that the driver attaches to a real PCI device, the BAR is live, the cdev is reachable from user space, and detach cleans up properly. Chapter 19 adds the interrupt path that will make the cdev's data path meaningful against a real device.

A small user-space program to exercise the cdev:

```c
/* Minimal read-write test. */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/myfirst0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    char buf[16];
    ssize_t n = read(fd, buf, sizeof(buf));
    printf("read returned %zd\n", n);

    close(fd);
    return 0;
}
```

Compile and run:

```sh
cc -o myfirst_test myfirst_test.c
./myfirst_test
```

The output may be a short read, a zero read, or `EAGAIN` (depending on whether the Chapter 10 buffer has any data ready). What matters is that the read path does not crash the kernel, does not produce an error in `dmesg`, and returns to user space with a defined result.

A write test is equally straightforward:

```c
char cmd[16] = "hello\n";
write(fd, cmd, 6);
```

Writes push data into the Chapter 10 circular buffer. On the PCI backend, the data sits in the buffer until a reader pulls it out; no simulation callout is running to process it. The test is that the cycle runs without crashing and that `dmesg` stays quiet.

### Reading and Writing Configuration Space from User Space

`pciconf(8)` has two flags that let user-space programs inspect and modify PCI configuration space directly:

- `pciconf -r <selector> <offset>:<length>` reads configuration-space bytes and prints them in hex.
- `pciconf -w <selector> <offset> <value>` writes a value to a specific offset.

The selector identifies the device. It can be the device's driver name (`myfirst0`) or its B:D:F (`pci0:0:4:0`).

A read example:

```sh
sudo pciconf -r myfirst0 0x00:8
```

Output:

```text
00: 1a f4 05 10 07 05 10 00
```

The bytes are the first eight bytes of configuration space, in order: vendor ID (`1af4`, little-endian), device ID (`1005`), command register (`0507`, with `MEMEN` and `BUSMASTER` set), status register (`0010`).

A write example (dangerous, do not do this casually):

```sh
sudo pciconf -w myfirst0 0x04 0x0503
```

This clears `BUSMASTER` and leaves `MEMEN` set. The effect on the device depends on the device; on a running device it may cause DMA operations to fail. For a device the driver does not use DMA against (Chapter 18's case), the change is essentially harmless but also essentially meaningless.

A reader should use `pciconf -w` only in deliberate diagnostic scenarios, with full knowledge of the consequences. Writing a garbage value to the wrong field can deadlock the device, the bus, or the kernel.

### devinfo -v and What It Tells You

`devinfo -v` is the newbus tree inspector. It walks every device in the system and prints each one with its resources, its parent, its unit, and its children. For a driver author, it is the canonical reference for "what does the kernel think the driver owns".

A fragment of output, for the `myfirst0` device:

```text
nexus0
  acpi0
    pcib0
      pci0
        myfirst0
            pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x00ff00
            resources:
                memory: 0xc1000000-0xc100001f
        virtio_pci0
            pnpinfo vendor=0x1af4 device=0x1000 ...
            resources: ...
        ... (other pci children)
```

The tree shows the path from the root (nexus) down through the platform (ACPI on x86), the PCI bridge (pcib), the PCI bus (pci0), and finally the devices on that bus. `myfirst0` is a child of `pci0`. Its resource list shows the claimed memory BAR.

Using `devinfo -v | grep -B 1 -A 5 myfirst` to extract just the relevant block is the standard technique when the tree is large.

### dmesg as a Diagnostic Tool

`dmesg` is the kernel's message buffer. Every `device_printf`, `printf`, and `KASSERT` failure in the kernel appears in `dmesg`. For a driver author, it is the primary debugging surface.

Tailing `dmesg` while loading, operating, and unloading the driver is how you catch subtle problems early. A typical session:

```sh
# Start a dmesg tail in a second terminal.
dmesg -w
```

Then, in the primary terminal:

```sh
sudo kldload ./myfirst.ko
```

The tailing terminal shows the attach messages as they happen. Run your tests:

```sh
./myfirst_test
```

The tailing terminal shows any messages the driver emits during the test. Unload:

```sh
sudo kldunload myfirst
```

The tailing terminal shows the detach messages.

If any step produces an unexpected warning or error, you see it in real time. Without a tailing `dmesg`, you might miss a single warning that indicates a latent problem.

### Using devctl to Simulate Hotplug

`devctl(8)` lets a user-space program simulate the newbus events that a real hotplug or device-removal would generate. The common invocations:

```sh
# Force a device to detach (calls the driver's detach method).
sudo devctl detach myfirst0

# Re-attach the device (calls the driver's probe and attach).
sudo devctl attach myfirst0

# Disable a device (prevent future probes from binding).
sudo devctl disable myfirst0

# Re-enable a disabled device.
sudo devctl enable myfirst0

# Rescan a bus (equivalent to a hotplug notification).
sudo devctl rescan pci0
```

For testing Chapter 18's detach path, `devctl detach myfirst0` is the primary tool. It exercises the detach code without unloading the module. The driver's detach runs; the cdev disappears; the BAR is released; the device goes back to unclaimed.

A subsequent `devctl attach` re-triggers probe and attach. If the probe succeeds (the vendor and device IDs still match) and attach succeeds, the device is bound again. This is the cycle a reader uses to test that the driver can attach, detach, and reattach without leaking resources.

Running this cycle in a loop is the standard regression pattern:

```sh
for i in 1 2 3 4 5; do
    sudo devctl detach myfirst0
    sudo devctl attach myfirst0
done
sudo vmstat -m | grep myfirst
```

If `vmstat -m` shows the `myfirst` malloc type with zero current allocations after the loop, the driver is clean: every attach allocated, every detach freed, and the totals balance.

### A Simple Regression Script

Putting it all together, a script that verifies Stage 3's PCI path:

```sh
#!/bin/sh
#
# Chapter 18 Stage 3 regression test.
# Run inside a bhyve guest that exposes a virtio-rnd device.

set -eu

echo "=== Unloading virtio_random if present ==="
kldstat | grep -q virtio_random && kldunload virtio_random || true

echo "=== Loading myfirst ==="
kldload ./myfirst.ko
sleep 1

echo "=== Checking attach ==="
devinfo -v | grep -q 'myfirst0' || { echo FAIL: no attach; exit 1; }

echo "=== Checking BAR claim ==="
devinfo -v | grep -A 3 'myfirst0' | grep -q 'memory:' || \
    { echo FAIL: no BAR; exit 1; }

echo "=== Exercising cdev ==="
./myfirst_test
sleep 1

echo "=== Detach-attach cycle ==="
for i in 1 2 3; do
    devctl detach myfirst0
    sleep 0.5
    devctl attach pci0:0:4:0
    sleep 0.5
done

echo "=== Unloading myfirst ==="
kldunload myfirst
sleep 1

echo "=== Checking for leaks ==="
vmstat -m | grep -q myfirst && echo WARN: myfirst malloc type still present

echo "=== Success ==="
```

The script follows a repeatable pattern: set up, load, check attach, check resources, exercise, cycle, unload, check for leaks. Running this after every change to the driver is how you catch regressions early.

### Reading Configuration Space for Diagnostic Purposes

A small worked example of using `pciconf -r` to verify that the driver's view of configuration space matches user space's view.

Inside the driver, the attach path reads the vendor ID via `pci_get_vendor` and the device ID via `pci_get_device`. User space reads the same bytes via `pciconf -r myfirst0 0x00:4`.

Expected output:

```text
00: f4 1a 05 10
```

The bytes are the vendor ID (`0x1af4`) and the device ID (`0x1005`), in little-endian order. Reversing the bytes gives `0x1af4` for vendor and `0x1005` for device, matching the driver's probe table.

Doing this check is not something you would do in production; the PCI subsystem is well-tested and the values are reliable. It is useful as a learning exercise: it proves that the driver's view of configuration space matches what user space sees, and it cements the reader's understanding of how `pci_get_vendor` relates to the underlying bytes.

### What Section 7 Does Not Test

Section 7 verifies that the Chapter 18 driver attaches to a real PCI device, claims the BAR, exposes the cdev, reads the BAR through the Chapter 16 accessor layer, detaches cleanly, releases the BAR, and unloads without leaks. It does not test:

- Interrupt handling. The driver does not register an interrupt; Chapter 19 does.
- MSI or MSI-X. Chapter 20 does.
- DMA. Chapter 21 does.
- Device-specific protocol. The Chapter 17 simulation's command-response protocol is not the virtio-rnd protocol, so the results of a write are not meaningful. Chapter 18's driver is not a virtio-rnd driver.

A reader who wants a driver that actually implements the virtio-rnd protocol should read `/usr/src/sys/dev/virtio/random/virtio_random.c`. It is a clean, focused driver that a reader who has finished Chapter 18 should be able to follow.

### Wrapping Up Section 7

Testing a PCI driver means standing up an environment where the driver can meet a device. For Chapter 18 that environment is a bhyve or QEMU guest with a virtio-rnd device exposed to the guest. The tools are `pciconf -lv` (to see the device), `kldload` and `kldunload` (to load and unload the driver), `devinfo -v` (to see the newbus tree and the driver's resources), `devctl` (to simulate hotplug), `dmesg` (to watch diagnostic messages), and `vmstat -m` (to check for leaks). The discipline is to run a repeatable script after every change, inspect its output, and fix any warnings or failures before moving on.

The regression script at the end of this section is the template every reader should adapt to their own driver and their own lab. Running it ten times in a row and seeing identical output each time is the proof that the driver is solid. Running it once and seeing a crash is a sign the driver has a bug that Chapter 18's discipline (attach order, detach order, resource pairing) should have prevented; the fix is usually small.

Section 8 is the final section of the instructional body. It refactors the Chapter 18 code into its final shape, bumps the version to `1.1-pci`, writes the new `PCI.md`, and prepares the ground for Chapter 19.



## Section 8: Refactoring and Versioning Your PCI Driver

The PCI driver is now working. Section 8 is the housekeeping section. It consolidates the Chapter 18 code into a clean, maintainable structure, updates the driver's `Makefile` and module metadata, writes the `PCI.md` document that will live alongside `LOCKING.md`, `HARDWARE.md`, and `SIMULATION.md`, bumps the version to `1.1-pci`, and runs the full regression pass against both the simulation and the real PCI backend.

A reader who has made it this far may be tempted to skip Section 8. It is boring compared to the earlier sections. It does not introduce any new PCI concepts. The temptation is real, and it is a mistake. The refactor is what turns a working driver into a maintainable driver. A driver that works today but is organised badly will be painful to extend in Chapter 19 (when interrupts arrive), Chapter 20 (when MSI and MSI-X arrive), Chapters 20 and 21 (when DMA arrives), and every later chapter. The few lines of housekeeping Section 8 does pay dividends across the rest of Part 4 and beyond.

### The Final File Layout

At the end of Chapter 18, the `myfirst` driver consists of these files:

```text
myfirst.c       - Main driver: softc, cdev, module events, data path.
myfirst.h       - Shared declarations: softc, lock macros, prototypes.
myfirst_hw.c    - Chapter 16 hardware access layer: CSR_* accessors,
                   access log, sysctl handlers.
myfirst_hw.h    - Chapter 16 register map and accessor declarations,
                   extended in Chapter 17.
myfirst_sim.c   - Chapter 17 simulation backend: callouts, fault
                   injection, command-response.
myfirst_sim.h   - Chapter 17 simulation interface.
myfirst_pci.c   - Chapter 18 PCI attach: probe, attach, detach,
                   DRIVER_MODULE, MODULE_DEPEND.
myfirst_pci.h   - Chapter 18 PCI declarations: ID table entry struct,
                   vendor and device ID constants.
myfirst_sync.h  - Part 3 synchronisation primitives.
cbuf.c / cbuf.h - The Chapter 10 circular buffer, still in use.
Makefile        - kmod build: KMOD, SRCS, CFLAGS.
HARDWARE.md     - Chapter 16/17 documentation of the register map.
LOCKING.md      - Chapter 15 onward documentation of lock discipline.
SIMULATION.md   - Chapter 17 documentation of the simulation backend.
PCI.md          - Chapter 18 documentation of PCI support.
```

The split is the same split Chapter 17 anticipated. `myfirst_pci.c` and `myfirst_pci.h` are new. Every other file existed before Chapter 18 and has been either extended (`myfirst_hw.c` gained `myfirst_hw_attach_pci`) or left unchanged. The driver's main file (`myfirst.c`) grew by a few lines to add the PCI-related softc fields and a call to the PCI-specific detach helper; it did not grow substantially.

A rule of thumb worth stating: each file should have one responsibility. `myfirst.c` is the driver's integration point; it ties every piece together. `myfirst_hw.c` is about hardware access. `myfirst_sim.c` is about simulating the hardware. `myfirst_pci.c` is about attaching to real PCI hardware. When a reader opens a file, they should be able to predict, from the file name, what is in it. When Chapter 19 adds `myfirst_intr.c`, the prediction will hold: that file is about interrupts.

### The Final Makefile

```makefile
# Makefile for the Chapter 18 myfirst driver.
#
# Combines the Chapter 10-15 driver, the Chapter 16 hardware layer,
# the Chapter 17 simulation backend, and the Chapter 18 PCI attach.
# The driver is loadable as a standalone kernel module via
# kldload(8); when loaded, it attaches automatically to any PCI
# device whose vendor/device ID matches an entry in
# myfirst_pci_ids[] (see myfirst_pci.c).

KMOD=  myfirst
SRCS=  myfirst.c myfirst_hw.c myfirst_sim.c myfirst_pci.c cbuf.c

# Version string. Update this line alongside any user-visible change.
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.1-pci\"

# Optional: build without PCI support (simulation only).
# CFLAGS+= -DMYFIRST_SIMULATION_ONLY

# Optional: build without simulation fallback (PCI only).
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

Four SRCS, one version string, two commented-out compile options. The build is one command:

```sh
make
```

The output is `myfirst.ko`, loadable into any FreeBSD 14.3 kernel with `kldload`.

### The Version String

The version string moves from `1.0-simulated` to `1.1-pci`. The bump reflects that the driver has acquired a new capability (real PCI support) without changing any user-visible behaviour (the cdev still does what it did). A minor-version bump is appropriate; a major-version bump would imply an incompatible change.

Later chapters will continue the numbering: `1.2-intr` after Chapter 19, `1.3-msi` after Chapter 20, `1.4-dma` after Chapters 20 and 21, and so on. By the end of Part 4 the driver will be at `1.4-dma` or so, with each minor version reflecting one significant capability addition.

The version string is visible in two places: `kldstat -v` shows it, and the driver's `dmesg` banner on load prints it. A user or system administrator who wants to know which version of the driver they have running can grep `dmesg` for the banner.

### The PCI.md Document

A new document joins the driver's corpus. `PCI.md` is short; its job is to describe the PCI support the driver provides, in a form a future reader can consult without reading the source.

```markdown
# PCI Support in the myfirst Driver

## Supported Devices

As of version 1.1-pci, myfirst attaches to PCI devices matching
the following vendor/device ID pairs:

| Vendor | Device | Description                                    |
| ------ | ------ | ---------------------------------------------- |
| 0x1af4 | 0x1005 | Red Hat/virtio-rnd (demo target; see README)   |

This list is maintained in `myfirst_pci.c` in the static array
`myfirst_pci_ids[]`. Adding a new supported device requires:

1. Adding an entry to `myfirst_pci_ids[]` with the vendor and
   device IDs and a human-readable description.
2. Verifying that the driver's BAR layout and register map are
   compatible with the new device.
3. Testing the driver against the new device.
4. Updating this document.

## Attach Behaviour

The driver's probe routine returns `BUS_PROBE_DEFAULT` on a match
and `ENXIO` otherwise. Attach allocates BAR0 as a memory resource,
walks the PCI capability list (Power Management, MSI, MSI-X, PCIe,
PCIe AER if present), attaches the Chapter 16 hardware layer
against the BAR, and creates `/dev/myfirstN`. The Chapter 17
simulation backend is NOT attached on the PCI path; the driver's
accessors read and write the real BAR without the simulation
callouts running.

## Detach Behaviour

Detach refuses to proceed if the driver has open file descriptors
or in-flight commands (returns `EBUSY`). Otherwise it destroys the
cdev, drains any active callouts and tasks, detaches the hardware
layer, releases the BAR, and deinit the softc.

## Module Dependencies

The driver's `MODULE_DEPEND` declarations:

- `pci`, version 1: the kernel's PCI subsystem.

No other module dependencies are declared.

## Known Limitations

- The driver does not currently handle interrupts. See Chapter 19
  for the interrupt-handling extension.
- The driver does not currently support DMA. See Chapters 20 and 21
  for the DMA extension.
- The Chapter 17 simulation backend is not attached on the PCI
  path. The simulation's callouts and command protocol remain
  available in a simulation-only build (`-DMYFIRST_SIMULATION_ONLY`)
  for readers without matching PCI hardware.

## See Also

- `HARDWARE.md` for the register map.
- `SIMULATION.md` for the simulation backend.
- `LOCKING.md` for the lock discipline.
- `README.md` for how to set up the bhyve test environment.
```

This document lives next to the driver source. A future reader (the author themselves, three months from now, or a contributor, or a port maintainer) can read it in five minutes and understand the driver's PCI story without opening the code.

### Updating LOCKING.md

`LOCKING.md` already documents the Chapter 11 through 17 lock discipline. Chapter 18 adds two small items:

1. The detach order: new steps for `destroy_dev`, quiescing callouts, `myfirst_hw_detach`, `bus_release_resource`, and `myfirst_deinit_softc`, in that order.
2. The attach failure cascade: the goto labels (`fail_hw`, `fail_release`, `fail_softc`) and what each one undoes.

The update is a handful of lines in the existing document. No new lock is introduced in Chapter 18; the Chapter 15 lock hierarchy is unchanged.

### Updating HARDWARE.md

`HARDWARE.md` already documents the Chapter 16 and Chapter 17 register map. Chapter 18 adds one small item:

- The BAR the driver attaches to is BAR 0, requested with `rid = PCIR_BAR(0)`, allocated as `SYS_RES_MEMORY` with `RF_ACTIVE`. The tag and handle are extracted with `rman_get_bustag(9)` and `rman_get_bushandle(9)`.

That is the entire addition. The register map itself does not change in Chapter 18; the same offsets, the same widths, the same bit definitions.

### The Regression Pass

With the refactor complete, the full regression pass for Chapter 18 is:

1. **Compile cleanly.** `make` produces `myfirst.ko` without warnings. The CFLAGS already include `-Wall -Werror` from Chapter 4 onward; the build fails if any warning appears.
2. **Load without errors.** `kldload ./myfirst.ko` succeeds and `dmesg` shows the module-level banner.
3. **Attach to a real PCI device.** In a bhyve guest with a virtio-rnd device, the driver attaches and `dmesg` shows the full Chapter 18 attach sequence.
4. **Create and exercise the cdev.** `/dev/myfirst0` exists, `open` / `read` / `write` / `close` work, no kernel messages indicate errors.
5. **Walk capabilities.** `dmesg` shows the capability offsets for any capabilities the guest's virtio-rnd exposes.
6. **Read configuration space from user space.** `pciconf -r myfirst0 0x00:8` produces the expected bytes.
7. **Detach cleanly.** `devctl detach myfirst0` produces the detach banner in `dmesg`; the cdev disappears; `vmstat -m | grep myfirst` shows zero live allocations.
8. **Reattach cleanly.** `devctl attach pci0:0:4:0` re-triggers probe and attach; the full cycle runs again.
9. **Unload cleanly.** `kldunload myfirst` succeeds; `kldstat -v | grep myfirst` returns nothing.
10. **No leaks.** `vmstat -m | grep myfirst` returns nothing.

The regression script from Section 7 runs steps 1 through 10 in sequence and reports success or the first failure. Running it after every change is the discipline that catches regressions early.

### What the Refactor Accomplished

At the start of Chapter 18 the `myfirst` driver was a simulation. It had a `malloc(9)`-backed register block, a simulation backend, and an elaborate testing harness. It did not attach to real hardware; it was a module loaded by hand.

At the end of Chapter 18 the driver is a PCI driver. It attaches to a real PCI device when one is present. It claims the device's BAR through the standard FreeBSD bus-allocation API. It uses the Chapter 16 accessor layer to read and write the device's registers through `bus_space(9)`. The Chapter 17 simulation remains available through a compile-time switch (`-DMYFIRST_SIMULATION_ONLY`) for readers without matching PCI hardware, but the default build targets the PCI path and leaves the simulation callouts idle. The attach and detach paths follow the newbus conventions every other FreeBSD driver uses.

The code is recognisably FreeBSD. The layout is the layout real drivers use when they have distinct simulation, hardware, and bus responsibilities. The vocabulary is the vocabulary real drivers share. A contributor opening the driver for the first time finds a familiar structure, reads the documentation, and can navigate the code by subsystem.

### A Short Note on Symbol Visibility

A reader who diffs the Chapter 17 driver against the Chapter 18 driver will notice that several functions have changed visibility. Some that were `static` in Chapter 17 are now exported (non-static) because `myfirst_pci.c` needs them. Examples include `myfirst_init_softc`, `myfirst_deinit_softc`, and `myfirst_quiesce`.

The convention is: a function that is called only from within its own file is `static`. A function that is called across files (but only within this driver) is non-static with a declaration in `myfirst.h` or another project-local header. A function that is callable from other modules (rare, and typically only through a KPI) is explicitly exported via a kernel-style symbol table; this is not relevant for Chapter 18.

The refactor does not export any new symbol outside the driver; it only promotes a few functions from file-local to driver-local. A reader who is bothered by the promotion has two options: leave the functions in `myfirst.c` and call them through a small helper that `myfirst_pci.c` calls (one more layer of indirection), or accept the promotion and document it in the source comments. The book chooses the latter; the driver is small enough that the occasional driver-local export is easy to audit.

### Wrapping Up Section 8

The refactor is, again, small in code but significant in organisation. A new file split, a new documentation file, updates to existing documentation files, a version bump, and a regression pass. Each step is cheap; together they turn a working driver into a maintainable one.

The Chapter 18 driver is done. The chapter closes with labs, challenges, troubleshooting, and a bridge to Chapter 19, where the PCI-attached driver gains a real interrupt handler. Chapter 20 then adds MSI and MSI-X; Chapters 20 and 21 add DMA. Each of those chapters will add a file (`myfirst_intr.c`, `myfirst_dma.c`) and extend the attach and detach paths. The shape Chapter 18 established will hold.



## Reading a Real Driver Together: uart_bus_pci.c

The preceding eight sections built Chapter 18's driver one step at a time. Before the labs, it is worth spending time with a real FreeBSD driver that follows the same pattern. `/usr/src/sys/dev/uart/uart_bus_pci.c` is a clean example. It is the PCI attach for the `uart(4)` driver, which handles PCI-attached serial ports: modem cards, chipset integrated UARTs, hypervisor serial emulation, and the console redirection chips that enterprise servers use.

Reading this file after writing Chapter 18's driver is a short exercise in pattern recognition. Nothing in the file is new. Every line maps to a concept Chapter 18 taught. The file is 366 lines; this section walks through the important parts, flagging where each piece corresponds to a Chapter 18 concept.

### The Top of the File

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2006 Marcel Moolenaar All rights reserved.
 * Copyright (c) 2001 M. Warner Losh <imp@FreeBSD.org>
 ...
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>
```

The SPDX license tag is BSD-2-Clause, the standard FreeBSD license. The include list is almost identical to Chapter 18's `myfirst_pci.c`. The `dev/pci/pcivar.h` and `dev/pci/pcireg.h` includes are the PCI subsystem interface; the `dev/uart/uart.h` and friends are the driver's internal headers that Chapter 18's driver does not have equivalents of.

### Method Table and Driver Structure

```c
static device_method_t uart_pci_methods[] = {
	DEVMETHOD(device_probe,		uart_pci_probe),
	DEVMETHOD(device_attach,	uart_pci_attach),
	DEVMETHOD(device_detach,	uart_pci_detach),
	DEVMETHOD(device_resume,	uart_bus_resume),
	DEVMETHOD_END
};

static driver_t uart_pci_driver = {
	uart_driver_name,
	uart_pci_methods,
	sizeof(struct uart_softc),
};
```

Four method entries, not three: `uart(4)` also implements `device_resume` for system suspend and resume support. The resume function is `uart_bus_resume`, which lives in the core `uart(4)` driver and is reused across every UART attach variant. Chapter 18's driver skipped `resume`; a production-quality driver usually implements it.

The `driver_t`'s name is `uart_driver_name`, defined elsewhere in the core UART driver as `"uart"`. The softc size is `sizeof(struct uart_softc)`, a structure defined in `uart_bus.h`.

### The ID Table

```c
struct pci_id {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subven;
	uint16_t	subdev;
	const char	*desc;
	int		rid;
	int		rclk;
	int		regshft;
};
```

The table entry is richer than Chapter 18's. The `subven` and `subdev` fields let the match discriminate among cards from different vendors that share a common chipset. The `rid` field carries the BAR's configuration-space offset (different boards use different BARs). The `rclk` carries the reference clock frequency in Hz, which varies among manufacturers. The `regshft` carries a register shift (some boards put their UART registers on 4-byte boundaries, some on 8-byte boundaries).

```c
static const struct pci_id pci_ns8250_ids[] = {
	{ 0x1028, 0x0008, 0xffff, 0, "Dell Remote Access Card III", 0x14,
	    128 * DEFAULT_RCLK },
	{ 0x1028, 0x0012, 0xffff, 0, "Dell RAC 4 Daughter Card Virtual UART",
	    0x14, 128 * DEFAULT_RCLK },
	/* ... many more entries ... */
	{ 0xffff, 0, 0xffff, 0, NULL, 0, 0 }
};
```

The table has dozens of entries. Each one is a board the `uart(4)` driver supports. A subvendor value of `0xffff` means "match any subvendor". The last entry is the sentinel.

Chapter 18's driver has one entry because it targets one demo device. `uart_bus_pci.c` has dozens because the UART hardware ecosystem is large and drivers must enumerate every supported variant.

### The Probe Routine

```c
static int
uart_pci_probe(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	struct pci_id cid = {
		.regshft = 0,
		.rclk = 0,
		.rid = 0x10 | PCI_NO_MSI,
		.desc = "Generic SimpleComm PCI device",
	};
	int result;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if (id != NULL) {
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
	    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
	    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
		id = &cid;
		sc->sc_class = &uart_ns8250_class;
		goto match;
	}
	return (ENXIO);

match:
	result = uart_bus_probe(dev, id->regshft, 0, id->rclk,
	    id->rid & PCI_RID_MASK, 0, 0);
	if (result > 0)
		return (result);
	if (sc->sc_sysdev == NULL)
		uart_pci_unique_console_match(dev);
	if (id->desc)
		device_set_desc(dev, id->desc);
	return (result);
}
```

The probe is more complex than Chapter 18's. It first searches the vendor/device ID table. If that fails, it falls back to a class-based match: any device whose class is `PCIC_SIMPLECOMM` (Simple Communications) and subclass is `PCIS_SIMPLECOMM_UART` (UART controller) and interface is older than `PCIP_SIMPLECOMM_UART_16550A` (older than 16550A means "a classic UART without enhanced features"). This is the fallback probe that lets the driver handle generic UART controllers even when their vendor and device ID are not in the table.

The `match:` label is reached from either path. It calls `uart_bus_probe` (the core UART driver's probe helper) with the entry's register shift, reference clock, and BAR offset. The return value is either a `BUS_PROBE_*` priority or a positive error code. Chapter 18's driver returns `BUS_PROBE_DEFAULT` directly; `uart(4)` delegates to `uart_bus_probe` because the core driver has additional checks.

The `pci_get_class`, `pci_get_subclass`, and `pci_get_progif` accessors return the class-code fields Chapter 18 described. Their use here is a concrete example of class-based matching.

### The Attach Routine

```c
static int
uart_pci_attach(device_t dev)
{
	struct uart_softc *sc;
	const struct pci_id *id;
	int count;

	sc = device_get_softc(dev);

	id = uart_pci_match(dev, pci_ns8250_ids);
	if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
	    pci_msi_count(dev) == 1) {
		count = 1;
		if (pci_alloc_msi(dev, &count) == 0) {
			sc->sc_irid = 1;
			device_printf(dev, "Using %d MSI message\n", count);
		}
	}

	return (uart_bus_attach(dev));
}
```

The attach is short. It re-matches the device (because the probe's match state is not preserved across probe/attach calls), checks whether the device supports MSI (single-vector), allocates one MSI vector if available, and then delegates to `uart_bus_attach` for the actual attach.

This is a pattern Chapter 18 did not use. `uart(4)` takes advantage of MSI when available, falling back to legacy IRQs otherwise. Chapter 20 in this book will introduce MSI and MSI-X; `uart(4)`'s attach is a preview.

The `PCI_NO_MSI` flag in some table entries marks boards where MSI is known to be buggy or unreliable; for those boards the attach skips MSI and relies on legacy IRQs.

### The Detach Routine

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

Eight lines, every one meaningful. Release MSI if it was allocated. Delegate to `uart_bus_detach` for the rest of the teardown.

Chapter 18's detach is longer because the `myfirst` driver does not delegate to a core driver; everything is in the PCI file. `uart(4)` factors the common teardown into `uart_bus_detach`, called from every attach variant's detach.

### The DRIVER_MODULE Line

```c
DRIVER_MODULE(uart, pci, uart_pci_driver, NULL, NULL);
```

One line. The module name is `uart` (matching the `driver_t`). The bus is `pci`. The two `NULL`s are for module init and cleanup handlers that `uart(4)` does not need.

Chapter 18's driver has the same line with `myfirst` in place of `uart`.

### What This Walkthrough Teaches

`uart_bus_pci.c` is 366 lines. About 60 lines are code; the rest is the ID table (250+ entries, many on multiple lines each) and helper functions specific to UART handling.

The code is almost indistinguishable from Chapter 18's driver in shape. A `pci_id` struct. An ID table. A probe that matches the table. An attach that claims the BAR (through `uart_bus_attach`). A detach that releases everything. `DRIVER_MODULE`. `MODULE_DEPEND`. The differences are all about UART-specific features: the subvendor match, the class-based fallback, the MSI allocation, the register-shift and reference-clock fields.

A reader who finds `uart_bus_pci.c` readable after Chapter 18 has made the chapter's point. The Chapter 18 driver is a real FreeBSD PCI driver, not a toy. It is missing a few features (MSI, resume, DMA) that later chapters will add, but its bones are the bones of every real driver in the tree.

Worth reading after `uart_bus_pci.c`, for comparison: `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`, which is the modern (non-legacy) virtio PCI attach. It is richer than `uart_bus_pci.c` because it handles virtio's layered transport, but the shape is the same.



## A Deeper Look at the PCI Capability List

Section 5 introduced `pci_find_cap(9)` and `pci_find_extcap(9)` as tools for discovering a device's optional features. This subsection goes a level deeper, showing how the capability list is structured in configuration space and how a driver might walk the whole list rather than looking up a specific capability.

### Structure of the Legacy Capability List

The legacy capability list lives in the first 256 bytes of configuration space. It starts at an offset held in the device's `PCIR_CAP_PTR` byte (at offset `0x34`). The byte at that offset is the capability ID; the byte immediately after is the offset of the next capability (or zero if this is the last one); the remaining bytes of the capability are feature-specific.

The minimum capability header is two bytes:

```text
offset 0: capability ID (one byte, values like PCIY_MSI = 0x05)
offset 1: next pointer (one byte, offset of next capability, 0 means end)
```

A driver walking the list reads the cap pointer from `PCIR_CAP_PTR`, then follows the chain by reading each capability's `next` byte until a zero is reached.

A concrete walk, in code:

```c
static void
myfirst_dump_caps(device_t dev)
{
	uint8_t ptr, id;
	int safety = 64;  /* protects against malformed lists */

	ptr = pci_read_config(dev, PCIR_CAP_PTR, 1);
	while (ptr != 0 && safety-- > 0) {
		id = pci_read_config(dev, ptr, 1);
		device_printf(dev,
		    "legacy capability ID 0x%02x at offset 0x%02x\n", id, ptr);
		ptr = pci_read_config(dev, ptr + 1, 1);
	}
}
```

The `safety` counter guards against a malformed configuration space where the `next` pointer forms a cycle. A well-behaved device never produces this, but defensive code treats the configuration space as potentially adversarial.

The walk prints each capability's ID and offset. The driver can then match the IDs against the `PCIY_*` constants and handle those it supports.

### Structure of the Extended Capability List

The PCIe extended capability list starts at offset `PCIR_EXTCAP` (`0x100`) and uses 4-byte headers. The layout, as encoded in `/usr/src/sys/dev/pci/pcireg.h`, is:

```text
bits 15:0   capability ID    (PCIM_EXTCAP_ID,       mask 0x0000ffff)
bits 19:16  capability version (PCIM_EXTCAP_VER,     mask 0x000f0000)
bits 31:20  next pointer     (PCIM_EXTCAP_NEXTPTR,  mask 0xfff00000)
```

FreeBSD exposes three helper macros on top of the raw masks:

- `PCI_EXTCAP_ID(header)` returns the capability ID.
- `PCI_EXTCAP_VER(header)` returns the version.
- `PCI_EXTCAP_NEXTPTR(header)` returns the next pointer (already shifted into its natural range).

The 12-bit next pointer is always 4-byte aligned; a next pointer of zero terminates the list.

A walk using the helpers:

```c
static void
myfirst_dump_extcaps(device_t dev)
{
	uint32_t header;
	int off = PCIR_EXTCAP;
	int safety = 64;

	while (off != 0 && safety-- > 0) {
		header = pci_read_config(dev, off, 4);
		if (header == 0 || header == 0xffffffff)
			break;
		device_printf(dev,
		    "extended capability ID 0x%04x ver %u at offset 0x%03x\n",
		    PCI_EXTCAP_ID(header), PCI_EXTCAP_VER(header), off);
		off = PCI_EXTCAP_NEXTPTR(header);
	}
}
```

The walker reads the 4-byte header and unpacks it with the helpers. A zero or all-ones header means there are no extended capabilities (the latter is what a non-PCIe device returns for any extended-capability read).

### Why the Walk Matters

A driver rarely needs the full walk. `pci_find_cap` and `pci_find_extcap` are the common interface: the driver asks for a specific capability and either gets the offset or gets `ENOENT`. A driver that wants to dump the full capability list for diagnostic purposes uses the walkers shown above.

The value of understanding the structure is in reading datasheets. A datasheet that says "the device implements the MSI capability starting at offset 0xa0" is saying: the byte at configuration-space offset `0xa0` is the capability ID (will equal `0x05` for MSI), the byte at `0xa1` is the next pointer, and the bytes from `0xa2` onward are the MSI capability structure. `pci_find_cap(dev, PCIY_MSI, &capreg)` returns `capreg = 0xa0` because that is where the capability lives.

A driver accessing the capability structure reads from `capreg + offset`, where `offset` is defined in the MSI capability's own structure. Specific fields have specific offsets; the pcireg.h header defines the offsets as `PCIR_MSI_*`.

### Walking a Specific Capability's Fields

An example. The MSI capability has several fields the driver cares about, at specific offsets relative to the capability header:

```text
PCIR_MSI_CTRL (0x02): message control (16 bits, enables, vector count)
PCIR_MSI_ADDR (0x04): message address low (32 bits)
PCIR_MSI_ADDR_HIGH (0x08): message address high (32 bits, 64-bit only)
PCIR_MSI_DATA (0x08 or 0x0c): message data (16 bits)
```

A driver that has `capreg` from `pci_find_cap(dev, PCIY_MSI, &capreg)` reads the message control register with:

```c
uint16_t msi_ctrl = pci_read_config(dev, capreg + PCIR_MSI_CTRL, 2);
```

The macro `PCIR_MSI_CTRL` is `0x02`; the full offset is `capreg + 0x02`. Similar patterns apply to every capability.

For Chapter 18 this level of detail is not needed because the driver does not use MSI. Chapter 20 does, and uses helper functions (`pci_alloc_msi`, `pci_alloc_msix`, `pci_enable_msi`, `pci_enable_msix`) that hide the raw field access. The walker shown here is useful mostly for diagnostics and for reading datasheets.



## A Deeper Look at Configuration Space

Section 1 and Section 5 introduced configuration space; this subsection adds a few practical details a driver author should know.

### Configuration Space Layout

The first 64 bytes of every PCI configuration space are standardised. The layout is:

| Offset | Width | Field |
|--------|-------|-------|
| 0x00 | 2 | Vendor ID |
| 0x02 | 2 | Device ID |
| 0x04 | 2 | Command register |
| 0x06 | 2 | Status register |
| 0x08 | 1 | Revision ID |
| 0x09 | 3 | Class code (progIF, subclass, class) |
| 0x0c | 1 | Cache line size |
| 0x0d | 1 | Latency timer |
| 0x0e | 1 | Header type |
| 0x0f | 1 | BIST (built-in self test) |
| 0x10 | 4 | BAR 0 |
| 0x14 | 4 | BAR 1 |
| 0x18 | 4 | BAR 2 |
| 0x1c | 4 | BAR 3 |
| 0x20 | 4 | BAR 4 |
| 0x24 | 4 | BAR 5 |
| 0x28 | 4 | CardBus CIS pointer |
| 0x2c | 2 | Subsystem vendor ID |
| 0x2e | 2 | Subsystem device ID |
| 0x30 | 4 | Expansion ROM base address |
| 0x34 | 1 | Capability list pointer |
| 0x35 | 7 | Reserved |
| 0x3c | 1 | Interrupt line |
| 0x3d | 1 | Interrupt pin |
| 0x3e | 1 | Min grant |
| 0x3f | 1 | Max latency |

The bytes from 0x40 to 0xff are reserved for device-specific use and for the legacy capability list (starting at the offset stored in `PCIR_CAP_PTR`).

PCIe extends configuration space to 4096 bytes. The bytes from 0x100 to 0xfff hold the extended capability list, which starts at offset `0x100` and follows its own chain of 4-byte-aligned capabilities.

### Header Type

The byte at `PCIR_HDRTYPE` (`0x0e`) distinguishes among three kinds of PCI configuration headers:

- `0x00`: standard device (what Chapter 18 assumes).
- `0x01`: PCI-to-PCI bridge (a bridge that connects a secondary bus to the primary bus).
- `0x02`: CardBus bridge (a PC card bridge; increasingly obsolete).

The layout beyond offset `0x10` differs among header types. A driver for a standard device uses offsets `0x10` through `0x24` as BARs; a driver for a bridge uses the same offsets for secondary bus number, subordinate bus number, and bridge-specific registers.

The high bit of `PCIR_HDRTYPE` indicates a multi-function device: if set, the device has functions beyond function 0. The kernel's PCI enumerator uses this bit to decide whether to probe functions 1 through 7.

### Commands and Status

The command register (`PCIR_COMMAND`, offset `0x04`) holds enable bits that control the device's PCI-level behaviour:

- `PCIM_CMD_PORTEN` (0x0001): enable I/O BARs.
- `PCIM_CMD_MEMEN` (0x0002): enable memory BARs.
- `PCIM_CMD_BUSMASTEREN` (0x0004): allow the device to initiate DMA.
- `PCIM_CMD_SERRESPEN` (0x0100): report system errors.
- `PCIM_CMD_INTxDIS` (0x0400): disable legacy INTx assertion (used when the driver uses MSI or MSI-X instead).

The kernel sets `MEMEN` and `PORTEN` automatically during resource activation. The driver sets `BUSMASTEREN` through `pci_enable_busmaster` if it uses DMA. The driver sets `INTxDIS` when it has successfully allocated MSI or MSI-X vectors and wants to prevent the device from also raising legacy interrupts.

The status register (`PCIR_STATUS`, offset `0x06`) holds sticky bits that the driver reads to find out about PCI-level events: the device has received a master abort, a target abort, a parity error, or a signalled system error. A driver that cares about PCI error recovery reads the status register periodically or in its error handler; a driver that does not care (most drivers, at the Chapter 18 level) ignores it.

### Reading Wider Than Available Width

`pci_read_config(dev, offset, width)` takes a width of 1, 2, or 4. It never takes a width of 8, even though some 64-bit fields (64-bit BARs) exist in configuration space. A driver reading a 64-bit BAR does it as two 32-bit reads:

```c
uint32_t bar_lo = pci_read_config(dev, PCIR_BAR(0), 4);
uint32_t bar_hi = pci_read_config(dev, PCIR_BAR(1), 4);
uint64_t bar_64 = ((uint64_t)bar_hi << 32) | bar_lo;
```

Note that this reads the *configuration-space* BAR, which the driver rarely needs after the kernel has allocated the resource. The kernel's allocation returns the same information as a `struct resource *` whose start address is available through `rman_get_start`.

### Alignment in Configuration-Space Reads

Configuration-space accesses are aligned by design. A read of width 1 may start at any offset; a read of width 2 must start at an even offset; a read of width 4 must start at an offset divisible by 4. Unaligned accesses (for example, a width-4 read at offset `0x03`) are not supported by the PCI bus's configuration transaction and will return undefined values or an error on some implementations. Every standard field in the first 64 bytes of configuration space is laid out so that its natural width is naturally aligned, so a driver that reads each field at its documented offset and width never runs into alignment trouble.

A driver reading a vendor-specific field whose layout is unclear should read it in the width the datasheet specifies. Do not assume that a 32-bit-wide read of a 16-bit field returns well-defined values in the high bits. The PCI specification requires unused byte lanes to return zeros, but a cautious driver reads only the width it needs.

### Writing Configuration Space: Caveats

Three caveats for configuration-space writes.

First, some fields are sticky: once set, they do not clear. The command register's `INTxDIS` bit is an example. Writing zero to the bit does not re-enable legacy interrupts in all cases; the device may latch the disabled state. A driver that needs to toggle such a bit must write the full register (read-modify-write) and may need to tolerate the device ignoring the clearing write.

Second, some fields are RW1C ("read-write-one-to-clear"). Writing a 1 to the bit clears it; writing 0 is a no-op. The status register's error bits are all RW1C. A driver that wants to clear a sticky error bit writes a 1 to that bit's position.

Third, some writes have timing requirements. The power-management capability's control register, for example, requires 10 milliseconds of settle time after a state transition. A driver that writes such a field must respect the timing, usually with a `DELAY(9)` or `pause_sbt(9)` call.

For Chapter 18's driver, only the probe's ID reads and the capability walker's reads touch configuration space. No writes are done. Chapter 19 onward will add writes (enabling interrupts, clearing status bits); each write will have the relevant caveats called out at the point it is introduced.



## A Deeper Look at the bus_space Abstraction

Section 4 used the Chapter 16 accessor layer unchanged against a real BAR. This subsection describes, in more depth, what the `bus_space` layer does under the hood and why it matters.

### What bus_space_tag_t Is

On x86, `bus_space_tag_t` is an integer that selects between two address spaces: memory (`X86_BUS_SPACE_MEM`) and I/O port (`X86_BUS_SPACE_IO`). The tag tells the accessor which CPU instructions to emit: memory accesses use normal load and store instructions; I/O port accesses use `in` and `out`.

On arm64, `bus_space_tag_t` is a pointer to a structure of function pointers (a `struct bus_space`). The tag encodes not only memory vs I/O but also properties like endianness and access granularity.

On every platform, the tag is opaque to the driver. The driver stores it, passes it to `bus_space_read_*` and `bus_space_write_*`, and never inspects its contents. The inclusion of `machine/bus.h` brings in the platform-specific definition.

### What bus_space_handle_t Is

On x86 for memory space, `bus_space_handle_t` is a kernel virtual address. The accessor dereferences it as a `volatile` pointer of the appropriate width.

On x86 for I/O port space, `bus_space_handle_t` is an I/O port number (0 to 65535). The accessor uses the `in` or `out` instruction with the port number.

On arm64, `bus_space_handle_t` is a kernel virtual address, similar to x86 memory space. The platform's MMU is configured to map the physical BAR into the virtual range with device-memory attributes.

The handle is also opaque to the driver. Together with the tag, it uniquely identifies a range of addresses where a specific resource lives.

### What Happens Inside bus_space_read_4

On x86 for memory space, `bus_space_read_4(tag, handle, offset)` expands to roughly:

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	return (*(volatile uint32_t *)(handle + offset));
}
```

A volatile pointer dereference. The `volatile` keyword prevents the compiler from caching the value or reordering the access past other volatile accesses.

On x86 for I/O port space, the implementation uses the `inl` instruction:

```c
static inline uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset)
{
	uint32_t value;
	__asm volatile ("inl %w1, %0" : "=a"(value) : "Nd"(handle + offset));
	return (value);
}
```

The tag selects between the two implementations. On arm64 and other platforms, the tag is richer and the implementation dispatches through a function-pointer table.

### Why the Abstraction Matters

A driver that uses `bus_space_read_4` and `bus_space_write_4` compiles to the right CPU instructions on every supported platform. The driver author does not need to know whether the BAR is memory or I/O; does not need to write platform-specific code; does not need to annotate pointers with the correct access attributes. The `bus_space` layer handles all of this.

A driver that bypasses `bus_space` and dereferences a raw pointer may work on x86 by accident (because the kernel's pmap layer happens to set up the mapping in a way that pointer accesses work). On arm64 it will fail: the device memory is mapped with attributes that prevent normal memory access patterns from working correctly.

The lesson is: always use `bus_space` or the Chapter 16 accessors that wrap it. Never dereference a raw pointer into device memory, even if you know the virtual address.

### The bus_read vs bus_space_read Naming

FreeBSD has two families of accessor functions that do essentially the same thing. The older `bus_space_read_*` family takes a tag, a handle, and an offset. The newer `bus_read_*` family takes a `struct resource *` and an offset, and pulls the tag and handle out of the resource internally.

The newer family is more convenient; the driver stores only the resource and does not need to store the tag and handle separately. The older family is more flexible; the driver can build a tag and handle from scratch (which Chapter 16's simulation used).

Chapter 18's driver uses the older family because it inherits from Chapter 16. A rewrite could use the newer family with no semantic change. Both families produce the same output. The book's choice is to teach the tag-and-handle story because it makes the abstraction explicit; the newer family hides the abstraction, which is nicer for writing drivers but less pedagogical.

For reference, the newer family members are named like `bus_read_4(res, offset)` and `bus_write_4(res, offset, value)`. They are defined in `/usr/src/sys/sys/bus.h` as inline functions that extract the tag and handle and delegate to `bus_space_read_*` and `bus_space_write_*`.



## Hands-On Labs

The labs in this chapter are structured as graduated checkpoints. Each lab builds on the previous one. A reader who works through all five labs will have a complete PCI driver, a bhyve test environment, a regression script, and a small library of diagnostic tools. Labs 1 and 2 can be done on any FreeBSD machine without a guest; labs 3, 4, and 5 require a bhyve or QEMU guest with a virtio-rnd device.

The time budget for each lab assumes the reader has already read the relevant sections and understood the concepts. A reader who is still learning should budget more time.

### Lab 1: Explore Your PCI Topology

Time: thirty minutes to an hour, depending on how much time you want to spend on comprehension.

Objective: Build an intuition for PCI on your own system.

Steps:

1. Run `pciconf -lv` on your lab host and redirect the output to a file: `pciconf -lv > ~/pci-inventory.txt`.
2. Count the devices: `wc -l ~/pci-inventory.txt`. Divide by an estimate (typically 5 lines per device in the output) to get the approximate device count.
3. Identify the following classes of devices from the inventory:
   - Host bridges (class = bridge, subclass = HOST-PCI)
   - PCI-PCI bridges (class = bridge, subclass = PCI-PCI)
   - Network controllers (class = network)
   - Storage controllers (class = mass storage)
   - USB host controllers (class = serial bus, subclass = USB)
   - Graphics (class = display)
4. For each of the above, note:
   - The device's `name@pciN:B:D:F` string.
   - The vendor and device IDs.
   - The driver binding (look at the leading name, before the `@`).
5. Pick one PCI device (any unclaimed device, visible as `none@...`, works best). Note its B:D:F.
6. Run `devinfo -v | grep -B 1 -A 5 <B:D:F>` and note the resources.
7. Compare the resource list with the BAR information in the `pciconf -lv` entry.

Expected observations:

- Most devices on a modern system are on `pci0` (the primary bus) or on a bus behind a PCIe bridge. Your machine probably has three to ten visible buses.
- Every device has at least a vendor and device ID. Many have subvendor and subsystem IDs.
- Most devices are bound to a driver. Some (especially on laptops, where a manufacturer ships hardware that FreeBSD does not yet support) are unclaimed.
- The resource lists in `devinfo -v` match the BAR information you can see in `pciconf -lv`. The addresses are the ones the firmware assigned.

This lab is about building vocabulary. No code. No driver. Just reading.

### Lab 2: Write the Probe Skeleton on Paper

Time: one to two hours.

Objective: Internalise the probe-attach-detach sequence by writing it out longhand.

Steps:

1. Open a blank file, `myfirst_pci_sketch.c`, in your editor.
2. Without looking at Section 2's finished code, write:
   - A `myfirst_pci_id` struct.
   - A `myfirst_pci_ids[]` table with one entry for a hypothetical vendor `0x1234` and device `0x5678`.
   - A `myfirst_pci_probe` function that matches against the table.
   - A `myfirst_pci_attach` function that prints `device_printf(dev, "attached\n")`.
   - A `myfirst_pci_detach` function that prints `device_printf(dev, "detached\n")`.
   - A `device_method_t` table with probe, attach, detach.
   - A `driver_t` with the driver's name.
   - A `DRIVER_MODULE` line.
   - A `MODULE_DEPEND` line.
   - A `MODULE_VERSION` line.
3. Compare your sketch to the Section 2 code. Note every difference.
4. For each difference, ask: is mine wrong, or does it work differently for a reason?
5. Update your sketch to match the Section 2 code where yours was wrong.

Expected outcomes:

- You will probably forget `MODULE_DEPEND` and `MODULE_VERSION` on first pass.
- You may use `0` instead of `BUS_PROBE_DEFAULT` in probe (a common beginner mistake).
- You may forget the `device_set_desc` call in probe.
- You may use `printf` instead of `device_printf` in attach and detach.
- You may forget `DEVMETHOD_END` at the end of the method table.

Each of these is a real mistake that produces a real bug. Finding them in your own sketch, rather than in a compiled driver at two in the morning, is the point of the lab.

### Lab 3: Load the Stage 1 Driver in a bhyve Guest

Time: two to three hours, including setting up the guest if you do not already have one.

Objective: Observe the probe-attach-detach sequence in action.

Steps:

1. If you do not already have a bhyve guest running FreeBSD 14.3, set one up. The canonical recipe is in `/usr/share/examples/bhyve/` or in the FreeBSD Handbook. Include a `virtio-rnd` device in the guest's bhyve command line: `-s 4:0,virtio-rnd`.
2. Inside the guest, list the PCI devices: `pciconf -lv | grep -B 1 -A 2 0x1005`. Note whether the virtio-rnd entry is bound to `virtio_random` (a leading `virtio_random0@...`), to `none` (unclaimed), or missing entirely (check your bhyve command line).
3. Copy the Chapter 18 Stage 1 source into the guest (scp, shared filesystem, or any method you prefer).
4. Inside the guest, in the driver's source directory: `make`. Verify that `myfirst.ko` is produced.
5. If `virtio_random` claimed the device at step 2, unload it: `sudo kldunload virtio_random`. If the device was already unclaimed (`none`), skip this step.
6. Load `myfirst`: `sudo kldload ./myfirst.ko`.
7. Check the attach: `dmesg | tail -10`. You should see the Stage 1 attach banner.
8. Check the device: `devinfo -v | grep -B 1 -A 3 myfirst`. You should see `myfirst0` as a child of `pci0`.
9. Check the binding: `pciconf -lv | grep myfirst`. You should see the entry with `myfirst0` as the device name.
10. Unload the driver: `sudo kldunload myfirst`.
11. Check the detach: `dmesg | tail -5`. You should see the Stage 1 detach banner.
12. If you unloaded `virtio_random` at step 5 and want to restore it: `sudo kldload virtio_random`.

Expected outcomes:

- Every step produces the expected output.
- If step 7's `dmesg` does not show the attach banner, the driver did not probe the device. Check that you unloaded any other driver that might have claimed it.
- If step 7 shows the attach banner but step 8 does not show `myfirst0`, there is an accounting bug in newbus; unlikely, but worth reporting if you see it.
- If step 10 fails with `Device busy`, the driver's detach returned `EBUSY`. At Stage 1 there is no open cdev; the failure is unexpected. Check the detach code.

This lab is the first time the reader's driver meets a real device. The emotional payoff is real: `myfirst0: attaching` in `dmesg` is the proof that the driver works.

### Lab 4: Claim the BAR and Read a Register

Time: two to three hours.

Objective: Extend the Stage 1 driver to Stage 2 (BAR allocation) and Stage 3 (first real register read).

Steps:

1. Starting from the Stage 1 driver from Lab 3, edit `myfirst_pci.c` to add Stage 2's BAR allocation. Compile. Load. Verify the BAR allocation banner in `dmesg`.
2. Verify the resource is visible: `devinfo -v | grep -A 3 myfirst0` should show a memory resource.
3. Unload. Verify the detach cleanly releases the BAR.
4. Edit `myfirst_pci.c` again to add Stage 3's capability walk and first register read. Compile. Load. Verify the capability output in `dmesg`.
5. Verify that `CSR_READ_4` operates on the real BAR by reading the first four bytes of the BAR and comparing to the first four bytes of `pciconf -r myfirst0 0x00:4`. (These are different; one is configuration space, the other is the BAR. The point of comparison is that both produce plausible values without crashing.)
6. Run the full regression script from Section 7. Verify that it completes without errors.

Expected outcomes:

- The BAR allocation succeeds and the resource is visible in `devinfo -v`.
- The capability walk may show zero offsets for the virtio-rnd device (the legacy layout does not have PCI capabilities in the way modern devices do); this is normal.
- The first register read returns a non-zero value; the exact value depends on the device's current state.

If any step produces a crash or a page fault, consult Section 7's common mistakes and re-check each step against Section 3's allocation discipline and Section 4's tag-and-handle code.

### Lab 5: Exercise the cdev and Verify Detach Cleanup

Time: two to three hours.

Objective: Prove that the full Chapter 18 driver works end-to-end.

Steps:

1. Starting from the Stage 3 driver from Lab 4, write a small user-space program (`myfirst_test.c`) that opens `/dev/myfirst0`, reads up to 64 bytes, writes 16 bytes, and closes the device.
2. Compile and run the program. Observe the output. Ensure that no kernel message reports an error.
3. In a second terminal, tail `dmesg` with `dmesg -w`.
4. Run the program several times, watching for any warnings or errors.
5. Run the detach-attach cycle ten times with `devctl detach myfirst0; devctl attach pci0:0:4:0`. Verify that `dmesg` shows clean attach and detach banners each cycle.
6. After the cycle, run `vmstat -m | grep myfirst` and verify that the `myfirst` malloc type has zero live allocations.
7. Unload the driver. Verify `kldstat -v | grep myfirst` returns nothing.
8. Reload the driver. Verify the attach fires again.

Expected outcomes:

- Every step succeeds.
- The `vmstat -m` check at step 6 is the most important. If it shows live allocations after the detach cycle, there is a leak that needs fixing.
- The attach-detach-reattach cycle is stable. The driver can be bound, unbound, rebound indefinitely.

This lab is the regression proof. A driver that passes Lab 5 ten times in a row without issue is a driver that Chapter 19 can extend safely.

### Lab Summary

The five labs together take ten to fifteen hours. They produce a complete PCI driver, a working test environment, a regression script, and a small toolbox of diagnostic commands the reader can reuse in later chapters. A reader who has done all five labs has done the hands-on equivalent of reading the chapter twice: the concepts are grounded in code that ran, failures that were fixed, and outputs that were observed.

If any lab resists (the BAR allocation fails, the capability walk produces an error, the detach leaks a resource), stop and diagnose. The troubleshooting section at the end of this chapter covers the common failure modes. The labs are calibrated to work; if a lab does not work, either the lab has a subtle error (rare) or the reader's environment has a detail that differs from the author's (much more common). Either way, the diagnosis is where the real learning happens.



## Challenge Exercises

The challenges build on the labs. Each challenge is optional: the chapter is complete without them. But a reader who works through them will consolidate what they learned and extend the driver in ways the chapter did not.

### Challenge 1: Support a Second Vendor and Device ID

Extend `myfirst_pci_ids[]` with a second entry. Target a different bhyve-emulated device: `virtio-blk` (vendor `0x1af4`, device `0x1001`) or `virtio-net` (`0x1af4`, `0x1000`). Unload the corresponding base-system driver (`virtio_blk` or `virtio_net`), load `myfirst`, and verify that the attach picks up the new device.

This exercise is trivial in code (one table entry) but exercises the reader's understanding of how the probe decision is made. After the change, both virtio devices will be eligible for `myfirst` if their drivers are unloaded.

### Challenge 2: Print the Full Capability Chain

Extend the capability-walking code in `myfirst_pci_attach` to print every capability in the list, not only the ones the driver knows about. Walk the legacy capability list starting at `PCIR_CAP_PTR` and following the `next` pointers; for each capability, print the ID and the offset. Do the same for the extended capability list starting at offset `0x100`.

This exercise goes beyond the chapter's treatment of `pci_find_cap`. It requires reading `/usr/src/sys/dev/pci/pcireg.h` to find the layout of capability and extended-capability headers. The output on a typical virtio-rnd device may be sparse; on a real-hardware PCIe device it is richer.

### Challenge 3: Implement a Simple ioctl for Configuration-Space Access

Extend the cdev's `ioctl` entry point to accept a request for reading configuration space. Define a new `ioctl` command `MYFIRST_IOCTL_PCI_READ_CFG` that takes a `{ offset, width }` input and returns a `uint32_t` value. Have the implementation call `pci_read_config` under `sc->mtx`.

Write a user-space program that uses the new `ioctl` to read the first 16 bytes of configuration space, byte by byte, and prints them.

This exercise introduces the reader to custom ioctls, which are a common pattern for exposing driver-specific behaviour to user space without adding new system calls.

### Challenge 4: Refuse to Attach if the BAR Is Too Small

The Chapter 18 driver assumes BAR 0 is at least `MYFIRST_REG_SIZE` (64) bytes. A different device with the same vendor and device IDs might expose a smaller BAR. Extend the attach path to read `rman_get_size(sc->bar_res)`, compare it to `MYFIRST_REG_SIZE`, and refuse to attach (return `ENXIO` after cleanup) if the BAR is too small.

Verify the behaviour by artificially setting `MYFIRST_REG_SIZE` to a value larger than the actual BAR size. The driver should refuse to attach and `dmesg` should print an informative message.

### Challenge 5: Split the Driver into Two Modules

Using the technique sketched in Section 5, split the driver into `myfirst_core.ko` (hardware layer, simulation, cdev, locks) and `myfirst_pci.ko` (PCI attach). Add a `MODULE_DEPEND(myfirst_pci, myfirst_core, 1, 1, 1)` declaration. Verify that `kldload myfirst_pci` automatically loads `myfirst_core` as a dependency.

This exercise is a moderate refactor. It introduces the reader to cross-module symbol visibility (which functions need to be exported from `myfirst_core` to `myfirst_pci`) and to the module loader's dependency resolution. The result is a clean separation between the driver's generic machinery and its PCI-specific attach.

### Challenge 6: Reimplement probe Using Class and Subclass Matching

Instead of matching by vendor and device ID, extend the probe routine to also match by class and subclass. For example, match any device in class `PCIC_BASEPERIPH` (base peripheral) with subclass matching a chosen value. Return `BUS_PROBE_GENERIC` (a lower-priority match) when the class-based match succeeds but no vendor/device-specific entry matched.

This exercise teaches the reader how drivers coexist. The vendor-specific match wins over the class match (by returning `BUS_PROBE_DEFAULT` vs `BUS_PROBE_GENERIC`). A fallback driver can claim devices that no specific driver recognises.

### Challenge 7: Add a Read-Only sysctl That Reports the Driver's PCI State

Add a sysctl `dev.myfirst.N.pci_info` that returns a short string describing the driver's PCI attachment: the vendor and device IDs, the subvendor and subsystem, the B:D:F, and the BAR size and address. Use `sbuf_printf` to format the string.

The result is a user-space-readable dump of the driver's view of its device. This is useful for diagnostics and becomes a pattern that drivers for more complex devices reuse.

### Challenge 8: Simulate a Failed Attach

Introduce a sysctl `hw.myfirst.fail_attach` that, when set to 1, causes attach to fail after claiming the BAR. Verify that the goto cascade cleans up correctly and `vmstat -m | grep myfirst` shows zero leaks after the failed attach.

This exercise exercises the partial-failure path that Section 6 described but the lab sequence did not test explicitly. It is the best way to confirm that the unwind cascade is correct.

### Challenge Summary

Eight challenges, covering a range of difficulty. A reader who completes four or five of them has deepened their understanding meaningfully. A reader who completes all eight has essentially written a second Chapter 18.

Save your solutions. Several of them (Challenge 1, Challenge 3, Challenge 7) are natural starting points for Chapter 19 extensions.



## Troubleshooting and Common Mistakes

This section consolidates the common failure modes a reader may encounter in Chapter 18's labs. Each entry names the symptom, the likely cause, and the fix.

### "Driver does not attach; no dmesg banner"

Symptom: `kldload ./myfirst.ko` returns success. `dmesg | tail` shows nothing from `myfirst`. `devinfo -v` does not list `myfirst0`.

Likely causes:

1. Another driver has claimed the target device. Check `pciconf -lv` for the device and see which driver (if any) is bound. If `virtio_random0` owns the virtio-rnd device, the probe priority tie goes to `virtio_random` and `myfirst` never attaches. Fix: `kldunload virtio_random` first.

2. The vendor or device ID in `myfirst_pci_ids[]` is wrong. Check against the guest's actual device. Fix: correct the IDs.

3. The probe routine has a bug that always returns `ENXIO`. Check that the comparison compares `vendor` and `device` against the table entries, not against themselves. Fix: re-read the probe code carefully.

4. The `DRIVER_MODULE` declaration is missing or wrong. Check that the third argument is the `driver_t` and the second is `"pci"`. Fix: correct the declaration.

### "kldload panics the kernel"

Symptom: `kldload ./myfirst.ko` crashes the kernel before it returns.

Likely causes:

1. Missing `MODULE_DEPEND(myfirst, pci, ...)`. The driver tries to register against a bus that is not yet initialised. Fix: add the declaration.

2. The driver's initialisation calls a function that does not exist at module-load time. Rare, but possible if the driver defines a `MOD_LOAD` handler that accesses `device_*` functions before the bus is ready.

3. The softc size declared in the `driver_t` is wrong. If the attach code expects fields that are not in the declared structure, the kernel writes past the allocated block and crashes. Fix: ensure `sizeof(struct myfirst_softc)` matches the structure definition.

The debug kernel is good at catching all three; the backtrace in `ddb` will name the function where the crash occurred.

### "BAR allocation fails with NULL"

Symptom: `bus_alloc_resource_any` returns NULL. `dmesg` says "cannot allocate BAR0".

Likely causes:

1. Wrong `rid`. Use `PCIR_BAR(0)` for BAR 0, not `0`. Fix: use the macro.

2. Wrong type. If the device's BAR 0 is an I/O port (bit 0 of the BAR is set in configuration space), passing `SYS_RES_MEMORY` fails. Read the BAR value with `pci_read_config(dev, PCIR_BAR(0), 4)` and check the low bit. Fix: use the correct type.

3. The BAR is already allocated by another driver or by the BIOS. Unlikely on a bhyve guest; possible on real hardware with a misconfigured BIOS. Fix: check `devinfo -v` for the claimed resources.

4. The `RF_ACTIVE` flag is missing. The resource is allocated but not activated. The handle is not usable for `bus_space` accesses. Fix: add `RF_ACTIVE`.

### "CSR_READ_4 returns 0xffffffff"

Symptom: register reads return all ones. The reader expects non-zero values.

Likely causes:

1. The BAR is not activated. Check `RF_ACTIVE` in the `bus_alloc_resource_any` call.

2. The tag and handle are swapped. Read `rman_get_bustag` returns the tag; `rman_get_bushandle` returns the handle. Passing them to `bus_space_read_4` in the wrong order produces undefined behaviour.

3. The offset is wrong. The BAR is 32 bytes; reading at offset 64 reads past the end. The debug kernel's `KASSERT` in `myfirst_reg_read` catches this.

4. The device has been reset or powered off. Some devices return all ones when off. Read the command register with `pci_read_config(dev, PCIR_COMMAND, 2)`; if it returns `0xffff`, the device is unresponsive.

### "kldunload returns Device busy"

Symptom: `kldunload myfirst` fails with `Device busy`.

Likely causes:

1. A user-space process has `/dev/myfirst0` open. Close the process. Check with `fstat /dev/myfirst0`.

2. The driver has an in-flight command (simulation callout, taskqueue work). Wait a few seconds and retry.

3. The detach function incorrectly returns `EBUSY` unconditionally. Check the detach code.

4. The driver's busy check has a stale reference to an uninitialised field. Check that `sc->open_count` is zero when no descriptors are open.

### "dmesg says 'cleanup failed in detach'"

Symptom: `dmesg` shows a warning from the detach path.

Likely causes:

1. A callout was still scheduled when detach ran. Check that `callout_drain` was called before the driver's softc cleanup.

2. A taskqueue work item was still pending. Check that `taskqueue_drain` was called.

3. The cdev was open at detach. The `destroy_dev` call should block until closed, but if the driver releases other resources first, the close will find stale state. Fix the order: destroy the cdev before releasing dependent resources.

### "ioctl or read returns an unexpected error"

Symptom: a user-space system call returns an error that the reader did not expect (EINVAL, ENODEV, ENXIO, etc.).

Likely causes:

1. The cdev's entry point checks for state the driver did not set. Example: the Chapter 10 driver checks `sc->is_attached`; the Chapter 18 driver might have forgotten to set it.

2. The ioctl command number in user space does not match the one in the driver. Check the `_IOR`/`_IOW`/`_IOWR` macros and confirm that the types are the same.

3. The lock order is wrong. The cdev entry point takes a lock in an order that conflicts with some other code. `WITNESS` on a debug kernel reports this.

### "vmstat -m shows leaked allocations"

Symptom: after a load-unload cycle, `vmstat -m | grep myfirst` shows non-zero "Allocations" or "InUse".

Likely causes:

1. A malloc in attach that is not freed in detach. Usually the hardware-layer wrapper struct or a sysctl buffer.

2. A callout that was not drained. The callout allocates a small structure; if it runs after detach, the structure leaks.

3. The `M_MYFIRST` malloc type is used for the softc. Newbus frees the softc automatically; the driver should not `malloc(M_MYFIRST, sizeof(softc))` at attach. The softc is allocated by newbus.

### "pci_find_cap returns ENOENT for a capability I know the device has"

Symptom: `pci_find_cap(dev, PCIY_EXPRESS, &capreg)` returns `ENOENT`, but the device is a PCIe device and should have the PCI Express capability.

Likely causes:

1. The device is a legacy PCI device in a PCIe slot (it works because PCIe is backward-compatible with PCI). Legacy devices do not have the PCI Express capability. Check by reading `pci_get_class(dev)` and comparing to what you expected.

2. The capability list is corrupt or empty. Read `PCIR_CAP_PTR` directly with `pci_read_config(dev, PCIR_CAP_PTR, 1)`; if it returns zero, the device does not implement capabilities.

3. Wrong capability ID. `PCIY_EXPRESS` is `0x10`, not `0x1f`. Check `pcireg.h` for the correct constant.

4. The status register's `PCIM_STATUS_CAPPRESENT` bit is zero. This bit tells the PCI subsystem that the device implements a capability list. Without it, the list is not present. The bit is in `PCIR_STATUS`.

### "Module unloads, but dmesg shows a page fault during unload"

Symptom: `kldunload myfirst` appears to succeed, but `dmesg` shows a page fault that occurred during the unload.

Likely causes:

1. A callout fired after `myfirst_hw_detach` but before the driver returned. The callout accessed `sc->hw`, which had been set to NULL. Fix: ensure `callout_drain` is called before `myfirst_hw_detach`.

2. A taskqueue work item ran after resources were freed. Fix: ensure `taskqueue_drain` is called before freeing anything the task touches.

3. A user-space process still has `/dev/myfirst0` open. The `destroy_dev` call completes quickly, but any outstanding I/O against the cdev continues until the process closes the descriptor or exits. Fix: ensure all user-space consumers close the cdev before detach; in emergency situations, `devctl detach` followed by killing the process works.

### "devinfo -v shows the driver attached but the cdev does not appear"

Symptom: `devinfo -v | grep myfirst` shows `myfirst0`, but `ls /dev/myfirst*` returns nothing.

Likely causes:

1. The `make_dev` call failed and the attach did not check the return value. Check `sc->cdev` after `make_dev`; if it is NULL, the call failed.

2. The cdev name is not `myfirst%d`. Check the `make_dev` call's format string. The device node path uses the exact string passed to `make_dev`.

3. The `cdevsw` structure has not been registered or has wrong methods. Check that `myfirst_cdevsw` is initialised correctly.

4. A stale `/dev` entry is hiding the new one. Try `sudo devfs rule -s 0 apply` or reboot. Unlikely on modern FreeBSD but possible in edge cases.

### "Attach takes a long time to return"

Symptom: `kldload ./myfirst.ko` hangs for seconds or minutes.

Likely causes:

1. A `DELAY` or `pause_sbt` call in attach is too long. Check for hidden delays in capability walks or device bring-up.

2. A `bus_alloc_resource_any` call is blocked on a resource that another driver has allocated. Rare on PCI; more common on platforms with limited I/O port space.

3. An infinite loop in the capability walker. A malformed device could produce a loop; the safety counter in the walker protects against this.

4. A `callout_init_mtx` call is waiting on a lock that another code path holds. Deadlock; check `WITNESS` output in `dmesg`.

### "Driver attaches on boot but never produces output for the first few seconds"

Symptom: after a guest reboot with `myfirst` loaded at boot time, the driver attaches but takes seconds to produce any log output.

Likely causes:

1. The module was loaded early in boot, before the console was fully initialised. The messages are in the kernel buffer but not yet written to the console. Check `dmesg` for the messages; they should be present.

2. A callout was scheduled but has not yet fired. Chapter 17's sensor callout runs every second; the first tick is one second after attach.

3. The driver is waiting for a condition that takes time. Not a Chapter 18 issue, but possible in drivers that wait for the device to complete a reset.

### "A second attempt to attach after a failed first attempt succeeds"

Symptom: `kldload` on a misconfigured kernel fails; a second `kldload` after fixing the configuration succeeds. This is actually expected behaviour.

Likely cause: the kernel's module loader is stateless between load attempts. A failed load removes any partial state. A subsequent load tries again with the fresh state. The symptom is not a bug.

### "vmstat -m InUse grows after each load-unload cycle"

Symptom: the `myfirst` malloc type shows a few bytes of `InUse` memory increasing every cycle.

Likely causes:

1. A leak in attach or detach that is too small to notice in a single cycle but accumulates. Run 100 cycles and watch the growth.

2. The `myfirst_hw` or `myfirst_sim` wrapper struct is allocated but not freed. Check that the detach path calls `myfirst_hw_detach` and `myfirst_sim_detach` (if the simulation is loaded).

3. A string or similar small allocation in a sysctl handler is leaking. Check the sysctl handlers for `sbuf` that are created but not deleted.

The `vmstat -m` output has columns for `Requests`, `InUse`, `MemUse`. `Requests` is the total number of allocations ever made. `InUse` is the number currently allocated. `MemUse` is the total bytes. A healthy driver's `InUse` returns to zero after detach and unload.

### Troubleshooting Summary

Every one of these failures is recoverable. The debug kernel (with `INVARIANTS`, `WITNESS`, and `KDB`) catches most of them with a useful message. A reader who runs a debug kernel and reads the message carefully will fix most Chapter 18 bugs in under an hour.

If a bug resists, the next step is to read the relevant section of this chapter again. The troubleshooting list above is short because the chapter's teaching is deliberately designed to prevent these failures. When a failure occurs, the question is usually "which section's discipline did I break?" and the answer is usually obvious on a second reading.

### A Debug Kernel Checklist

If you are serious about driver development and debugging, build a debug kernel. The configuration options that catch PCI-driver bugs reliably are:

```text
options INVARIANTS
options INVARIANT_SUPPORT
options WITNESS
options WITNESS_SKIPSPIN
options DEBUG_VFS_LOCKS
options DEBUG_MEMGUARD
options DIAGNOSTIC
options DDB
options KDB
options KDB_UNATTENDED
options MALLOC_DEBUG_MAXZONES=8
```

A driver that passes its regression tests under a kernel with all of these enabled is a driver that will rarely produce production bugs. The runtime cost is significant (the kernel is slower, and `WITNESS` in particular adds measurable overhead to every lock operation), but the debugging value is enormous.

Build the debug kernel with:

```sh
cd /usr/src
sudo make buildkernel KERNCONF=GENERIC-DEBUG
sudo make installkernel KERNCONF=GENERIC-DEBUG
sudo shutdown -r now
```

Use the debug kernel for all driver development; switch back to `GENERIC` only for performance benchmarking.



## Wrapping Up

Chapter 18 turned the simulated driver into a PCI driver. The starting point was `1.0-simulated`, a module with a `malloc(9)`-backed register block and a Chapter 17 simulation that made the registers breathe. The ending point is `1.1-pci`, the same module with one new file (`myfirst_pci.c`), one new header (`myfirst_pci.h`), and a handful of small extensions to the existing files. The accessor layer did not change. The command-response protocol did not change. The lock discipline did not change. What changed is the origin of the tag and handle that the accessors use.

The transition walked through eight sections. Section 1 introduced PCI as a concept, covering the topology, the B:D:F tuple, configuration space, BARs, vendor and device IDs, and the `pci(4)` subsystem. Section 2 wrote the probe-attach-detach skeleton, tied to the PCI bus with `DRIVER_MODULE(9)` and `MODULE_DEPEND(9)`. Section 3 explained what BARs are and claimed one through `bus_alloc_resource_any(9)`. Section 4 wired the claimed BAR to the Chapter 16 accessor layer, completing the transition from simulated to real register access. Section 5 added the attach-time plumbing: `pci_find_cap(9)` and `pci_find_extcap(9)` for capability discovery, the cdev creation, and the discipline that keeps the Chapter 17 simulation inactive on the PCI path. Section 6 consolidated the detach path with strict reverse ordering, a busy check, callout and task draining, and partial-failure recovery. Section 7 tested the driver in a bhyve or QEMU guest, exercising every path the driver exposes. Section 8 refactored the code into its final shape and documented the result.

What Chapter 18 did not do is interrupt handling. The virtio-rnd device under bhyve has an interrupt line; our driver does not register a handler for it; the device's internal state changes do not reach the driver. The cdev is still reachable, but the data path has no active producer on the PCI build (the Chapter 17 simulation callouts are not running). Chapter 19 introduces the real handler that will give the data path a producer.

What Chapter 18 did accomplish is the crossing of a threshold. Up to the end of Chapter 17, the `myfirst` driver was a teaching module: it existed because we loaded it, not because any device required it. From Chapter 18 onward, the driver is a PCI driver: it exists because the kernel enumerated a device and our probe said yes. The newbus machinery carries the driver now. Every later Part 4 chapter extends it without changing this fundamental relationship.

The file layout has grown: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_pci.c`, `myfirst_pci.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`, `myfirst.h`. The documentation has grown: `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`. The test suite has grown: the bhyve or QEMU setup scripts, the regression script, the small user-space test programs. Each of these is a layer; each of them was introduced in a specific chapter and is now a permanent part of the driver's story.

### A Reflection Before Chapter 19

A pause before the next chapter. Chapter 18 taught the PCI subsystem and the newbus attach dance. The patterns you practised here (probe-attach-detach, resource claim-release, tag-and-handle extraction, capability discovery) are patterns you will use throughout your driver-writing life. They apply as much to the USB attach dance in Chapter 21 as to the PCI dance you just wrote, as much to the NIC driver you might write for a real card as to the demo driver you just extended. The PCI skill is permanent.

Chapter 18 also taught the discipline of strict reverse ordering in detach. The goto cascade in attach, the mirror detach, the busy check, the quiesce step: these are the patterns that keep a driver from leaking resources across its lifecycle. They apply to every kind of driver, not only PCI. A reader who has internalised Chapter 18's detach discipline will write cleaner Chapter 19, Chapter 20, and Chapter 21 code.

One more observation. The payoff of Chapter 16's accessor layer is now visible. A reader who wrote the Chapter 16 accessors and wondered "is this worth the effort?" can look at Chapter 18's Stage 3 attach and see the answer. The upper-layer code of the driver (every call site that uses `CSR_READ_4`, `CSR_WRITE_4`, or `CSR_UPDATE_4`) did not change at all when the backend switched from simulated to real PCI. That is what a good abstraction buys: a major change in the lower layer costs zero changes in the upper layer. The Chapter 16 accessors were the abstraction. Chapter 18 was the proof.

### What to Do If You Are Stuck

Two suggestions.

First, focus on the regression script from Section 7. If the script runs end-to-end without error, the driver is working; every confusion about internal details is decorative. If the script fails, the first failing step is the starting point for debugging.

Second, open `/usr/src/sys/dev/uart/uart_bus_pci.c` and read it slowly. The file is 366 lines. Every line is a pattern that Chapter 18 taught or referenced. Reading it after Chapter 18 should feel familiar: probe, attach, detach, ID table, `DRIVER_MODULE`, `MODULE_DEPEND`. A reader who finds the file readable after Chapter 18 has made the chapter's real progress.

Third, skip the challenges on first pass. The labs are calibrated for Chapter 18; the challenges assume the chapter's material is already solid. Come back to them after Chapter 19 if they feel out of reach now.

Chapter 18's goal was to let the driver meet real hardware. If it has, the rest of Part 4 will feel like a natural progression: Chapter 19 adds interrupts, Chapter 20 adds MSI and MSI-X, Chapters 20 and 21 add DMA. Each chapter extends what Chapter 18 established.



## Bridge to Chapter 19

Chapter 19 is titled *Handling Interrupts*. Its scope is the topic Chapter 18 deliberately did not take: the path that lets a device tell the driver, asynchronously, that something happened. Chapter 17's simulation used callouts to produce autonomous state changes. Chapter 18's real-PCI driver ignores the device's interrupt line entirely. Chapter 19 registers a handler through `bus_setup_intr(9)`, attaches it to an IRQ resource allocated through `bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, and teaches the driver to react to the device's own signals.

Chapter 18 prepared the ground in four specific ways.

First, **you have a PCI-attached driver**. The Chapter 18 driver at `1.1-pci` allocates a BAR, claims a memory resource, and has every newbus hook in place. Chapter 19 adds one more resource (an IRQ) and one more pair of calls (`bus_setup_intr` and `bus_teardown_intr`). The rest of the attach and detach flow stays put.

Second, **you have an accessor layer that can be called from an interrupt context**. The Chapter 16 accessors take `sc->mtx`; an interrupt handler that needs to read or write a register acquires `sc->mtx` and calls `CSR_READ_4` or `CSR_WRITE_4`. The Chapter 19 handler will compose with the accessors without any new plumbing.

Third, **you have a detach order that accommodates IRQ teardown**. Chapter 18's detach releases the BAR at a specific point in the sequence; Chapter 19's detach will release the IRQ resource before releasing the BAR. The goto cascade expands by one label; the pattern does not change.

Fourth, **you have a test environment that produces interrupts**. The bhyve or QEMU guest with a virtio-rnd device is the same environment Chapter 19 uses; the virtio-rnd device's interrupt line is what Chapter 19's handler receives. No new lab setup is required.

Specific topics Chapter 19 will cover:

- What an interrupt is, in contrast to a polled callout.
- The two-stage model of FreeBSD interrupt handlers: filter (fast, in interrupt context) and ithread (slow, in a kernel thread context).
- `bus_alloc_resource_any(9)` with `SYS_RES_IRQ`.
- `bus_setup_intr(9)` and `bus_teardown_intr(9)`.
- `INTR_TYPE_*` and `INTR_MPSAFE` flags.
- What an interrupt handler may and may not do (no sleeping, no blocking locks, no `malloc(M_WAITOK)`).
- Reading a status register at interrupt time to decide what happened.
- Clearing interrupt flags to prevent re-entry.
- Logging interrupts safely.
- Interaction between interrupts and the Chapter 16 access log.
- A minimal interrupt handler that increments a counter and logs.

You do not need to read ahead. Chapter 18 is sufficient preparation. Bring your `myfirst` driver at `1.1-pci`, your `LOCKING.md`, your `HARDWARE.md`, your `SIMULATION.md`, your new `PCI.md`, your `WITNESS`-enabled kernel, and your regression script. Chapter 19 starts where Chapter 18 ended.

Chapter 20 is two chapters further along; it is worth a brief forward pointer. MSI and MSI-X will replace the single legacy interrupt line with a richer routing mechanism: separate vectors for separate tasks, interrupt coalescing, per-queue affinity. The `pci_alloc_msi(9)` and `pci_alloc_msix(9)` functions are part of the PCI subsystem Chapter 18 introduced; we saved them for Chapter 20 because MSI-X in particular requires a deeper understanding of interrupt handling than Chapter 18 was ready to introduce. If the reader has glanced at the `PCIY_MSI` and `PCIY_MSIX` offsets in the capability walk and wondered what they were for, Chapter 20 is the answer.

The hardware conversation is deepening. The vocabulary is yours; the protocol is yours; the discipline is yours. Chapter 19 adds the next missing piece.



## Reference: PCI Header Offsets the Chapter Used

A compact reference of the configuration-space offsets Chapter 18 references, sourced from `/usr/src/sys/dev/pci/pcireg.h`. Keep this at hand while writing PCI code.

| Offset | Macro | Width | Meaning |
|--------|-------|-------|---------|
| 0x00 | `PCIR_VENDOR` | 2 | Vendor ID |
| 0x02 | `PCIR_DEVICE` | 2 | Device ID |
| 0x04 | `PCIR_COMMAND` | 2 | Command register |
| 0x06 | `PCIR_STATUS` | 2 | Status register |
| 0x08 | `PCIR_REVID` | 1 | Revision ID |
| 0x09 | `PCIR_PROGIF` | 1 | Programming interface |
| 0x0a | `PCIR_SUBCLASS` | 1 | Subclass |
| 0x0b | `PCIR_CLASS` | 1 | Class |
| 0x0c | `PCIR_CACHELNSZ` | 1 | Cache line size |
| 0x0d | `PCIR_LATTIMER` | 1 | Latency timer |
| 0x0e | `PCIR_HDRTYPE` | 1 | Header type |
| 0x0f | `PCIR_BIST` | 1 | Built-in self test |
| 0x10 | `PCIR_BAR(0)` | 4 | BAR 0 |
| 0x14 | `PCIR_BAR(1)` | 4 | BAR 1 |
| 0x18 | `PCIR_BAR(2)` | 4 | BAR 2 |
| 0x1c | `PCIR_BAR(3)` | 4 | BAR 3 |
| 0x20 | `PCIR_BAR(4)` | 4 | BAR 4 |
| 0x24 | `PCIR_BAR(5)` | 4 | BAR 5 |
| 0x2c | `PCIR_SUBVEND_0` | 2 | Subsystem vendor |
| 0x2e | `PCIR_SUBDEV_0` | 2 | Subsystem device |
| 0x34 | `PCIR_CAP_PTR` | 1 | Capability list start |
| 0x3c | `PCIR_INTLINE` | 1 | Interrupt line |
| 0x3d | `PCIR_INTPIN` | 1 | Interrupt pin |

### Command Register Bits

| Bit | Macro | Meaning |
|-----|-------|---------|
| 0x0001 | `PCIM_CMD_PORTEN` | Enable I/O space |
| 0x0002 | `PCIM_CMD_MEMEN` | Enable memory space |
| 0x0004 | `PCIM_CMD_BUSMASTEREN` | Enable bus master |
| 0x0008 | `PCIM_CMD_SPECIALEN` | Enable special cycles |
| 0x0010 | `PCIM_CMD_MWRICEN` | Memory write and invalidate |
| 0x0020 | `PCIM_CMD_PERRESPEN` | Parity error response |
| 0x0040 | `PCIM_CMD_SERRESPEN` | SERR# enable |
| 0x0400 | `PCIM_CMD_INTxDIS` | Disable INTx generation |

### Capability IDs (legacy)

| Value | Macro | Meaning |
|-------|-------|---------|
| 0x01 | `PCIY_PMG` | Power Management |
| 0x05 | `PCIY_MSI` | Message Signaled Interrupts |
| 0x09 | `PCIY_VENDOR` | Vendor-specific |
| 0x10 | `PCIY_EXPRESS` | PCI Express |
| 0x11 | `PCIY_MSIX` | MSI-X |

### Extended Capability IDs (PCIe)

| Value | Macro | Meaning |
|-------|-------|---------|
| 0x0001 | `PCIZ_AER` | Advanced Error Reporting |
| 0x0002 | `PCIZ_VC` | Virtual Channel |
| 0x0003 | `PCIZ_SERNUM` | Device Serial Number |
| 0x0004 | `PCIZ_PWRBDGT` | Power Budgeting |
| 0x000d | `PCIZ_ACS` | Access Control Services |
| 0x0010 | `PCIZ_SRIOV` | Single Root I/O Virtualisation |

A reader who needs other PCI constants should open `/usr/src/sys/dev/pci/pcireg.h` directly. The file is well commented; finding a specific offset or bit takes less than a minute.



## Reference: A Comparison with Chapter 16 and Chapter 17 Patterns

A side-by-side comparison of where Chapter 18 extends Chapters 16 and 17 and where it introduces genuinely new material.

| Pattern | Chapter 16 | Chapter 17 | Chapter 18 |
|---------|-----------|-----------|-----------|
| Register access | `CSR_READ_4`, etc. | Same API, unchanged | Same API, unchanged |
| Access log | Introduced | Extended with fault-injection entries | Unchanged |
| Lock discipline | `sc->mtx` around each access | Same, plus callouts | Same |
| File layout | `myfirst_hw.c` added | `myfirst_sim.c` added | `myfirst_pci.c` added |
| Register map | 10 registers, 40 bytes | 16 registers, 60 bytes | Same |
| Attach routine | Simple (`malloc` block) | Simple (`malloc` block plus sim setup) | Real PCI BAR claim |
| Detach routine | Simple | Same plus callout drain | Same plus BAR release |
| Module loading | `kldload` triggers load | Same | `kldload` plus PCI probe |
| Device instance | Global (implicit) | Global | Per-PCI-device, numbered |
| BAR | N/A | N/A | BAR 0, `SYS_RES_MEMORY`, `RF_ACTIVE` |
| Capability walk | N/A | N/A | `pci_find_cap` / `pci_find_extcap` |
| cdev | Created at module load | Same | Created per attach |
| Version | 0.9-mmio | 1.0-simulated | 1.1-pci |
| Documentation | `HARDWARE.md` introduced | `SIMULATION.md` introduced | `PCI.md` introduced |

Chapter 18 builds on Chapters 16 and 17 without breaking anything. Every earlier-chapter capability is preserved; the real-PCI attach is added as a new backend that composes with the existing structure. The driver at `1.1-pci` is a strict superset of the driver at `1.0-simulated`.



## Reference: Patterns From Real FreeBSD PCI Drivers

A short tour of patterns that appear repeatedly in the `/usr/src/sys/dev/` tree. Each pattern is a concrete snippet from a real driver, rewritten slightly for readability, with a pointer to the file and a short note on why the pattern matters. Reading these patterns after Chapter 18 consolidates the vocabulary.

### Pattern: Walking BARs by Type

From `/usr/src/sys/dev/e1000/if_em.c`:

```c
for (rid = PCIR_BAR(0); rid < PCIR_CIS;) {
	val = pci_read_config(dev, rid, 4);
	if (EM_BAR_TYPE(val) == EM_BAR_TYPE_IO) {
		break;
	}
	rid += 4;
	if (EM_BAR_MEM_TYPE(val) == EM_BAR_MEM_TYPE_64BIT)
		rid += 4;
}
```

This loop walks the BAR table looking for the I/O port BAR. It reads each BAR's configuration-space value, checks its type bit, and advances by 4 bytes (one BAR slot) or 8 bytes (two slots, for a 64-bit memory BAR). The loop terminates either at `PCIR_CIS` (the CardBus pointer, which is just beyond the BAR table) or when it finds an I/O BAR.

Why it matters: on drivers that support a mix of memory and I/O BARs across a range of hardware revisions, the BAR layout is not fixed. Walking dynamically is the right approach. Chapter 18's driver targets one device with a known BAR layout and does not need this walker; a driver like `em(4)` that covers a family of chips does.

### Pattern: Matching on Class, Subclass, and progIF

From `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
if (pci_get_class(dev) == PCIC_SIMPLECOMM &&
    pci_get_subclass(dev) == PCIS_SIMPLECOMM_UART &&
    pci_get_progif(dev) < PCIP_SIMPLECOMM_UART_16550A) {
	id = &cid;
	sc->sc_class = &uart_ns8250_class;
	goto match;
}
```

This snippet is a class-based fallback. If the vendor-and-device match failed, the probe falls back to matching any device advertising "simple communications / UART / pre-16550A" in its class code. The progIF field distinguishes 16450, 16550A, and later variants; the snippet specifically targets the older ones.

Why it matters: class codes let a driver attach to families of devices the specific match table does not enumerate. A UART chip from a vendor not in the `uart(4)` table is still handled as long as the class code is standard. The pattern works well for standardised device types (AHCI, xHCI, UART, NVMe, HD Audio) where the programming interface is class-defined.

### Pattern: Conditional MSI Allocation

From `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
id = uart_pci_match(dev, pci_ns8250_ids);
if ((id == NULL || (id->rid & PCI_NO_MSI) == 0) &&
    pci_msi_count(dev) == 1) {
	count = 1;
	if (pci_alloc_msi(dev, &count) == 0) {
		sc->sc_irid = 1;
		device_printf(dev, "Using %d MSI message\n", count);
	}
}
```

This snippet allocates MSI if the device supports it and the driver has not marked the entry with `PCI_NO_MSI`. The `pci_msi_count(dev)` call returns the number of MSI vectors the device advertises; `pci_alloc_msi` allocates them. The `sc->sc_irid = 1` line reflects the rid assigned to the MSI resource (MSI resources start at rid 1; legacy IRQs use rid 0).

Why it matters: MSI is preferred over legacy IRQs on modern systems because it avoids the IRQ-sharing problems of the INTx pin. A driver that supports MSI and falls back to legacy IRQs when MSI is unavailable is the right pattern. Chapter 20 returns to MSI in detail; the snippet here is a preview.

### Pattern: Detach-Time IRQ Release

From `/usr/src/sys/dev/uart/uart_bus_pci.c`:

```c
static int
uart_pci_detach(device_t dev)
{
	struct uart_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_irid != 0)
		pci_release_msi(dev);

	return (uart_bus_detach(dev));
}
```

Detach releases MSI (if allocated) and delegates the rest to `uart_bus_detach`. The `sc->sc_irid != 0` check guards against calling `pci_release_msi` on a driver that used legacy IRQs; releasing MSI when it was not allocated is an error.

Why it matters: every resource allocated at attach must be released at detach. The driver tracks what it allocated through state (here, `sc_irid != 0` means MSI was used) and releases accordingly. Chapter 19 and Chapter 20 will extend Chapter 18's detach in a similar pattern.

### Pattern: Reading Vendor-Specific Configuration Fields

From `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c` (simplified):

```c
cap_offset = 0;
while (pci_find_next_cap(dev, PCIY_VENDOR, cap_offset, &cap_offset) == 0) {
	uint8_t cap_type = pci_read_config(dev,
	    cap_offset + VIRTIO_PCI_CAP_TYPE, 1);
	if (cap_type == VIRTIO_PCI_CAP_COMMON_CFG) {
		/* This is the capability we're looking for. */
		break;
	}
}
```

This walks every vendor-specific capability in the list (ID = `PCIY_VENDOR` = `0x09`), checking each one's vendor-defined type byte until it finds the one the driver wants. The `pci_find_next_cap` function is the iterating version of `pci_find_cap`, picking up where the last call left off.

Why it matters: when multiple capabilities share the same ID (as with virtio's vendor-specific capabilities), the driver must walk and disambiguate by reading the capability's own type field. The `pci_find_next_cap` function exists specifically for this case.

### Pattern: A Power-Aware Resume Handler

From various drivers:

```c
static int
myfirst_pci_resume(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Restore the device to its pre-suspend state. */
	MYFIRST_LOCK(sc);
	CSR_WRITE_4(sc, MYFIRST_REG_CTRL, sc->saved_ctrl);
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);
	MYFIRST_UNLOCK(sc);

	/* Re-enable the user-space interface. */
	return (0);
}
```

A suspend handler saves the device state; the resume handler restores it. The pattern matters for systems that support suspend-to-RAM (S3) or suspend-to-disk (S4); a driver that does not implement suspend and resume prevents the system from entering those states.

Chapter 18's driver does not implement suspend and resume. Chapter 22 adds them.

### Pattern: Responding to a Device-Specific Error State

From `/usr/src/sys/dev/e1000/if_em.c`:

```c
if (reg_icr & E1000_ICR_RXO)
	sc->rx_overruns++;
if (reg_icr & E1000_ICR_LSC)
	em_handle_link(ctx);
if (reg_icr & E1000_ICR_INT_ASSERTED) {
	/* ... */
}
```

After an interrupt, the driver reads the interrupt cause register (`reg_icr`) and dispatches based on which bits are set. Each bit corresponds to a different event: receive overrun, link state change, general interrupt. The driver takes a different action for each.

Why it matters: a real driver handles many event types. The dispatching pattern is familiar from Chapter 17's fault injection, where the simulation could inject different fault types. Chapter 19 will introduce the interrupt-handling version of this pattern.

### Pattern: Using sysctl to Expose Driver Configuration

From any number of drivers:

```c
SYSCTL_ADD_U32(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
    "max_retries", CTLFLAG_RW,
    &sc->max_retries, 0,
    "Maximum retry attempts");
```

Drivers expose tunable parameters through sysctl. The parameter can be read or written from user space with `sysctl dev.myfirst.0.max_retries`. A driver that exposes a handful of such tunables gives its operators a way to adjust behaviour without rebuilding the driver.

Why it matters: sysctl is the right place for per-driver tunables. Kernel command-line options (tunables set at boot time) are for early-boot parameters only; runtime adjustment goes through sysctl.

### Pattern: Recording Supported Features in a Capabilities Structure

From `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`:

```c
sc->vtpci_modern_res.vtprm_common_cfg_cap_off = common_cfg_off;
sc->vtpci_modern_res.vtprm_notify_cap_off = notify_off;
sc->vtpci_modern_res.vtprm_isr_cfg_cap_off = isr_cfg_off;
sc->vtpci_modern_res.vtprm_device_cfg_cap_off = device_cfg_off;
```

The driver stores the offsets of each capability in a per-device state structure. Later code that needs to access a capability's registers reaches it through the stored offset.

Why it matters: after attach, the driver should not need to re-walk the capability list. Storing the offsets at attach time saves a walk on every access. Chapter 18's driver walks the capabilities for informational purposes but does not store the offsets because it does not use them. A real driver that cares about a capability stores its offset.

### Pattern Summary

The patterns above are the common currency of FreeBSD PCI drivers. A reader who recognises them in unfamiliar code is a reader who can learn from any driver in the tree. Chapter 18 taught the base patterns; the real drivers layer specific variations on top. The specific variations are always small (a class-based match here, an MSI allocation there); the base patterns are what recur.

After finishing Chapter 18 and the labs, pick a driver in `/usr/src/sys/dev/` you find interesting (perhaps for a device you own, or perhaps just one whose name you recognise) and read its PCI attach. Use this section as a checklist: which patterns does the driver use? Which ones does it skip? Why? A driver author who has done this exercise three or four times across different drivers has built an enormous fund of pattern recognition.



## Reference: A Closing Note on PCI Driver Philosophy

A paragraph to close the chapter with, worth returning to after the labs.

A PCI driver's job is not to understand the device. A PCI driver's job is to present the device to the kernel in a form the kernel can use. The understanding of the device (what its registers mean, what protocol it speaks, what invariants it maintains) belongs to the driver's upper layers: the hardware abstraction, the protocol implementation, the user-space interface. The PCI layer is a narrow thing. It matches a vendor and device ID. It claims a BAR. It hands the BAR to the upper layers. It registers an interrupt handler. It hands control to the upper layers. It exists to connect two halves of the driver's identity: the device half, which belongs to the hardware, and the software half, which belongs to the kernel.

A reader who has written the Chapter 18 driver has written one PCI layer. It is small. The rest of the driver is what makes it useful. In Chapter 19 the driver's PCI layer will gain one more responsibility (interrupt registration). In Chapter 20 it will gain MSI and MSI-X. In Chapters 20 and 21 it will manage DMA tags. Each of these is a narrow extension of the PCI layer's existing role. None of them changes the PCI layer's fundamental character.

For this reader and for this book's future readers, the Chapter 18 PCI layer is a permanent piece of the `myfirst` driver's architecture. Every later chapter assumes it. Every later chapter extends it. The driver's overall complexity will grow, but the PCI layer will remain what Chapter 18 made it: a connector between the device and the rest of the driver, small and predictable.

The skill Chapter 18 teaches is not "how to write a driver for virtio-rnd". It is "how to connect a driver to a PCI device, regardless of what the device is". That skill is transferable, and it is the skill that will serve you across every PCI driver you write.



## Reference: Chapter 18 Quick-Reference Card

A compact summary of the vocabulary, APIs, macros, and procedures Chapter 18 introduced. Useful as a single-page refresher while working through Chapter 19 and later chapters.

### Vocabulary

- **PCI**: Peripheral Component Interconnect, the shared parallel bus introduced by Intel in the early 1990s.
- **PCIe**: PCI Express, the modern serial successor. Same software-visible model as PCI.
- **B:D:F**: Bus, Device, Function. The address of a PCI device. Written `pciN:B:D:F` in FreeBSD output.
- **Configuration space**: the small metadata area each PCI device exposes. 256 bytes on PCI, 4096 bytes on PCIe.
- **BAR**: Base Address Register. A configuration-space field where a device advertises a range of addresses it needs.
- **Vendor ID**: 16-bit identifier assigned by the PCI-SIG to a manufacturer.
- **Device ID**: 16-bit identifier assigned by the vendor to a specific product.
- **Subvendor/subsystem ID**: secondary 16+16-bit tuple identifying the board.
- **Capability list**: a linked list of optional feature blocks in configuration space.
- **Extended capability list**: the PCIe-specific list, starting at offset `0x100`.

### Essential APIs

- `pci_get_vendor(dev)` / `pci_get_device(dev)`: read cached ID fields.
- `pci_get_class(dev)` / `pci_get_subclass(dev)` / `pci_get_progif(dev)` / `pci_get_revid(dev)`: read cached classification fields.
- `pci_get_subvendor(dev)` / `pci_get_subdevice(dev)`: read cached subsystem identification.
- `pci_read_config(dev, offset, width)` / `pci_write_config(dev, offset, val, width)`: raw configuration-space access (width 1, 2, or 4).
- `pci_find_cap(dev, cap, &offset)` / `pci_find_next_cap(dev, cap, start, &offset)`: walk the legacy capability list.
- `pci_find_extcap(dev, cap, &offset)` / `pci_find_next_extcap(dev, cap, start, &offset)`: walk the PCIe extended capability list.
- `pci_enable_busmaster(dev)` / `pci_disable_busmaster(dev)`: toggle the bus-master enable bit.
- `pci_msi_count(dev)` / `pci_msix_count(dev)`: report MSI and MSI-X vector counts.
- `pci_alloc_msi(dev, &count)` / `pci_alloc_msix(dev, &count)`: allocate MSI or MSI-X vectors (Chapter 20).
- `pci_release_msi(dev)`: release MSI or MSI-X.
- `bus_alloc_resource_any(dev, type, &rid, flags)`: claim a resource (BAR, IRQ, etc.).
- `bus_release_resource(dev, type, rid, res)`: release a claimed resource.
- `rman_get_bustag(res)` / `rman_get_bushandle(res)`: extract the `bus_space` tag and handle.
- `rman_get_start(res)` / `rman_get_size(res)` / `rman_get_end(res)`: inspect a resource's range.
- `bus_space_read_4(tag, handle, off)` / `bus_space_write_4(tag, handle, off, val)`: the low-level accessors.
- `bus_read_4(res, off)` / `bus_write_4(res, off, val)`: resource-based shorthand.

### Essential Macros

- `DEVMETHOD(device_probe, probe_fn)` and similar: populate a method table.
- `DEVMETHOD_END`: terminate a method table.
- `DRIVER_MODULE(name, bus, driver, modev_fn, modev_arg)`: register a driver against a bus.
- `MODULE_DEPEND(name, dep, minver, prefver, maxver)`: declare a module dependency.
- `MODULE_VERSION(name, version)`: declare the driver's version.
- `PCIR_BAR(n)`: compute the configuration-space offset of BAR `n`.
- `BUS_PROBE_DEFAULT`, `BUS_PROBE_GENERIC`, `BUS_PROBE_VENDOR`, `BUS_PROBE_SPECIFIC`: probe priority values.
- `SYS_RES_MEMORY`, `SYS_RES_IOPORT`, `SYS_RES_IRQ`: resource types.
- `RF_ACTIVE`, `RF_SHAREABLE`: resource allocation flags.

### Common Procedures

**Attach a PCI driver to a specific device ID:**

1. Write a probe that reads `pci_get_vendor(dev)` and `pci_get_device(dev)`, compares against a table, returns `BUS_PROBE_DEFAULT` on match and `ENXIO` otherwise.
2. Write an attach that calls `bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE)` with `rid = PCIR_BAR(0)`.
3. Extract the tag and handle with `rman_get_bustag` and `rman_get_bushandle`.
4. Store them where the accessor layer can reach them.

**Release a PCI resource at detach:**

1. Drain any callouts or tasks that might access the resource.
2. Release the resource with `bus_release_resource(dev, type, rid, res)`.
3. Set the stored resource pointer to NULL.

**Unload a conflicting base-system driver before loading your own:**

```sh
sudo kldunload virtio_random   # or whatever driver owns the device
sudo kldload ./myfirst.ko
```

**Force a device to rebind from one driver to another:**

```sh
sudo devctl detach <driver0_name>
sudo devctl set driver -f <pci_selector> <new_driver_name>
```

### Useful Commands

- `pciconf -lv`: list every PCI device with its IDs, class, and driver binding.
- `pciconf -r <selector> <offset>:<length>`: dump configuration-space bytes.
- `pciconf -w <selector> <offset> <value>`: write a configuration-space value.
- `devinfo -v`: dump the newbus tree with resources and bindings.
- `devctl detach`, `attach`, `disable`, `enable`, `rescan`: control bus bindings at runtime.
- `dmesg`, `dmesg -w`: view (and follow) the kernel message buffer.
- `kldstat -v`: list loaded modules with verbose info.
- `kldload`, `kldunload`: load and unload kernel modules.
- `vmstat -m`: report memory allocations by malloc type.

### Files to Keep Bookmarked

- `/usr/src/sys/dev/pci/pcireg.h`: PCI register definitions (`PCIR_*`, `PCIM_*`, `PCIY_*`, `PCIZ_*`).
- `/usr/src/sys/dev/pci/pcivar.h`: PCI accessor function declarations.
- `/usr/src/sys/sys/bus.h`: newbus method and resource macros.
- `/usr/src/sys/sys/rman.h`: resource manager accessors.
- `/usr/src/sys/sys/module.h`: module registration macros.
- `/usr/src/sys/dev/uart/uart_bus_pci.c`: a clean, readable example PCI driver.
- `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`: a modern transport example.



## Reference: Glossary of Chapter 18 Terms

A short glossary for readers who want a compact reminder of Chapter 18's vocabulary.

**AER (Advanced Error Reporting)**: a PCIe extended capability that reports transaction-layer errors to the OS.

**Attach**: the newbus method a driver implements to take ownership of a specific device instance. Called once per device, after probe succeeds.

**Bar (Base Address Register)**: a configuration-space field where a device advertises one range of addresses it needs mapped.

**Bus Master**: a device that initiates its own transactions on the PCI bus. Necessary for DMA. Enabled through the `BUSMASTEREN` bit of the command register.

**Capability**: an optional feature block in configuration space. Discovered by walking the capability list.

**Class code**: a three-byte classification (class, subclass, programming interface) that categorises the device's function.

**cdev**: a character device node in `/dev/`, created by `make_dev(9)`.

**Configuration space**: the per-device metadata area. 256 bytes on PCI, 4096 bytes on PCIe.

**Detach**: the newbus method that undoes everything attach did. Called once per device, when the driver is being unbound.

**device_t**: the opaque handle the newbus layer passes to driver methods.

**DRIVER_MODULE**: a macro that registers a driver against a bus and wraps it as a kernel module.

**ENXIO**: the errno a probe returns to say "I do not match this device".

**EBUSY**: the errno a detach returns to say "I refuse to detach; the driver is in use".

**IRQ**: an interrupt request. In PCI, the `PCIR_INTLINE` configuration-space field holds the legacy IRQ number.

**Legacy interrupt (INTx)**: the pin-based interrupt mechanism inherited from PCI. Superseded by MSI and MSI-X on modern systems.

**MMIO (Memory-Mapped I/O)**: the pattern of accessing device registers through memory-like load and store instructions.

**MSI / MSI-X**: Message Signaled Interrupts; interrupt mechanisms that use writes to specific memory addresses rather than pin assertions. Chapter 20.

**Newbus**: FreeBSD's device tree abstraction. Every device has a parent bus and a driver.

**PCI**: the older parallel bus standard.

**PCIe**: the modern serial successor to PCI. Software-compatible with PCI.

**PIO (Port-Mapped I/O)**: the pattern of accessing device registers through x86 `in` and `out` instructions. Largely obsolete.

**Probe**: the newbus method that tests whether the driver can handle a specific device. Must be idempotent.

**Resource**: a generic name for a kernel-managed device resource (memory range, I/O port range, IRQ). Allocated through `bus_alloc_resource_any(9)`.

**Softc (software context)**: the per-device state structure a driver maintains. Sized through the `driver_t` and allocated by newbus.

**Subclass**: the middle byte of the class code; refines the class.

**Subvendor / subsystem ID**: a second-level identification tuple that refines the primary vendor/device pair for distinct board designs.

**Vendor ID**: the 16-bit manufacturer identifier assigned by the PCI-SIG.



---

_End of Chapter 18._

