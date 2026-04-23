---
title: "Accessing Hardware"
description: "Part 4 opens with the first chapter that teaches the driver how to speak to hardware directly: what hardware I/O means, how memory-mapped I/O differs from port-mapped I/O, how bus_space(9) gives FreeBSD drivers a portable vocabulary for register access, how to simulate a register block in kernel memory so the reader can learn without real hardware, how to integrate register-style access into the evolving myfirst driver, and how to keep MMIO safe under concurrency."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 16
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 195
---

# Accessing Hardware

## Reader Guidance & Outcomes

Part 3 ended with a driver that knew how to coordinate itself. The `myfirst` module at version `0.9-coordination` has a mutex protecting its data path, a pair of condition variables that let readers and writers wait patiently for the buffer state they need, a shared and exclusive lock protecting its configuration, three callouts that give it internal time, a private taskqueue with three tasks that defer work out of constrained contexts, a counting semaphore that caps concurrent writers, an atomic flag that carries the shutdown story across every context, and a small header that names every synchronisation primitive the driver uses. The chapters that built it introduced seven primitives, tied them together with one detach ordering, and documented the whole story in a living `LOCKING.md`.

What the driver does not yet have is a hardware story. Every invariant it coordinates is internal. Every byte that flows through it originates in user space through `write(2)` or is produced by a callout from its own imagination. Nothing in the driver reaches outside the kernel's own memory. A real FreeBSD driver usually exists because there is a device to talk to: a network card, a storage controller, a serial port, a sensor, a custom FPGA, a GPU. That conversation is what Part 4 is about, and Chapter 16 is where it begins.

Chapter 16's scope is deliberately narrow. It teaches the mental model of hardware I/O and the vocabulary of `bus_space(9)`, the FreeBSD abstraction that lets a single driver talk to a device the same way on every supported architecture. It walks through a simulated register block so the reader can practise register-style access without owning real hardware, and it integrates that simulation into the `myfirst` driver in a way that evolves naturally from Chapter 15 rather than throwing the driver away. It covers the safety rules that MMIO demands (memory barriers, access ordering, locking around shared registers) and shows how to debug and trace register-level access. It ends with a small refactor that separates hardware-access code from the driver's business logic, preparing the file layout every later Part 4 chapter will rely on.

Several questions are deliberately postponed so that Chapter 16 can dwell on the vocabulary itself. Dynamic register behaviour, callout-driven status changes, and fault injection belong to Chapter 17. Real PCI devices, BAR mapping, vendor and device ID matching, `pciconf` and `pci(4)`, and the newbus glue that ties PCI drivers to the bus subsystem belong to Chapter 18. Interrupts and their split between filter handlers and interrupt threads are Chapter 19. DMA and bus master programming are Chapter 20 and Chapter 21. Chapter 16 stays inside the ground it can cover well, and it hands off explicitly when a topic deserves its own chapter.

Part 4 opens here, and an opening deserves a small pause. Part 3 taught you how to behave inside the driver when many actors touch shared state. Part 4 teaches how the driver reaches outside itself. An interrupt handler runs in a context Part 3 taught you how to reason about and accesses memory Part 4 will teach you how to map. A register write in Part 4 must respect a lock Part 3 taught you how to manage. The disciplines stack. Chapter 16 is your first practice with `bus_space(9)`; Part 3's discipline is what keeps the practice honest.

### Why bus_space(9) Earns a Chapter of Its Own

You may already be wondering whether MMIO really needs a whole chapter. Why not jump into Chapter 17's simulation or Chapter 18's real PCI work and pick up the accessor vocabulary in passing? If you have used `bus_space(9)` before, the primitive calls in this chapter will not be new to you.

What Chapter 16 adds is the mental model. Hardware I/O is a topic where a small set of ideas done well pays off across every subsequent chapter, and a small set of ideas done sloppily produces quiet, persistent bugs that are hard to find later. The distinction between memory-mapped I/O and port-mapped I/O is simple once you have it and confusing until you do. The meaning of a `bus_space_tag_t` and a `bus_space_handle_t` is simple once you see what they stand for and opaque until then. The reason memory barriers matter around register access is obvious once you have thought about cache coherence and compiler reordering and irrelevant until the day a driver misbehaves for reasons you cannot explain.

The chapter also earns its place by being the chapter where the `myfirst` driver gains its first hardware-facing layer. Until now, the softc held only internal state: a circular buffer, some locks, some counters, some flags. After Chapter 16, the softc will hold a simulated register block and the accessors that read and write it. That change in the driver's shape is small but formative. It sets up the file organisation and the locking discipline that every later Part 4 chapter will extend. Skipping Chapter 16 would leave the reader trying to learn register-access idioms in the middle of learning PCI, interrupts, or DMA. Doing them one at a time is kinder.

### Where Chapter 15 Left the Driver

A short checkpoint before we go further. Chapter 16 extends the driver produced at the end of Chapter 15 Stage 4, tagged as version `0.9-coordination`. If any of the items below feels uncertain, return to Chapter 15 before starting this chapter.

- Your `myfirst` driver compiles cleanly and identifies itself as version `0.9-coordination`.
- The softc holds a data-path mutex (`sc->mtx`), a configuration sx (`sc->cfg_sx`), a stats-cache sx (`sc->stats_cache_sx`), two condition variables (`sc->data_cv`, `sc->room_cv`), three callouts (`heartbeat_co`, `watchdog_co`, `tick_source_co`), a private taskqueue (`sc->tq`) with four tasks (`selwake_task`, `bulk_writer_task`, `reset_delayed_task`, `recovery_task`), and a counting semaphore (`writers_sema`) that caps concurrent writers.
- A header `myfirst_sync.h` encapsulates every synchronisation operation under named inline functions.
- The lock order `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` is documented in `LOCKING.md` and enforced by `WITNESS`.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel, and you have built and booted it.
- The Chapter 15 stress kit runs cleanly under the debug kernel.

That driver is what Chapter 16 extends. The additions are again modest in volume: one new header (`myfirst_hw.h`), one new structure inside the softc (a simulated register block), a handful of accessor helpers, a small hardware-driven task, and a set of barriers and locks around register access. The mental model change is larger than the line count suggests.

### What You Will Learn

Walking away from this chapter, you should be able to:

- Describe what hardware I/O means in a driver context and why a driver cannot usually touch device memory by simply dereferencing a pointer.
- Distinguish memory-mapped I/O (MMIO) from port-mapped I/O (PIO), and explain why MMIO dominates modern FreeBSD drivers on modern platforms.
- Explain what a register is, what an offset is, and what a control or status field means, using the vocabulary real device datasheets use.
- Read a simple register map table and translate it into a C header of offsets and bitmasks.
- Describe the roles of `bus_space_tag_t` and `bus_space_handle_t` and why they are an abstraction rather than just a pointer.
- Recognise the shape of `bus_space_read_*`, `bus_space_write_*`, `bus_space_barrier`, `bus_space_read_multi_*`, `bus_space_read_region_*`, and their `bus_*` shorthand counterparts defined over a `struct resource *`.
- Simulate a register block in kernel memory, wrap access to it behind accessor helpers, and use those helpers to build a small driver-visible device that behaves like a real MMIO device without touching real hardware.
- Integrate simulated register access into the evolving `myfirst` driver, with the data path reading and writing through register accessors instead of touching a raw buffer.
- Identify when a register read has side effects and when a register write does, and why that matters for caching, compiler reordering, and debugging.
- Use `bus_space_barrier` correctly to enforce access ordering where it is needed, and recognise when it is not needed.
- Protect shared register state with the right kind of lock, and avoid busy-wait loops that starve other threads.
- Log register access in a way that helps you debug a driver without drowning the reader in noise.
- Refactor a driver so that its hardware-access layer is a named, documented, testable unit of code.
- Tag the driver as version `0.9-mmio`, update `LOCKING.md` and `HARDWARE.md`, and run the full regression suite with hardware-access enabled.

The list is long; each item is narrow. The point of the chapter is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 16 stays focused.

- **Full hardware simulation with dynamic behaviour.** The simulation in this chapter is static enough to teach the register-access vocabulary. Chapter 17 makes the simulation dynamic, with timers that change status registers, events that flip ready bits, and fault injection paths.
- **The PCI subsystem.** `pci(4)`, vendor and device ID matching, `pciconf`, `pci_enable_busmaster`, BAR mapping, MSI and MSI-X, and power management quirks belong to Chapter 18. Chapter 16 mentions PCI only when it is useful to say "this is where your BAR would come from if this were real".
- **Interrupt handlers.** `bus_setup_intr(9)`, filter handlers, interrupt threads, `INTR_MPSAFE`, and the filter-plus-task split are Chapter 19. Chapter 16 hints at them only to explain why a read with side effects matters.
- **DMA.** `bus_dma(9)`, `bus_dma_tag_create`, `bus_dmamap_load`, bounce buffers, cache flushing around DMA, and scatter-gather lists are Chapter 20 and Chapter 21.
- **Architecture-specific register access oddities.** Weakly-ordered memory models, arm64 device memory attributes, big-endian byte swapping on MIPS and PowerPC, and non-coherent caches on some embedded platforms are mentioned in passing; a deep treatment belongs to the portability chapters.
- **Real-world driver case studies.** Chapter 16 points at `if_ale.c`, `if_em.c`, and `uart_bus_pci.c` as examples of the patterns taught here, but it does not dissect them at length. Later chapters do that work where it fits their own themes.

Staying inside those lines keeps Chapter 16 a chapter about the vocabulary of hardware access. The vocabulary is what transfers; the specific subsystems are what Chapters 17 through 22 apply the vocabulary to.

### Estimated Time Investment

- **Reading only**: three to four hours. The vocabulary is small but it requires thinking carefully about each term.
- **Reading plus typing the worked examples**: seven to nine hours over two sessions. The driver evolves in four stages and each stage is a small but real refactor.
- **Reading plus all labs and challenges**: twelve to fifteen hours over three or four sessions, including stress testing and reading some real FreeBSD drivers.

Section 2 and Section 3 are the densest. If the abstraction of a tag and a handle feels opaque on first pass, that is normal. Stop, re-read the worked mapping in Section 3, and continue when the shape has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 15 Stage 4 (`stage4-final`). The starting point assumes every Chapter 15 primitive, every Chapter 14 taskqueue, every Chapter 13 callout, every Chapter 12 cv and sx, and the Chapter 11 concurrent IO model.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- You understand the Chapter 15 detach ordering well enough to extend it without getting lost.
- You are comfortable reading hexadecimal offsets and bitmasks.

If any item above is shaky, fix it now rather than pushing through Chapter 16 and trying to reason from a moving foundation. Hardware-access code is sensitive to small mistakes, and a debug kernel catches most of them on first contact.

### How to Get the Most Out of This Chapter

Three habits will pay off quickly.

First, keep `/usr/src/sys/sys/bus.h` and `/usr/src/sys/x86/include/bus.h` bookmarked. The `bus.h` file in `/usr/src/sys/sys/` defines the shorthand `bus_read_*` and `bus_write_*` macros over a `struct resource *`. The per-architecture `bus.h` in `/usr/src/sys/x86/include/` (or its equivalent for your platform) defines the lower-level `bus_space_read_*` and `bus_space_write_*` functions and shows you exactly what they compile to. Reading those two files once takes about thirty minutes and removes almost all the mystery from the chapter.

Second, compare every new accessor to what you would have written with plain C. The exercise "if I did not have `bus_space_read_4`, how would I express this register read?" is instructive. The answer on x86 is usually "a `volatile` pointer dereference plus a compiler barrier". Seeing the contrast is how the value of the abstraction becomes concrete: the abstraction is the same code, wrapped in a name that carries portability, tracing, and documentation.

Third, type the changes by hand and run each stage. Hardware-access code is code where muscle memory matters. Typing `sc->regs[MYFIRST_REG_CONTROL]` and `bus_write_4(sc->res, MYFIRST_REG_CONTROL, value)` a dozen times is how the difference between raw access and abstracted access becomes visible at a glance. The companion source under `examples/part-04/ch16-accessing-hardware/` is the reference version, but the muscle memory comes from typing.

### Roadmap Through the Chapter

The sections in order are:

1. **What Is Hardware I/O?** The mental model of register access, how the driver talks to the device without touching its internals, and what kinds of resources drivers care about.
2. **Understanding Memory-Mapped I/O (MMIO).** How devices appear as memory regions, why a raw pointer cast is not how a driver reaches them, and what alignment and endianness mean here.
3. **Introduction to `bus_space(9)`.** The tag and handle abstraction, the shape of the read and write functions, and the difference between the `bus_space_*` family and the `bus_*` shorthand over a `struct resource *`.
4. **Simulating Hardware for Testing.** A register block allocated with `malloc(9)`, shaped to resemble a real device, with accessors that mirror `bus_space` semantics. Stage 1 of the Chapter 16 driver.
5. **Using `bus_space` in a Real Driver Context.** Integrating the simulated block into `myfirst`, exposing a tiny read-write register interface through the driver, and demonstrating how a task can change register state over time. Stage 2 begins here.
6. **Safety and Synchronisation with MMIO.** Memory barriers, access ordering, locking around registers, and why busy-wait loops are a mistake. Stage 3 of the driver adds the safety discipline.
7. **Debugging and Tracing Hardware Access.** Logging, DTrace, sysctl probes, and the small observability layer that makes register access visible without cluttering the driver.
8. **Refactoring and Versioning Your MMIO-Ready Driver.** The final split into `myfirst_hw.h` and `myfirst.c`, the documentation update, and the version bump to `0.9-mmio`. Stage 4 of the driver.

After the eight sections come hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up that closes Part 3's habits and opens Part 4's, and a bridge to Chapter 17. The reference-and-cheat-sheet material at the end of the chapter is meant to be re-read as you work through later Part 4 chapters; the vocabulary of Chapter 16 is reused in every one of them.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3 and 6 stand alone and make good single-sitting reads.



## Section 1: What Is Hardware I/O?

The Chapter 15 driver's world is small. Everything it cares about, it allocates for itself. The circular buffer is a `cbuf_t` inside the softc. The heartbeat is a `struct callout` inside the softc. The writers semaphore is a `struct sema` inside the softc. Every piece of state the driver touches is memory the kernel allocator has given it. Reading from or writing to that state is a plain C memory access: a field assignment, a pointer dereference, or a call to a helper that does one of those things under a lock.

Hardware changes that world. A hardware device is not kernel memory. It is a separate piece of silicon, usually on a different chip from the CPU, with its own registers, its own buffers, its own internal state, and its own rules for how the CPU may communicate with it. A driver's job is to translate the kernel's view of the world, which is software, into the device's view of the world, which is hardware. The first step in that translation is learning how the CPU and the device talk to each other at all.

This section introduces the mental model. Later sections build on it. The goal of Section 1 is not to make you write any code yet. The goal is to establish the vocabulary and the model clearly enough that everything that follows lands softly.

### The Device as a Cooperating Partner

A useful first picture is to think of a hardware device not as an object the driver controls, but as a cooperating partner the driver talks to. The device does some fixed amount of work autonomously. A disk rotates whether the driver is attentive or not. A network card receives packets off the wire without asking permission. A temperature sensor measures temperature continuously. A keyboard controller scans the keyboard matrix on a timer of its own. In every case, the device has internal behaviour the driver cannot directly influence.

What the driver can do is send the device commands and receive the device's status and data. The device exposes a small interface: a set of registers, each with a specific meaning, each with a specific protocol for how the driver may read or write it. The driver writes a value to a control register to tell the device what to do. The driver reads a value from a status register to find out what the device is doing. The driver reads a value from a data register to get data the device has received. The driver writes a value to a data register to send data the device should transmit.

Registers, in this picture, are the conversation. The driver does not call a method on the device and does not pass arguments in the C sense. The driver writes a specific value at a specific offset, and the device reads that write and responds. The device writes a specific value at a specific offset, and the driver reads that write to see what the device is telling it. The protocol is entirely defined by the device's documentation, which is usually a datasheet. The driver's job is to follow the protocol.

That word "partner" is doing a lot of work. Hardware is famously unforgiving. A device does not document every wrong thing the driver could do; it simply does something wrong, or undefined, if the driver breaks the protocol. A driver that writes the wrong value to a control register may brick a device until the next power cycle. A driver that reads a status register before the device has finished a previous command may see stale data and make a bad decision. A driver that does not clear an interrupt flag before returning from an interrupt handler may leave the device thinking the interrupt is still pending. The partner metaphor is cooperative in intent; the actual relationship is one where the driver must be very polite and very attentive, because the device has no way to object except by misbehaving.

### What "Accessing Hardware" Really Means

The phrase "accessing hardware" shows up in every kernel programming text, and it rewards a closer look. What does the CPU actually do when a driver reads a register from a device?

On modern platforms the most common answer is: the CPU issues a memory access to a specific physical address, and the memory controller routes that access to the device rather than to RAM. From the CPU's point of view, it looks like an ordinary load or store. From the device's point of view, it is a message from the CPU addressed to a specific internal register. The wiring in between, the memory controller and the bus fabric, is what makes the routing work.

That is memory-mapped I/O. The CPU uses normal load and store instructions. The address happens to be routed to a device instead of to RAM. The device exposes its registers as a range of physical addresses, and the driver reads and writes within that range the way it would read and write any other memory.

An older, less common answer on x86 CPUs is: the CPU issues a special I/O instruction (`in` or `out`) to a specific I/O port number, and the chipset routes that instruction to the device. The CPU is not using a load or store; it is using a dedicated instruction that operates on a separate address space, called I/O port space or port-mapped I/O. This was the original mechanism on x86 and is still in use for some legacy devices, but modern drivers rarely see it except in compatibility paths.

FreeBSD abstracts both mechanisms behind a single API. The driver does not care, most of the time, whether a register is reached through a memory access or through an I/O instruction. The abstraction is `bus_space(9)`, which Section 3 introduces. For now, notice that "accessing hardware" is a physical operation on the CPU that the OS hides behind a software interface. The driver writes `bus_space_write_4(tag, handle, offset, value)`, and the kernel does the right thing depending on the platform and the resource type.

### Why a Raw Pointer Cast Is Not How Drivers Reach Hardware

A new reader might reasonably ask: if a device appears as a range of physical addresses, why not just take the address of the device's registers, cast it to a `volatile uint32_t *`, and dereference it? Technically, on some platforms, that works. Practically, no FreeBSD driver does this, and several real reasons make the raw cast a poor choice.

First, the driver does not know the physical address of the device at compile time. The address is assigned by the bus enumeration code at boot or hotplug, based on the Base Address Registers (BARs) that the device advertises. The driver's attach routine asks the bus subsystem for the resource; the bus subsystem returns a handle that carries the mapping. The driver then uses the handle through the `bus_space` API. There is no place in the driver's source where the physical address is a constant.

Second, physical addresses are not virtual addresses. The CPU runs in virtual address mode; dereferencing a pointer reads from virtual memory, not physical memory. The kernel's `pmap(9)` layer maintains the translation. A driver that wants to dereference a device's registers needs a virtual mapping into the device's physical range, with the right cache and access attributes. `bus_space_map` does this. A raw pointer cast does not.

Third, different architectures require different access attributes for device memory than for RAM. Device memory must usually be marked uncached, or weakly-cached, or marked as "device memory" in the MMU's page tables, so that the CPU does not reorder or cache the accesses in ways that confuse the device. On arm64, device memory pages use the `nGnRnE` or `nGnRE` attributes that disable speculative prefetching. On x86, the `PAT` and `MTRR` mechanisms mark device regions as uncached or write-combining. A raw pointer cast uses whatever attributes the surrounding virtual mapping happens to have, which is usually cached, which is usually wrong for device registers.

Fourth, `bus_space` carries extra information beyond just where to read and write. The tag encodes which address space (memory or I/O port) is in use. On architectures where the two are different, the tag chooses the correct CPU instruction. On architectures where a driver might map a region with byte-swapping for endianness, the tag encodes that too. The tag-plus-handle interface is a portable way to express "access this device with the right semantics", where a raw pointer is just "poke this virtual address and hope".

Fifth, `bus_space` supports tracing, hardware access auditing, and optional sanity checks through the `bus_san(9)` layer when the kernel is built with sanitisers enabled. A raw pointer cast is invisible to those tools. If you ever want to know when your driver read a particular register, `bus_space` can tell you; a raw dereference cannot.

The short version is: FreeBSD drivers use `bus_space` because it abstracts a real problem the driver needs to solve, and the raw pointer cast works on some platforms by accident rather than by design. Accepting the abstraction is inexpensive; refusing it creates bugs that surface weeks after deployment.

### Categories of Resources a Driver Cares About

Most drivers deal with a small handful of resource categories. Each has a different access pattern. Chapter 16 focuses on one of them (memory-mapped registers), and the others are covered in later chapters. Knowing the whole catalogue helps you place the current topic.

**Memory-mapped I/O (MMIO) registers.** A range of device physical addresses, mapped into the kernel's virtual address space, used to send commands and receive status. Every modern device has at least one MMIO region; most have several. Chapter 16's focus.

**Port-mapped I/O (PIO) registers.** A range of I/O port numbers on x86, accessed through the `in` and `out` CPU instructions. Older devices used this as their primary mechanism. Newer devices sometimes expose a small compatibility window through ports (a legacy serial controller, for example) while putting the main interface in MMIO. The `bus_space` API abstracts both behind the same read and write calls, which is why this chapter treats them together.

**Interrupts.** A signal from the device to the CPU that something has happened. The driver registers an interrupt handler through `bus_setup_intr(9)`, and the kernel arranges for the handler to run when the interrupt line asserts. Chapter 19 covers interrupts.

**DMA channels.** The device reads or writes directly into system RAM, bypassing the CPU. The driver prepares a DMA descriptor that tells the device which RAM addresses it may use. FreeBSD's `bus_dma(9)` API manages the mappings, the cache coherence, and the synchronisation. Chapters 20 and 21 cover DMA.

**Configuration space.** On PCI, a separate address space per device, used to describe the device to the OS. BARs live here, the vendor and device IDs live here, power management state lives here. Most drivers read configuration space only once, during attach, to discover the device's capabilities. Chapter 18 covers PCI configuration space.

**Bus-specific capabilities.** MSI, MSI-X, PCIe extended capabilities, hot-plug events, errata workarounds. Bus-specific chapters cover these.

This chapter lives in the MMIO box. The `bus_space` abstraction also covers PIO, so we will see port-mapped paths in passing, but the working example is MMIO throughout.

### The Register, Closer Up

A register, in the device's language, is a unit of communication. It has a name, an offset, a width, a set of fields, and a protocol.

The **name** is how the datasheet refers to it. `CONTROL`, `STATUS`, `DATA_IN`, `DATA_OUT`, `INTR_MASK`. Names are for humans; the device does not know them.

The **offset** is the distance from the start of the device's register block to the start of this specific register. Offsets are usually given in hexadecimal. `CONTROL` at `0x00`. `STATUS` at `0x04`. `DATA_IN` at `0x08`. `DATA_OUT` at `0x0c`. `INTR_MASK` at `0x10`. The driver uses offsets in every read and write.

The **width** is how many bits the register carries. Common widths are 8, 16, 32, and 64 bits. A 32-bit register is accessed with `bus_space_read_4` and `bus_space_write_4`, where the `4` is the width in bytes. Mismatching the width is a surprisingly common bug; reading a 32-bit register with an 8-bit access reads only one byte, and on some platforms with byte-lane restrictions it may return the wrong byte or no byte at all.

The **fields** inside a register are sub-bit-ranges that each mean something specific. A 32-bit `CONTROL` register might have an `ENABLE` bit at bit 0, a `RESET` bit at bit 1, a 4-bit `MODE` field at bits 4 through 7, and a 16-bit `THRESHOLD` field at bits 16 through 31, with the remaining bits reserved. The driver uses bit masks and shifts to extract or set specific fields, and the datasheet defines every mask and shift.

The **protocol** is the rules the driver must follow when reading or writing the register. Some protocols are trivial ("write this register with the value you want"). Some are subtle ("set the ENABLE bit, then poll the READY bit in STATUS for up to 100 microseconds, then write the DATA_IN register"). Some have side effects that the driver must know about ("reading STATUS clears the error bits"). Getting the protocol right is the bulk of driver development time in many hardware projects.

For Chapter 16 the registers are simple, because the device is simulated. But the vocabulary is the vocabulary of real datasheets, and every term introduced here transfers directly to any real device you will meet later.

### A First Mental Model: The Control Panel

A useful analogy, only if you keep it disciplined, is that of a control panel on an industrial machine. The machine does its work on its own schedule. The panel exposes knobs the operator can turn to tell the machine what to do, gauges the operator can read to see what the machine is doing, and a few lights that go on and off to signal events. The operator does not reach inside the machine; the operator reaches the machine through the panel.

The driver is the operator. The register block is the control panel. A knob on the panel is a control field in a register. A gauge is a status field. A light is a status bit. The wiring behind the panel is the device's internal logic, which the driver does not see and cannot directly influence. The cable between the operator and the panel is `bus_space`: it carries the operator's turns of the knob and the gauge's readings back and forth, in a language the operator and the panel both understand.

The analogy breaks down quickly if pushed. Real hardware has timing constraints the panel does not. Real hardware has side effects that the panel does not. Real hardware speaks a protocol that changes when the machine's internal state changes. But for the first pass, the panel is good enough: the driver writes to a control field, the device reacts, the driver reads a status field, the device tells it what happened.

Later sections replace the analogy with more precise mental models: the register block as a window into device state, the MMIO region as a memory range with side-effects, the `bus_space` interface as a platform-aware messenger. For now, the panel is the on-ramp.

### Why Simulating Hardware Is a Good First Practice

Chapter 16's teaching strategy is to simulate a device rather than require the reader to have a specific piece of real hardware. The reasons are practical and deliberate.

A reader who is practising register-style access for the first time benefits enormously from an environment where they can see the register values directly. A real PCI device's registers are hidden behind a BAR; the reader can read them with `pciconf -r`, but only for specific known offsets, and the values change based on device state in ways a datasheet may not fully document. A simulated device, by contrast, is a struct in kernel memory. The reader can print its contents with a sysctl. The reader can modify it from user space through an ioctl. The reader can inspect it in ddb. The simulation closes the loop between action and observation, which is what makes the practice effective.

A simulated device is also safe. A real device that receives a wrong register value may lock up, corrupt data, or require a physical power cycle. A simulated device that receives a wrong register value does nothing worse than set a wrong bit in kernel memory; if the driver leaks that, `INVARIANTS` will complain. Beginners benefit from the safety net.

A simulated device is reproducible. Every reader running the Chapter 16 code sees the same register values in the same order. A real device's behaviour depends on firmware version, hardware revision, and environmental conditions. Teaching over a reproducible target is much easier than teaching over the union of every possible target.

Chapter 17 expands the simulation with timers, events, and fault injection. Chapter 18 introduces a real PCI device (typically a virtio device in a VM) so the reader can practise the real-hardware path. Chapter 16 lays the ground by teaching the vocabulary against a static simulation, which is the gentlest version of the material.

### A Glance at Real Drivers That Use Hardware I/O

Before moving on, a short tour of real FreeBSD drivers that exercise the patterns Chapter 16 teaches. You do not need to read these files yet; they are waypoints you can return to as the chapter unfolds.

`/usr/src/sys/dev/uart/uart_bus_pci.c` is a PCI glue layer for UART (serial) controllers. It shows how a driver finds its PCI device, claims an MMIO resource, and hands the resource to a lower layer that actually drives the hardware. It is small and readable, and it uses `bus_space` only indirectly.

`/usr/src/sys/dev/uart/uart_dev_ns8250.c` is the real UART driver for the classic 8250-family serial controller. It is the file where register reads and writes happen. The register layout is defined in `uart_bus.h` and `uart_dev_ns8250.h`. The reads use the abstraction the chapter teaches.

`/usr/src/sys/dev/ale/if_ale.c` is an Ethernet driver for the Attansic L1E chipset. Its `if_alevar.h` defines `CSR_READ_4` and `CSR_WRITE_4` macros over `bus_read_4` and `bus_write_4`, which is a pattern you will adopt in your own driver by Stage 4 of this chapter.

`/usr/src/sys/dev/e1000/if_em.c` is the driver for Intel's gigabit Ethernet controllers (e1000 family). It is larger and more complex than `if_ale.c`, but it uses the same `bus_space` vocabulary. Its attach path is a good reference for how a non-trivial driver allocates MMIO resources.

`/usr/src/sys/dev/led/led.c` is the LED driver. It is a pseudo-device driver that does not talk to real hardware at all; it exposes a small interface through `/dev/led.NAME` and delegates the actual LED control to whichever driver registered it. Chapter 16's simulated device borrows the shape of this driver: a small, self-contained module with a clear interface and no dependency on external hardware.

These files will reappear throughout Part 4. Chapter 16 uses them as tour points; later chapters dissect them where their patterns are the chapter's focus.

### What Comes Next in This Chapter

Section 2 moves from the abstract picture to the specific mechanism of memory-mapped I/O. It explains what a mapping is, why device memory is accessed with different rules than ordinary memory, and how the driver can think about alignment, endianness, and caching. Section 3 introduces `bus_space(9)` itself. Section 4 builds the simulated device. Section 5 integrates it into `myfirst`. Section 6 adds the safety discipline. Sections 7 and 8 finish the chapter with debugging, refactoring, and versioning.

The pace from here is slower than Section 1. Section 1 was meant to be read linearly and absorbed as a whole; the later sections are meant to be read section by section, with breaks to type the code and run it.

### Wrapping Up Section 1

Hardware I/O is the activity through which a driver talks to a device. The driver cannot reach inside the device; it can only send commands and read status through a defined register interface. On modern platforms the interface is usually memory-mapped; on x86 there is a legacy port-mapped path as well. The FreeBSD abstraction `bus_space(9)` hides the difference from the driver most of the time. A register is a named, offset-located, width-specific unit of communication, with fields and a protocol that the device's datasheet defines.

Chapter 16's simulation lets you practise the vocabulary without real hardware. Later Part 4 chapters apply the vocabulary to real subsystems. The vocabulary is what transfers, and the rest of this chapter exists to give you that vocabulary with enough depth to use it comfortably.

Section 2 begins by looking closely at memory-mapped I/O.



## Section 2: Understanding Memory-Mapped I/O (MMIO)

Section 1 introduced the idea that a device's registers can be reached through ordinary-looking memory accesses. That idea is worth slowing down for. Memory-mapped I/O is the dominant mechanism on modern FreeBSD platforms, and understanding it well is what makes every later chapter in Part 4 feel tractable rather than mysterious.

This section answers three closely related questions. How does a device appear in memory? Why must the CPU access that memory with different rules than ordinary memory? What does a driver need to think about when reading and writing a register?

The section works from the ground up: physical addresses, virtual mappings, cache attributes, alignment, and endianness. Each piece is small. The composition is where the subtlety lives.

### Physical Addresses and Device Memory

The CPU executes instructions. Each load and store instruction names a virtual address, which the memory management unit (MMU) translates into a physical address. Physical addresses are what the memory controller sees. The memory controller's job is to route the access to the right destination.

For most physical addresses, the destination is DRAM. The controller reads or writes a location in system RAM and returns the result to the CPU. This is the common case. Every `malloc(9)` allocation the driver makes returns kernel memory whose physical address is backed by DRAM.

Some physical address ranges, however, are routed to devices instead. The memory controller is configured at boot (by firmware, usually by the BIOS or UEFI on x86, or by the device tree on arm and the `acpi` tables on everything) to send accesses in certain ranges to specific devices. A PCI device might live at physical address `0xfebf0000` through `0xfebfffff`, a 64 KiB region. An embedded UART might live at `0x10000000` through `0x10000fff`, a 4 KiB region. Whatever the range, an access inside it routes to the device rather than to RAM.

From the CPU's point of view, the access looks identical to an access to RAM. The instruction is the same; the address happens to be elsewhere. From the device's point of view, the access looks like an incoming message: a read at offset X of the device's internal register file, or a write of some value at offset Y.

The key property is that the same CPU instruction (a load or a store) is being reused for a different purpose. That is where "memory-mapped" in MMIO comes from: the device's interface is mapped into the CPU's memory address space, so memory-access instructions reach it.

Port-mapped I/O, the x86 alternative, uses separate instructions (`in`, `out`, and their wider variants) that target a different address space. The port space has its own 16-bit address range on x86. Modern FreeBSD drivers rarely reach port space directly, because modern devices prefer MMIO, but the abstraction is the same: the driver writes a value at an address, and the address happens to route to a device.

### The Virtual Mapping

The CPU does not access physical memory directly. Every memory access goes through the MMU, which translates a virtual address into a physical address using page tables. The kernel maintains those page tables in its `pmap(9)` layer. For a driver to read device registers, it needs a virtual mapping into the device's physical range.

When the kernel's bus subsystem discovers a device and the driver's attach routine requests an MMIO resource, the bus layer does two things. First, it finds the physical address range the device occupies, which the device's Base Address Register (BAR) or the platform's device tree describes. Second, it establishes a virtual mapping from a fresh kernel virtual address range to that physical range, with appropriate cache and access attributes. The result is a virtual address that, when dereferenced, produces an access at the corresponding physical address, which the memory controller then routes to the device.

The handle that `bus_alloc_resource` returns is (on most platforms) a wrapper around that kernel virtual address. The driver does not typically see the address; it passes the resource handle to `bus_space_read_*` and `bus_space_write_*`, which extract the virtual address internally. But the underlying mechanism is a plain virtual-to-physical mapping, set up once at attach and torn down at detach.

This matters for two reasons. First, it explains why `bus_alloc_resource` is not something a driver can skip. Without the resource allocation, there is no virtual mapping; without a virtual mapping, any attempt to access the device will fault or access random memory. Second, it explains why the virtual address is not a constant: the kernel picks it at attach time, and two boots of the same system may produce different addresses.

### Cache Attributes Matter

Memory pages have cache attributes. Ordinary RAM uses "write-back" caching: the CPU caches reads and writes in its L1, L2, and L3 caches, writing back to RAM only when the cache line is evicted or explicitly flushed. Write-back caching is great for performance on RAM, where the memory controller's job is to preserve whatever value the CPU most recently wrote.

Device memory is different. A device's registers usually have side effects on read and on write. Reading a `STATUS` register may consume an event the device has signalled. Writing a `DATA_IN` register may queue data for transmission. Caching a read of a status register means the CPU returns a stale value on the second read; caching a write of a data register means the write goes to the cache and never reaches the device until the cache happens to evict the line.

For these reasons, device memory pages are marked with different cache attributes than ordinary memory. On x86, the attributes are controlled through the PAT (Page Attribute Table) and MTRR (Memory Type Range Registers). Device memory is typically marked `UC` (uncached) or `WC` (write-combining). On arm64, device memory pages use the `Device-nGnRnE` or `Device-nGnRE` attributes, which disable caching and speculation. The specific names are architecture-dependent; the principle is the same: the CPU must treat device memory differently from RAM.

`bus_space_map` (or the equivalent path inside `bus_alloc_resource`) knows to request the right cache attributes when it establishes the virtual mapping. A driver that dereferences a raw pointer into a device region without going through `bus_space` skips this step and gets whatever attributes the surrounding mapping happens to have, which is usually wrong.

This is one of the most concrete reasons to use the FreeBSD abstraction: the abstraction encodes a correctness requirement (uncached access to devices) that a raw pointer cast cannot.

### Alignment

Hardware registers have alignment requirements. A 32-bit register must be accessed with a 32-bit load or store at an offset that is a multiple of 4. A 64-bit register must be accessed with a 64-bit load or store at an offset that is a multiple of 8. On most architectures, an unaligned access to device memory is either slower (decomposed into multiple smaller accesses by the hardware) or outright illegal (trapping with an alignment fault).

The rule for drivers is simple: when reading or writing a register, use the function whose width matches the register's width, and use the correct offset. If the register is 32 bits wide at offset `0x10`, the access is `bus_space_read_4(tag, handle, 0x10)` or `bus_space_write_4(tag, handle, 0x10, value)`. If the register is 16 bits wide at offset `0x08`, it is `bus_space_read_2(tag, handle, 0x08)` or `bus_space_write_2(tag, handle, 0x08, value)`. The byte-wide variants `bus_space_read_1` and `bus_space_write_1` exist for 8-bit registers.

Mismatching the width is a common early-stage bug and often silent on x86, which has very permissive alignment rules. On arm64 the same code may fault on first contact. Drivers that are developed on x86 and then ported to arm64 often trip on exactly this issue, which is why the FreeBSD style guidance favours matching widths strictly from the beginning.

There is also an offset alignment rule. The offset must be a multiple of the access width. A 32-bit read at offset `0x10` is fine (`0x10` is a multiple of 4). A 32-bit read at offset `0x11` is wrong, even if the device nominally has a register starting there; the hardware will usually refuse or return garbage. This rule is easy to follow when the offsets come from a well-named header; it becomes a trap when offsets are computed arithmetically and the arithmetic is wrong.

### Endianness

Device memory and the CPU's native byte order may disagree. A device that originated in a PowerPC or network context may present 32-bit registers in big-endian order, meaning the most significant byte of the register is at the lowest byte address within the register. An x86 CPU is little-endian, so the lowest byte address holds the least significant byte. When the CPU reads the device's big-endian register and interprets it with little-endian semantics, the bytes are in the wrong order.

FreeBSD's `bus_space` family has stream variants (`bus_space_read_stream_*`) and ordinary variants (`bus_space_read_*`). On architectures where the bus tag encodes an endian swap, the ordinary variants swap bytes to produce a host-order value. The stream variants do not swap; they return the bytes in device order. A driver that is reading a device whose registers are in a different endianness than the CPU will use the ordinary variants most of the time, relying on the tag to handle the swap. A driver that is reading a data payload (a stream of bytes whose interpretation depends on the protocol, not on the register layout) may use the stream variants.

On x86 the distinction often does not matter because the bus tag does not encode an endian swap by default. The stream variants are aliases for the ordinary ones in `/usr/src/sys/x86/include/bus.h`:

```c
#define bus_space_read_stream_1(t, h, o)  bus_space_read_1((t), (h), (o))
#define bus_space_read_stream_2(t, h, o)  bus_space_read_2((t), (h), (o))
#define bus_space_read_stream_4(t, h, o)  bus_space_read_4((t), (h), (o))
```

The comment in that file explains: "Stream accesses are the same as normal accesses on x86; there are no supported bus systems with an endianess different from the host one." On other architectures, the two families can differ, and a driver that cares about endianness picks the appropriate variant based on what the device expects.

For Chapter 16, the simulation is designed to be host-endian. The chapter's drivers use the ordinary `bus_space_read_*` and `bus_space_write_*` without worrying about byte swaps. Later chapters that deal with real network controllers will revisit the endianness story.

### Read and Write Side Effects

One of the most important properties of device memory, and one that trips up drivers that treat it as ordinary memory, is that reads and writes can have side effects.

A write to a control register is, by design, a side effect: writing `1` to the `ENABLE` bit tells the device to start operating. The driver expects that side effect, because that is what the register is for. The subtlety is that the write has a side effect on the device even though the value written is also remembered; a driver that writes `0x00000001` to `CONTROL` and then reads `CONTROL` may see `0x00000001` (if the register echoes the written value) or some other value (if the register echoes the device's current state, which may differ from the last written value).

A read from a status register may also be a side effect. Some devices implement "read-to-clear" semantics, where reading the register returns the current status and, as part of the read, clears pending error bits or interrupt flags. A driver that reads the status twice in close succession may see different values on the two reads, because the first read changed the device's internal state. This is by design; the datasheet says so.

Some registers are **write-only**. Reading them returns a fixed value (often all zeros) and reveals nothing about the device. Writing them has the intended effect. A driver that tries to read a write-only register to check its current value will be misled.

Some registers are **read-only**. Writing them is either ignored or hazardous. A driver that writes a read-only register may do nothing (if the hardware is defensive) or may cause undefined behaviour (if it is not).

Some registers are **read-modify-write** unsafe. A naive update pattern (read the current value, modify one field, write the value back) is safe on a register where the read returns the current contents and the write replaces them. It is unsafe on a register where the read has side effects, where the write has side effects on unintended fields, or where another agent (another CPU, a DMA engine, an interrupt handler) can modify the register between the read and the write.

For Chapter 16 the simulated device has a simple protocol: reads and writes each affect only the specific field the caller changes, and no read has side effects. This is not realistic; Chapter 17 introduces read-to-clear and write-only behaviours. For now, the simplicity is a feature: the reader can focus on the mechanics of access without also juggling the device's protocol quirks.

### A Concrete Picture: A Device's Register Block

A concrete example, though invented, makes the picture stick. Imagine a simple temperature-and-fan controller exposed as a 64-byte MMIO region. The register map might look like this:

| Offset | Width  | Name            | Direction | Description                               |
|--------|--------|-----------------|-----------|-------------------------------------------|
| 0x00   | 32 bit | `CONTROL`       | Read/Write| Global enable, reset, and mode bits.      |
| 0x04   | 32 bit | `STATUS`        | Read-only | Device ready, fault, data-available.      |
| 0x08   | 32 bit | `TEMP_SAMPLE`   | Read-only | Most recent temperature reading.          |
| 0x0c   | 32 bit | `FAN_PWM`       | Read/Write| Fan PWM duty cycle (0-255).               |
| 0x10   | 32 bit | `INTR_MASK`     | Read/Write| Per-interrupt enable bits.                |
| 0x14   | 32 bit | `INTR_STATUS`   | Read/Clear| Pending interrupt flags (read-to-clear).  |
| 0x18   | 32 bit | `DEVICE_ID`     | Read-only | Fixed identifier; vendor code.            |
| 0x1c   | 32 bit | `FIRMWARE_REV`  | Read-only | Device firmware revision.                 |
| 0x20-0x3f | 32 bytes | reserved  | -         | Must be written as zero; reads undefined. |

A driver for this device would read `DEVICE_ID` at attach to confirm the hardware is what the driver expects, write `CONTROL` to enable the device, poll `STATUS` to confirm the device is ready, periodically read `TEMP_SAMPLE` to report the temperature, and periodically write `FAN_PWM` to adjust the fan. The interrupt path would read `INTR_STATUS` to see which events are pending (which also clears them) and write `INTR_MASK` during setup to choose which interrupts to enable.

Chapter 16's simulated device borrows heavily from this shape. The simulation has a `CONTROL`, a `STATUS`, a `DATA_IN`, a `DATA_OUT`, an `INTR_MASK`, and an `INTR_STATUS`. It is deliberately a toy; the fields and protocol are chosen so the reader can manipulate them easily from user space through the driver's existing `read(2)` and `write(2)` paths. The register map is kept simple because Chapter 17 will introduce the complexity that real devices add on top.

### The Shape of a Register Access

Putting the pieces together, a single register access consists of:

1. The driver has a `bus_space_tag_t` and a `bus_space_handle_t` that together describe a specific device region with specific cache attributes.
2. The driver picks an offset within the region, corresponding to a register defined in the device's datasheet.
3. The driver picks an access width that matches the register's width.
4. The driver calls `bus_space_read_*` or `bus_space_write_*` with tag, handle, offset, and (for writes) value.
5. The kernel's `bus_space` implementation for the current architecture compiles the call down to the appropriate CPU instruction (a `mov` on x86 MMIO, an `inb`/`outb` on x86 PIO, a `ldr`/`str` on arm64, and so on).
6. The memory controller or I/O fabric routes the access to the device.
7. The device responds: for a read, it returns the requested value; for a write, it performs the action the register's protocol defines.

The abstraction hides all of this from the driver, most of the time. The driver writes `bus_space_read_4(tag, handle, 0x04)` and gets a 32-bit value back. The machinery between the C call and the device is the kernel's and the hardware's job.

What the driver must remain aware of is the handful of correctness rules: alignment, width, side effects, and access ordering. The chapter revisits ordering in Section 6.

### What MMIO Is Not

A short list of things MMIO is not, to clear up common confusions.

**MMIO is not DMA.** DMA is when the device reads or writes system RAM on its own. MMIO is when the CPU reads or writes the device's registers. Both may be used in the same driver, for different purposes. DMA is faster for bulk data; MMIO is necessary for commands and status. Chapter 20 and Chapter 21 cover DMA.

**MMIO is not shared memory.** Shared memory (in the POSIX sense) is RAM accessible to multiple processes. MMIO is device memory accessible to the kernel only. User space cannot (and should not) access MMIO directly; the driver mediates.

**MMIO is not a block of RAM with the device living behind it.** MMIO is a direct interface to the device's internal registers. Reading MMIO does not return kernel memory; it returns whatever the device decides to return at that offset. Writing MMIO does not store a value in kernel memory; it sends a message to the device at that offset.

**MMIO is not free.** Each access is a transaction on the CPU's bus. On a deep cache hierarchy with high memory latency, a single uncached MMIO read can take hundreds of cycles, because the CPU cannot use the cache and must wait for the device to respond. Drivers that issue thousands of MMIO accesses per operation are usually doing something wrong; most operations can be batched or eliminated.

### Wrapping Up Section 2

Memory-mapped I/O is the mechanism by which a modern CPU reaches a device through ordinary load and store instructions, with the address routed to the device instead of to RAM. The kernel's virtual mapping layer and the `bus_space` abstraction together hide the plumbing, but the driver must still be aware of alignment, endianness, cache attributes, and side effects. A register is accessed with a read or write of the right width at the right offset; the kernel compiles the call into the appropriate CPU instruction for the current architecture.

Section 3 introduces `bus_space(9)` itself: the tag, the handle, the read and write functions, and the shape of the API as it appears in every FreeBSD driver that talks to hardware. After Section 3, you will be ready to simulate a register block in Section 4 and start writing code.



## Section 3: Introduction to `bus_space(9)`

`bus_space(9)` is the FreeBSD abstraction for portable hardware access. Every driver that talks to memory-mapped or port-mapped hardware uses it, directly or through a thin wrapper. The abstraction is small: two opaque types, a dozen read and write functions in several widths, a barrier function, and a few helpers for multi-register and region accesses. Section 3 walks through the whole thing in the order a reader would naturally meet it.

The section starts with the two types, moves to the read and write functions, covers the multi and region helpers, introduces the barrier function, and closes with the `bus_*` shorthand defined over a `struct resource *` that most real drivers use in practice. By the end you will recognise every `bus_space` call you meet in `/usr/src/sys/dev/`, and you will have a mental model for writing your own.

### The Two Types: `bus_space_tag_t` and `bus_space_handle_t`

Every `bus_space` call takes a tag and a handle as its first two arguments, in that order. Understanding what each one represents is the first step.

A **`bus_space_tag_t`** identifies an address space. "Address space" here is narrower than its general usage; it specifically refers to the combination of a bus and an access method. On x86, there are two address spaces: memory and I/O port. Each has its own tag value. On other architectures, there may be more: a memory space with host-endian access, a memory space with swapped-endian access, and so on. The tag tells the `bus_space` functions which rules to apply.

The tag is architecture-specific. On x86, the tag is an integer: `0` for I/O port space (`X86_BUS_SPACE_IO`) and `1` for memory space (`X86_BUS_SPACE_MEM`). On arm64, the tag is a pointer to a structure that describes the bus's endian and access behaviour. On MIPS, it is yet another shape. Drivers do not usually see these architecture details; they obtain the tag from the bus subsystem (through `rman_get_bustag(resource)` or equivalent) and pass it through without inspection.

A **`bus_space_handle_t`** identifies a specific region within the address space. It is effectively a pointer, but the pointer's meaning depends on the tag. For a memory tag on x86, the handle is the kernel virtual address at which the device's physical range has been mapped. For an I/O port tag on x86, the handle is the I/O port base address. For more elaborate tags, the handle may be a structure or an encoded value. Drivers treat the handle as opaque and pass it through.

The pairing is important. A tag alone does not identify a specific device; it identifies only the address space. A handle alone does not carry the access rules. The pair (tag, handle) together identifies a specific mappable region with specific access rules, and that pair is what the `bus_space_read_*` and `bus_space_write_*` functions operate on.

In practice a driver obtains a `struct resource *` from the bus subsystem at attach time and extracts the tag and handle from it with `rman_get_bustag` and `rman_get_bushandle`. It stores the pair in the softc, or it stores the resource pointer and uses the shorthand `bus_read_*` and `bus_write_*` macros that extract the tag and handle internally. Section 5 walks through the real pattern.

### Offsets

Every read and write function takes an **offset** inside the region. The offset is a `bus_size_t`, which is typically a 64-bit unsigned integer, measured in bytes from the start of the region. A 32-bit register at the start of a device's MMIO region has offset 0. A 32-bit register at the next slot has offset 4. A 32-bit register at offset `0x10` is 16 bytes into the region.

Offsets are expressed in bytes regardless of the access width. `bus_space_read_4(tag, handle, 0x10)` reads a 32-bit value starting at byte offset `0x10`. `bus_space_read_2(tag, handle, 0x12)` reads a 16-bit value starting at byte offset `0x12`. The function's suffix names the width in bytes, not the offset granularity.

The driver is responsible for ensuring the offset falls within the mapped region. `bus_space` does not check bounds; an out-of-range access is a driver bug that reads or writes whatever happens to be beyond the device's mapping, which on most platforms is either unmapped memory (faulting the kernel) or another device's memory (corrupting that device's state). Keep your offsets in headers, derive them from the datasheet, and never compute them arithmetically without bounds-checking the result.

### The Read Functions

The basic read functions come in four widths:

```c
u_int8_t  bus_space_read_1(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
u_int16_t bus_space_read_2(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
u_int32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
uint64_t  bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
                           bus_size_t offset);
```

The `_1`, `_2`, `_4`, `_8` suffixes are access widths in bytes. `_1` is an 8-bit read, `_2` is a 16-bit read, `_4` is a 32-bit read, `_8` is a 64-bit read. The return type is the corresponding unsigned integer.

Not all widths are supported on all platforms. On x86, `bus_space_read_8` is defined only for `__amd64__` (the 64-bit x86) and only for memory space, not I/O port space. The definition in `/usr/src/sys/x86/include/bus.h` is explicit:

```c
#ifdef __amd64__
static __inline uint64_t
bus_space_read_8(bus_space_tag_t tag, bus_space_handle_t handle,
                 bus_size_t offset)
{
        if (tag == X86_BUS_SPACE_IO)
                return (BUS_SPACE_INVALID_DATA);
        return (*(volatile uint64_t *)(handle + offset));
}
#endif
```

A 64-bit I/O port access returns `BUS_SPACE_INVALID_DATA` (all bits set). A 64-bit memory access dereferences the handle plus offset as a `volatile uint64_t *`. The `volatile` qualifier is what stops the compiler from caching or reordering the access.

The 32-bit case is similar:

```c
static __inline u_int32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
                 bus_size_t offset)
{
        if (tag == X86_BUS_SPACE_IO)
                return (inl(handle + offset));
        return (*(volatile u_int32_t *)(handle + offset));
}
```

Memory space compiles to a `volatile` dereference. I/O port space compiles to an `inl` instruction that reads a long from an I/O port.

The 16-bit (`inw`, `*(volatile u_int16_t *)`) and 8-bit (`inb`, `*(volatile u_int8_t *)`) cases follow the same pattern. On a 64-bit x86, `bus_space_read_4` on a memory region compiles to a single `mov` instruction from the mapped address. The cost of the abstraction at runtime, on this common platform, is literally one instruction's worth of call-frame setup if the inline expands, which it does in release builds.

### The Write Functions

The write functions mirror the read functions:

```c
void bus_space_write_1(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int8_t value);
void bus_space_write_2(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int16_t value);
void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, u_int32_t value);
void bus_space_write_8(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, uint64_t value);
```

On x86 memory space, a write compiles to a `volatile` store:

```c
static __inline void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t bsh,
                  bus_size_t offset, u_int32_t value)
{
        if (tag == X86_BUS_SPACE_IO)
                outl(bsh + offset, value);
        else
                *(volatile u_int32_t *)(bsh + offset) = value;
}
```

Port-mapped I/O compiles to an `outl`. The driver writes the same source line regardless of platform; the kernel's per-architecture `bus.h` does the rest.

As with reads, `bus_space_write_8` to I/O port space on x86 is not supported; the function silently returns without emitting a write. This reflects the hardware: x86 I/O ports are 32-bit at most.

### The Multi and Region Helpers

Sometimes a driver wants to read or write many values from or to a single register, or many values across a range of registers. The `bus_space` API provides two families of helpers.

**Multi accesses** repeatedly access a single register, transferring a buffer of values through it. The register stays at a fixed offset; the buffer is consumed or produced. The use case is a FIFO-style register, where the device's internal queue is exposed through a single address, and reading or writing that address pops or pushes one entry.

```c
void bus_space_read_multi_1(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int8_t *buf, size_t count);
void bus_space_read_multi_2(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int16_t *buf, size_t count);
void bus_space_read_multi_4(bus_space_tag_t tag, bus_space_handle_t handle,
                            bus_size_t offset, u_int32_t *buf, size_t count);
```

`bus_space_read_multi_4(tag, handle, 0x20, buf, 16)` reads a 32-bit value from offset `0x20` sixteen times, storing each value in successive entries of `buf`. The offset does not change between reads; only the buffer pointer advances.

The write variants mirror the reads:

```c
void bus_space_write_multi_1(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int8_t *buf, size_t count);
void bus_space_write_multi_2(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int16_t *buf, size_t count);
void bus_space_write_multi_4(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, const u_int32_t *buf, size_t count);
```

**Region accesses** transfer across a range of offsets. The offset advances each step; the buffer advances each step. The use case is a memory-like region inside the device, such as a block of configuration data or a frame buffer slice.

```c
void bus_space_read_region_1(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, u_int8_t *buf, size_t count);
void bus_space_read_region_4(bus_space_tag_t tag, bus_space_handle_t handle,
                             bus_size_t offset, u_int32_t *buf, size_t count);
void bus_space_write_region_1(bus_space_tag_t tag, bus_space_handle_t handle,
                              bus_size_t offset, const u_int8_t *buf, size_t count);
void bus_space_write_region_4(bus_space_tag_t tag, bus_space_handle_t handle,
                              bus_size_t offset, const u_int32_t *buf, size_t count);
```

`bus_space_read_region_4(tag, handle, 0x100, buf, 16)` reads 16 consecutive 32-bit values starting at offset `0x100` and ending at offset `0x13c`, storing them in `buf[0]` through `buf[15]`.

The distinction between multi and region corresponds to two different hardware patterns. A FIFO register at a single offset is a multi; a configuration block that spans many offsets is a region. Using the wrong family leaves the driver doing the wrong thing, even if the loop count matches, so take care to pick the right one.

Chapter 16's simulated device does not use multi or region accesses; the driver addresses registers one at a time. Chapter 17's richer simulation and later PCI-based chapters introduce the multi and region patterns where they apply.

### The Barrier Function

`bus_space_barrier` is the function that most drivers forget exists until they need it, and its correct use is one of the quiet disciplines of solid hardware programming.

```c
void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
                       bus_size_t offset, bus_size_t length, int flags);
```

The function enforces ordering on `bus_space` reads and writes issued before the call, relative to those issued after. The `flags` argument is a bitmask:

- `BUS_SPACE_BARRIER_READ` makes prior reads complete before subsequent reads.
- `BUS_SPACE_BARRIER_WRITE` makes prior writes complete before subsequent writes.
- The two may be OR-ed together to enforce both directions.

The `offset` and `length` parameters describe the region the barrier applies to. On x86 these are ignored; the barrier applies to the whole CPU. On other architectures, a bus bridge may be able to enforce barriers more narrowly, and the parameters are informative.

On x86 specifically, `bus_space_barrier` compiles to a small and well-defined sequence. From `/usr/src/sys/x86/include/bus.h`:

```c
static __inline void
bus_space_barrier(bus_space_tag_t tag __unused, bus_space_handle_t bsh __unused,
                  bus_size_t offset __unused, bus_size_t len __unused, int flags)
{
        if (flags & BUS_SPACE_BARRIER_READ)
#ifdef __amd64__
                __asm __volatile("lock; addl $0,0(%%rsp)" : : : "memory");
#else
                __asm __volatile("lock; addl $0,0(%%esp)" : : : "memory");
#endif
        else
                __compiler_membar();
}
```

A read barrier on amd64 emits a `lock addl` on the stack, which is a cheap way to issue a full memory fence on x86. A write barrier emits only a compiler barrier (`__compiler_membar()`), because x86 hardware retires writes in program order and the only reordering a driver can experience on writes is from the compiler. The distinction between "the CPU might reorder this" and "the compiler might reorder this" matters, and the x86 `bus_space_barrier` encodes it with minimum cost.

On arm64 the barrier compiles to a `dsb` or `dmb` instruction depending on the flags, because arm64's memory model is weaker and actual CPU reordering is possible. The driver's source does not change; the same `bus_space_barrier` call picks the right instruction for each platform.

When is a barrier required? The rule of thumb is: when the correctness of one register access depends on another access having completed first. Examples:

- A driver writes a command to `CONTROL` and reads the result from `STATUS`. The read must not be speculated before the write. A `bus_space_barrier(tag, handle, 0, 0, BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ)` between them enforces the ordering.
- A driver clears an interrupt flag in `INTR_STATUS` and expects the clear to reach the device before re-enabling interrupts. A write barrier after the clear, before the re-enable, is the correct discipline.
- A driver posts a DMA descriptor to memory and then writes a "doorbell" register to tell the device to process it. A write barrier between the memory write and the doorbell write is required on weakly-ordered platforms.

On x86, many of these cases are handled by the platform's strong ordering model, and a driver written without explicit barriers often works. The same driver ported to arm64 may fail subtly. The rule "use barriers when ordering matters" produces portable code; the rule "barriers do nothing on x86 so skip them" produces code that breaks on half of FreeBSD's supported platforms.

Section 6 of this chapter revisits barriers with worked examples in the simulated driver.

### The `bus_*` Shorthand over a `struct resource *`

The `bus_space_*` family takes a tag and a handle. In practice, drivers do not usually carry those around; they carry a `struct resource *`, which is what `bus_alloc_resource_any` returns. The resource structure contains the tag and the handle, among other things. Passing them separately would be noise.

To eliminate the noise, `/usr/src/sys/sys/bus.h` defines a family of shorthand macros that take a `struct resource *` and extract the tag and handle internally:

```c
#define bus_read_1(r, o) \
    bus_space_read_1((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_2(r, o) \
    bus_space_read_2((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_read_4(r, o) \
    bus_space_read_4((r)->r_bustag, (r)->r_bushandle, (o))
#define bus_write_1(r, o, v) \
    bus_space_write_1((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_write_4(r, o, v) \
    bus_space_write_4((r)->r_bustag, (r)->r_bushandle, (o), (v))
#define bus_barrier(r, o, l, f) \
    bus_space_barrier((r)->r_bustag, (r)->r_bushandle, (o), (l), (f))
```

There are equivalents for `_multi` and `_region` variants, stream variants, and the barrier. The macros cover the same functionality as the underlying `bus_space_*` family, just with a more compact call shape.

Most drivers in `/usr/src/sys/dev/` use the shorthand. A typical usage looks like this, adapted from `if_alevar.h`:

```c
#define CSR_READ_4(sc, reg)       bus_read_4((sc)->res[0], (reg))
#define CSR_WRITE_4(sc, reg, val) bus_write_4((sc)->res[0], (reg), (val))
```

The driver defines its own `CSR_READ_4` and `CSR_WRITE_4` macros in terms of `bus_read_4` and `bus_write_4`, abstracting one more layer on top. The softc holds an array of `struct resource *` pointers, and the macros reach through to the first one (the main MMIO region) without the driver having to write out the resource dereference every time.

This is a deliberate pattern. It makes register access statements short and scanable. It centralises the resource reference in one place, so if the driver later maps a second region, only the macros change. And it gives the driver's code a consistent look that anyone familiar with `/usr/src/sys/dev/` will recognise immediately.

Chapter 16's simulated driver adopts this pattern by Stage 4. The early stages use the `bus_space_*` family directly, to keep the mechanism visible; the final refactor wraps the accesses in `CSR_READ_*` and `CSR_WRITE_*` macros the way a production driver would.

### Setup and Teardown

A driver that uses `bus_space` does not call `bus_space_map` directly in most cases. Instead, it asks the bus subsystem for a resource through `bus_alloc_resource_any`:

```c
int rid = 0;
struct resource *res;

res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (res == NULL) {
        device_printf(dev, "cannot allocate MMIO resource\n");
        return (ENXIO);
}
```

The arguments are:

- `dev` is the `device_t` for the driver's device.
- `SYS_RES_MEMORY` selects a memory-mapped resource. `SYS_RES_IOPORT` selects a port-mapped resource. `SYS_RES_IRQ` selects an IRQ (used in Chapter 19).
- `rid` is the "resource ID", the index of the resource within the device's resources. A PCI device's first BAR is usually rid 0 (which for a legacy PCI device corresponds to the BAR at PCI config offset `0x10`). `rid` is a pointer because the bus subsystem may update it to reflect the actual rid it used, though for `_any` allocations on a known rid, the value passed in is usually returned unchanged.
- `RF_ACTIVE` tells the bus to activate the resource immediately, which includes establishing the virtual mapping. Without `RF_ACTIVE`, the resource is reserved but not mapped.

On success, `res` is a valid `struct resource *` whose tag and handle can be extracted with `rman_get_bustag(res)` and `rman_get_bushandle(res)`, or whose tag and handle are used implicitly by the `bus_read_*` and `bus_write_*` shorthand macros.

At detach, the driver releases the resource:

```c
bus_release_resource(dev, SYS_RES_MEMORY, rid, res);
```

The release undoes the allocation, including tearing down the virtual mapping and marking the range available for reuse.

This is the boilerplate every driver follows for MMIO resources. Chapter 16's simulated device skips it entirely, because there is no bus to allocate from; the "resource" is a chunk of kernel memory the driver allocated with `malloc(9)`. Chapter 17 introduces a slightly more sophisticated simulation that mimics the allocation path. Chapter 18, when real PCI enters the picture, uses the full `bus_alloc_resource_any` flow.

### A First Stand-Alone Example

Even without real hardware, a simple stand-alone program illustrates the shape of a `bus_space` call. Imagine a driver that wants to read the 32-bit `DEVICE_ID` register at offset `0x18` from a device whose MMIO region has been allocated as `res`:

```c
uint32_t devid = bus_read_4(sc->res, 0x18);
```

One line. The `sc->res` holds the `struct resource *`. The offset `0x18` comes from the datasheet. The return value is the 32-bit contents of the register.

To write a control value:

```c
bus_write_4(sc->res, 0x00, 0x00000001); /* set ENABLE bit */
```

To enforce ordering between the write and a subsequent read:

```c
bus_write_4(sc->res, 0x00, 0x00000001);
bus_barrier(sc->res, 0, 0, BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
uint32_t status = bus_read_4(sc->res, 0x04);
```

The barrier ensures the write reaches the device before the read is issued. On x86 the barrier is cheap; on arm64 it emits a fence instruction. The driver does not know or care which; the abstraction handles it.

These are three-line shapes that will appear, with small variations, in every driver you write in Part 4 and beyond. The patterns look identical whether the target is a real network card, a USB controller, a storage adapter, or a simulated device.

### A Look at a Real Driver's `bus_space` Usage

To connect the vocabulary to real code, open `/usr/src/sys/dev/ale/if_alevar.h` and scroll to the `CSR_WRITE_*` / `CSR_READ_*` macro block. You will find:

```c
#define CSR_WRITE_4(_sc, reg, val)    \
        bus_write_4((_sc)->ale_res[0], (reg), (val))
#define CSR_WRITE_2(_sc, reg, val)    \
        bus_write_2((_sc)->ale_res[0], (reg), (val))
#define CSR_WRITE_1(_sc, reg, val)    \
        bus_write_1((_sc)->ale_res[0], (reg), (val))
#define CSR_READ_2(_sc, reg)          \
        bus_read_2((_sc)->ale_res[0], (reg))
#define CSR_READ_4(_sc, reg)          \
        bus_read_4((_sc)->ale_res[0], (reg))
```

The softc stores an array `ale_res[]` of resources; the macros reach into the first slot. Everywhere else in the driver, a register access reads as `CSR_READ_4(sc, ALE_SOME_REG)` and reads naturally.

Or open `/usr/src/sys/dev/e1000/if_em.c` and search for `bus_alloc_resource_any`. You will find:

```c
sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
```

The resource goes into the softc's `memory` field; the rest of the driver uses macros over `sc->memory`. The pattern repeats in every driver you will meet in Part 4.

Chapter 16 builds up to this pattern gradually. Stage 1 uses plain struct access to emphasise the mechanics. Stage 2 introduces `bus_space_*` directly against a simulated handle. Stage 3 adds barriers and locking. Stage 4 wraps everything in `CSR_*` macros over a `struct resource *`-compatible pointer, matching the real-driver idiom.

> **A note on line numbers.** The chapter cites FreeBSD source by function, macro, or structure name rather than by line number, because line numbers drift between releases while symbol names survive. For approximate coordinates in FreeBSD 14.3, for orientation only: the `CSR_WRITE_*` macros in `if_alevar.h` sit near line 228, `em_allocate_pci_resources` in `if_em.c` near line 2415, `ale_attach` in `if_ale.c` near line 451, and the `ale_attach` resource-alloc and register-read block spans roughly lines 463 to 580. Open the file and jump to the symbol; the line is whatever your editor reports.

### Wrapping Up Section 3

`bus_space(9)` is a small, focused abstraction over hardware access. A tag identifies an address space; a handle identifies a specific region within it. Read and write functions come in 8, 16, 32, and 64-bit widths. Multi accesses repeat on a single offset; region accesses walk across offsets. Barriers enforce ordering where it matters. The `bus_*` shorthand over a `struct resource *` is what most drivers use day to day.

The mechanism underneath compiles to CPU instructions that match the platform: a `mov` on x86 MMIO, an `in` or `out` on x86 PIO, a `ldr` or `str` on arm64. The driver writes portable code; the compiler does the translation.

Section 4 now takes you from vocabulary to practice. We build a simulated register block in kernel memory, wrap it with accessor helpers, and start Stage 1 of the Chapter 16 driver refactor.



## Section 4: Simulating Hardware for Testing

Real hardware is a tough teacher. It is expensive to buy, fragile to mishandle, inconsistent across revisions, and unkind to beginners. For Chapter 16's purposes we want something different: an environment where the reader can practise register-style access, see the results, break things safely, and observe what happens. The answer is to simulate a device in kernel memory.

This section builds that simulation from scratch. First a mental model (what does "simulate a device" mean?), then a register map for the device we are going to fake, then the allocation, the accessors, and the first integration with the `myfirst` driver. By the end of Section 4 the driver has Stage 1: a softc that carries a register block, accessors that read and write it, and a couple of sysctls that let you poke at the simulation from user space.

### What "Simulating Hardware" Means Here

The simulation is deliberately minimal in Section 4. A chunk of kernel memory, allocated once, sized to match a register block, and accessed through functions that look like `bus_space` calls. Reads fetch values from the chunk; writes store values into the chunk. There is no dynamic behaviour yet: no timers changing a status register, no events setting a ready bit, no fault injection. Chapter 17 adds all of that. Section 4 gives you the skeleton.

This narrowness is deliberate. Chapter 16's job is to teach the access mechanism. A richer simulation, where the reader has to reason about both the mechanism and the device's behaviour, would compete for attention with the vocabulary the reader is still learning. Section 4's simulation exists so every register read and write returns a predictable result, which lets the reader focus on correctness of access rather than on whether the device liked the access or not.

A small but important point about the simulation: because the "device" is kernel memory, the reader can inspect it, poke it, and dump it through mechanisms the kernel already provides (`sysctl`, `ddb`, `gdb` on a core dump). This transparency is a pedagogical feature. A real device's registers are only visible through the register interface; the simulated registers are visible through the interface *and* through the allocator. When something goes wrong in the driver, the reader can compare "what the driver thinks the register is" with "what the register actually contains". That debugging pathway is very educational and will be lost when we eventually point the driver at real hardware.

### The Register Map for the Simulated Device

Before allocating anything, decide what the device looks like. Picking a register map up front is exactly what a datasheet does for real hardware, and doing it before writing code is a habit worth building.

The Chapter 16 simulated device is a minimal "widget": it can accept a command, report a status, receive a single byte of data, and send a single byte back. The register map is:

| Offset | Width  | Name            | Direction | Description                                             |
|--------|--------|-----------------|-----------|---------------------------------------------------------|
| 0x00   | 32 bit | `CTRL`          | Read/Write| Control: enable, reset, mode bits.                      |
| 0x04   | 32 bit | `STATUS`        | Read-only | Status: ready, busy, error, data available.            |
| 0x08   | 32 bit | `DATA_IN`       | Write-only| Data written to the device for processing.              |
| 0x0c   | 32 bit | `DATA_OUT`      | Read-only | Data the device has produced.                           |
| 0x10   | 32 bit | `INTR_MASK`     | Read/Write| Interrupt enable mask.                                  |
| 0x14   | 32 bit | `INTR_STATUS`   | Read/Clear| Pending interrupt flags (read-to-clear, Chapter 17).    |
| 0x18   | 32 bit | `DEVICE_ID`     | Read-only | Fixed identifier: 0x4D594649 ('MYFI').                 |
| 0x1c   | 32 bit | `FIRMWARE_REV`  | Read-only | Firmware revision: encoded as major<<16 | minor.        |
| 0x20   | 32 bit | `SCRATCH_A`     | Read/Write| Free scratch register. Always echoes writes.            |
| 0x24   | 32 bit | `SCRATCH_B`     | Read/Write| Free scratch register. Always echoes writes.            |

The total size is 40 bytes of register space, which we round up to 64 bytes to give ourselves room to grow in Chapter 17.

For Chapter 16, all register access is simplified to direct read-and-write on kernel memory. Read-to-clear semantics on `INTR_STATUS`, write-only semantics on `DATA_IN`, and the behaviour of `CTRL` on reset are deferred to Chapter 17. For now, `DATA_IN` echoes whatever the driver wrote; `INTR_STATUS` holds whatever value the driver last set; and the whole block behaves like a plain block of 32-bit slots.

This is deliberate. Chapter 16 is teaching register access. Chapter 17 introduces the protocol layer. Splitting the two keeps each chapter focused.

### The Register Offsets Header

A real driver separates register offsets into a header so the datasheet mapping lives in one place. The Chapter 16 driver follows the same discipline. Create a file `myfirst_hw.h` alongside `myfirst.c`:

```c
/* myfirst_hw.h -- Chapter 16 simulated register definitions. */
#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

/* Register offsets for the simulated myfirst widget. */
#define MYFIRST_REG_CTRL         0x00
#define MYFIRST_REG_STATUS       0x04
#define MYFIRST_REG_DATA_IN      0x08
#define MYFIRST_REG_DATA_OUT     0x0c
#define MYFIRST_REG_INTR_MASK    0x10
#define MYFIRST_REG_INTR_STATUS  0x14
#define MYFIRST_REG_DEVICE_ID    0x18
#define MYFIRST_REG_FIRMWARE_REV 0x1c
#define MYFIRST_REG_SCRATCH_A    0x20
#define MYFIRST_REG_SCRATCH_B    0x24

/* Total size of the register block. */
#define MYFIRST_REG_SIZE         0x40

/* CTRL register bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u   /* bit 0: device enabled      */
#define MYFIRST_CTRL_RESET       0x00000002u   /* bit 1: reset (write 1 to)  */
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u   /* bits 4..7: operating mode  */
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u   /* bit 8: loopback DATA_IN -> OUT */

/* STATUS register bits. */
#define MYFIRST_STATUS_READY     0x00000001u   /* bit 0: device ready        */
#define MYFIRST_STATUS_BUSY      0x00000002u   /* bit 1: device busy         */
#define MYFIRST_STATUS_ERROR     0x00000004u   /* bit 2: error latch         */
#define MYFIRST_STATUS_DATA_AV   0x00000008u   /* bit 3: DATA_OUT has data   */

/* INTR_MASK and INTR_STATUS bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u   /* bit 0: data available      */
#define MYFIRST_INTR_ERROR       0x00000002u   /* bit 1: error condition     */
#define MYFIRST_INTR_COMPLETE    0x00000004u   /* bit 2: operation complete  */

/* Fixed identifier values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u   /* 'MYFI' in little-endian    */
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE \
        ((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

#endif /* _MYFIRST_HW_H_ */
```

Every offset is a named constant. Every bit mask has a name. Every fixed value has a constant. Later chapters add more registers and more bits; the header grows incrementally. The discipline of "no magic numbers inside the driver's code" starts here and pays off throughout Part 4.

A note on the `u` suffix on the numeric constants. The `u` makes each constant an `unsigned int`, which is important when the value has the high bit set (32-bit registers use the full `0x80000000` bit, which a plain `int` constant cannot represent portably). Using `u` everywhere keeps the driver consistent; getting into the habit prevents the class of bug where a signed-vs-unsigned mismatch leads to a sign-extended comparison that silently passes or fails.

### Allocating the Register Block

With the offsets defined, the driver needs a register block. For the simulation, the block is kernel memory. Add the following to the softc (in `myfirst.c` where the softc is declared):

```c
struct myfirst_softc {
        /* ... all existing Chapter 15 fields ... */

        /* Chapter 16: simulated MMIO register block. */
        uint8_t         *regs_buf;      /* malloc'd register storage */
        size_t           regs_size;     /* size of the register region */
};
```

`regs_buf` is a byte pointer to an allocation. Using `uint8_t *` rather than `uint32_t *` makes the per-byte offset arithmetic in the accessors straightforward; we cast to the appropriate width at each access.

Before the allocation itself, a small but useful improvement. The Chapter 15 driver uses `M_DEVBUF`, the kernel's generic driver-memory bucket, for its allocations. That works, but it blurs our driver's footprint with every other driver on the system: `vmstat -m` reports the aggregated usage under `devbuf`, with no way to tell what came from `myfirst`. Chapter 16 is a good moment to introduce a per-driver malloc type. Near the top of `myfirst.c`, alongside the other file-scoped declarations:

```c
static MALLOC_DEFINE(M_MYFIRST, "myfirst", "myfirst driver allocations");
```

`MALLOC_DEFINE` registers a new malloc bucket named `myfirst`, with the long description used by `vmstat -m`. Every allocation the driver makes from this chapter onward is tagged with `M_MYFIRST`, so `vmstat -m` can report the driver's total memory use directly. Chapter 15's allocations that previously used `M_DEVBUF` can be migrated to `M_MYFIRST` in the same pass, or left alone; the practical difference is small and the migration is purely cosmetic.

With the type defined, the allocation happens in `myfirst_attach`, before any code that might access the registers:

```c
/* In myfirst_attach, after softc initialisation, before registering /dev nodes. */
sc->regs_size = MYFIRST_REG_SIZE;
sc->regs_buf = malloc(sc->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);

/* Initialise fixed registers to their documented values. */
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_DEVICE_ID)   = MYFIRST_DEVICE_ID_VALUE;
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_FIRMWARE_REV) = MYFIRST_FW_REV_VALUE;
*(uint32_t *)(sc->regs_buf + MYFIRST_REG_STATUS)       = MYFIRST_STATUS_READY;
```

`M_WAITOK | M_ZERO` produces a zero-filled allocation that can sleep to complete if memory is tight, which is fine at attach time. `M_WAITOK` is the right choice because the caller is the kernel's attach path, which is a process context and can block; `M_NOWAIT` would be required only from a callout or filter-interrupt context.

The initialisation writes three fixed values: the device ID, the firmware revision, and an initial `STATUS` with the `READY` bit set. A real device would set these through hardware logic at power-on; the simulation does them explicitly in code.

The teardown is symmetric, in `myfirst_detach`:

```c
/* In myfirst_detach, after all consumers of regs_buf have quiesced. */
if (sc->regs_buf != NULL) {
        free(sc->regs_buf, M_MYFIRST);
        sc->regs_buf = NULL;
        sc->regs_size = 0;
}
```

As always in the Chapter 11-15 tradition, the free happens after every code path that could touch the memory has finished. By the time we reach this point in detach, the callouts are drained, the taskqueue is drained, the cdev is destroyed, and no syscall can reach the driver.

A subtle but important point: the allocation uses `malloc(9)` rather than `contigmalloc(9)` or `bus_dmamem_alloc(9)`. For simulation, any kernel memory works. For real hardware with DMA requirements, the allocation would need to be physically contiguous, page-aligned, and bounce-buffered as appropriate; that is Chapter 20's topic, not ours.

### The First Accessor Helpers

Direct struct access through raw casts (`*(uint32_t *)(sc->regs_buf + MYFIRST_REG_CTRL)`) works but is ugly, unsafe (no bounds checking), and inconsistent with the `bus_space` idiom the chapter is teaching. Replace it with named accessors.

In `myfirst_hw.h`, add function prototypes and inline definitions:

```c
/* Simulated accessor helpers. Stage 1: direct memory, no barriers. */

static __inline uint32_t
myfirst_reg_read(uint8_t *regs_buf, size_t regs_size, bus_size_t offset)
{
        KASSERT(offset + 4 <= regs_size,
            ("myfirst: register read past end of register block: "
             "offset=%#x size=%zu", (unsigned)offset, regs_size));
        return (*(volatile uint32_t *)(regs_buf + offset));
}

static __inline void
myfirst_reg_write(uint8_t *regs_buf, size_t regs_size, bus_size_t offset,
    uint32_t value)
{
        KASSERT(offset + 4 <= regs_size,
            ("myfirst: register write past end of register block: "
             "offset=%#x size=%zu", (unsigned)offset, regs_size));
        *(volatile uint32_t *)(regs_buf + offset) = value;
}
```

Two helpers: one read, one write. Each bounds-checks the offset with `KASSERT` so an out-of-range access is caught immediately on a debug kernel. Each uses `volatile` to prevent the compiler from caching or reordering the access. `bus_size_t` is the same type `bus_space` uses for offsets; using it keeps the accessors compatible with the later transition.

A driver that wants to read `STATUS` from its softc now writes:

```c
uint32_t status = myfirst_reg_read(sc->regs_buf, sc->regs_size, MYFIRST_REG_STATUS);
```

Two arguments of boilerplate per call feels like a lot. Real drivers wrap their accessors in shorter macros that take the softc directly. Let us do the same:

```c
#define MYFIRST_REG_READ(sc, offset) \
        myfirst_reg_read((sc)->regs_buf, (sc)->regs_size, (offset))
#define MYFIRST_REG_WRITE(sc, offset, value) \
        myfirst_reg_write((sc)->regs_buf, (sc)->regs_size, (offset), (value))
```

Now the register access reads:

```c
uint32_t status = MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS);
```

Short, named, scannable. The macros do not add cost beyond the inline expansion the compiler would have done anyway.

One more helper worth introducing for Stage 1. A common operation is "read a register, modify one field, write it back":

```c
static __inline void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
        uint32_t value;

        value = MYFIRST_REG_READ(sc, offset);
        value &= ~clear_mask;
        value |= set_mask;
        MYFIRST_REG_WRITE(sc, offset, value);
}
```

The helper reads the register, clears the bits named in `clear_mask`, sets the bits named in `set_mask`, and writes the result back. A typical use:

```c
/* Clear the ENABLE bit in CTRL. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE, 0);

/* Set the ENABLE bit in CTRL. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_ENABLE);

/* Change MODE to 0x3, keeping other bits intact. */
myfirst_reg_update(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_MODE_MASK,
    3 << MYFIRST_CTRL_MODE_SHIFT);
```

A caveat: `myfirst_reg_update` as written is not atomic. Between the read and the write, another context could read the same register, modify it, and write it back; our write would then overwrite the other context's update. For Stage 1 this is acceptable, because the simulated registers are accessed only from the syscall context and are not shared with interrupts or tasks yet. Section 6 revisits the atomicity story and introduces locking around the update.

### Exposing the Registers Through Sysctls

To make the Stage 1 register block observable without writing a user-space tool, expose each register as a read-only sysctl. In `myfirst_attach`, alongside the other sysctl definitions:

```c
/* Chapter 16, Stage 1: sysctls that read the simulated registers. */
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ctrl",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_CTRL,
    myfirst_sysctl_reg, "IU", "Control register (read-only view)");

SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_status",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_STATUS,
    myfirst_sysctl_reg, "IU", "Status register (read-only view)");

SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_device_id",
    CTLTYPE_UINT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, MYFIRST_REG_DEVICE_ID,
    myfirst_sysctl_reg, "IU", "Device ID register (read-only view)");
```

(Equivalent entries for each interesting register follow the same pattern. The examples/part-04 source has the full list.)

The sysctl handler translates the arg1/arg2 pair into a register read:

```c
static int
myfirst_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        value = MYFIRST_REG_READ(sc, offset);
        return (sysctl_handle_int(oidp, &value, 0, req));
}
```

With these sysctls in place, the reader can type:

```text
# sysctl dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_ctrl: 0
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1
# sysctl dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_device_id: 1298498121
```

`1298498121` in decimal is `0x4D594649`, the fixed device ID. `1` in `reg_status` is the `READY` bit. These are the values the attach path set; the reader can see them from user space. The loop from "the driver writes a register" to "the reader observes the register value" is closed.

### A Writeable Sysctl for `CTRL` and `DATA_IN`

Reading is half the story. The Stage 1 simulation also benefits from a writeable sysctl that lets the reader poke register values:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ctrl_set",
    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE, sc, MYFIRST_REG_CTRL,
    myfirst_sysctl_reg_write, "IU",
    "Control register (writeable, Stage 1 test aid)");
```

With the write handler:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;
        int error;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        value = MYFIRST_REG_READ(sc, offset);
        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        MYFIRST_REG_WRITE(sc, offset, value);
        return (0);
}
```

The handler reads the current value, accepts a new value from the caller, writes it back. Writes are unrestricted for now; later sections add validation and side effects.

The reader can now experiment:

```text
# sysctl dev.myfirst.0.reg_ctrl_set
dev.myfirst.0.reg_ctrl_set: 0
# sysctl dev.myfirst.0.reg_ctrl_set=1
dev.myfirst.0.reg_ctrl_set: 0 -> 1
# sysctl dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_ctrl: 1
```

Setting the `ctrl_set` sysctl to `1` enables the (notional) device by setting the `ENABLE` bit in `CTRL`. Reading `reg_ctrl` confirms it. The loop is now complete: user space writes, the register updates, user space reads, the value matches.

### The Observer Pattern: Coupling Registers to Driver State

At this point the driver has a register block that is inert. Writing to `CTRL` does not make the driver do anything. Reading from `STATUS` returns whatever was last written. The registers exist; the driver ignores them.

Stage 1's closing step couples two small pieces of driver state to the register block, so that user-space observation of registers reflects something real.

The first coupling: clear the `READY` bit in `STATUS` while the driver is in a reset state, and set it when the driver is attached and operational. In `myfirst_attach`, immediately after allocating `regs_buf`:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_READY);
```

In `myfirst_detach`, before freeing `regs_buf`:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_STATUS, 0);
```

(In practice, detach's `free(regs_buf)` makes the clearing pointless, but the explicit clear documents the intent and mirrors how a real driver would signal the device that the driver is going away.)

The second coupling: if the user-space caller sets `CTRL.ENABLE`, set a soft flag in the softc that the driver uses to decide whether to emit heartbeat output. If the user clears it, the flag clears. This needs a small change in the writeable sysctl handler and a short routine that applies the change:

```c
static void
myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old, uint32_t new)
{
        if ((old & MYFIRST_CTRL_ENABLE) != (new & MYFIRST_CTRL_ENABLE)) {
                device_printf(sc->dev, "CTRL.ENABLE now %s\n",
                    (new & MYFIRST_CTRL_ENABLE) ? "on" : "off");
        }
        /* Other fields will grow in later stages. */
}
```

The writeable sysctl handler calls it after updating the register:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t oldval, newval;
        int error;

        if (sc->regs_buf == NULL)
                return (ENODEV);
        oldval = MYFIRST_REG_READ(sc, offset);
        newval = oldval;
        error = sysctl_handle_int(oidp, &newval, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        MYFIRST_REG_WRITE(sc, offset, newval);

        /* Apply side effects of specific registers. */
        if (offset == MYFIRST_REG_CTRL)
                myfirst_ctrl_update(sc, oldval, newval);

        return (0);
}
```

Now writing `1` to `reg_ctrl_set` produces a `device_printf` in `dmesg` noting the transition. Writing `0` to `reg_ctrl_set` produces another. The register is no longer inert; it drives an observable behaviour.

This is a tiny example of a pattern that will recur: the register is a control surface, the driver reacts to register changes, user-space (or in real drivers, the device) triggers those changes. In Chapter 17 we automate the device side with a callout that changes registers on a timer; in Chapter 18 we point the driver at a real PCI device.

### What Stage 1 Accomplished

At the end of Section 4, the driver has:

- A `myfirst_hw.h` header with register offsets, bit masks, and fixed values.
- A `regs_buf` in the softc, allocated at attach and freed at detach.
- Accessor helpers (`myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`) and macros (`MYFIRST_REG_READ`, `MYFIRST_REG_WRITE`) that wrap access.
- Sysctls that expose several registers for read and a writeable sysctl for one of them.
- A small coupling between `CTRL.ENABLE` and a driver-level printf.

The version tag becomes `0.9-mmio-stage1`. The driver still does everything Chapter 15 gave it; it has simply grown a register-shaped appendage.

Build, load, and test:

```text
# cd examples/part-04/ch16-accessing-hardware/stage1-register-map
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0 | grep reg_
# sysctl dev.myfirst.0.reg_ctrl_set=1
# dmesg | tail
# sysctl dev.myfirst.0.reg_ctrl_set=0
# dmesg | tail
# kldunload myfirst
```

You should see the ENABLE transitions in `dmesg` and the register values changing through `sysctl`. If any step fails, Section 4's troubleshooting entries at the end of the chapter catch the most common problems.

### A Note on What Stage 1 Is Not

Stage 1 is a register-shaped state container. It is not yet using `bus_space(9)` at the API level. The accessors are plain C memory access behind a named helper. Section 5 takes the next step: replace those helpers with real `bus_space_*` calls that operate on the same kernel memory, so the driver's access pattern matches what a real driver with a real resource would look like.

The reason to introduce the abstraction in two passes is pedagogical. Stage 1 makes the mechanism visible: you can see exactly what `MYFIRST_REG_READ` does under the covers. Stage 2 replaces the visible mechanism with the portable API, and you can compare the two and see that the API is doing nothing the helper was not already doing, only more portably and with platform-appropriate barriers where needed. The two-step teaches both.

### Wrapping Up Section 4

Simulating hardware starts with a register map and a chunk of kernel memory. A small header declares the offsets, bit masks, and fixed values. A softc field holds the allocation. Accessor helpers wrap read and write. Sysctls expose the registers so the reader can observe and poke them from user space. Small couplings between registers and driver-level behaviour make the abstraction concrete.

The driver is now at Stage 1 of Chapter 16. Section 5 integrates `bus_space(9)` into this setup, replacing the direct accessors with the portable API the rest of FreeBSD uses.



## Section 5: Using `bus_space` in a Real Driver Context

Section 4 gave the driver a simulated register block and direct accessors. Section 5 replaces the direct accessors with the `bus_space(9)` API and integrates register access into the `myfirst` data path. The shape of the driver changes in a few small but deliberate ways, all of which look more like a real hardware driver than the Stage 1 version did.

This section starts with the smallest possible change (replace the accessor bodies with `bus_space_*` calls) and builds up. By the end, the driver's `write(2)` path produces a register access as a side effect, the driver's `read(2)` path reflects register state, and a task can change register values on a timer so the reader can watch the device "breathe" without user-space poking.

### The Pedagogical Trick: Using `bus_space` on Kernel Memory

A minor sleight-of-hand that makes Section 5 possible: on x86, the `bus_space_read_*` and `bus_space_write_*` functions for memory space compile to a plain `volatile` dereference of `handle + offset`. The handle is just a `uintptr_t`-shaped value that the functions cast to a pointer. If we set the handle to `(bus_space_handle_t)sc->regs_buf`, and the tag to `X86_BUS_SPACE_MEM`, the `bus_space_read_4(tag, handle, offset)` call will do `*(volatile u_int32_t *)(regs_buf + offset)`, which is exactly what our Stage 1 accessor did.

That means, on x86 at least, we can drive our simulated register block through the real `bus_space` API by filling in a tag and handle that point at our `malloc`'d memory. The driver's code then becomes indistinguishable, at the source level, from a driver that is accessing real MMIO. That is the whole point: the vocabulary transfers.

On non-x86 platforms, the trick is slightly less clean. On arm64 and some other architectures, `bus_space_tag_t` is a pointer to a structure describing the bus, and using a manufactured tag requires more setup. For Chapter 16 the simulation path is x86-centric; the chapter acknowledges the architectural limitation and defers portability-across-architectures to the later portability chapter. The lessons Chapter 16 teaches are universally applicable; only this one shortcut for simulation is x86-specific.

### Stage 2: Setting Up the Simulated Tag and Handle

Add two fields to the softc:

```c
struct myfirst_softc {
        /* ... all existing fields ... */

        /* Chapter 16 Stage 2: simulated bus_space tag and handle. */
        bus_space_tag_t  regs_tag;
        bus_space_handle_t regs_handle;
};
```

In `myfirst_attach`, after allocating `regs_buf`:

```c
#if defined(__amd64__) || defined(__i386__)
sc->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation path supports x86 only; see text for portability note."
#endif
sc->regs_handle = (bus_space_handle_t)(uintptr_t)sc->regs_buf;
```

That is all the setup. The handle is the virtual address of the allocation cast through `uintptr_t`; the tag is the architecture's constant for memory space.

The `#error` for non-x86 is a deliberate teaching signal: the chapter explicitly flags what is portable (the vocabulary) and what is not (this specific simulation shortcut). Chapter 17 and the portability chapter will teach a cleaner alternative. Until then, x86 is the supported platform for the chapter's exercises.

### Replacing the Accessors

With the tag and handle set up, the accessors become one-liners over `bus_space_*`:

```c
static __inline uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        KASSERT(offset + 4 <= sc->regs_size,
            ("myfirst: register read past end of block: offset=%#x size=%zu",
             (unsigned)offset, sc->regs_size));
        return (bus_space_read_4(sc->regs_tag, sc->regs_handle, offset));
}

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        KASSERT(offset + 4 <= sc->regs_size,
            ("myfirst: register write past end of block: offset=%#x size=%zu",
             (unsigned)offset, sc->regs_size));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);
}
```

The signature changes: the helpers now take a `struct myfirst_softc *` rather than `regs_buf` and `regs_size` separately. Internal bounds checking is retained; the KASSERT fires if a bug in the driver produces an out-of-range offset. The body uses `bus_space_read_4` and `bus_space_write_4` instead of direct memory access.

The `MYFIRST_REG_READ` and `MYFIRST_REG_WRITE` macros simplify accordingly:

```c
#define MYFIRST_REG_READ(sc, offset)        myfirst_reg_read((sc), (offset))
#define MYFIRST_REG_WRITE(sc, offset, value) myfirst_reg_write((sc), (offset), (value))
```

Every register access in the driver, including the sysctl handlers and `myfirst_reg_update`, continues to use these macros. None of the call sites change. The driver's behaviour is identical to Stage 1, but the access path now travels through `bus_space`, and the path would work just as well if `regs_tag` and `regs_handle` came from `rman_get_bustag` and `rman_get_bushandle` on a real resource.

Build the driver and confirm it still passes the Stage 1 sysctl exercises:

```text
# cd examples/part-04/ch16-accessing-hardware/stage2-bus-space
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_device_id: 1298498121
# sysctl dev.myfirst.0.reg_ctrl_set=1
# dmesg | tail
# kldunload myfirst
```

The output matches Stage 1. The driver now uses `bus_space` the way a real driver does.

### Exposing `DATA_IN` Through the Write Path

With the accessor layer in place, Stage 2 couples the driver's `write(2)` syscall to the `DATA_IN` register. Every byte written to the device file `/dev/myfirst0` now ends up at the `DATA_IN` register, where the reader can observe it.

Modify `myfirst_write` (the `d_write` callback). The existing handler reads bytes from the uio, copies them into the ring buffer, signals waiters, and returns. The new handler does the same, plus: just before returning, it writes the most recently copied byte to `DATA_IN` and sets the `DATA_AV` bit in `STATUS`:

```c
static int
myfirst_write(struct cdev *cdev, struct uio *uio, int flag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        uint8_t buf[MYFIRST_BOUNCE];
        size_t n;
        int error = 0;
        uint8_t last_byte = 0;
        bool wrote_any = false;

        /* ... existing writer-cap and lock acquisition ... */

        while (uio->uio_resid > 0) {
                n = MIN(uio->uio_resid, sizeof(buf));
                error = uiomove(buf, n, uio);
                if (error != 0)
                        break;

                /* Remember the most recent byte for the register update. */
                if (n > 0) {
                        last_byte = buf[n - 1];
                        wrote_any = true;
                }

                /* ... existing copy into the ring buffer ... */
        }

        /* ... existing unlock and cv_signal ... */

        /* Chapter 16 Stage 2: reflect the last byte in DATA_IN. */
        if (wrote_any) {
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN,
                    (uint32_t)last_byte);
                myfirst_reg_update(sc, MYFIRST_REG_STATUS,
                    0, MYFIRST_STATUS_DATA_AV);
        }

        return (error);
}
```

Now, after any `echo foo > /dev/myfirst0`, the `DATA_IN` register contains the byte value of `'o'` (the last character of `"foo\n"` is `\n`, actually, which is `0x0a`), and the `DATA_AV` bit in `STATUS` is set. The reader can observe this through sysctl:

```text
# echo -n "Hello" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in
dev.myfirst.0.reg_data_in: 111
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 9
```

`111` is the ASCII code for `'o'`, the last byte of "Hello". `9` is `MYFIRST_STATUS_READY | MYFIRST_STATUS_DATA_AV` (`1 | 8 = 9`). The driver has, for the first time, produced an externally-observable register-level side effect in response to user-space action.

### Exposing `DATA_OUT` Through the Read Path

Symmetrically, every byte read from `/dev/myfirst0` can update `DATA_OUT` to reflect what was last read. Modify `myfirst_read`:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int flag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        uint8_t buf[MYFIRST_BOUNCE];
        size_t n;
        int error = 0;
        uint8_t last_byte = 0;
        bool read_any = false;

        /* ... existing blocking logic and lock acquisition ... */

        while (uio->uio_resid > 0) {
                /* ... existing ring-buffer extraction ... */

                if (n > 0) {
                        last_byte = buf[n - 1];
                        read_any = true;
                }

                error = uiomove(buf, n, uio);
                if (error != 0)
                        break;
        }

        /* ... existing unlock and cv_signal ... */

        /* Chapter 16 Stage 2: reflect the last byte in DATA_OUT. */
        if (read_any) {
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_OUT,
                    (uint32_t)last_byte);
                /* If the ring buffer is now empty, clear DATA_AV. */
                if (cbuf_is_empty(&sc->cb))
                        myfirst_reg_update(sc, MYFIRST_REG_STATUS,
                            MYFIRST_STATUS_DATA_AV, 0);
        }

        return (error);
}
```

Now `DATA_OUT` reflects the last byte the reader read, and `DATA_AV` clears when the ring buffer drains. The loop from "user writes a byte" to "driver updates register" to "user reads a byte" to "driver updates registers" is closed.

Testing:

```text
# echo -n "ABC" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_in: 67
dev.myfirst.0.reg_status: 9
# dd if=/dev/myfirst0 bs=1 count=3 of=/dev/null
# sysctl dev.myfirst.0.reg_data_out dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_out: 67
dev.myfirst.0.reg_status: 1
```

`67` is `'C'`, the last byte written. After the `dd` consumes all three bytes, `DATA_OUT` holds `'C'` (the last byte read) and `STATUS` is back to just `READY` because `DATA_AV` cleared.

### Driving Register State From a Task

The register block so far reflects driver actions triggered by user-space syscalls. To illustrate a task-driven pattern, add a small task that periodically increments `SCRATCH_A`. This is an artificial example; it exists so the reader can see register values changing autonomously in response to task-triggered events, preparing for Chapter 17 where callouts and timers drive more realistic changes.

In the softc:

```c
struct task     reg_ticker_task;
int             reg_ticker_enabled;
```

The task callback:

```c
static void
myfirst_reg_ticker_cb(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (!myfirst_is_attached(sc))
                return;

        MYFIRST_REG_WRITE(sc, MYFIRST_REG_SCRATCH_A,
            MYFIRST_REG_READ(sc, MYFIRST_REG_SCRATCH_A) + 1);
}
```

The task is enqueued from the existing tick_source callout (Chapter 14's callout that already fires on a timer). In the callout callback, alongside the selwake task enqueue:

```c
if (sc->reg_ticker_enabled)
        taskqueue_enqueue(sc->tq, &sc->reg_ticker_task);
```

And a sysctl to enable it:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "reg_ticker_enabled",
    CTLFLAG_RW, &sc->reg_ticker_enabled, 0,
    "Enable the periodic register ticker (increments SCRATCH_A each tick)");
```

Initialisation in attach:

```c
TASK_INIT(&sc->reg_ticker_task, 0, myfirst_reg_ticker_cb, sc);
sc->reg_ticker_enabled = 0;
```

Drain in detach, in the existing task drain block:

```c
taskqueue_drain(sc->tq, &sc->reg_ticker_task);
```

With this in place, enabling the ticker produces a visible register effect:

```text
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 5
# sysctl dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_a: 5
# sleep 5
# sysctl dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_a: 10
# sysctl dev.myfirst.0.reg_ticker_enabled=0
```

The register value climbs at one per second, as the tick_source callout fires. The driver is now exhibiting autonomous register-level behaviour, triggered by a task, mediated by `bus_space`.

### The Full Stage 2 Sysctl Tree

After Stage 2 the full sysctl tree under `dev.myfirst.0` looks roughly like:

```text
dev.myfirst.0.debug_level
dev.myfirst.0.soft_byte_limit
dev.myfirst.0.nickname
dev.myfirst.0.heartbeat_interval_ms
dev.myfirst.0.watchdog_interval_ms
dev.myfirst.0.tick_source_interval_ms
dev.myfirst.0.bulk_writer_batch
dev.myfirst.0.reset_delayed
dev.myfirst.0.writers_limit
dev.myfirst.0.writers_sema_value
dev.myfirst.0.writers_trywait_failures
dev.myfirst.0.stats_cache_refresh_count
dev.myfirst.0.reg_ctrl
dev.myfirst.0.reg_status
dev.myfirst.0.reg_data_in
dev.myfirst.0.reg_data_out
dev.myfirst.0.reg_intr_mask
dev.myfirst.0.reg_intr_status
dev.myfirst.0.reg_device_id
dev.myfirst.0.reg_firmware_rev
dev.myfirst.0.reg_scratch_a
dev.myfirst.0.reg_scratch_b
dev.myfirst.0.reg_ctrl_set
dev.myfirst.0.reg_ticker_enabled
```

Ten register views, one writeable register, one ticker toggle, plus every previous sysctl from Chapters 11 through 15. The driver has grown, but every addition is small and named.

### A Note on Reads From `STATUS` While the Driver Is Running

In the Stage 1 and Stage 2 setups, reading `STATUS` via sysctl returns whatever bits the driver most recently set. No read has side effects. This is intentional for Chapter 16. But notice a subtle consequence: the driver can set the `STATUS.DATA_AV` bit in the write path and clear it in the read path, and the user-space reader can observe the bit change over time. Running `sysctl -w dev.myfirst.0.reg_status=0` is possible through the writeable sysctl, but the driver's automatic updates will re-set the bit on the next write to the device file.

This is how a "polled" device driver works at a conceptual level: the driver polls the status register periodically, reacts to state changes, and updates driver-visible state accordingly. A real device's `STATUS` bits change for hardware reasons; the simulated device's bits change for simulated-driver reasons. The mechanism is the same.

Chapter 19 introduces interrupts, which replace the polling model with an event-driven one. Until then, polling is a reasonable pattern for the simulated device.

### The Detach Path, Updated

Every chapter in Part 3 added a few lines to the detach ordering. Chapter 16 Stage 2 adds two: drain the `reg_ticker_task` and free the register buffer. The full detach order at Stage 2:

1. Refuse detach if `active_fhs > 0`.
2. Clear `is_attached` (atomically), broadcast cvs.
3. Drain all callouts (heartbeat, watchdog, tick_source).
4. Drain all tasks (selwake, bulk_writer, reset_delayed, recovery, reg_ticker).
5. `seldrain(&sc->rsel)`, `seldrain(&sc->wsel)`.
6. `taskqueue_free(sc->tq)`.
7. Destroy cdev.
8. Release sysctl context.
9. **Free `regs_buf`.** (New in Stage 2.)
10. Destroy cbuf, counters, cvs, sx locks, semaphore, mutex.

The `regs_buf` free happens after the sysctl context is torn down, because a sysctl handler could in principle be running on another CPU during detach. After `sysctl_ctx_free`, no sysctl handler can reach the softc, and the free is safe. Real drivers follow the same discipline for their resource releases.

### Updating `LOCKING.md` (Now `HARDWARE.md` Too)

Part 3 established `LOCKING.md` as the driver's synchronisation map. Chapter 16 opens a sibling document: `HARDWARE.md`. It lives alongside `LOCKING.md` and documents the register interface, access patterns, and ownership rules for hardware-facing state.

A first cut:

```text
# myfirst Hardware Interface

## Register Block

Size: 64 bytes (MYFIRST_REG_SIZE).
Access: 32-bit reads and writes on 32-bit-aligned offsets.
Allocated in attach, freed in detach.

### Register Map

| Offset | Name          | Direction | Owner      |
|--------|---------------|-----------|------------|
| 0x00   | CTRL          | R/W       | driver    |
| 0x04   | STATUS        | R/W       | driver    |
| 0x08   | DATA_IN       | W         | syscall   |
| 0x0c   | DATA_OUT      | R         | syscall   |
| 0x10   | INTR_MASK     | R/W       | driver    |
| 0x14   | INTR_STATUS   | R/W       | driver    |
| 0x18   | DEVICE_ID     | R         | attach    |
| 0x1c   | FIRMWARE_REV  | R         | attach    |
| 0x20   | SCRATCH_A     | R/W       | ticker    |
| 0x24   | SCRATCH_B     | R/W       | free      |

## Write Protections

Stage 2 does not lock register access. A sysctl writer, a syscall
writer, and the ticker task can each access the same register without
a lock. See Section 6 for the locking story.

## Access Paths

- Sysctl read handlers:  MYFIRST_REG_READ
- Sysctl write handlers: MYFIRST_REG_WRITE, with side-effect call
- Syscall write path:    MYFIRST_REG_WRITE(DATA_IN), myfirst_reg_update(STATUS)
- Syscall read path:     MYFIRST_REG_WRITE(DATA_OUT), myfirst_reg_update(STATUS)
- Ticker task:           MYFIRST_REG_WRITE(SCRATCH_A)
```

The document is short now and will grow as later chapters add more registers and more access paths. The discipline of documenting the register interface alongside the code is the same as Part 3's discipline of documenting locks.

### Stage 2 Complete

At the end of Section 5, the driver is at `0.9-mmio-stage2`. It has:

- A real `bus_space_*` access path over a simulated tag and handle.
- `DATA_IN` reflecting the last written byte, with `DATA_AV` tracking the ring buffer's state.
- `DATA_OUT` reflecting the last read byte.
- A task that autonomously increments a scratch register on a timer.
- Full sysctl visibility into every register.
- A `HARDWARE.md` document describing the interface.

Build, load, test, observe, unload:

```text
# cd examples/part-04/ch16-accessing-hardware/stage2-bus-space
# make clean && make
# kldload ./myfirst.ko
# echo -n "hello" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_data_in dev.myfirst.0.reg_status
# dd if=/dev/myfirst0 bs=1 count=5 of=/dev/null
# sysctl dev.myfirst.0.reg_data_out dev.myfirst.0.reg_status
# sysctl dev.myfirst.0.reg_ticker_enabled=1 ; sleep 3
# sysctl dev.myfirst.0.reg_scratch_a
# sysctl dev.myfirst.0.reg_ticker_enabled=0
# kldunload myfirst
```

The outputs should tell a consistent story about what the driver and the registers are doing.

### A Look at a Real Pattern: The `em` Attach Path

Now that Stage 2 mirrors a real driver's structure, a brief look at what Stage 2 would look like if the register block were a real PCI MMIO region, just to anchor the expectation. In `/usr/src/sys/dev/e1000/if_em.c`, inside `em_allocate_pci_resources`, you will find:

```c
sc->memory = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
if (sc->memory == NULL) {
        device_printf(dev, "Unable to allocate bus resource: memory\n");
        return (ENXIO);
}
sc->osdep.mem_bus_space_tag = rman_get_bustag(sc->memory);
sc->osdep.mem_bus_space_handle = rman_get_bushandle(sc->memory);
sc->hw.hw_addr = (uint8_t *)&sc->osdep.mem_bus_space_handle;
```

The resource is allocated, the tag and handle are extracted into the softc's `osdep` structure, and an additional `hw_addr` pointer is set up for the hardware-abstraction-layer code that Intel shares between drivers. The rest of the driver uses macros (`E1000_READ_REG`, `E1000_WRITE_REG`) defined over `bus_space_*` to talk to the hardware.

The shape is the same as our Stage 2. The difference is exactly one function call: `bus_alloc_resource_any` for a real driver, `malloc(9)` plus a handcrafted tag for ours. Everything above the allocation layer is identical.

Chapter 18 will swap our `malloc` for `bus_alloc_resource_any` and point the driver at a real PCI device. The driver's upper layers will not change.

### Wrapping Up Section 5

Stage 2 replaces the direct accessors of Stage 1 with real `bus_space(9)` calls operating on the simulated tag and handle. The driver's `write(2)` and `read(2)` paths now produce register-level side effects. A task updates a scratch register on a timer. The `HARDWARE.md` document describes the register interface. The driver's shape closely matches that of a real driver like `if_em`.

Section 6 introduces the safety discipline that real MMIO demands: memory barriers, locking around shared registers, and the reasons busy-wait loops are a mistake. Stage 3 of the driver adds that discipline.



## Section 6: Safety and Synchronization with MMIO

Stage 2 works, but it is unsafe in three specific ways that Section 6 names and fixes. The first is that register access is not atomic. The second is that register ordering is not enforced. The third is that there is no discipline around what context may access which registers. Each of these is a category of bug that can hurt a real driver; each is fixable with discipline the chapter has already taught in Part 3, applied to the new hardware-facing state.

This section walks through each problem, explains why it matters, and produces Stage 3 of the driver: a version that is correct under concurrent access, ordering-safe for platforms that need it, and clearly partitioned by context.

### Why a Register Access Can Be Unsafe Without a Lock

Consider two user-space threads writing different bytes to `/dev/myfirst0` concurrently. In Stage 2, both call `myfirst_write`, which in turn calls `MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, last_byte)`. Without a lock, the two writes race: one finishes first, one finishes second, and `DATA_IN` ends up with whichever value was written last. That is not wrong, exactly; both bytes were really the last byte of their respective writes. But the driver has no way of telling which value in `DATA_IN` came from which writer.

More subtly, consider `myfirst_reg_update`, which does a read-modify-write sequence on a register. Two threads calling it on the same register in parallel can produce a classic lost-update. Thread A reads `CTRL = 0`. Thread B reads `CTRL = 0`. Thread A sets the `ENABLE` bit and writes `CTRL = 1`. Thread B sets the `RESET` bit and writes `CTRL = 2`. The result is `CTRL = 2`, with `ENABLE` lost. On any register where multiple contexts perform read-modify-write operations, this is a data-race bug that can cause real protocol failures.

The solution is familiar: a lock. The only question is which lock. Chapter 11's `sc->mtx` protects the data path; it is the natural choice for register access that happens inside the data path. A separate mutex, `sc->reg_mtx`, can be introduced for register access that happens outside the data path (sysctl handlers, the ticker task). The two can be the same lock, or different locks, depending on the driver's access patterns.

For Stage 3, we take the simpler path: use `sc->mtx` for every register access. This enforces the rule "no register access without the driver mutex" with a single primitive. The cost is that sysctl handlers and the ticker task must acquire the driver mutex briefly, which serialises them with the data path. For a driver this small, the cost is negligible.

### Adding the Lock

Modify `myfirst_reg_read` and `myfirst_reg_write` to assert that the driver lock is held, and modify their callers to acquire it. An assertion is cheaper than acquiring the lock inside the accessor, and it makes the locking rule visible at every call site.

```c
static __inline uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        MYFIRST_ASSERT(sc);   /* Chapter 11: mtx_assert(&sc->mtx, MA_OWNED). */
        KASSERT(offset + 4 <= sc->regs_size, (...));
        return (bus_space_read_4(sc->regs_tag, sc->regs_handle, offset));
}

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);
}
```

The `MYFIRST_ASSERT` macro from Chapter 11 asserts that `sc->mtx` is held in `MA_OWNED` mode. A debug kernel catches any caller that forgot to acquire the lock; a production kernel elides the check.

Now every call site must acquire the lock. The sysctl handler becomes:

```c
static int
myfirst_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t value;

        if (!myfirst_is_attached(sc))
                return (ENODEV);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        value = MYFIRST_REG_READ(sc, offset);
        MYFIRST_UNLOCK(sc);

        return (sysctl_handle_int(oidp, &value, 0, req));
}
```

The lock is acquired before the register read, held briefly, released before the sysctl framework's `sysctl_handle_int`. The framework may sleep (it copies the value to user space), so the lock cannot be held across that call.

Similarly, the writeable sysctl handler:

```c
static int
myfirst_sysctl_reg_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        bus_size_t offset = arg2;
        uint32_t oldval, newval;
        int error;

        if (!myfirst_is_attached(sc))
                return (ENODEV);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        oldval = MYFIRST_REG_READ(sc, offset);
        MYFIRST_UNLOCK(sc);

        newval = oldval;
        error = sysctl_handle_int(oidp, &newval, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);

        MYFIRST_LOCK(sc);
        if (sc->regs_buf == NULL) {
                MYFIRST_UNLOCK(sc);
                return (ENODEV);
        }
        MYFIRST_REG_WRITE(sc, offset, newval);
        if (offset == MYFIRST_REG_CTRL)
                myfirst_ctrl_update(sc, oldval, newval);
        MYFIRST_UNLOCK(sc);

        return (0);
}
```

The handler acquires the lock twice: once to read the current value, once to apply the new value. Between them the lock is released and `sysctl_handle_int` runs. The pattern is slightly awkward but is standard in FreeBSD: you acquire a lock for a brief operation, release it for a sleepable call, reacquire for the next brief operation, and tolerate the fact that state may have changed in between.

The `myfirst_ctrl_update` call now happens under the lock, so its printf still works but any future state changes it makes can rely on lock ownership.

The ticker task callback likewise acquires the lock:

```c
static void
myfirst_reg_ticker_cb(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;

        if (!myfirst_is_attached(sc))
                return;

        MYFIRST_LOCK(sc);
        if (sc->regs_buf != NULL) {
                uint32_t v = MYFIRST_REG_READ(sc, MYFIRST_REG_SCRATCH_A);
                MYFIRST_REG_WRITE(sc, MYFIRST_REG_SCRATCH_A, v + 1);
        }
        MYFIRST_UNLOCK(sc);
}
```

And the `myfirst_write` and `myfirst_read` paths, which already held `sc->mtx` around ring-buffer access, need to extend the hold across the register updates, or to release and reacquire briefly. The simplest change is to keep the register updates inside the existing locked region:

```c
/* In myfirst_write, while still holding sc->mtx after the ring-buffer update: */
if (wrote_any) {
        MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, (uint32_t)last_byte);
        myfirst_reg_update(sc, MYFIRST_REG_STATUS, 0, MYFIRST_STATUS_DATA_AV);
}
```

Because `myfirst_reg_update` now asserts the mutex is held, and it is, the call succeeds. The lock-hold time grows slightly, but only by a handful of `bus_space_write_4` calls, which compile to single `mov` instructions; the cost is negligible.

### Why Barriers Matter Even On x86

With locking in place, the driver is correct under concurrency on x86. On weakly-ordered platforms (arm64, RISC-V, some older MIPS), the story is not quite finished. A sequence like:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, value);
MYFIRST_REG_WRITE(sc, MYFIRST_REG_CTRL, CTRL_GO);
```

implies that the `DATA_IN` write reaches the device before the `CTRL.GO` trigger. On x86, hardware preserves program order for stores, and the compiler's reordering is limited by the `volatile` qualifier in `bus_space_write_4`. On arm64, the CPU can reorder the two stores, and the device might see `CTRL.GO` before `DATA_IN` is ready, which breaks the protocol.

The fix is a write barrier:

```c
MYFIRST_REG_WRITE(sc, MYFIRST_REG_DATA_IN, value);
bus_space_barrier(sc->regs_tag, sc->regs_handle, 0, sc->regs_size,
    BUS_SPACE_BARRIER_WRITE);
MYFIRST_REG_WRITE(sc, MYFIRST_REG_CTRL, CTRL_GO);
```

On x86 this barrier is a compiler fence. On arm64 it is a DSB or DMB that forces the first store to complete before the second is issued.

For Chapter 16 the protocol we are simulating does not actually require this ordering, because our "device" is kernel memory whose observers all take the same lock and do not reorder within a critical section. But the habit is worth developing. When the code eventually talks to a real device, the barriers will be there, and the driver will be portable across architectures.

As a teaching vehicle, introduce a helper that makes barrier-annotated writes easy:

```c
static __inline void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
        MYFIRST_ASSERT(sc);
        MYFIRST_REG_WRITE(sc, offset, value);
        bus_space_barrier(sc->regs_tag, sc->regs_handle, 0, sc->regs_size,
            flags);
}
```

Flags are `BUS_SPACE_BARRIER_READ`, `BUS_SPACE_BARRIER_WRITE`, or the OR of the two. A driver that reads status right after writing a command uses the combined flag. One that only wants subsequent writes to see this write's effect uses just `WRITE`.

The Stage 3 driver does not use `myfirst_reg_write_barrier` in many places; it is defined and used in a single demonstration path (inside the ticker, after the scratch increment, to illustrate the usage). Later chapters that deal with real protocols will use it more heavily.

### Partitioning Register Access by Context

With locking uniform, the next question is: which contexts access which registers, and is that mix intentional?

A Stage 3 audit on the driver shows:

- Syscall context (write): accesses `DATA_IN`, `STATUS`.
- Syscall context (read): accesses `DATA_OUT`, `STATUS`.
- Sysctl context: accesses every register (read) and `CTRL`, `SCRATCH_A`, `SCRATCH_B`, etc. (write) through the writeable sysctl.
- Task context (ticker): accesses `SCRATCH_A`.

Every access is lock-protected. Every access touches a register the driver has explicitly allocated for that purpose. The access discipline is simple: syscalls read and write the data registers and the data-available bit; sysctls are for inspection and one-off configuration; the ticker writes one specific register. A rule of "contexts do not overlap their register responsibilities" is easy to state and easy to hold.

This is the kind of thing `HARDWARE.md` exists to document. Update the document to include per-register ownership:

```text
## Per-Register Owners

CTRL:          sysctl writer, driver (via myfirst_ctrl_update)
STATUS:        driver (via write/read paths)
DATA_IN:       syscall write path
DATA_OUT:      syscall read path
INTR_MASK:     sysctl writer only (Stage 3); driver attach (Chapter 19)
INTR_STATUS:   sysctl writer only (Stage 3)
DEVICE_ID:     attach only (initialised once, never written thereafter)
FIRMWARE_REV:  attach only (initialised once, never written thereafter)
SCRATCH_A:     ticker task; sysctl writer
SCRATCH_B:     sysctl writer only
```

A future contributor can glance at this table and immediately see where a register write is expected. A future change that adds a new owner must update the table, which keeps the documentation honest.

### Avoiding Busy-Wait Loops

One style of bug that new driver authors fall into is the busy-wait loop for register state. The canonical example:

```c
/* BAD: busy-waits forever if the device never becomes ready. */
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0)
        ;
```

The loop spins reading the register until the `READY` bit becomes set. On real hardware, the time between "not ready" and "ready" may be microseconds. On an overloaded system, it may be longer. During the spin, the CPU is consumed by the loop; no other thread on that CPU can run; the driver's own other threads cannot even unlock the mutex the loop may be holding.

Several better patterns exist.

**Bounded spin with `DELAY(9)` for short waits.** If the expected wait is short (less than a few hundred microseconds, typically), use a loop with a bounded iteration count and a `DELAY` between iterations. `DELAY(usec)` busy-waits for at least `usec` microseconds, allowing the CPU to serve interrupts in the meantime.

```c
for (i = 0; i < 100; i++) {
        if (MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY)
                break;
        DELAY(10);
}
if ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        device_printf(sc->dev, "timeout waiting for READY\n");
        return (ETIMEDOUT);
}
```

The loop runs at most 100 times, waiting 10 microseconds between reads, for a total bound of 1 millisecond. On a successful completion the loop exits early. On timeout the driver gives up and returns an error.

**Sleep-based wait with `msleep`.** For longer expected waits (milliseconds to seconds), do not busy-wait at all. Sleep the thread until a wakeup arrives, or until a timeout fires. The example below is hypothetical (our simulated device never clears `READY` in Chapter 16), but it shows the shape you will reach for when real hardware starts changing status bits:

```c
/* Hypothetical; assumes sc->status_wait is a dummy address the driver
 * uses as a sleep channel and a matching wakeup(&sc->status_wait) fires
 * when the ready bit is expected to change. */
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        error = msleep(&sc->status_wait, &sc->mtx, PCATCH, "myfready", hz / 10);
        if (error == EWOULDBLOCK) {
                /* Timeout: return to caller with ETIMEDOUT. */
                return (ETIMEDOUT);
        }
        if (error != 0)
                return (error);
}
```

The thread sleeps on `&sc->status_wait`, with the driver mutex as the interlock, for up to 100ms. A wakeup from another context (typically an interrupt handler or a task that observed the register change) breaks the sleep. On arm64, where register polling would be expensive and imprecise, this pattern is strongly preferred. Chapter 17 makes this pattern concrete: a callout flips `READY` on a timer, and a syscall path sleeps on the matching channel until the callout wakes it up.

**Event-driven with `cv_wait`.** Same as above, but using a condition variable, which is more natural in Part 3's idiom:

```c
MYFIRST_LOCK(sc);
while ((MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_READY) == 0) {
        cv_timedwait_sig(&sc->status_cv, &sc->mtx, hz / 10);
}
MYFIRST_UNLOCK(sc);
```

With a matching `cv_signal` in whichever context sets the bit.

Chapter 16's simulated device does not need any of these patterns yet, because the `READY` bit is set at attach and never cleared. But the patterns are named here because Section 6 is the right place to introduce them, and Chapter 17 will use them when status bits start changing dynamically.

### Interrupts and MMIO: A Forward Pointer

A brief note on a topic Chapter 19 owns. When a real driver has an interrupt handler that runs in filter or ithread context, register access from the handler has additional constraints. Filter handlers run in an interrupt context with very limited primitives available; they typically acknowledge the interrupt by writing a register, record the event somehow, and defer the real work to a task. The acknowledgement write is the kind of thing `bus_space_write_*` exists for, and it runs under specific locking rules that differ from the ordinary driver mutex.

Chapter 16's driver has no interrupt handler, so this concern does not yet apply. Chapter 19 introduces the handler and the lock-type changes it forces. For now, treat "interrupt context accessing registers" as a topic you know exists and will learn about later; the register-access mechanism (`bus_space_*`) is the same, but the locking around it changes.

### Stage 3 Complete

With locking added, barriers introduced, context ownership documented, and busy-wait patterns discouraged, the driver is at `0.9-mmio-stage3`. The shape of the driver is still that of Stage 2, but every register access is now lock-protected, every context's access pattern is documented, and the driver is prepared to handle more sophisticated hardware protocols in later chapters.

Build, test, and stress:

```text
# cd examples/part-04/ch16-accessing-hardware/stage3-synchronized
# make clean && make
# kldload ./myfirst.ko
# examples/part-04/ch16-accessing-hardware/labs/reg_stress.sh
```

The stress script spawns several concurrent writers, readers, sysctl readers, and ticker-toggle operations, and checks that the final register state is consistent. Under WITNESS, any locking violation produces an immediate warning; under INVARIANTS, any out-of-bounds access produces a panic. If the script completes cleanly, the driver's register discipline is sound.

### Wrapping Up Section 6

MMIO safety rests on three disciplines: locking (every register access happens under the appropriate lock), ordering (barriers where the protocol requires them, even if the platform is x86), and context partitioning (each register has a named owner and an intended access path). The driver's `HARDWARE.md` captures the last two; the mutex assertions in the accessors enforce the first.

Section 7 takes the next step: making the register access observable for debugging. Logs, sysctls, DTrace probes, and the small observability layer that catches bugs before they become crashes.



## Section 7: Debugging and Tracing Hardware Access

A register access is, by design, invisible. It compiles to a CPU instruction that reads or writes a handful of bytes. There is no stack frame, no call log, no return value you can `printf` without adding code. When a driver works, invisibility is a virtue. When it does not, invisibility is the problem.

Section 7 covers the tools and idioms that make register access visible enough to debug without being so noisy that the driver becomes unreadable. The goal is a small observability layer: enough instrumentation to catch bugs early, placed where a beginner can switch it on and off, and integrated with the logging the rest of the driver already does.

### What You Want to Observe

Three things are worth seeing when a driver is misbehaving.

**The value at a specific register, right now.** The sysctl handlers from Stage 2 already give you this. A `sysctl dev.myfirst.0.reg_ctrl` returns the current value at any moment, and nothing else in the driver's behaviour changes because of the read.

**The sequence of register accesses the driver made recently.** When a bug involves register ordering or incorrect bit manipulation, knowing "the driver wrote 0x1 then 0x2 then 0x4 in that order" is exactly what you need. The raw sequence cannot be reconstructed from register contents alone.

**The stack and context for a specific register access.** When a write happens from an unexpected code path, you want to know which function did it, which thread was running, and what was on the stack. DTrace is good at this.

The rest of this section walks through each kind of observation and shows what to add to the driver to support it.

### A Simple Access Log

The simplest observability tool is a log of the last N register accesses, kept in a ring buffer inside the softc. Every register read and write records an entry; the sysctl exposes the ring.

Define the log entry:

```c
#define MYFIRST_ACCESS_LOG_SIZE 64

struct myfirst_access_log_entry {
        uint64_t      timestamp_ns;
        uint32_t      value;
        bus_size_t    offset;
        uint8_t       is_write;
        uint8_t       width;
        uint8_t       context_tag;
        uint8_t       _pad;
};
```

Each entry is 24 bytes, holding the time (nanoseconds since boot), the value, the offset, whether it was a read or a write, the access width, and a tag identifying the caller's context (syscall, task, sysctl). Padding rounds to 24; a log of 64 entries is 1.5 KiB, which is trivially small.

Add the ring to the softc:

```c
struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
unsigned int access_log_head;   /* index of next write */
bool          access_log_enabled;
```

Record an entry in the accessors. Stage 3's `myfirst_reg_write` becomes:

```c
static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));
        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);

        if (sc->access_log_enabled) {
                unsigned int idx = sc->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                sc->access_log[idx].timestamp_ns = nanouptime_ns();
                sc->access_log[idx].value = value;
                sc->access_log[idx].offset = offset;
                sc->access_log[idx].is_write = 1;
                sc->access_log[idx].width = 4;
                sc->access_log[idx].context_tag = myfirst_current_context_tag();
        }
}
```

(`nanouptime_ns()` is a small helper that wraps `nanouptime()` and returns a `uint64_t`. `myfirst_current_context_tag()` returns a small code like `'S'` for syscall, `'T'` for task, `'C'` for sysctl; its implementation is a few-line switch on the current thread's identity.)

The read accessor records the read (value is the value read). Access recording happens under the driver mutex (Stage 3 requires the mutex for every register access), so the ring itself needs no additional locking.

Enable with a sysctl:

```c
SYSCTL_ADD_BOOL(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "access_log_enabled",
    CTLFLAG_RW, &sc->access_log_enabled, 0,
    "Record every register access in a ring buffer");
```

Expose the log through a special sysctl handler that dumps the ring:

```c
static int
myfirst_sysctl_access_log(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        struct sbuf *sb;
        int error;
        unsigned int i, start;

        sb = sbuf_new_for_sysctl(NULL, NULL, 256 * MYFIRST_ACCESS_LOG_SIZE, req);
        if (sb == NULL)
                return (ENOMEM);

        MYFIRST_LOCK(sc);
        start = sc->access_log_head;
        for (i = 0; i < MYFIRST_ACCESS_LOG_SIZE; i++) {
                unsigned int idx = (start + i) % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &sc->access_log[idx];
                if (e->timestamp_ns == 0)
                        continue;
                sbuf_printf(sb, "%16ju ns  %s%1d  off=%#04x  val=%#010x  ctx=%c\n",
                    (uintmax_t)e->timestamp_ns,
                    e->is_write ? "W" : "R", e->width,
                    (unsigned)e->offset, e->value, e->context_tag);
        }
        MYFIRST_UNLOCK(sc);

        error = sbuf_finish(sb);
        sbuf_delete(sb);
        return (error);
}
```

The handler walks the ring from the oldest to the newest entry, skipping empty slots, and formats each as a line. The output looks like:

```text
  123456789 ns  W4  off=0x00  val=0x00000001  ctx=C
  123567890 ns  R4  off=0x00  val=0x00000001  ctx=C
  124001234 ns  W4  off=0x08  val=0x00000041  ctx=S
  124001567 ns  W4  off=0x04  val=0x00000009  ctx=S
```

Four entries: a writeable-sysctl write to `CTRL`, then its immediate readback (both `ctx=C`), then a syscall write that set `DATA_IN` to `0x41` ('A') and updated `STATUS` to `0x9` (READY | DATA_AV). The context tag makes the source obvious.

For debugging, this log is priceless. You see exactly what the driver did, in order, with timestamps.

### Kernel Printf: A Controlled Flood

Sometimes the log is not enough and you want a printed message per register access, perhaps during a specific failing test. The driver should support a knob for that.

Add a debug-level sysctl (if one does not already exist from Chapter 12's `debug_level`) and use it in the accessors:

```c
#define MYFIRST_DBG_REGS  0x10u

static __inline void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        MYFIRST_ASSERT(sc);
        KASSERT(offset + 4 <= sc->regs_size, (...));

        if ((sc->debug_level & MYFIRST_DBG_REGS) != 0)
                device_printf(sc->dev, "W%d reg=%#04x val=%#010x\n",
                    4, (unsigned)offset, value);

        bus_space_write_4(sc->regs_tag, sc->regs_handle, offset, value);

        /* ... access log update ... */
}
```

When `debug_level` has the `MYFIRST_DBG_REGS` bit set, every register write is printed to the console. Setting it during a test and clearing it immediately afterwards gives a focused log without flooding the system for the duration of the driver's life.

The debug-level bitfield is a common FreeBSD pattern. Many real drivers use a sysctl of `debug` or `verbose` with bits for different subsystems: `DBG_PROBE`, `DBG_ATTACH`, `DBG_INTR`, `DBG_REGS`, and so on. The user enables only the subset they need.

### DTrace Probes

DTrace is the right tool when you want to observe register access patterns without modifying the driver. FreeBSD's `fbt` (function boundary tracing) provider automatically instruments every non-inlined function in the kernel. If `myfirst_reg_read` and `myfirst_reg_write` are compiled as out-of-line functions (not inlined), DTrace can hook them.

By default, `static __inline` functions are candidates for inlining, and inlined functions do not have fbt probes. To make the accessors DTrace-visible in debug builds, split the declarations:

```c
#ifdef MYFIRST_DEBUG_REG_TRACE
static uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
static void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
                    uint32_t value);
#else
static __inline uint32_t myfirst_reg_read(struct myfirst_softc *sc,
                             bus_size_t offset);
static __inline void     myfirst_reg_write(struct myfirst_softc *sc,
                             bus_size_t offset, uint32_t value);
#endif
```

With `MYFIRST_DEBUG_REG_TRACE` set at compile time, the accessors are regular functions with function-boundary probes. DTrace can then show every call:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry { printf("off=%#x val=%#x", arg1, arg2); }'
```

The output lists every register write with its offset and value, live, across the whole system. DTrace can aggregate, count, filter by stack, and join with process information in ways a hand-rolled log cannot match.

For a release build, leave `MYFIRST_DEBUG_REG_TRACE` unset; the accessors inline and pay no runtime cost. For a debugging build, set the macro and get full visibility.

### Specialised DTrace Probes: `sdt(9)`

A more targeted alternative to fbt probes is to register Statically Defined Tracepoints (SDT) at specific points in the driver. FreeBSD's `sdt(9)` API lets you declare probes that DTrace can hook by name, without the overhead of a full function-boundary trace.

A probe for every register write:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE3(myfirst, , , reg_write,
    "struct myfirst_softc *", "bus_size_t", "uint32_t");
SDT_PROBE_DEFINE2(myfirst, , , reg_read,
    "struct myfirst_softc *", "bus_size_t");
```

And in the accessor:

```c
SDT_PROBE3(myfirst, , , reg_write, sc, offset, value);
```

DTrace picks up the probe by name:

```text
# dtrace -n 'sdt::myfirst:::reg_write { printf("off=%#x val=%#x", arg1, arg2); }'
```

The probe is visible in DTrace regardless of inlining, because the kernel registers it statically. When DTrace is not running the probe, it is a no-op on modern x86 (a single NOP instruction in the inlined expansion).

SDT probes are appropriate for production code. They are permanent, named, documented parts of the driver interface. A driver's users might rely on specific SDT probe names for their own monitoring tools; removing them breaks those tools.

Chapter 16 introduces SDT lightly. Later chapters (especially Chapter 23 on debugging and tracing) dig in.

### The Heartbeat Log From Stage 1 Onwards

One piece of instrumentation the driver already has is Chapter 13's heartbeat callout. With Chapter 16's register state added, the heartbeat becomes more informative if it prints a register snapshot:

```c
static void
myfirst_heartbeat_cb(void *arg)
{
        struct myfirst_softc *sc = arg;

        MYFIRST_ASSERT(sc);
        if (!myfirst_is_attached(sc))
                return;

        if (sc->debug_level & MYFIRST_DBG_HEARTBEAT) {
                uint32_t ctrl, status;
                ctrl = sc->regs_buf != NULL ?
                    MYFIRST_REG_READ(sc, MYFIRST_REG_CTRL) : 0;
                status = sc->regs_buf != NULL ?
                    MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) : 0;
                device_printf(sc->dev,
                    "heartbeat: ctrl=%#x status=%#x open=%d writers=%d\n",
                    ctrl, status, sc->open_count,
                    sema_value(&sc->writers_sema));
        }

        /* ... existing heartbeat work (stall detection, etc.) ... */

        callout_reset(&sc->heartbeat_co,
            msecs_to_ticks(sc->heartbeat_interval_ms),
            myfirst_heartbeat_cb, sc);
}
```

With the heartbeat bit set and a 1-second interval, the driver logs its register state every second. During a failing test, the heartbeat output often shows exactly when the state went off the rails.

### Using `kgdb` on a Core Dump

When a driver panics, the kernel produces a core dump. `kgdb` can read the dump and inspect the softc. With the register block inside the softc, a single command can print the current register values:

```text
(kgdb) print *(struct myfirst_softc *)0xfffff8000a123400
(kgdb) x/16xw ((struct myfirst_softc *)0xfffff8000a123400)->regs_buf
```

The `x/16xw` command dumps 16 words in hex at the register buffer's address. The output is literally the 64 bytes of register state at the moment of panic. A developer staring at those bytes can often spot the wrong value that led to the panic.

The reason this works is the simulation: `regs_buf` is kernel memory, visible to kgdb. A real device's registers would not be visible in a core dump, because the core dump captures RAM only, not device state. For simulated devices and DMA descriptors, a core dump is a gold mine.

### DDB Extensions

For live debugging without a panic, `ddb` can be extended with driver-specific commands. The `DB_COMMAND` macro registers a new command that ddb recognises at the prompt:

```c
#include <ddb/ddb.h>

DB_COMMAND(myfirst_regs, myfirst_ddb_regs)
{
        struct myfirst_softc *sc;

        /* ... find the softc, e.g., via devclass ... */
        if (sc == NULL || sc->regs_buf == NULL) {
                db_printf("myfirst: no device or no regs\n");
                return;
        }

        db_printf("CTRL    %#010x\n", *(uint32_t *)(sc->regs_buf + 0x00));
        db_printf("STATUS  %#010x\n", *(uint32_t *)(sc->regs_buf + 0x04));
        db_printf("DATA_IN %#010x\n", *(uint32_t *)(sc->regs_buf + 0x08));
        /* ... and so on ... */
}
```

At the `db>` prompt during a break:

```text
db> myfirst_regs
CTRL    0x00000001
STATUS  0x00000009
DATA_IN 0x0000006f
...
```

ddb commands are a niche tool. Chapter 16 introduces them to show that they exist. Later chapters (especially Chapter 23) use them more. For now, the access log and DTrace cover most day-to-day debugging.

### What to Do When a Register Read Returns Garbage

A short field guide to the most common mistakes that produce a "garbage" register value, with the diagnostic to try for each.

**Wrong offset.** The offset in the code does not match the offset in the datasheet. Diagnostic: cross-check the offset against the datasheet, and audit the header for transcription errors.

**Wrong width.** The code reads 32 bits of a 16-bit register, or reads 8 bits of a 32-bit register. Diagnostic: verify the width in the datasheet, and adjust the call.

**Missing virtual mapping.** The resource was allocated without `RF_ACTIVE`, or the driver is reading from a saved pointer that points to freed memory. Diagnostic: confirm `bus_alloc_resource_any` was called with `RF_ACTIVE`; assert `sc->regs_buf != NULL` before reading in the simulation path.

**Race with another writer.** Another context wrote a different value between the read and the expected observation. Diagnostic: enable the access log, reproduce the issue, inspect the log.

**Read-to-clear side effect the code did not expect.** The previous read cleared the bits you now expect to see. Diagnostic: check the datasheet for read side effects; consider whether the reading code should cache the value.

**Cache attribute mismatch.** On platforms where it matters, the virtual mapping was set up with wrong cache attributes. Diagnostic: not usually a problem on x86 with `bus_alloc_resource_any`; on other platforms, check `pmap_mapdev_attr` and the bus provider. Rare in practice if you are using the standard bus allocation path.

**Endianness mismatch.** On a big-endian device accessed from a little-endian CPU without the right tag or stream accessor, the bytes come back in the wrong order. Diagnostic: compare the value byte-swapped; if it now makes sense, you need the `_stream_` accessors or a swap-aware tag.

Each diagnosis points to a different fix. Keeping them in mind saves hours.

### What to Do When a Register Write Has No Effect

Sister field guide for writes.

**The register is read-only or write-once.** The datasheet defines the register as readable only, or writable only until the first successful write. Diagnostic: check the datasheet's direction column.

**The write was masked out.** The register has bits that are writable only under specific conditions (device disabled, chip in test mode, a specific field set). Diagnostic: enable debug_level printing; confirm the write happened; then check the device's state.

**The write was reordered.** A barrier-needed sequence was issued without barriers, and the device saw the writes in a different order than the code intended. Diagnostic: add explicit `bus_space_barrier` calls and retest.

**The write was lost to a concurrent read-modify-write.** Another context clobbered the new value. Diagnostic: access log; locking audit.

**The write went to the wrong offset.** A transcription error or an arithmetic mistake aimed the write at a different register. Diagnostic: access log; comparison with the datasheet.

The overlap with the read diagnostic is high: most issues boil down to "the code is not doing what you think it is", and the access log is the most direct way to see what the code is doing.

### A Lab: The Access Log Does Its Job

A small exercise that shows the access log paying off. Enable it, exercise the driver, dump the log.

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# echo hello > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=6 of=/dev/null
# sysctl dev.myfirst.0.reg_ticker_enabled=0
# sysctl dev.myfirst.0.access_log_enabled=0
# sysctl dev.myfirst.0.access_log
```

The last sysctl emits several dozen lines: the ticker's `SCRATCH_A` increments, the write's `DATA_IN` update and `STATUS` ORing, the read's `DATA_OUT` update and `STATUS` AND-ing, and every sysctl-driven read of a register along the way. The log reads like a transcript of the driver's conversation with itself.

A beginner who sees this transcript for the first time usually has a moment of recognition: "oh, *that* is what the driver is doing under the covers". The moment is the whole point of the exercise.

### Wrapping Up Section 7

The register access path is invisible by default. A small observability layer makes it visible: a ring buffer of recent accesses, a debug bitfield that controls per-access printf, DTrace probes through fbt or sdt, an enhanced heartbeat that logs register snapshots, and ddb or kgdb access to the softc for post-mortem inspection. Each tool fits a different use case, and together they cover almost every register-level bug a driver can have.

Section 8 consolidates everything Chapter 16 has added into a refactored, documented, versioned driver. The final stage.



## Section 8: Refactoring and Versioning Your MMIO-Ready Driver

Stage 3 produced a correct driver. Section 8 produces a maintainable one. The changes Stage 4 makes are organisational: split the hardware-access code out of `myfirst.c` into its own file, wrap the remaining register accesses in macros that mirror the `CSR_*` idiom real drivers use, update `HARDWARE.md` to its final form, bump the version to `0.9-mmio`, and run the full regression pass.

A driver that works is valuable. A driver that works *and* reads cleanly to the next person who opens it is far more valuable. Section 8 is about that second step.

### The File Split

Through Chapter 15, the `myfirst` driver lived in one C file plus a header. Chapter 16 adds about 200 to 300 lines of hardware-access code, which is enough that a reader opening `myfirst.c` is now greeted by a mix of "driver business logic" and "hardware register mechanics" that compete for attention.

Stage 4 separates them. Create a new file `myfirst_hw.c` alongside `myfirst.c`. Move into it:

- The accessor implementations (`myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`, `myfirst_reg_write_barrier`).
- The register-driven side-effect helpers (`myfirst_ctrl_update`).
- The ticker task callback (`myfirst_reg_ticker_cb`).
- The access log rotation helpers.
- The sysctl handlers for register views (`myfirst_sysctl_reg`, `myfirst_sysctl_reg_write`, `myfirst_sysctl_access_log`).

Move into `myfirst_hw.h`:

- Register offsets and bit masks (already there).
- Fixed-value constants (already there).
- Function prototypes for the hardware-access API (`myfirst_hw_attach`, `myfirst_hw_detach`, `myfirst_hw_set_ctrl`, `myfirst_hw_add_sysctls`, etc.).
- A small struct defining the hardware state (fewer softc fields, more grouping).

The remaining `myfirst.c` keeps:

- The softc declaration (including a `struct myfirst_hw *hw` pointer to a sub-struct for hardware state).
- The driver lifecycle (attach, detach, module init).
- The syscall handlers (open, close, read, write, ioctl, poll, kqfilter).
- The callout callbacks (heartbeat, watchdog, tick_source).
- The non-hardware tasks (selwake, bulk_writer, reset_delayed, recovery).
- The non-hardware sysctls.

This split mirrors how real drivers with multiple subsystems organise themselves. A network driver might have `foo.c` for the main lifecycle, `foo_hw.c` for hardware access, `foo_rx.c` for the receive path, `foo_tx.c` for the transmit path. The principle is that each file holds code of one kind, and cross-file calls go through a named API.

### The Hardware State Structure

Inside `myfirst_hw.h`, group the hardware-related fields into their own structure:

```c
struct myfirst_hw {
        uint8_t                *regs_buf;
        size_t                  regs_size;
        bus_space_tag_t         regs_tag;
        bus_space_handle_t      regs_handle;

        struct task             reg_ticker_task;
        int                     reg_ticker_enabled;

        struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
        unsigned int            access_log_head;
        bool                    access_log_enabled;
};
```

Add a pointer to it in the softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_hw      *hw;
};
```

Allocate the hw struct in `myfirst_hw_attach`:

```c
int
myfirst_hw_attach(struct myfirst_softc *sc)
{
        struct myfirst_hw *hw;

        hw = malloc(sizeof(*hw), M_MYFIRST, M_WAITOK | M_ZERO);

        hw->regs_size = MYFIRST_REG_SIZE;
        hw->regs_buf = malloc(hw->regs_size, M_MYFIRST, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
        hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation supports x86 only"
#endif
        hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;

        TASK_INIT(&hw->reg_ticker_task, 0, myfirst_hw_ticker_cb, sc);

        /* Initialise fixed registers. */
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_DEVICE_ID,
            MYFIRST_DEVICE_ID_VALUE);
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_FIRMWARE_REV,
            MYFIRST_FW_REV_VALUE);
        bus_space_write_4(hw->regs_tag, hw->regs_handle, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_READY);

        sc->hw = hw;
        return (0);
}
```

Free it in `myfirst_hw_detach`:

```c
void
myfirst_hw_detach(struct myfirst_softc *sc)
{
        struct myfirst_hw *hw;

        if (sc->hw == NULL)
                return;
        hw = sc->hw;
        sc->hw = NULL;

        taskqueue_drain(sc->tq, &hw->reg_ticker_task);
        if (hw->regs_buf != NULL) {
                free(hw->regs_buf, M_MYFIRST);
                hw->regs_buf = NULL;
        }
        free(hw, M_MYFIRST);
}
```

The `myfirst_attach` and `myfirst_detach` functions now call `myfirst_hw_attach(sc)` and `myfirst_hw_detach(sc)` at the appropriate points in their ordering. The hardware sub-attach fits between "softc locks initialised" and "cdev registered"; the hardware sub-detach fits between "tasks drained" and "sysctl context released".

### The CSR Macros

Wrap the register accesses in macros that match the real-driver idiom:

```c
#define CSR_READ_4(sc, off) \
        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val) \
        myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
        myfirst_reg_update((sc), (off), (clear), (set))
```

The driver body now reads:

```c
/* In myfirst_write: */
CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)last_byte);
CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, 0, MYFIRST_STATUS_DATA_AV);

/* In the ticker: */
uint32_t v = CSR_READ_4(sc, MYFIRST_REG_SCRATCH_A);
CSR_WRITE_4(sc, MYFIRST_REG_SCRATCH_A, v + 1);

/* In the heartbeat: */
uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
```

The call sites read like they are talking to hardware, because that is exactly what the abstraction represents. A newcomer opening the driver and reading any of these lines immediately understands what is happening: the driver is reading or writing a register named by a constant from the hardware header.

### Moving Sysctls

The register-view sysctls move to `myfirst_hw_add_sysctls`:

```c
void
myfirst_hw_add_sysctls(struct myfirst_softc *sc)
{
        /* ... SYSCTL_ADD_PROC calls for every register ... */
        /* ... SYSCTL_ADD_BOOL for ticker_enabled ... */
        /* ... SYSCTL_ADD_PROC for access_log ... */
}
```

The function is called from `myfirst_attach` at the usual point where sysctls are registered. The main file no longer needs to care about which sysctls exist for hardware; it delegates.

### Final `HARDWARE.md`

With the file split and API stable, `HARDWARE.md` finalises:

```text
# myfirst Hardware Interface

## Version

0.9-mmio.  Chapter 16 complete.

## Register Block

- Size: 64 bytes (MYFIRST_REG_SIZE)
- Access: 32-bit reads and writes on 32-bit-aligned offsets
- Storage: malloc(9), M_WAITOK|M_ZERO, allocated in myfirst_hw_attach,
  freed in myfirst_hw_detach
- bus_space_tag:    X86_BUS_SPACE_MEM (x86 only, simulation shortcut)
- bus_space_handle: pointer to the malloc'd block

## API

All register access goes through:

- CSR_READ_4(sc, offset):           read a 32-bit register
- CSR_WRITE_4(sc, offset, value):   write a 32-bit register
- CSR_UPDATE_4(sc, offset, clear, set): read-modify-write

The driver's main mutex (sc->mtx) must be held for every register
access.  Accessor macros assert this via MYFIRST_ASSERT.

## Register Map

(table as in Section 4 ...)

## Per-Register Owners

(table as in Section 6 ...)

## Observability

- dev.myfirst.0.reg_*:      read each register (sysctl)
- dev.myfirst.0.reg_ctrl_set:  write CTRL (sysctl, Stage 1 demo aid)
- dev.myfirst.0.access_log_enabled: record access ring
- dev.myfirst.0.access_log: dump recorded accesses
- Debug bit MYFIRST_DBG_REGS in debug_level: printf per access

## Architecture Portability

The simulation path uses X86_BUS_SPACE_MEM as the tag and a kernel
virtual address as the handle.  On non-x86 platforms, bus_space_tag_t
is a pointer to a structure and this shortcut does not compile;
Chapter 17 introduces a portable alternative.  Real-hardware
Chapter 18 drivers use rman_get_bustag and rman_get_bushandle on a
resource from bus_alloc_resource_any, which is portable by design.
```

The document is now a single source of truth for how the driver accesses hardware. A future contributor reads it once to understand the interface and never again needs to reverse-engineer it from code.

### The Version Bump

In `myfirst.c`:

```c
#define MYFIRST_VERSION "0.9-mmio"
```

The string appears in `kldstat -v` output (through `MODULE_VERSION`) and in the `device_printf` at attach time. Bumping it is a small change with a big signalling value: anyone looking at the running driver knows exactly which chapter's features it has.

Update the top-of-file comment to note the additions:

```c
/*
 * myfirst: a beginner-friendly device driver tutorial vehicle.
 *
 * Version 0.9-mmio (Chapter 16): adds a simulated MMIO register
 * block with bus_space(9) access, lock-protected register updates,
 * barrier-aware writes, an access log, and a refactored layout that
 * splits hardware-access code into myfirst_hw.c and myfirst_hw.h.
 *
 * ... (previous version notes preserved) ...
 */
```

The top-of-file comment is the shortest path for a newcomer to understand the driver's history. Keeping it current is a small discipline with a big payoff.

### The Final Regression Pass

Chapter 15 established the regression discipline: after every version bump, run the full stress suite from every previous chapter, confirm WITNESS is silent, confirm INVARIANTS is silent, confirm `kldunload` completes cleanly.

For Stage 4 that means:

- The Chapter 11 concurrency tests (multiple writers, multiple readers) pass.
- The Chapter 12 blocking tests (reader waits for data, writer waits for room) pass.
- The Chapter 13 callout tests (heartbeat, watchdog, tick source) pass.
- The Chapter 14 task tests (selwake, bulk writer, delayed reset) pass.
- The Chapter 15 coordination tests (writers sema, stats cache, interruptible waits) pass.
- The Chapter 16 register tests (see the Hands-On Labs below) pass.
- `kldunload myfirst` returns cleanly after the full suite.

No test is skipped. A regression in any previous chapter's test is a bug, not a deferred issue. The discipline is the same as it has been throughout Part 3.

### Running the Final Stage

```text
# cd examples/part-04/ch16-accessing-hardware/stage4-final
# make clean && make
# kldstat | grep myfirst
# kldload ./myfirst.ko
# kldstat -v | grep -i myfirst
# dmesg | tail -5
# sysctl dev.myfirst.0 | head -40
```

The `kldstat -v` output should show `myfirst` at version `0.9-mmio`. The `dmesg` tail should show the device probe and attach with no errors. The `sysctl` output should list every Chapter 11 through Chapter 16 sysctl, including the register sysctls.

Run the stress suite:

```text
# ../labs/full_regression.sh
```

If every test passes, Chapter 16 is complete.

### A Small Rule for Chapter 16's Refactor

A rule of thumb that Stage 4 embodies: when a module acquires a new responsibility, give it its own file before the responsibility grows big enough to require one. Chapter 16 adds register access as a new responsibility. The responsibility is currently small: 200 to 300 lines across all the code. Splitting it into `myfirst_hw.c` now, while it is still small, is cheap. Splitting it later, when Chapter 18 adds PCI attach logic and Chapter 19 adds an interrupt handler and Chapter 20 adds DMA, would require disentangling three intertwined subsystems at once, which is expensive.

The same rule applied to Chapter 10's cbuf: the ring buffer got its own `cbuf.c` as soon as it had any logic beyond "declare a struct", which paid off when concurrency and state machines entered the picture. It applies to every future subsystem this driver grows.

### What Stage 4 Accomplished

The driver is now at `0.9-mmio`. Compared to `0.9-coordination`, it has:

- A separate hardware-access layer in `myfirst_hw.c` and `myfirst_hw.h`.
- A full register map, documented in `myfirst_hw.h` and `HARDWARE.md`.
- `bus_space(9)`-based register accessors wrapped in `CSR_*` macros.
- Lock-protected, barrier-aware register access.
- An access log for post-hoc debugging.
- Per-register context ownership documented in `HARDWARE.md`.
- A ticker task that demonstrates autonomous register behaviour.
- An end-to-end path from user-space write to register update to user-space read.

The driver's code is recognisably FreeBSD. The layout is the layout real drivers use. The vocabulary is the vocabulary real drivers share. A reader opening the driver for the first time finds a familiar structure, reads the headers to understand the registers, and can navigate the code by subsystem.

### Wrapping Up Section 8

The refactor is small in code but large in organisation. A file split, a structure grouping, a macro layer, a documented interface, a version bump, and a regression pass. Each is a few minutes of work. Together they turn a correct driver into a maintainable one.

The Chapter 16 driver is done. The chapter closes with labs, challenges, troubleshooting, and a bridge to Chapter 17, where the simulated device acquires dynamic behaviour.



## Hands-On Labs

Labs in Chapter 16 focus on two things: observing register access while exercising the driver, and breaking the register contract to see how the driver reacts. Each lab takes 15 to 45 minutes.

### Lab 1: Observe the Register Dance

Enable the access log. Exercise the driver across its full interface. Dump the log. Read the transcript.

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.access_log_enabled=1

# echo -n "hello" > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=5 of=/dev/null 2>/dev/null
# sysctl dev.myfirst.0.reg_ctrl_set=1
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 2
# sysctl dev.myfirst.0.reg_ticker_enabled=0

# sysctl dev.myfirst.0.access_log
```

You should see, in order:

- Five writes to `DATA_IN` (one per byte of "hello").
- Updates to `STATUS` setting the `DATA_AV` bit.
- Five writes to `DATA_OUT` (one per byte read).
- Updates to `STATUS` clearing the `DATA_AV` bit as the buffer drains.
- The sysctl-driven `CTRL` write to enable, plus the read-back.
- Two increments of `SCRATCH_A` from the ticker.

Read each line. Every value should make sense. If a value does not make sense, the driver has a bug, the test has a typo, or your understanding of the register protocol has a gap.

### Lab 2: Trigger a Lock Violation (Debug Kernel)

This lab only works on a kernel built with `WITNESS` enabled. If you are not running one, skip this lab.

Temporarily remove the `MYFIRST_LOCK(sc)` from the sysctl read handler. Rebuild and reload the driver. Run:

```text
# sysctl dev.myfirst.0.reg_ctrl
```

The console should emit a `WITNESS` warning about an unprotected register access (via the `MYFIRST_ASSERT` in `myfirst_reg_read`). The sysctl output may still return a plausible value, because the lack of locking does not always produce incorrect results, but the assertion makes the violation visible.

Restore the lock. Rebuild. Verify the warning is gone.

This lab demonstrates the value of `MYFIRST_ASSERT` as a safety net. A production driver without the assertion would carry the bug silently until something went wrong.

### Lab 3: Simulate a Concurrent Writer Race

Two processes writing simultaneously to `/dev/myfirst0` exercise the register update code path twice over. Run:

```text
# for i in 1 2 3 4; do
    (for j in $(seq 1 100); do echo -n "$i"; done > /dev/myfirst0) &
done
# wait

# sysctl dev.myfirst.0.reg_data_in
# sysctl dev.myfirst.0.reg_status
```

The `DATA_IN` register should hold the ASCII code of whichever writer ran last (`'1'` = 49, `'2'` = 50, `'3'` = 51, `'4'` = 52). The result is non-deterministic, which is the point: register state from concurrent writers reflects the last winner.

With Stage 3 locking, the driver is correct (no lost updates, no torn reads). Without Stage 3 locking (try reverting to Stage 2 and re-running), you may observe inconsistencies or WITNESS warnings.

### Lab 4: Watch the Heartbeat Register Log

Enable the heartbeat debug bit, increase the interval, and let it run.

```text
# sysctl dev.myfirst.0.debug_level=0x8     # MYFIRST_DBG_HEARTBEAT
# sysctl dev.myfirst.0.heartbeat_interval_ms=1000
# sysctl dev.myfirst.0.reg_ticker_enabled=1
# sleep 5
# dmesg | tail -10

# sysctl dev.myfirst.0.reg_ticker_enabled=0
# sysctl dev.myfirst.0.debug_level=0
# sysctl dev.myfirst.0.heartbeat_interval_ms=0
```

The dmesg tail should contain five lines, one per heartbeat, each showing the current register values. `SCRATCH_A` should climb by one per heartbeat because the ticker is firing in parallel.

This lab demonstrates how a production driver might use a debug log to observe live behaviour without interrupting the driver's normal operation.

### Lab 5: Add a New Register

A practical exercise. Add a new register `SCRATCH_C` at offset `0x28`. Extend the header, extend the sysctl list, extend `HARDWARE.md`. Rebuild, reload, and verify the new register is readable and writable via sysctl.

This exercises the full workflow of adding a register: header change, sysctl addition, documentation update, test. A driver that makes all four steps easy is a well-organised driver.

### Lab 6: Inject a Bogus Access (Debug Kernel)

A deliberate break-and-observe exercise.

Modify the ticker callback to read from an out-of-bounds offset: `MYFIRST_REG_READ(sc, 0x80)`. Rebuild. Enable the ticker. On a debug kernel with INVARIANTS, the KASSERT in `myfirst_reg_read` should panic the kernel within a few seconds, with the panic string naming the offset.

Restore the callback. Rebuild. Verify the driver runs cleanly again.

This lab shows the value of bounds assertions: an out-of-bounds access fires immediately instead of silently corrupting nearby memory. Production code should never remove these assertions; they pay their cost many times over the life of the driver.

### Lab 7: Trace With DTrace

Compile the driver with `CFLAGS+=-DMYFIRST_DEBUG_REG_TRACE` to make the accessors out-of-line. Rebuild and reload.

Run DTrace:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry {
    printf("off=%#x val=%#x", arg1, arg2);
}'
```

In another terminal:

```text
# echo hi > /dev/myfirst0
```

DTrace should print two lines, one per register write (`DATA_IN` and the `STATUS` update).

Try more advanced queries:

```text
# dtrace -n 'fbt::myfirst_reg_write:entry /arg1 == 0/ { @ = count(); }'
```

Counts writes to `CTRL` (offset 0) for the duration of the run. Leave it running while triggering various operations, then Ctrl-C to see the total.

DTrace's power comes from the combination of low overhead, flexible filtering, and rich aggregation. A beginner who grows comfortable with it early will save hours on every later debugging session.

### Lab 8: The Watchdog-Meets-Register Scenario

The Chapter 13 watchdog callout was introduced to catch stalls in the ring buffer. Chapter 16's register integration adds a second failure mode: the watchdog could detect a register in an impossible state. Extend the watchdog callback to complain if `STATUS.ERROR` is set:

```c
if (MYFIRST_REG_READ(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_ERROR) {
        device_printf(sc->dev, "watchdog: STATUS.ERROR is set\n");
}
```

Set the error bit from a sysctl handler:

```text
# sysctl dev.myfirst.0.reg_ctrl_set=??  # use your writeable-register sysctl
```

(You can similarly make a writeable sysctl for `STATUS` to trigger the watchdog check.)

On the next watchdog tick (default 5 seconds), the message should appear. Clear the bit; the message should stop.

This lab integrates the register path with the callout-driven monitoring path, showing how the two subsystems compose.



## Challenge Exercises

Challenges go further than labs. Each should take one to four hours and exercises judgment, not just keystrokes.

### Challenge 1: Per-File-Handle Register Snapshot

Each open file descriptor gets its own snapshot of the register block, captured at open time and accessible via a custom ioctl. Modify `myfirst_open` to snapshot the registers into a per-fd structure; implement an ioctl that returns the snapshot; write a user-space program that opens `/dev/myfirst0`, fetches the snapshot, and prints it.

Think about: how much memory does the snapshot cost per open? When should the snapshot be refreshed? Should a second ioctl be added to refresh?

### Challenge 2: Register Diff Logging

Extend the access log to record only *changes* to registers (where the new value differs from the previous value at the same offset). Writes that do not change the value are not logged. This compresses the log significantly and focuses it on meaningful state transitions.

Think about: how do you track the "previous value"? Is it per-offset, or do you store it alongside each log entry?

### Challenge 3: Loopback Mode

Add a `CTRL.LOOPBACK` bit (already defined in `myfirst_hw.h`). When the bit is set, writes to `DATA_IN` are also copied to `DATA_OUT`, making the driver "loop back" user-space writes without needing a read. Implement the logic, add a lab test, and confirm user-space reads return the bytes just written.

Think about: where in the write path does the copy belong? Is it still correct if multiple bytes are written in one call? Do you set `DATA_AV` differently in loopback mode?

### Challenge 4: Read-to-Clear on `INTR_STATUS`

The Chapter 16 simulation has `INTR_STATUS` as a plain register. Real hardware often uses read-to-clear semantics. Implement them: make the sysctl read of `reg_intr_status` return the current value and then clear it, so the next read returns zero. Add a way to set pending bits (a writeable sysctl that ORs into the register).

Think about: is the read-to-clear behaviour safe for the debug sysctl? How do you handle the case where the sysctl is used to observe the value?

### Challenge 5: A Barrier Correctness Stress Test

Write a stress harness that exercises a specific pattern: write to `CTRL`, issue a barrier, read `STATUS`, verify the read reflects the write. Run it thousands of times, measure how often the verification fails. On x86 with correct barriers, failures should be zero.

Then remove the barrier and run again. On x86, failures might still be zero (strong memory model). On arm64 (if you have access), removing the barrier may produce failures.

Think about: what does this exercise tell you about the cost and value of barriers on different architectures? Should a driver always include them?

### Challenge 6: A Register-Aware Lockstat Run

Use `lockstat` to profile your Stage 3 driver under load. Identify the hottest locks. Is the driver mutex (`sc->mtx`) saturated by register access, or by the ring buffer path, or by neither? Generate a report and interpret the numbers.

Think about: does the result change if you split out a dedicated `sc->reg_mtx` for register access? Do WITNESS warnings appear? Is the driver faster or slower?

### Challenge 7: Read a Real Driver's Register Interface

Pick a real driver in `/usr/src/sys/dev/` and read its register header. Candidates include `/usr/src/sys/dev/ale/if_alereg.h`, `/usr/src/sys/dev/e1000/e1000_regs.h`, and `/usr/src/sys/dev/uart/uart_dev_ns8250.h`. Answer:

- How many registers does the driver define?
- What width are they (8, 16, 32, 64 bits)?
- Which registers have bit-field macros? Are there any bit-field macros that correspond to fields spanning multiple bytes?
- How does the driver wrap `bus_read_*` and `bus_write_*` (if at all)?
- How are the register offsets documented (comments, external spec references)?

Writing up the answers as a one-page analysis is a great way to consolidate Chapter 16's material. You will likely also find patterns you want to apply to your own driver.



## Troubleshooting Reference

A quick reference for the problems Chapter 16's code is most likely to surface.

### Driver fails to load

- **"resolve_symbol failed"**: Missing include or a typo in a function name. Check `/var/log/messages` for the exact symbol; add the include; retry.
- **"undefined reference to bus_space_read_4"**: Missing `#include <machine/bus.h>`. This pulls in the per-architecture bus header.
- **"invalid KMOD Makefile"**: A typo in the Makefile. Compare against the stage's known-good Makefile.

### Driver loads but `dmesg` shows `myfirst: cannot allocate register block`

`malloc(9)` returned NULL at attach. Usually means `M_WAITOK` was passed but the system was under memory pressure; rare for a 64-byte allocation. Check the kernel's malloc statistics (`vmstat -m`) for `myfirst`. Try a reboot.

### `sysctl dev.myfirst.0.reg_ctrl` returns ENOENT

The sysctl was not registered. Confirm `myfirst_hw_add_sysctls` is called in attach. Confirm the sysctl context and tree are the same as the rest of the driver's sysctls. Look for a typo in `OID_AUTO` or the leaf name.

### `sysctl dev.myfirst.0.reg_ctrl` returns a plausible value, but changes never happen

The writeable sysctl `reg_ctrl_set` might be missing `CTLFLAG_RW`. Without `_RW`, the sysctl is read-only. Also check the handler is not short-circuiting because of an early ENODEV return.

### Kernel panic on first register write: "page fault in kernel mode"

`sc->regs_buf` is NULL or dangling. Confirm `myfirst_hw_attach` ran successfully and set `sc->hw->regs_buf`. Confirm nothing freed the buffer prematurely (detach running in parallel, or a `free` in an error path).

### Kernel panic: "myfirst: register read past end of register block"

The `KASSERT` fired. A bug in the driver is passing an out-of-range offset. Use the crash stack to find the call site. Common cause: an arithmetic expression for the offset that exceeds `MYFIRST_REG_SIZE`.

### `WITNESS` warning: "acquiring duplicate lock"

Usually a sign that a call chain is acquiring `sc->mtx` recursively. The Chapter 11 mutex is a sleep mutex without the `MTX_RECURSE` flag, which is correct. Trace the stack; one of the callers is re-entering.

### `WITNESS` warning: "lock order reversal"

The driver is holding `sc->mtx` while acquiring another lock (or vice versa) in an order that violates the documented order. Check `LOCKING.md` against the stack trace and fix the call site.

### Ticker does not fire

The tick source callout's interval is zero (disabled). Confirm `dev.myfirst.0.tick_source_interval_ms` is positive. Confirm `reg_ticker_enabled` is 1. Look at the access log for SCRATCH_A writes; if there are none, the callout is the problem. If there are, the sysctl may be stale (re-read it).

### Access log returns empty

Confirm `access_log_enabled` is 1. Confirm the driver mutex is being acquired in access paths (the log update happens under the lock). If the log is genuinely empty but registers should have been accessed, check the access paths for missing accessor calls.

### `dmesg` shows no output from `myfirst_ctrl_update`

The debug_level is 0, or the specific bit is not set. Set `debug_level` to include the right bits and retry.

### `kldunload myfirst` returns EBUSY

Open file descriptors still exist on the device. Close them (or use `fstat -f /dev/myfirst0` to find who holds them) and retry.

### `kldunload myfirst` hangs

Detach is stuck draining a primitive. Use `procstat -kk <pid-of-kldunload>` to see where. Usually the stuck drain is a task or callout that is not cancelling. Check the detach ordering against `LOCKING.md`.

### The stress test complains about WITNESS warnings

Each warning is a real bug. Fix one, retest, continue. Do not mass-disable WITNESS and call the issue resolved; the warnings are pointing at the problem.



## Wrapping Up

Chapter 16 opened Part 4 by giving the `myfirst` driver its first hardware story. The driver now has a register block, even if that block is simulated. It uses `bus_space(9)` the way a real driver does. It protects register access with the Chapter 11 mutex, inserts barriers where ordering matters, and documents every register and every access path. It has an access log for post-hoc debugging, DTrace probes for live observation, and a ddb command for live inspection. It is organised across two files: the main driver lifecycle and the hardware-access layer.

What Chapter 16 deliberately did not do: real PCI (Chapter 18), real interrupts (Chapter 19), real DMA (Chapters 20 and 21), full hardware simulation with dynamic behaviour (Chapter 17). Each of those topics deserves its own chapter; each builds on Chapter 16's vocabulary.

The version is `0.9-mmio`. The file layout is `myfirst.c` plus `myfirst_hw.c` plus `myfirst_hw.h` plus `myfirst_sync.h` plus `cbuf.c` plus `cbuf.h`. The documentation is `LOCKING.md` plus the new `HARDWARE.md`. The test suite has grown by the Chapter 16 labs. Every earlier Part 3 test still passes.

### A Reflection Before Chapter 17

A pause before the next chapter. Chapter 16 was a careful introduction to a set of ideas that will recur for the rest of Part 4 and beyond. The register read and write, the barrier, the bus tag and handle, the locking discipline around MMIO: these are the pieces every later hardware-facing chapter uses without re-teaching them. You have met them all once, in a setting where you could observe, experiment, and make mistakes safely.

The same pattern that defined Part 3 defines Part 4: introduce a primitive, apply it to the driver in a small refactor, document it, test it, move on. The difference is that Part 4's primitives face outward. The driver is no longer a self-contained world; it is a participant in a conversation with hardware. That conversation has rules the driver must respect, and the rules have consequences when they are broken.

Chapter 17 makes the hardware side of the conversation more interesting. The simulated device will grow a callout that changes `STATUS` bits over time. It will signal "data available" after a write by flipping a bit on a delay. It will fail occasionally to teach error-handling paths. The register vocabulary stays the same; the device behaviour becomes richer.

### What to Do If You Are Stuck

If the Chapter 16 material feels overwhelming on first pass, a few suggestions.

First, re-read Section 3. The `bus_space` vocabulary is the foundation; if it is shaky, everything else is shaky.

Second, type Stage 1 by hand, end to end, and run it. Muscle memory produces understanding in a way that reading does not.

Third, open `/usr/src/sys/dev/ale/if_alevar.h` and find the `CSR_*` macros. The real driver's idiom is the same as your Stage 4's. Seeing the pattern in a production driver makes the abstraction feel less arbitrary.

Fourth, skip the challenges on first pass. The labs are calibrated for Chapter 16; the challenges assume the chapter's material is already solid. Come back to them after Chapter 17 if they feel out of reach now.

Chapter 16's goal was clarity of vocabulary. If you have that, the rest of Part 4 will feel navigable.



## Bridge to Chapter 17

Chapter 17 is titled *Simulating Hardware*. Its scope is the deeper simulation that Chapter 16 deliberately stepped around: a register block whose contents change over time, whose protocol has side effects, and whose failures can be injected deliberately for testing. The driver at `0.9-mmio` has a register block that behaves statically; Chapter 17 makes it breathe.

Chapter 16 prepared the ground in four specific ways.

First, **you have a register map**. The offsets, bit masks, and register semantics are documented in `myfirst_hw.h` and `HARDWARE.md`. Chapter 17 extends the map with a few new registers and enriches the protocol of the existing ones; the structure is established.

Second, **you have lock-protected, barrier-aware accessors**. Chapter 17 introduces a callout that updates registers periodically from its own context. Without Chapter 16's locking discipline, the callout would race with the syscall path. With it, the callout slots into the existing mutex without additional work.

Third, **you have an access log**. Chapter 17 uses more elaborate register updates (read-to-clear `INTR_STATUS`, write-triggered delayed `DATA_AV`, simulated errors). The access log is how you will see those updates in action, and Chapter 17 leans on it heavily.

Fourth, **you have a split file layout**. Chapter 17's simulation logic lands in `myfirst_hw.c`, alongside the Chapter 16 accessors. The main driver file stays focused on the driver lifecycle. The split keeps the simulation code contained.

Specific topics Chapter 17 will cover:

- A callout that updates `STATUS` bits on a schedule, simulating autonomous device activity.
- A write-to-trigger-delayed-event pattern: writing `CTRL.GO` schedules a callout that, after a delay, flips `STATUS.DATA_AV`.
- Read-to-clear semantics on `INTR_STATUS`, with the driver's sysctl being careful not to inadvertently clear bits.
- Simulated error injection: a sysctl that causes the next operation to "fail" with a fault bit set in `STATUS`.
- Timeouts: the driver reacts correctly when the simulated device fails to become ready.
- A latency simulation path with `DELAY(9)` and `callout_reset_sbt` for different granularities.

You do not need to read ahead. Chapter 16 is sufficient preparation. Bring your `myfirst` driver at `0.9-mmio`, your `LOCKING.md`, your `HARDWARE.md`, your `WITNESS`-enabled kernel, and your test kit. Chapter 17 starts where Chapter 16 ended.

A small closing reflection. Part 3 taught you the synchronisation vocabulary and a driver that coordinated itself. Chapter 16 added a register vocabulary and a driver that now has a hardware surface. Chapter 17 will give that surface dynamic behaviour; Chapter 18 will replace the simulation with real PCI hardware; Chapter 19 will add interrupts; Chapter 20 and Chapter 21 will add DMA. Each of those chapters is narrower than its topic suggests because Chapter 16 did the vocabulary work first.

The hardware conversation is now beginning. The vocabulary is yours. Chapter 17 opens the next round.



## Reference: `bus_space(9)` Cheat Sheet

A one-page summary of the `bus_space(9)` API, for quick reference while coding.

### Types

| Type                 | Meaning                                           |
|----------------------|---------------------------------------------------|
| `bus_space_tag_t`    | Identifies the address space (memory or I/O).     |
| `bus_space_handle_t` | Identifies a specific region in the address space.|
| `bus_size_t`         | Unsigned integer for offsets inside a region.     |

### Reads

| Function                        | Width | Notes                        |
|---------------------------------|-------|------------------------------|
| `bus_space_read_1(t, h, o)`     | 8     | Returns `u_int8_t`           |
| `bus_space_read_2(t, h, o)`     | 16    | Returns `u_int16_t`          |
| `bus_space_read_4(t, h, o)`     | 32    | Returns `u_int32_t`          |
| `bus_space_read_8(t, h, o)`     | 64    | amd64 memory only            |

### Writes

| Function                           | Width | Notes                        |
|------------------------------------|-------|------------------------------|
| `bus_space_write_1(t, h, o, v)`    | 8     | `v` is `u_int8_t`            |
| `bus_space_write_2(t, h, o, v)`    | 16    | `v` is `u_int16_t`           |
| `bus_space_write_4(t, h, o, v)`    | 32    | `v` is `u_int32_t`           |
| `bus_space_write_8(t, h, o, v)`    | 64    | amd64 memory only            |

### Multi Accesses (same offset, different buffer positions)

| Function                                         | Purpose                                  |
|--------------------------------------------------|------------------------------------------|
| `bus_space_read_multi_1(t, h, o, buf, count)`    | Read `count` bytes from `o`.             |
| `bus_space_read_multi_4(t, h, o, buf, count)`    | Read `count` 32-bit values from `o`.     |
| `bus_space_write_multi_4(t, h, o, buf, count)`   | Write `count` 32-bit values to `o`.      |

### Region Accesses (advancing offset and buffer)

| Function                                          | Purpose                                    |
|---------------------------------------------------|--------------------------------------------|
| `bus_space_read_region_4(t, h, o, buf, count)`    | Read `count` 32-bit values from `o..`      |
| `bus_space_write_region_4(t, h, o, buf, count)`   | Write `count` 32-bit values to `o..`       |

### Barrier

| Function                                     | Purpose                                         |
|----------------------------------------------|-------------------------------------------------|
| `bus_space_barrier(t, h, o, len, flags)`     | Enforce read/write ordering over offset range.  |

Flags:

| Flag                          | Meaning                                   |
|-------------------------------|-------------------------------------------|
| `BUS_SPACE_BARRIER_READ`      | Prior reads complete before later reads.  |
| `BUS_SPACE_BARRIER_WRITE`     | Prior writes complete before later writes.|

### Resource Shorthand (`/usr/src/sys/sys/bus.h`)

| Function                          | Equivalent                                      |
|-----------------------------------|-------------------------------------------------|
| `bus_read_4(r, o)`                | `bus_space_read_4(r->r_bustag, r->r_bushandle, o)` |
| `bus_write_4(r, o, v)`            | `bus_space_write_4(r->r_bustag, r->r_bushandle, o, v)` |
| `bus_barrier(r, o, l, f)`         | `bus_space_barrier(r->r_bustag, r->r_bushandle, o, l, f)` |

### Allocation

| Function                                                             | Purpose                     |
|---------------------------------------------------------------------|-----------------------------|
| `bus_alloc_resource_any(dev, type, &rid, flags)`                    | Allocate a resource.        |
| `bus_release_resource(dev, type, rid, res)`                         | Release a resource.         |
| `rman_get_bustag(res)`                                              | Extract the tag.            |
| `rman_get_bushandle(res)`                                           | Extract the handle.         |

### Types of Resource

| Constant           | Meaning                        |
|--------------------|--------------------------------|
| `SYS_RES_MEMORY`   | Memory-mapped I/O region.      |
| `SYS_RES_IOPORT`   | I/O port range.                |
| `SYS_RES_IRQ`      | Interrupt line.                |

### Flags

| Constant       | Meaning                                       |
|----------------|-----------------------------------------------|
| `RF_ACTIVE`    | Activate the resource (establish mapping).    |
| `RF_SHAREABLE` | The resource may be shared with other drivers.|



## Reference: Further Reading

### Manual Pages

- `bus_space(9)`: the full API reference.
- `bus_dma(9)`: DMA API (Chapter 20 reference).
- `bus_alloc_resource(9)`: resource allocation reference.
- `rman(9)`: the underlying resource manager.
- `pci(9)`: PCI subsystem overview (Chapter 18 preview).
- `device(9)`: the device identity API.
- `memguard(9)`: kernel memory debugging.

### Source Files

- `/usr/src/sys/sys/bus.h`: the bus shorthand macros and the resource API.
- `/usr/src/sys/x86/include/bus.h`: the x86 bus_space implementation.
- `/usr/src/sys/arm64/include/bus.h`: the arm64 equivalent (for comparison).
- `/usr/src/sys/dev/ale/if_alevar.h`: a clean example of `CSR_*` macros.
- `/usr/src/sys/dev/e1000/if_em.c`: a production driver's allocation flow.
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: a register-heavy driver for a classic device.
- `/usr/src/sys/dev/led/led.c`: a pseudo-device driver with no hardware.

### Reading Order

If you want to go deeper before Chapter 17, read in this order:

1. `/usr/src/sys/sys/bus.h`, the `bus_read_*` / `bus_write_*` shorthand-macro block (search for `#define bus_read_1`).
2. `/usr/src/sys/x86/include/bus.h` in full (the implementation).
3. `/usr/src/sys/dev/ale/if_alevar.h` in full (the softc, macros, constants).
4. `/usr/src/sys/dev/ale/if_ale.c` attach path (search for `bus_alloc_resource`).
5. `/usr/src/sys/dev/e1000/if_em.c` attach path (same search).

Each reading is 15 to 45 minutes. The cumulative effect is a strong grasp of the idioms Chapter 16 introduced.



## Reference: A Glossary of Chapter 16 Terms

### Terms Introduced in This Chapter

**access log**: A ring buffer of recent register accesses, kept in the softc for debugging.

**alignment**: The requirement that a register access's offset be a multiple of the access width.

**barrier**: A function or instruction that enforces ordering between prior and subsequent memory accesses.

**BAR (Base Address Register)**: On PCI, a device's register that advertises the physical address of its MMIO region. Chapter 18 treats BARs directly.

**bus_space_handle_t**: An opaque identifier of a specific region within a bus address space.

**bus_space_tag_t**: An opaque identifier of a bus address space (typically memory or I/O port).

**CSR macro**: A driver-specific wrapper macro (e.g., `CSR_READ_4`) that abstracts register access behind a short name.

**endianness**: The byte order in which a multi-byte register is laid out. Little-endian puts the low byte first; big-endian puts the high byte first.

**field**: A sub-bit-range of a register, with its own name and meaning.

**firmware revision register**: A read-only register that reports the device's firmware version.

**I/O port**: An x86-specific address space, accessed with `in` and `out` instructions. Contrasts with MMIO.

**MMIO (memory-mapped I/O)**: The mechanism where device registers are exposed as a range of physical addresses, reachable through ordinary load and store instructions.

**offset**: The distance, in bytes, from the start of a device region to a specific register.

**PIO (port-mapped I/O)**: On x86, the alternative to MMIO, using separate I/O port instructions.

**region**: A contiguous range of device address space, or the API family that walks across offsets.

**register**: A named, offset-located, width-specific unit of communication between the driver and a device.

**register map**: A table describing every register in a device's interface: offset, width, direction, meaning.

**resource (FreeBSD)**: A named allocation from the bus subsystem, encapsulating a tag, handle, and ownership of a specific range.

**sbuf**: The sbuf(9) kernel API for building variable-length strings, used by the access-log sysctl handler.

**side effect (register)**: A change in device state that a read or write causes as part of its semantics, beyond returning or storing the value.

**simulation**: In Chapter 16, the use of kernel memory allocated with `malloc(9)` to stand in for a device's MMIO region.

**stream accessor**: A `bus_space_*_stream_*` variant that does not apply endian swaps.

**virtual mapping**: The MMU's translation from a virtual address to a physical address, with specific cache and access attributes.

**width**: The bit count of a register or an access function's operand (8, 16, 32, 64).

### Terms Previously Introduced (Reminders)

- **softc**: The per-instance driver state structure (Chapter 6).
- **device_t**: The kernel's identity for a device instance (Chapter 6).
- **malloc(9)**: The kernel allocator (Chapter 5).
- **WITNESS**: The kernel's lock-order checker (Chapter 11).
- **INVARIANTS**: The kernel's defensive assertion framework (Chapter 11).
- **callout**: A timer primitive that invokes a callback after a delay (Chapter 13).
- **taskqueue**: A deferred-work primitive (Chapter 14).
- **cv_wait / cv_timedwait_sig**: Condition-variable waits (Chapter 12, Chapter 15).



## Reference: The Chapter 16 Driver Diff Summary

A compact view of what Chapter 16 added to the `myfirst` driver, stage by stage, for readers who want to see the whole arc on one page.

### Stage 1 (Section 4)

- New file: `myfirst_hw.h` with register offsets, masks, fixed values.
- `regs_buf` and `regs_size` in softc; allocated and freed in attach/detach.
- Accessor helpers: `myfirst_reg_read`, `myfirst_reg_write`, `myfirst_reg_update`.
- Macros: `MYFIRST_REG_READ`, `MYFIRST_REG_WRITE`.
- Sysctls: `reg_ctrl`, `reg_status`, `reg_device_id`, `reg_firmware_rev` (read), `reg_ctrl_set` (write).
- Coupling: `myfirst_ctrl_update` on CTRL writes.
- Version tag: `0.9-mmio-stage1`.

### Stage 2 (Section 5)

- Added `regs_tag`, `regs_handle` in softc.
- Accessor bodies rewritten to use `bus_space_read_4` and `bus_space_write_4`.
- Write path updates `DATA_IN` and `STATUS.DATA_AV`.
- Read path updates `DATA_OUT` and clears `STATUS.DATA_AV` when the buffer drains.
- `reg_ticker_task` added; increments `SCRATCH_A` per tick.
- New sysctls: `reg_data_in`, `reg_data_out`, `reg_intr_mask`, `reg_intr_status`, `reg_scratch_a`, `reg_scratch_b`, `reg_ticker_enabled`.
- New document: `HARDWARE.md`.
- Version tag: `0.9-mmio-stage2`.

### Stage 3 (Section 6)

- `MYFIRST_ASSERT` added to accessors.
- All register access paths acquire `sc->mtx`.
- Access log and its sysctls added (`access_log_enabled`, `access_log`).
- `myfirst_reg_write_barrier` helper for barrier-aware writes.
- `HARDWARE.md` extended with per-register ownership.
- Version tag: `0.9-mmio-stage3`.

### Stage 4 (Section 8)

- File split: `myfirst_hw.c`, `myfirst_hw.h`, `myfirst.c`.
- `struct myfirst_hw` grouping of hardware state.
- `myfirst_hw_attach`, `myfirst_hw_detach`, `myfirst_hw_add_sysctls` APIs.
- `CSR_READ_4`, `CSR_WRITE_4`, `CSR_UPDATE_4` macros.
- `HARDWARE.md` finalised.
- Full regression pass.
- Version tag: `0.9-mmio`.

### Lines of Code

- Stage 1 adds about 80 lines (header, accessor helpers, sysctls).
- Stage 2 adds about 90 lines (accessor rewrite, data-path coupling, ticker task).
- Stage 3 adds about 70 lines (locking, access log, barrier helper).
- Stage 4 is a net reorganisation: lines move between files but the total is roughly unchanged.

Total additions, Chapter 16: roughly 240 to 280 lines across four small stages.



## Reference: A Comparison with Linux Device Register Access

Because many readers come to FreeBSD from Linux, a short comparison of the register-access vocabulary clarifies what translates and what does not.

### Linux: `ioremap` + `readl` / `writel`

Linux uses a different shape. A driver obtains a virtual address through `ioremap` (for MMIO) or uses the raw I/O port number directly (for PIO). Register access is performed through `readl(addr)` and `writel(value, addr)`, with variants for different widths (`readb`, `readw`, `readl`, `readq`). The `addr` is a kernel virtual pointer cast to a specific marker type.

### FreeBSD: `bus_alloc_resource` + `bus_read_*` / `bus_write_*`

FreeBSD uses the tag-and-handle abstraction. A driver obtains a `struct resource *` through `bus_alloc_resource_any`, then uses `bus_read_4` and `bus_write_4` on it. The tag and handle are extracted by the macro from the resource; the driver does not see them directly in most code.

### What Translates

- The mental model: registers at fixed offsets, accessed by width, with barriers for ordering.
- The idea of defining a header of register offsets and bit masks.
- The idea of wrapping access in driver-specific macros (Linux's `read_reg32`, FreeBSD's `CSR_READ_4`).
- The discipline of lock-protected access for shared state.

### What Differs

- FreeBSD carries an explicit tag that encodes the address-space type. Linux does not; the function variants choose the address space implicitly.
- FreeBSD's resource abstraction is more explicit about ownership and lifecycle. Linux's `ioremap` is a thinner wrapper.
- FreeBSD's barrier function takes offset and length arguments that bus bridges can use for narrow barriers. Linux's `mb`, `rmb`, `wmb`, and `mmiowb` are CPU-wide.
- FreeBSD's `bus_space` is usable for simulation (as in this chapter); Linux's equivalent path is less friendly for that use.

Porting a driver from Linux to FreeBSD or vice versa involves rewriting the register-access layer but not the register map, because the map is defined by the device, not the OS. A well-organised driver that keeps its register access behind CSR-style macros can have its CSR macros replaced with minimal other code changes.



## Reference: A Worked Example: The Full `myfirst_hw.h`

For reference, the complete Stage 4 header. This is what lives at `examples/part-04/ch16-accessing-hardware/stage4-final/myfirst_hw.h`.

```c
/* myfirst_hw.h -- Chapter 16 Stage 4 simulated hardware interface. */
#ifndef _MYFIRST_HW_H_
#define _MYFIRST_HW_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <machine/bus.h>

/* Register offsets. */
#define MYFIRST_REG_CTRL         0x00
#define MYFIRST_REG_STATUS       0x04
#define MYFIRST_REG_DATA_IN      0x08
#define MYFIRST_REG_DATA_OUT     0x0c
#define MYFIRST_REG_INTR_MASK    0x10
#define MYFIRST_REG_INTR_STATUS  0x14
#define MYFIRST_REG_DEVICE_ID    0x18
#define MYFIRST_REG_FIRMWARE_REV 0x1c
#define MYFIRST_REG_SCRATCH_A    0x20
#define MYFIRST_REG_SCRATCH_B    0x24

#define MYFIRST_REG_SIZE         0x40

/* CTRL bits. */
#define MYFIRST_CTRL_ENABLE      0x00000001u
#define MYFIRST_CTRL_RESET       0x00000002u
#define MYFIRST_CTRL_MODE_MASK   0x000000f0u
#define MYFIRST_CTRL_MODE_SHIFT  4
#define MYFIRST_CTRL_LOOPBACK    0x00000100u

/* STATUS bits. */
#define MYFIRST_STATUS_READY     0x00000001u
#define MYFIRST_STATUS_BUSY      0x00000002u
#define MYFIRST_STATUS_ERROR     0x00000004u
#define MYFIRST_STATUS_DATA_AV   0x00000008u

/* INTR bits. */
#define MYFIRST_INTR_DATA_AV     0x00000001u
#define MYFIRST_INTR_ERROR       0x00000002u
#define MYFIRST_INTR_COMPLETE    0x00000004u

/* Fixed values. */
#define MYFIRST_DEVICE_ID_VALUE  0x4D594649u
#define MYFIRST_FW_REV_MAJOR     1
#define MYFIRST_FW_REV_MINOR     0
#define MYFIRST_FW_REV_VALUE     ((MYFIRST_FW_REV_MAJOR << 16) | MYFIRST_FW_REV_MINOR)

/* Access log. */
#define MYFIRST_ACCESS_LOG_SIZE  64

struct myfirst_access_log_entry {
        uint64_t   timestamp_ns;
        uint32_t   value;
        bus_size_t offset;
        uint8_t    is_write;
        uint8_t    width;
        uint8_t    context_tag;
        uint8_t    _pad;
};

/* Hardware state, grouped. */
struct myfirst_hw {
        uint8_t                *regs_buf;
        size_t                  regs_size;
        bus_space_tag_t         regs_tag;
        bus_space_handle_t      regs_handle;

        struct task             reg_ticker_task;
        int                     reg_ticker_enabled;

        struct myfirst_access_log_entry access_log[MYFIRST_ACCESS_LOG_SIZE];
        unsigned int            access_log_head;
        bool                    access_log_enabled;
};

/* API. */
struct myfirst_softc;

int  myfirst_hw_attach(struct myfirst_softc *sc);
void myfirst_hw_detach(struct myfirst_softc *sc);
void myfirst_hw_add_sysctls(struct myfirst_softc *sc);

uint32_t myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset);
void     myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value);
void     myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t clear_mask, uint32_t set_mask);
void     myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
             uint32_t value, int flags);

#define CSR_READ_4(sc, off)        myfirst_reg_read((sc), (off))
#define CSR_WRITE_4(sc, off, val)  myfirst_reg_write((sc), (off), (val))
#define CSR_UPDATE_4(sc, off, clear, set) \
        myfirst_reg_update((sc), (off), (clear), (set))

#endif /* _MYFIRST_HW_H_ */
```

This single header is what the rest of the driver includes to gain access to the hardware interface. A newcomer reads it once and understands what registers exist, how they are accessed, and which macros the driver body uses.



## Reference: A Worked Example: The `myfirst_hw.c` Accessor Functions

Complementing the header, the implementations. For reference and as a template.

```c
/* myfirst_hw.c -- Chapter 16 Stage 4 hardware access layer. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/taskqueue.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <machine/bus.h>

#include "myfirst.h"      /* struct myfirst_softc, MYFIRST_LOCK, ... */
#include "myfirst_hw.h"

MALLOC_DECLARE(M_MYFIRST);

uint32_t
myfirst_reg_read(struct myfirst_softc *sc, bus_size_t offset)
{
        struct myfirst_hw *hw = sc->hw;
        uint32_t value;

        MYFIRST_ASSERT(sc);
        KASSERT(hw != NULL, ("myfirst: hw is NULL in reg_read"));
        KASSERT(offset + 4 <= hw->regs_size,
            ("myfirst: register read past end: offset=%#x size=%zu",
             (unsigned)offset, hw->regs_size));

        value = bus_space_read_4(hw->regs_tag, hw->regs_handle, offset);

        if (hw->access_log_enabled) {
                unsigned int idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &hw->access_log[idx];
                struct timespec ts;
                nanouptime(&ts);
                e->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                e->value = value;
                e->offset = offset;
                e->is_write = 0;
                e->width = 4;
                e->context_tag = 'd';
        }

        return (value);
}

void
myfirst_reg_write(struct myfirst_softc *sc, bus_size_t offset, uint32_t value)
{
        struct myfirst_hw *hw = sc->hw;

        MYFIRST_ASSERT(sc);
        KASSERT(hw != NULL, ("myfirst: hw is NULL in reg_write"));
        KASSERT(offset + 4 <= hw->regs_size,
            ("myfirst: register write past end: offset=%#x size=%zu",
             (unsigned)offset, hw->regs_size));

        bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value);

        if (hw->access_log_enabled) {
                unsigned int idx = hw->access_log_head++ % MYFIRST_ACCESS_LOG_SIZE;
                struct myfirst_access_log_entry *e = &hw->access_log[idx];
                struct timespec ts;
                nanouptime(&ts);
                e->timestamp_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
                e->value = value;
                e->offset = offset;
                e->is_write = 1;
                e->width = 4;
                e->context_tag = 'd';
        }
}

void
myfirst_reg_update(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t clear_mask, uint32_t set_mask)
{
        uint32_t v;

        MYFIRST_ASSERT(sc);
        v = myfirst_reg_read(sc, offset);
        v &= ~clear_mask;
        v |= set_mask;
        myfirst_reg_write(sc, offset, v);
}

void
myfirst_reg_write_barrier(struct myfirst_softc *sc, bus_size_t offset,
    uint32_t value, int flags)
{
        struct myfirst_hw *hw = sc->hw;

        MYFIRST_ASSERT(sc);
        myfirst_reg_write(sc, offset, value);
        bus_space_barrier(hw->regs_tag, hw->regs_handle, 0, hw->regs_size, flags);
}
```

This is a complete, working file. The `myfirst_hw_attach`, `myfirst_hw_detach`, and `myfirst_hw_add_sysctls` functions follow in the same file; they are longer but follow the same patterns as Section 4's worked text.



## Reference: A Minimal Stand-Alone Test Module

For readers who want to practise `bus_space(9)` in isolation from the `myfirst` driver, here is a minimal stand-alone kernel module that allocates a kernel-memory "device", exposes it through sysctls, and lets the reader experiment. Save as `hwsim.c`:

```c
/* hwsim.c -- Chapter 16 stand-alone bus_space(9) practice module. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <machine/bus.h>

MALLOC_DEFINE(M_HWSIM, "hwsim", "hwsim test module");

#define HWSIM_SIZE 0x40

static uint8_t            *hwsim_buf;
static bus_space_tag_t     hwsim_tag;
static bus_space_handle_t  hwsim_handle;

static SYSCTL_NODE(_dev, OID_AUTO, hwsim,
    CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, "hwsim practice module");

static int
hwsim_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
        bus_size_t offset = arg2;
        uint32_t value;
        int error;

        if (hwsim_buf == NULL)
                return (ENODEV);
        value = bus_space_read_4(hwsim_tag, hwsim_handle, offset);
        error = sysctl_handle_int(oidp, &value, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        bus_space_write_4(hwsim_tag, hwsim_handle, offset, value);
        return (0);
}

static int
hwsim_modevent(module_t mod, int event, void *arg)
{
        switch (event) {
        case MOD_LOAD:
                hwsim_buf = malloc(HWSIM_SIZE, M_HWSIM, M_WAITOK | M_ZERO);
#if defined(__amd64__) || defined(__i386__)
                hwsim_tag = X86_BUS_SPACE_MEM;
#else
                free(hwsim_buf, M_HWSIM);
                hwsim_buf = NULL;
                return (EOPNOTSUPP);
#endif
                hwsim_handle = (bus_space_handle_t)(uintptr_t)hwsim_buf;

                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
                    OID_AUTO, "reg0",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0x00, hwsim_sysctl_reg, "IU",
                    "Offset 0x00");
                SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_dev_hwsim),
                    OID_AUTO, "reg4",
                    CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
                    NULL, 0x04, hwsim_sysctl_reg, "IU",
                    "Offset 0x04");
                return (0);
        case MOD_UNLOAD:
                if (hwsim_buf != NULL) {
                        free(hwsim_buf, M_HWSIM);
                        hwsim_buf = NULL;
                }
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}

static moduledata_t hwsim_mod = {
        "hwsim",
        hwsim_modevent,
        NULL
};

DECLARE_MODULE(hwsim, hwsim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(hwsim, 1);
```

A two-line `Makefile`:

```text
KMOD=  hwsim
SRCS=  hwsim.c

.include <bsd.kmod.mk>
```

Build, load, and play:

```text
# make clean && make
# kldload ./hwsim.ko
# sysctl dev.hwsim.reg0
# sysctl dev.hwsim.reg0=0xdeadbeef
# sysctl dev.hwsim.reg0
# sysctl dev.hwsim.reg4=0x12345678
# sysctl dev.hwsim.reg4
# kldunload hwsim
```

The module demonstrates `bus_space(9)` in its simplest possible form: a memory buffer, a tag, a handle, two register slots, a pair of sysctls. A reader who types this in and runs it has the whole vocabulary of Section 3 in about 80 lines of C.



## Reference: Why `volatile` Matters in `bus_space`

A note on a subtle detail the chapter touched on but did not expand.

When `bus_space_read_4` on x86 expands to `*(volatile u_int32_t *)(handle + offset)`, the `volatile` qualifier is not decorative. It is load-bearing.

Without `volatile`, the compiler assumes that reading a memory location twice in sequence, with no intervening store to that location, must return the same value. It is free to reorder, coalesce, or elide reads based on that assumption. For ordinary memory, the assumption holds: RAM does not change underneath you. For device memory, the assumption is wrong. A read might consume an event; a write might have immediate and visible effects that a subsequent read sees.

The `volatile` qualifier tells the compiler: treat this access as having observable side effects. Do not reorder it with other volatile accesses. Do not elide it. Do not cache its result. Emit a load (or store) every time, exactly as written.

On x86, this is enough. The CPU's memory model is strong enough that once the load is emitted in program order, it executes in program order. On arm64, additional barriers are needed to enforce program order across CPU-level reordering, which is why `bus_space_barrier` on arm64 emits DMB or DSB instructions and on x86 emits only a compiler fence.

The short rule: every time you write a hand-rolled accessor for device memory, use `volatile`. Every time you use `bus_space_*` directly, the `volatile` is already there. Every time you cast a pointer to device memory through a non-volatile type, you have a bug waiting to happen.



## Reference: A Short Comparison of Access Patterns in Real FreeBSD Drivers

An informal survey of patterns used in real drivers. Each example cites the file and the characteristic pattern; read the files themselves to see the pattern in context.

**`/usr/src/sys/dev/ale/if_ale.c`**: Uses `CSR_READ_4(sc, reg)` and `CSR_WRITE_4(sc, reg, val)` macros defined in `if_alevar.h` over `bus_read_4` and `bus_write_4`. Softc holds `ale_res[]`, an array of resources. The pattern is clean and scales well.

**`/usr/src/sys/dev/e1000/if_em.c`**: Uses `E1000_READ_REG(hw, reg)` and `E1000_WRITE_REG(hw, reg, val)` that wrap `bus_space_read_4` and `bus_space_write_4` on the `osdep` struct's tag and handle. More indirection than `ale`, justified by Intel's cross-OS shared-code model.

**`/usr/src/sys/dev/uart/uart_bus_pci.c`**: A glue driver that allocates resources and hands them to the generic UART subsystem. The register access happens in subsystem code (`uart_dev_ns8250.c`), not in the PCI glue.

**`/usr/src/sys/dev/uart/uart_dev_ns8250.c`**: Direct `bus_read_1` and `bus_write_1` on a `struct uart_bas *` that holds tag and handle. Legacy 8-bit register layout.

**`/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`**: Uses `bus_read_4` and `bus_write_4` through `struct resource *` fields in the softc. Chapter 18 uses virtio as a test target for real-PCI exercises.

**`/usr/src/sys/dev/random/ivy.c`** (Intel Ivy Bridge RDRAND): Uses CPU instructions directly (`rdrand`) rather than `bus_space`; this is an unusual case because the "device" is the CPU itself, accessible through inline assembly.

Across all of these, the pattern is "wrap `bus_*` or `bus_space_*` in driver-specific macros, keep register offsets in a header, access registers through the macros in the body". Chapter 16's Stage 4 matches this convention.



## Reference: The Road Ahead in Part 4

A preview of how Chapter 16's material feeds into later chapters, for readers who like a single-page map.

**Chapter 17 (Simulating Hardware)**: Extends the simulation with dynamic behaviour. Timers change `STATUS` bits. Writing `CTRL.GO` triggers a delayed update. Errors can be injected. The register vocabulary stays the same; the simulation becomes richer.

**Chapter 18 (Writing a PCI Driver)**: Replaces the simulation with real PCI. `bus_alloc_resource_any` enters in earnest. Vendor and device IDs, BAR mapping, `pci_enable_busmaster`, `pciconf`. The simulation path remains available behind a compile-time flag for continued testing.

**Chapter 19 (Handling Interrupts)**: Adds `bus_setup_intr`, filter vs ithread, interrupt acknowledgement, the `INTR_STATUS` register's read-to-clear semantics in a real context. The Chapter 16 access log becomes invaluable for debugging interrupt sequences.

**Chapter 20 and Chapter 21 (DMA)**: Add `bus_dma(9)`. Register accesses become the control surface for DMA operations: set up a descriptor, write a doorbell register, wait for completion. The `bus_space_barrier` story becomes essential.

**Chapter 22 (Power Management)**: Suspend, resume, dynamic power states. Registers that save and restore device state. Most of the Part 4 vocabulary applies; power management adds a few more idioms.

Each chapter introduces a new layer; Chapter 16's layer (the register) is the foundation for all of them. A reader who leaves Chapter 16 comfortable with `bus_space(9)` will find each subsequent chapter adds a new vocabulary on top of familiar ground.



## Reference: How to Read a Datasheet

Every real driver starts with a datasheet: the document the device's manufacturer publishes describing the register interface, the programming model, and the operational behaviour. Chapter 16 works with a simulated device, so there is no datasheet to consult. Later chapters point at real devices, and a driver author who is comfortable with datasheets will learn faster.

A brief primer follows. The reader can skip this on first pass and return when Chapter 18 or a later chapter points at a real device's specification.

### The Shape of a Datasheet

A datasheet is usually a PDF of fifty to fifteen hundred pages. It covers:

- A functional overview (what the device does, at a high level).
- Pinout or physical interface description (what signals the device has, what they mean).
- Register reference (the mapping Chapter 16 has been teaching you to read).
- Programming model (the sequence of operations a driver must perform for each high-level action).
- Electrical characteristics (voltages, timings, environmental ratings).
- Package dimensions (mechanical data for the circuit board designer).

Driver authors care primarily about the register reference and the programming model. Everything else is for hardware designers.

### Reading the Register Reference

The register reference is usually a series of tables, one per register, with the following columns:

- Offset.
- Width.
- Reset value (the value the register has after power-on or reset).
- Access type (R, W, RW, R/W1C for read with write-one-to-clear, and so on).
- Field names and bit ranges.
- Field descriptions.

A seasoned driver author reads this table first, notes any unusual access types, and translates the register map into a C header with named offsets and bit masks. The translation is mechanical; the care is in getting every bit right.

A particular note on **reset values**. The reset value tells you what the register reads immediately after the device has been powered on or reset. If the driver writes a field and later reads it back, the read should return the value written (not the reset value). But if the driver has not written the register, the read returns the reset value. Getting this wrong produces surprising bugs: the driver "sees" a register it did not initialise and misinterprets the reset value as a state change.

### Reading the Programming Model

The programming model section describes the sequences of register operations required to drive the device. A typical entry looks like:

> **Transmit one packet.**
> 1. Confirm `STATUS.TX_READY` is set.
> 2. Write the packet data to `TX_BUF[0..n-1]` in order.
> 3. Write the packet length to `TX_LEN`.
> 4. Write `CTRL.TX_START`.
> 5. Wait for `STATUS.TX_DONE` to assert (may take up to 100us).
> 6. Clear `STATUS.TX_DONE` by writing 1 to the same bit.

This sequence is what a driver's transmit path implements. The order of steps is fixed; reordering can leave the device in an inconsistent state. The driver's job is to translate each step into a `bus_space_write_*` or `bus_space_read_*` call, with the proper barriers and locking.

Most datasheets have several such sequences. A network device might have receive, transmit, link initialisation, error recovery, and shutdown sequences. Each is documented independently.

### Extracting the C Header

A skilled driver author reads the register reference once and produces a C header something like:

```c
/* foo_regs.h -- derived from Foo Corp. Foo-9000 datasheet, rev 3.2. */

#define FOO_REG_CTRL     0x0000
#define FOO_REG_STATUS   0x0004
#define FOO_REG_TX_LEN   0x0010
#define FOO_REG_TX_BUF   0x0100  /* base of 4 KiB TX buffer region */

#define FOO_CTRL_TX_START 0x00000001u
#define FOO_CTRL_RX_ENABLE 0x00000002u

#define FOO_STATUS_TX_READY 0x00000001u
#define FOO_STATUS_TX_DONE  0x00000002u

/* ... and so on ... */
```

The header is where the driver's knowledge of the device's register interface lives. Keep it up to date with the datasheet; reference the datasheet revision in the header so future contributors know which version the offsets match.

### A Pattern for Each Type of Register

Different access types imply different coding patterns.

**Read-only, no side effect.** Read whenever. Cache if convenient. No locking needed beyond "do not read a register from a region that has been freed".

**Read-only, with side effect (read-to-clear).** Read exactly as often as the protocol requires. Do not add debug reads that clear state. Do not re-read to "confirm" a value.

**Write-only.** Write with whatever value the protocol requires. Do not read it back; the read returns garbage.

**Read/write, no side effect.** Safe read-modify-write sequences under lock.

**Read/write, with side effect on write.** Be careful with read-modify-write: the write of an unchanged bit may still trigger the side effect. Sometimes a datasheet documents this by saying "writes of 0 to bit X have no effect"; sometimes it does not, and the driver must be conservative.

**Write-one-to-clear (W1C).** Common for interrupt status registers. Writing 1 to a bit clears it; writing 0 has no effect. Use `CSR_WRITE_4(sc, REG, mask_of_bits_to_clear)`, not a read-modify-write.

### An Exercise: Pretend the Chapter 16 Device Has a Datasheet

To close this reference, practise extracting a register header from a "datasheet" for the Chapter 16 simulated device. Write a fake datasheet in prose describing each register, its reset value, its access type, and its field layout. Then produce the corresponding `myfirst_hw.h`. Compare your version with the one Chapter 16 provided.

The exercise builds the muscle you will need for every real device later in the book.



## Reference: A Case Study in Missing Barriers

A short cautionary tale to make the barrier story concrete, using only the MMIO vocabulary Chapter 16 has already introduced.

Imagine a real device whose datasheet says: "To send a command, write the 32-bit command word into `CMD_DATA`, then write the 32-bit command code into `CMD_GO`. The device picks up the command word when `CMD_GO` is written." The driver expresses this sequence naively:

```c
/* Step 1: write the command payload. */
CSR_WRITE_4(sc, MYFIRST_REG_CMD_DATA, payload);

/* Step 2: write the command code to trigger execution. */
CSR_WRITE_4(sc, MYFIRST_REG_CMD_GO, opcode);
```

On x86 this works. The x86 memory model guarantees that stores retire in program order from the CPU's point of view, and the compiler's `volatile`-annotated write inside `bus_space_write_4` prevents it from reordering the two statements. By the time `CMD_GO` reaches the device, `CMD_DATA` has already been written.

On arm64 the same code can fail. The CPU is free to reorder the two stores at the memory subsystem level. The device can observe `CMD_GO` first, grab whatever stale value still sits in `CMD_DATA`, and execute a command the driver did not intend. The symptom is intermittent, load-dependent, and only appears on arm64 hardware. A driver tested only on x86 would ship with this bug undetected.

The fix is a one-line change:

```c
CSR_WRITE_4(sc, MYFIRST_REG_CMD_DATA, payload);

/* Ensure the payload write reaches the device before the doorbell. */
bus_space_barrier(sc->hw->regs_tag, sc->hw->regs_handle, 0, sc->hw->regs_size,
    BUS_SPACE_BARRIER_WRITE);

CSR_WRITE_4(sc, MYFIRST_REG_CMD_GO, opcode);
```

On x86, `bus_space_barrier` with `BUS_SPACE_BARRIER_WRITE` emits only a compiler fence, which is free in instruction terms and preserves the program order the x86 CPU was already going to preserve. On arm64, it emits a `dmb` or `dsb` that forces the CPU to drain its store buffer before the next store is issued. The same source code does the right thing on both.

The tale makes three points.

**First, x86 gives driver authors a false sense of security.** Code tested only on x86 may pass every test yet be broken on arm64 in ways that manifest only under specific load patterns.

**Second, portability costs are tiny if you build them in early.** Adding a `bus_space_barrier` at the right spot is a one-line change. Diagnosing the bug a year later on an arm64 deployment is a week of work.

**Third, the cost of barriers on x86 is negligible for typical drivers.** A compiler fence is free in instruction terms; it constrains the compiler's reordering, which for a driver's cold paths matters not at all.

A related family of ordering bugs appears when the driver writes to memory the device reads through DMA (a descriptor ring, for example) and then rings a doorbell register. That pattern needs a `bus_dmamap_sync` call, not just a `bus_space_barrier`; Chapter 20 teaches the DMA path in depth. The vocabulary is different, but the intuition (writes must drain before the doorbell) is the same.

The discipline Chapter 16 encourages, even when the immediate benefit is invisible, pays off when the code runs on hardware the author never saw.



## Reference: Reading `if_ale.c` Step By Step

A guided walk through the attach path of a real driver so the vocabulary of Chapter 16 lands in production code. Open `/usr/src/sys/dev/ale/if_ale.c`, jump to `ale_attach`, and follow along.

### Step 1: The Attach Entry Point

The `ale_attach` function begins:

```c
static int
ale_attach(device_t dev)
{
        struct ale_softc *sc;
        if_t ifp;
        uint16_t burst;
        int error, i, msic, msixc;
        uint32_t rxf_len, txf_len;

        error = 0;
        sc = device_get_softc(dev);
        sc->ale_dev = dev;
```

`device_get_softc(dev)` is the same pattern Chapter 6 introduced. Nothing new here.

### Step 2: Early Locking Setup

The driver initialises its data-path mutex, its callout, and its first task:

```c
mtx_init(&sc->ale_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
    MTX_DEF);
callout_init_mtx(&sc->ale_tick_ch, &sc->ale_mtx, 0);
NET_TASK_INIT(&sc->ale_int_task, 0, ale_int_task, sc);
```

Every line here came straight out of Part 3. The mutex is the Chapter 11 primitive; the lock-aware callout is the Chapter 13 primitive; the task is the Chapter 14 primitive. A reader who has done Part 3 recognises all three immediately.

### Step 3: PCI Bus-Mastering and Resource Allocation

```c
pci_enable_busmaster(dev);
sc->ale_res_spec = ale_res_spec_mem;
sc->ale_irq_spec = ale_irq_spec_legacy;
error = bus_alloc_resources(dev, sc->ale_res_spec, sc->ale_res);
if (error != 0) {
        device_printf(dev, "cannot allocate memory resources.\n");
        goto fail;
}
```

`pci_enable_busmaster` is PCI-specific; Chapter 18 covers it. The `ale_res_spec` is a `struct resource_spec` array (defined earlier in the file) that describes which resources the driver wants. `bus_alloc_resources` (plural) takes the spec and fills in the `sc->ale_res` array with the allocated resources. This is a slight convenience wrapper over calling `bus_alloc_resource_any` in a loop; either pattern is common, and the Chapter 18 discussion of PCI resource allocation covers both.

After this call, `sc->ale_res[0]` holds a `struct resource *` for the device's MMIO region, and the `CSR_READ_*` / `CSR_WRITE_*` macros (defined in `/usr/src/sys/dev/ale/if_alevar.h`, right after the softc structure) can be used to access registers through it.

### Step 4: Reading the First Register

The driver reads the `PHY_STATUS` register to decide which chip variant it is running on:

```c
if ((CSR_READ_4(sc, ALE_PHY_STATUS) & PHY_STATUS_100M) != 0) {
        /* L1E AR8121 */
        sc->ale_flags |= ALE_FLAG_JUMBO;
} else {
        /* L2E Rev. A. AR8113 */
        sc->ale_flags |= ALE_FLAG_FASTETHER;
}
```

This is the first register access in `ale_attach`. It is a single `CSR_READ_4` call that returns a 32-bit value, masked against the `PHY_STATUS_100M` bit, and used to select a code path. The `ALE_PHY_STATUS` constant is a register offset defined in `/usr/src/sys/dev/ale/if_alereg.h`. The bit mask `PHY_STATUS_100M` is defined in the same header.

Every element of that one line is Chapter 16 vocabulary. `CSR_READ_4` expands to `bus_read_4` over the first resource; `bus_read_4` expands to `bus_space_read_4` over the tag and handle inside the resource; on x86 memory space, `bus_space_read_4` compiles to a single `mov` instruction.

### Step 5: Reading More Registers

A few lines later the driver reads three more registers to gather chip-identification data:

```c
sc->ale_chip_rev = CSR_READ_4(sc, ALE_MASTER_CFG) >>
    MASTER_CHIP_REV_SHIFT;
/* ... */
txf_len = CSR_READ_4(sc, ALE_SRAM_TX_FIFO_LEN);
rxf_len = CSR_READ_4(sc, ALE_SRAM_RX_FIFO_LEN);
```

Same pattern, three more registers. Notice the uninitialised-hardware check a few lines down, guarded by `sc->ale_chip_rev == 0xFFFF`: if any of the returned values looks like `0xFFFFFFFF`, the driver assumes the hardware is not correctly initialised and bails out with `ENXIO`. This kind of sanity check is a common, quiet habit in production drivers: hardware that returns all-ones on every register typically means the mapping is wrong, the device is not responding, or the device was power-gated and never brought up.

### Step 6: IRQ Setup

Further down:

```c
error = bus_alloc_resources(dev, sc->ale_irq_spec, sc->ale_irq);
```

IRQ resources get their own allocation. Chapter 19 covers what happens next: `bus_setup_intr`, the filter-ithread split, and the interrupt handler.

### Step 7: Reading the Whole File

After the IRQ allocation, `ale_attach` continues with DMA tag creation (Chapter 20), ifnet registration (Chapter 28 in Part 6), PHY attach, and so on. Each step uses patterns that will be introduced by later chapters in this book. What Chapter 16 gave you is the vocabulary to read every `CSR_*` macro call without stopping.

The exercise that consolidates the walk: pick three `CSR_READ_4` or `CSR_WRITE_4` calls from anywhere in `/usr/src/sys/dev/ale/if_ale.c`, look up the register offset in `if_alereg.h`, decode the bit mask in the same header, and write one sentence explaining what the driver is doing at that call site. If you can do that for three arbitrary calls, you have internalised the vocabulary this chapter taught.



## Reference: An Honest Accounting of Chapter 16's Simplifications

A chapter that teaches a small slice of a large topic inevitably simplifies. For honesty with the reader, a catalogue of what Chapter 16 simplified and what the full story looks like.

### The Simulated Tag

Chapter 16's simulation uses `X86_BUS_SPACE_MEM` as the tag and a kernel virtual address as the handle. On x86 this works because the x86 `bus_space_read_*` functions reduce to a `volatile` dereference of `handle + offset` for memory space. On other architectures the trick fails because the tag is not an integer; it is a pointer to a structure, and manufacturing one by hand requires reproducing the structure the platform's `bus_space` expects.

The full story: real drivers never manufacture a tag; they receive one from the bus subsystem through `rman_get_bustag`. The Chapter 16 simulation shortcut is pedagogical, and the chapter explicitly marks it as x86-only. Chapter 17's richer simulation introduces a portable alternative, and Chapter 18's real PCI path retires the shortcut entirely.

### The Register Protocol

The simulated device's registers have no side effects on read or write. `STATUS` is set by the driver; it does not change autonomously. `DATA_IN` is written by the driver; it does not forward the write to an imaginary downstream consumer. `INTR_STATUS` is a plain register, not read-to-clear.

The full story: real devices have protocols. Reading a status register may consume an event. Writing a command register may trigger a multi-cycle operation inside the device. The driver's job is to follow the protocol exactly; a single missed step produces a misbehaving device. Chapter 17 introduces some of this complexity by adding a callout-driven protocol: a write triggers a delayed status change.

### Locking Granularity

Chapter 16 uses a single driver mutex (`sc->mtx`) for all register access. In practice, real drivers sometimes split locks: a fast-path lock for per-packet register writes (in a network driver) and a slower lock for configuration changes. Splitting increases concurrency at the cost of more locking discipline.

The full story: lock splitting is a performance tuning decision that belongs to later chapters on scaling and profiling. Chapter 16 uses a single lock because it is the simplest correct design, and because the driver's throughput requirements are nowhere near where lock contention matters.

### Endianness

Chapter 16 assumes host byte order for all register values. Real devices sometimes use a different byte order than the host CPU. The `_stream_` variants of `bus_space` handle this; Chapter 16 does not use them.

The full story: FreeBSD's `bus_space` API supports per-tag byte-swap semantics. A driver whose device is big-endian on a little-endian CPU uses either a swap-aware tag or the `_stream_` variants plus explicit `htobe32`/`be32toh` conversions. Chapter 16's simulation is host-endian, so the issue does not arise; real drivers for big-endian devices handle it explicitly.

### Cache Attributes

Chapter 16's `malloc(9)` allocation produces ordinary cacheable kernel memory. Real device memory is mapped with different cache attributes (uncached, write-combining, device-strongly-ordered) depending on the platform and the device's requirements.

The full story: `bus_alloc_resource_any` with `RF_ACTIVE` on a real PCI BAR produces a mapping with the right cache attributes. The simulation does not go through this path; it uses plain cacheable memory. Under Chapter 16's patterns (serialised access, volatile accesses), the cache attribute difference does not manifest. In Chapter 18's real-PCI path, the allocation flow takes care of it.

### Error Handling

Chapter 16's simulated device never returns an error from a register access. Real hardware sometimes does: a read may time out, a write may be rejected, a bus may hang. The driver must handle these.

The full story: FreeBSD provides `bus_peek_*` and `bus_poke_*` variants (since FreeBSD 13) that return an error if the access faults. Chapter 16 does not use them because the simulation cannot fault. Chapter 19 introduces them in the context of interrupt handlers that may touch a device in an uncertain state.

### Interrupts

Chapter 16's driver polls registers through callouts and syscall paths. Real drivers typically use interrupts to know when to read a register.

The full story: interrupts are Chapter 19's topic. Chapter 16's polling pattern is a stepping stone; after Chapter 19 the driver will have an interrupt handler that replaces much of the polling logic.

### DMA

Chapter 16's driver does not use DMA. Every byte flowing through the driver is copied by the CPU, register by register.

The full story: real high-throughput devices use DMA for bulk data. The driver programs the device's DMA engine through registers, then the device reads or writes system RAM directly. Chapter 20 and Chapter 21 cover the DMA API.

### Summary

Chapter 16 is an on-ramp. Every simplification it makes is deliberate, named, and picked up by a later chapter. The vocabulary Chapter 16 teaches is the vocabulary every later chapter extends; the discipline Chapter 16 builds is the discipline every later chapter relies on. The chapter is short of the full hardware story on purpose. Subsequent chapters fill in the rest.



## Reference: A Quick Reference for Common MMIO Bugs

When a driver misbehaves, the bug is often in a small set of recurring categories. A quick reference to recognise each.

### 1. Off-by-One in the Register Map

**Symptom**: A read returns a plausible but wrong value, or a write has no effect.

**Cause**: An offset in the driver's header is one or two or four bytes off from the datasheet.

**Diagnosis**: Cross-check the header against the datasheet, register by register.

**Fix**: Correct the offset.

### 2. Wrong Access Width

**Symptom**: A read returns a value that looks like only part of the register, or a write affects only part of the register.

**Cause**: The driver uses `bus_read_4` on a 16-bit register or vice versa.

**Diagnosis**: Check the datasheet's width column against the accessor suffix.

**Fix**: Use the correct width.

### 3. Missing Volatile Qualifier in a Hand-Rolled Accessor

**Symptom**: The compiler optimises away a register access, and the driver misses a state change.

**Cause**: A driver that wraps `bus_space_*` in a non-volatile intermediate loses the volatile annotation.

**Diagnosis**: Audit any custom accessor that is not a direct `bus_space_*` call.

**Fix**: Keep accessors as simple wrappers around `bus_space_*`; do not introduce intermediate variables without `volatile`.

### 4. Lost Update in Read-Modify-Write

**Symptom**: A bit set by the driver disappears; another bit set by a second context disappears.

**Cause**: Two contexts do RMW on the same register without a lock; one clobbers the other.

**Diagnosis**: Use the access log or DTrace to observe two writes in rapid succession.

**Fix**: Protect RMW with the driver mutex, or use a write-one-to-clear idiom if the hardware supports it.

### 5. Missing Barrier Before a Doorbell

**Symptom**: The device sometimes reads stale descriptor data or wrong command buffers.

**Cause**: The descriptor writes are reordered past the doorbell register write (on arm64 or other weakly-ordered platforms).

**Diagnosis**: The symptom is often transient and load-dependent.

**Fix**: Insert `bus_barrier` with `BUS_SPACE_BARRIER_WRITE` between the descriptor writes and the doorbell.

### 6. Reading a Write-Only Register

**Symptom**: The driver reads a register and gets zero or garbage; based on that value, it takes the wrong action.

**Cause**: The register is marked write-only in the datasheet; reads return a fixed value unrelated to state.

**Diagnosis**: Check the access type in the datasheet.

**Fix**: Do not read write-only registers. If you need to remember the last written value, cache it in the softc.

### 7. Unexpected Side Effect on Read

**Symptom**: A debug read changes driver behaviour.

**Cause**: The register has read-to-clear semantics and the debug read consumes an event.

**Diagnosis**: Disable the debug read; if the issue disappears, the read was the culprit.

**Fix**: Cache the value in the softc on the protocol-driven read; expose the cached value through the debug interface.

### 8. Dangling Tag or Handle

**Symptom**: Kernel panic on the first register access, with a fault at an address that does not look like the mapped region.

**Cause**: The driver stored a tag and handle before allocation completed, or kept them after release.

**Diagnosis**: `MYFIRST_ASSERT` firing; `regs_buf == NULL` in the panic.

**Fix**: Set the tag and handle only after successful allocation; clear them (or null the `sc->hw` pointer) before release.

### 9. Sysctl Handler Without Lock

**Symptom**: WITNESS warning about an unprotected register access, or rare incorrect values observed from user space.

**Cause**: A sysctl handler reads or writes a register without acquiring the driver lock.

**Diagnosis**: The `MYFIRST_ASSERT` inside the accessor produces a WITNESS entry.

**Fix**: Wrap the register access in `MYFIRST_LOCK`/`MYFIRST_UNLOCK`.

### 10. Detach Races

**Symptom**: Kernel panic during `kldunload` with a stack that includes a register access.

**Cause**: A callout or task accesses registers after the register buffer has been freed.

**Diagnosis**: `regs_buf == NULL` in the panic; the caller is a task or callout that was not drained before the free.

**Fix**: Review detach ordering; drain all callouts and tasks before freeing `regs_buf`.

Each of these bugs has a short diagnostic path and a well-defined fix. Keeping the list nearby during development catches most issues on first contact.
