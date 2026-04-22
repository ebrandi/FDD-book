---
title: "DMA and High-Speed Data Transfer"
description: "Chapter 21 extends the Chapter 20 multi-vector driver with Direct Memory Access support through FreeBSD's bus_dma(9) interface. It teaches what DMA is at the hardware level; why direct device access to RAM is unsafe without an abstraction layer; how bus_dma tags, maps, memory allocations, and synchronisation work together to make DMA portable across architectures; how to allocate coherent DMA memory and load it through a mapping callback; how the PRE/POST sync pairs keep the CPU's and the device's view of memory consistent; how to extend the simulated backend with a DMA engine that accepts a physical address, runs the transfer under a callout, and raises a completion interrupt; how to consume completions in the Chapter 20 filter-plus-task pipeline; how to recover from mapping failures, unaligned buffers, timeouts, and partial transfers; and how to refactor the DMA code into its own file and document it. The driver grows from 1.3-msi to 1.4-dma, gains myfirst_dma.c and myfirst_dma.h, gains a DMA.md document, and leaves Chapter 21 ready for the power-management work in Chapter 22."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 21
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 225
---

# DMA and High-Speed Data Transfer

## Reader Guidance & Outcomes

Chapter 20 closed with a driver that handles interrupts well. The `myfirst` module at version `1.3-msi` has a three-tier fallback ladder (MSI-X first, MSI next, legacy INTx last); three per-vector filter handlers wired to separate roles (admin, receive, transmit); per-vector counters; per-vector CPU binding through `bus_bind_intr(9)`; a clean teardown that releases every vector in reverse order; a new `myfirst_msix.c` file that keeps the multi-vector code tidy; and an `MSIX.md` document that describes the per-vector design. Every vector's handler runs in the Chapter 19 discipline: a short filter in primary interrupt context that reads status, acknowledges, and either handles or defers, and a task in thread context that does the bulk work under the sleep-mutex.

What the driver still does not do is move data. Every byte the device has produced so far has been read one register at a time from the CPU's point of view. The simulated device writes a word into `DATA_OUT`; the CPU reads it with `bus_read_4`. That works when there is one word to read. It does not work when there are sixty-four receive descriptors to walk, or when a storage controller has completed a four-kilobyte block transfer, or when a NIC has just placed a nine-kilobyte jumbo frame into host memory. At those rates, reading word by word collapses the throughput the hardware was designed to deliver. The device needs to reach into RAM on its own, write the data there, and then tell the driver where to find it. That is what DMA means, and it is what Chapter 21 teaches.

The chapter's scope is precisely this transition: what DMA is at the level of the PCIe fabric; why a device writing to arbitrary memory would be a correctness and safety nightmare without an abstraction layer; how FreeBSD's `bus_dma(9)` interface provides that layer in a portable way; how to create DMA tags that describe a device's addressing constraints; how to allocate DMA-capable memory and load it into a mapping; how to synchronise the CPU's view with the device's view at the right moments; how to extend the Chapter 17 simulated backend with a DMA engine; how to process completion interrupts through the Chapter 20 per-vector path; how to recover from the failures that real DMA code must handle; and how to refactor all of this into a file the reader can read, test, and extend. The chapter stops short of the subsystems that build on top of DMA. The network driver chapter in Part 6 (Chapter 28) revisits DMA inside the iflib(9) framework; the power-management chapter that follows (Chapter 22) will teach how to quiesce DMA safely during suspend; the performance chapter in Part 7 (Chapter 33) will return to DMA with a tuning lens. Chapter 21 stays focused on the primitives that every later chapter assumes.

Chapter 21 stops short of several load paths that build on the same foundation. Scatter-gather DMA for mbuf chains (`bus_dmamap_load_mbuf_sg`), CAM control blocks (`bus_dmamap_load_ccb`), user I/O structures (`bus_dmamap_load_uio`), and crypto operations (`bus_dmamap_load_crp`) all use the same underlying mechanism and the same synchronisation discipline, but each comes with its own context (networking, storage, VFS, OpenCrypto) that belongs in its own chapter. Zero-copy user-space DMA, in which a driver lets user space map a DMA buffer and coordinate its synchronisation through `sys_msync(2)` or similar primitives, sits outside the scope of Chapter 21; Chapter 10 Section 8 introduced the `d_mmap(9)` path it would build on, and Chapter 31 discusses the security implications of exposing kernel memory through `mmap(2)`-style interfaces. IOMMU-assisted remapping (`busdma_iommu`) is mentioned for context but not configured by hand. The reader who finishes Chapter 21 will understand the primitives well enough that the specialised load paths look like variations on the base case; that is the goal.

The `bus_dma(9)` story sits on every Part 4 layer we have built. Chapter 16 gave the driver a vocabulary of register access. Chapter 17 taught it to think like a device. Chapter 18 introduced it to a real PCI device. Chapter 19 gave it ears on one IRQ. Chapter 20 gave it several ears, one per queue the device wants. Chapter 21 gives it hands: the ability to hand the device a physical address and say "put your data here, tell me when you are done, and let me process it without bothering you for each byte". That is the last missing primitive before Part 4 closes.

### Why bus_dma(9) Earns a Chapter of Its Own

Before we go further, it is worth pausing on what `bus_dma(9)` buys us that a loop of `bus_read_4` calls cannot. The Chapter 20 driver has a complete interrupt pipeline. If the filter-plus-task pattern is already right, and the register accessor layer already handles reads and writes cleanly, why not just read bigger blocks word by word and be done?

Three reasons.

The first is **bandwidth**. A single `bus_read_4` is one MMIO transaction on the PCIe bus, and each transaction costs hundreds of nanoseconds once the overhead of address decoding, the read completion round-trip, and the CPU's memory ordering are accounted for. On a PCIe 3.0 x4 link, a driver that uses `bus_read_4` word by word tops out around twenty megabytes per second of effective throughput; a driver that arranges for the device to DMA into a contiguous buffer reaches gigabytes per second on the same link. The order-of-magnitude gap is what separates a ten-gigabit NIC from an unusable NIC, a modern NVMe from a late-1990s IDE controller. DMA is not an optimisation for drivers that could work without it; it is the only mechanism that lets modern devices deliver their designed throughput.

The second is **CPU cost**. Every `bus_read_4` or `bus_write_4` keeps a CPU busy for the duration of the transaction. A driver that moves a megabyte of data one word at a time burns ten to twenty milliseconds of CPU time just shuttling bytes across MMIO. DMA offloads that cost to the device's own bus master engine: the CPU hands over an address and a length, the device runs the transfer independently, and the CPU is free to do other work (including handling interrupts for other devices). On a server pushing millions of packets per second through several NICs at once, the CPU cannot afford to touch each byte; DMA is what makes aggregate throughput achievable.

The third is **correctness under concurrency**. A driver that reads a descriptor ring word by word is racing the device: the device may be writing new entries while the driver is reading old ones, and the driver sees torn reads of half-updated fields unless it takes out a global lock that serialises the whole transfer. DMA with proper synchronisation replaces that race with a clean producer-consumer protocol: the device writes entries in order, signals completion through a single register write or a completion interrupt, and the CPU processes the entries as a batch with the guarantee that every byte is there. The `bus_dmamap_sync` call makes the hand-off explicit; the `bus_dmamap_unload` call makes the cleanup explicit. The driver becomes easier to reason about, not harder, even though the mechanism is more sophisticated.

Chapter 21 earns its place by teaching all three benefits concretely. A reader finishes the chapter able to create a tag, allocate a buffer, load it into a map, trigger a transfer, synchronise around it, wait for completion, verify the result, and tear everything down. With those primitives in hand, the reader can open any DMA-capable FreeBSD driver and recognise its structure, the same way Chapter 20's graduate can read any multi-vector driver.

### Where Chapter 20 Left the Driver

A few prerequisites to verify before starting. Chapter 21 extends the driver produced at the end of Chapter 20 Stage 4, tagged as version `1.3-msi`. If any of the items below feels uncertain, return to Chapter 20 before starting this chapter.

- Your driver compiles cleanly and identifies itself as `1.3-msi` in `kldstat -v`.
- On a QEMU guest that exposes `virtio-rng-pci` with MSI-X, the driver attaches; chooses MSI-X; allocates three vectors (admin, rx, tx); registers a distinct filter per vector; binds each vector to a CPU; prints an `interrupt mode: MSI-X, 3 vectors` banner; and creates `/dev/myfirst0`.
- On a bhyve guest with `virtio-rnd`, the driver attaches; falls through to MSI with one vector, or further through to legacy INTx; prints the corresponding banner.
- Per-vector counters (`dev.myfirst.0.vec0_fire_count` through `vec2_fire_count`) increment when the matching simulation sysctl (`dev.myfirst.0.intr_simulate_admin`, `intr_simulate_rx`, or `intr_simulate_tx`) is written to.
- The detach path tears down vectors in reverse order, drains per-vector tasks, releases resources, and calls `pci_release_msi` exactly once.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, and `MSIX.md` are current in your working tree.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel.

That driver is what Chapter 21 extends. The additions are noticeable in scope: one new file (`myfirst_dma.c`), one new header (`myfirst_dma.h`), several new softc fields to track DMA tag, map, memory pointer, and the DMA engine's simulated state, a new pair of helper functions (`myfirst_dma_setup` and `myfirst_dma_teardown`), a new completion path in the receive filter, a version bump to `1.4-dma`, a new `DMA.md` document, and updates to the regression test. The mental model grows too: the driver starts thinking of memory ownership as something that passes back and forth between the CPU and the device, and every hand-off becomes a deliberate `bus_dmamap_sync` call.

### What You Will Learn

After finishing this chapter you will be able to:

- Describe what DMA is at the hardware level, how a PCIe device performs a bus-master write to host memory, and why that mechanism delivers bandwidth and CPU-offload benefits that MMIO cannot match.
- Explain why direct device access to arbitrary memory would be unsafe without an abstraction layer, and name the three hardware realities that layer has to hide (device addressing limits, IOMMU remapping, cache coherence).
- Recognise where bounce buffers fit in the picture: when the kernel silently inserts them, what they cost, and when an explicit address constraint can avoid them.
- Read and write the central `bus_dma(9)` vocabulary: `bus_dma_tag_t`, `bus_dmamap_t`, `bus_dma_segment_t`, the PRE/POST sync operations, the tag constraints (`alignment`, `boundary`, `lowaddr`, `highaddr`, `maxsize`, `nsegments`, `maxsegsz`), and the common flag set (`BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, `BUS_DMA_COHERENT`, `BUS_DMA_ZERO`).
- Create a device-scoped DMA tag with `bus_dma_tag_create`, inheriting the parent bridge's constraints through `bus_get_dma_tag(9)`, and choosing alignment, boundary, and address limits that match the device's datasheet.
- Allocate DMA-capable memory with `bus_dmamem_alloc`, obtain its kernel virtual address, and understand why the allocator returns a single segment with `bus_dmamem_alloc` but may return several segments for arbitrary memory loaded later.
- Load a kernel buffer into a DMA map with `bus_dmamap_load`, extract the bus address from the single-segment callback, and understand the cases where the callback is deferred and what that means for the driver's locking discipline.
- Use `bus_dmamap_sync` with the correct PRE/POST flags around every device-visible memory access: PREWRITE before the device reads, POSTREAD after the device writes, and the combined pairs for descriptor rings where both directions happen.
- Extend the Chapter 17 simulated backend with a small DMA engine that takes a bus address, a length, and a direction from the softc, runs the transfer under a `callout(9)` to emulate latency, and raises a completion interrupt through the Chapter 19/20 filter path.
- Process completion interrupts by reading a status register inside the rx filter, acknowledging, enqueueing the task, and letting the task perform `bus_dmamap_sync` and access the buffer.
- Recover from every recoverable failure mode: `bus_dma_tag_create` returning `ENOMEM`, `bus_dmamap_load` returning `EINVAL` or `EFBIG`, the engine reporting a partial transfer, a timeout that expires before the device signals completion, and a detach that fires while a transfer is in flight.
- Refactor the DMA code into a dedicated `myfirst_dma.c` / `myfirst_dma.h` pair, with `myfirst_dma_setup` and `myfirst_dma_teardown` as the only entry points used by the rest of the driver.
- Version the driver as `1.4-dma`, update the Makefile's `SRCS` line, run the extended regression test, and produce `DMA.md` documenting the DMA flow, the buffer layouts, and the observable counters.
- Read the DMA code in a real driver (`/usr/src/sys/dev/re/if_re.c` is the chapter's running reference) and map each call to the concepts introduced in Chapter 21.

The list is long; each item is narrow. The point of the chapter is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 21 stays focused.

- **Scatter-gather DMA for heterogeneous buffers.** `bus_dmamap_load_mbuf_sg` (networking), `bus_dmamap_load_ccb` (CAM storage), `bus_dmamap_load_uio` (VFS), and `bus_dmamap_load_crp` (OpenCrypto) all sit on top of the same `bus_dma(9)` foundation but are used in subsystem-specific ways. Part 6 (Chapter 27 storage, Chapter 28 network) cover them in context. Chapter 21 uses `bus_dmamap_load` on a single contiguous buffer; the rest is a specialisation.
- **iflib(9) and its hidden DMA pools.** The networking framework wraps `bus_dma` with per-queue helpers that allocate, load, and sync receive and transmit rings automatically. The framework is the subject of its own chapter in Part 6 (Chapter 28); Chapter 21 teaches the raw layer that iflib uses internally.
- **IOMMU-assisted DMA on amd64 with Intel VT-d or AMD-Vi.** The `busdma_iommu` machinery integrates transparently with the `bus_dma` API, so a driver written for the generic path automatically benefits from IOMMU remapping when the kernel is built with `DEV_IOMMU`. The chapter mentions IOMMU presence, explains what it does, and shows how to observe it; it does not configure it by hand.
- **NUMA-aware DMA memory placement.** `bus_dma_tag_set_domain(9)` lets a driver bind a tag's allocations to a specific NUMA domain. The function is named and mentioned; the full placement story is a Part 7 (Chapter 33) performance topic.
- **Power-aware DMA quiescing.** Stopping in-flight DMA before suspend is Chapter 22's topic. Chapter 21 arranges the primitives that Chapter 22 will use; the `myfirst_dma_teardown` path is designed so that Chapter 22's suspend handler can invoke it cleanly.
- **Zero-copy DMA to and from user-space buffers.** Mapping user pages into a DMA map requires `vm_fault_quick_hold_pages(9)` plus `bus_dmamap_load_ma_triv` or an equivalent, and touches on memory pinning, VM reference counts, and capability enforcement. These topics belong in a later chapter.
- **DMA descriptor rings with hardware tail/head registers.** A full ring design (producer/consumer indexes, wrap-around, doorbell writes) is a next-level pattern built on Chapter 21's primitives. The chapter shows a single-transfer pattern; descriptor rings are a natural extension the reader can build as a challenge.

Staying inside those lines keeps Chapter 21 a chapter about DMA primitives. The vocabulary is what transfers; the later chapters apply it to networking, storage, power, and performance in turn.

### Estimated Time Investment

- **Reading only**: five to six hours. The DMA conceptual model is the densest in Part 4; the sync discipline in particular benefits from a careful read and then a second pass once the code examples have concretised it.
- **Reading plus typing the worked examples**: twelve to fifteen hours over two or three sessions. The driver evolves in four stages: tag and allocation, simulated engine with polling, simulated engine with interrupt completion, and final refactor. Each stage is small but builds on the previous one, and each requires careful attention to the PRE/POST pairs.
- **Reading plus all labs and challenges**: eighteen to twenty-four hours over four or five sessions, including reading real drivers (`if_re.c`, the `nvme_qpair.c` allocation code, and the `busdma_bufalloc.c` sources if curiosity takes you that far), running the chapter's regression test on both bhyve and QEMU targets, and attempting one or two of the production-style challenges.

Sections 3, 4, and 5 are the densest. If the sync discipline or the tag constraints feel opaque on first pass, that is normal. Stop, re-read Section 4's diagram, run the Section 4 exercise, and continue when the shape has settled. DMA is one of the topics where a working mental model pays off many times; it is worth building slowly.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 20 Stage 4 (`1.3-msi`). The starting point assumes every Chapter 20 primitive: the three-tier fallback ladder, the per-vector filters, per-vector CPU binding, and the clean multi-vector teardown.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly. The `WITNESS` option is especially valuable for DMA work because several `bus_dma` functions must not be called with non-sleepable locks held, and `WITNESS` catches the violations early.
- `bhyve(8)` or `qemu-system-x86_64` is available. Chapter 21's labs work on either target; the DMA simulation happens inside the `myfirst` driver, so no particular DMA-capable guest device is required. The real-DMA sanity check at the end of Section 5 uses whatever PCI device the chapter's virtio host exposes, without depending on it for DMA.
- The `devinfo(8)`, `vmstat(8)`, `pciconf(8)`, `sysctl(8)`, and `procstat(1)` tools are in your path. `procstat -kke` is useful for watching the driver's threads during DMA transfers.

If any item above is shaky, fix it now. DMA tends to expose any latent weakness in the driver's lock discipline (the sync calls sit inside the task context, the allocation calls sit inside attach, and the unload calls sit inside detach; each context has different lock rules), and `WITNESS` in a debug kernel is what catches the mistakes at development time rather than in production.

### How to Get the Most Out of This Chapter

Four habits will pay off quickly.

First, keep `/usr/src/sys/sys/bus_dma.h` and `/usr/src/share/man/man9/bus_dma.9` bookmarked. The header is compact (about four hundred lines) and lists every API the chapter uses, with a brief comment on each. The manual page is substantial (over eleven hundred lines) and is the authoritative reference on every parameter. Reading both once at the start of Section 2 and returning to them as you work each section is the single most useful thing you can do for fluency.

Second, keep `/usr/src/sys/dev/re/if_re.c` bookmarked as the running real-driver example. `if_re(4)` is a reasonably compact network driver that uses `bus_dma` in a textbook way: a parent tag that inherits from the PCI bridge, a per-ring tag for descriptors, a per-buffer tag for mbuf payloads, a single-segment callback, and clean teardown. Most of Chapter 21's patterns have a direct analogue in `if_re.c`. When the chapter shows a pattern, it will often say "look at `re_allocmem`" and give a specific line.

> **A note on line numbers.** When the chapter pins a reference to a line inside `if_re.c`, treat the number as a pointer for the FreeBSD 14.3 tree at time of writing, not a stable coordinate. `re_allocmem` and `re_dma_map_addr` are durable names; the line each one sits on is not. Open the file, search for the function name, and let your editor report where it actually lives on the system in front of you.

Third, type the changes by hand and run each stage. DMA is where the cost of a wrong flag or a missed sync is paid in subtle corruption rather than an obvious crash, and typing carefully makes the common mistakes visible at the moment they happen. The chapter's regression test will catch many of the mistakes, but the typing itself is what builds the mental model.

Fourth, after finishing Section 4, re-read Sections 2 and 3 of Chapter 17. The simulated backend's MMIO register map is the base the simulated DMA engine extends, and seeing the two layered together is what makes the engine's design feel inevitable rather than arbitrary. The Chapter 17 simulation was built with DMA in mind; Chapter 21 is where that design pays off.

### Roadmap Through the Chapter

The sections in order are:

1. **What Is DMA and Why Use It?** The hardware picture: bus-master writes, the bandwidth and CPU-cost argument, concrete device examples, and why modern driver development assumes DMA.
2. **Understanding FreeBSD's bus_dma(9) Interface.** The abstraction layer: what the layer hides (addressing limits, IOMMU, coherence), what a tag is, what a map is, what a segment is, and how the pieces fit together. Concepts first, code next.
3. **Allocating and Mapping DMA Memory.** The tag parameters in detail, `bus_dmamem_alloc`, the `bus_dmamap_load` callback pattern, the single-segment case, and the first running code: Stage 1 of the Chapter 21 driver (`1.4-dma-stage1`).
4. **Synchronising and Using DMA Buffers.** The sync discipline: PREWRITE, POSTREAD, the combined pairs, the coherent-vs-non-coherent nuance, and the mental model of memory ownership passing between CPU and device. The exercise at the end is a paper walkthrough before any code.
5. **Building a Simulated DMA Engine.** The simulated backend's DMA registers, the state machine, the `callout(9)`-driven transfer, and the completion path. Stage 2 (`1.4-dma-stage2`) makes the driver hand a physical address to the engine and poll for completion.
6. **Handling DMA Completion in the Driver.** The filter-plus-task path from Chapter 19/20 consumes the completion interrupt. The task performs `bus_dmamap_sync(..., POSTREAD)` and reads the buffer. Stage 3 (`1.4-dma-stage3`) wires the interrupt path to the DMA engine.
7. **DMA Error Handling and Edge Cases.** Every failure mode the chapter addresses, with the matching recovery pattern: mapping failures, partial transfers, timeouts, and detach-while-in-flight.
8. **Refactoring and Versioning Your DMA-Capable Driver.** The final split into `myfirst_dma.c` and `myfirst_dma.h`, the updated Makefile, the `DMA.md` document, and the version bump. Stage 4 (`1.4-dma`).

After the eight sections come an extended walkthrough of `if_re.c`'s DMA code, several deeper looks at bounce buffers, 32-bit devices, IOMMU, coherent flags, and parent tag inheritance, a set of hands-on labs, a set of challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 21's story and opens Chapter 22's, a bridge, and the usual quick-reference and glossary material at the end of the chapter. The reference material is meant to be re-read as you work through the next few chapters; Chapter 21's vocabulary (tag, map, segment, PRE/POST, coherent, bounce, callback) is the foundation every chapter after this one assumes.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3, 4, and 5 stand alone and make good single-sitting reads.



## Section 1: What Is DMA and Why Use It?

Before the driver code, the hardware picture. Section 1 teaches what DMA is at the level of the PCIe bus, what it gains the driver over MMIO reads and writes, what the real-world examples look like, and why modern driver development takes DMA as a baseline assumption rather than an optimisation. A reader who finishes Section 1 can read the rest of the chapter with the kernel's `bus_dma` layer as a concrete object rather than a vague abstraction.

### The Problem With MMIO-Only Data Movement

Chapter 16 introduced memory-mapped I/O: a device's registers live inside a region of physical memory, the kernel maps that region into the kernel virtual address space, and `bus_read_4` and `bus_write_4` wrappers turn reads and writes into bus transactions. Chapter 17 built a simulated backend that looks the same from the driver's perspective. Chapter 18 moved that backend onto a real PCI BAR. Every word of data the Chapter 19 and Chapter 20 driver has handled has come through `bus_read_4` from the device's `DATA_OUT` register.

For a driver that handles one word per event, that model works. For a driver that has to handle a megabyte of data, it does not. Each `bus_read_4` is one MMIO transaction; each MMIO transaction is a posted or completion bus cycle with its own overhead. On a typical PCIe 3.0 x4 link the round-trip time per transaction is a few hundred nanoseconds once the PCIe header, the read completion, and the CPU's memory ordering instructions are added up. A driver that moves data one word at a time tops out at a few tens of megabytes per second regardless of what the device is actually capable of.

A modern NIC is capable of tens of gigabytes per second. A modern NVMe is capable of several gigabytes per second of sustained I/O. A USB3 host controller handles thousands of descriptors per second. A GPU handles hundreds of megabytes of command buffers per frame. The hardware is not the bottleneck in any of these cases; the MMIO model is. Every driver for these devices has to use a different mechanism to move the bulk of the data, and that mechanism is DMA.

### What DMA Means at the Hardware Level

DMA stands for Direct Memory Access. The word "direct" is the key. In an MMIO transfer, the CPU is the master: the CPU issues a read or write to the device's MMIO region, and the device responds. In a DMA transfer, the device is the master: the device issues a read or write to host memory, and the memory controller responds as if the CPU had made the request. The CPU is not in the loop for each word; the device moves the bytes on its own schedule.

Physically, a DMA transfer is a memory transaction on the same bus the CPU uses to reach RAM. On PCIe this is called a **bus-master** transaction, and PCI devices that can do it are said to be **bus-mastering**. The device holds an internal state (a source address, a destination address, a length, a direction, a status) and advances through the transfer under its own clock. The CPU sets up the transfer by writing the relevant parameters to the device's control registers, then issues a go-ahead write (often called a "doorbell"), and can then do something else until the device signals completion.

From the CPU's perspective, a DMA transfer passes through roughly these steps:

1. The driver allocates a region of host memory with properties the device can use (addressable by the device, aligned as the device requires, contiguous if the device requires that).
2. The driver arranges for the device to see that region at some **bus address** (which may or may not equal the host's physical address depending on the platform).
3. The driver writes the bus address, the length, and the direction to the device's registers.
4. The driver writes a doorbell register that tells the device to begin.
5. The device performs the transfer on its own, reading from or writing to the host memory at the bus address, through the memory controller, while the CPU is free.
6. The device signals completion, usually by writing a status bit in an MMIO register, raising an interrupt, or both.
7. The driver performs any necessary cache synchronisation between the CPU's view of the memory and the device's view.
8. The driver reads or uses the transferred data.

Each step looks simple. The subtlety lies in step 2 (what bus address to use, and how to make the device see the buffer) and step 7 (what "necessary" means, and how the driver knows when to do which kind of sync). Most of Chapter 21 is about those two steps.

### What the Driver Gains

Three concrete benefits matter in every DMA-capable driver.

The first is **throughput**. On PCIe, a bus-master transfer uses the device's own width and clock. A ten-gigabit NIC's PCIe 3.0 x4 link can sustain roughly three and a half gigabytes per second of bus-mastered data. The same link used for MMIO alone tops out near twenty megabytes per second: the NIC cannot touch its advertised bandwidth without DMA. The ratio is worse for higher-performance devices. A PCIe 4.0 x16 GPU has a theoretical link budget over thirty gigabytes per second; no amount of `bus_write_4` can approach it.

The second is **CPU offload**. Bus-master transfers consume little to no CPU time once started. The CPU writes a few registers, triggers the doorbell, and is free until the completion interrupt arrives. On a system with many DMA-capable devices, the CPU can orchestrate dozens of concurrent transfers as long as the device has enough DMA engines. On a NIC that supports multi-queue transmit, the CPU can queue up receive descriptors, hand transmit buffers to the device, and process completions for both directions at once, with the actual byte movement happening in the device's internal DMA engines. The CPU's job becomes policy and bookkeeping; the data movement is elsewhere.

The third is **determinism under load**. An MMIO-based driver that is busy-looping through a large buffer can be preempted by an interrupt, the ithread that handles the interrupt can monopolise the CPU for an extended period, and the throughput becomes a function of kernel preemption timing rather than device capability. A DMA-based driver has the device do the work; the driver's own code runs for a predictable, small number of cycles per transfer and yields to the scheduler in between. Latency distributions tighten, tail latencies shrink, and the driver's performance becomes easier to reason about.

These three benefits compound. A driver that uses DMA consumes less CPU per byte, moves more bytes per unit time, and does both more predictably. The cost is the `bus_dma` setup code, which adds a few hundred lines to the driver. The chapter is about writing those lines well.

### Real-World Examples

DMA is pervasive in modern hardware. A few concrete examples make the scale of its use clear.

A **network card** uses DMA for both receive and transmit. The device has a **receive ring** in host memory: an array of descriptors, each of which contains a bus address and a length pointing at a packet buffer. The device copies incoming packets from the wire into the buffers via bus-master writes, updates each descriptor's status field when the packet is complete, and raises an interrupt. The driver walks the ring, processes each completed descriptor, and refills the ring with fresh buffers. Transmit works the other way: the driver places outgoing packet headers and payloads into buffers, writes descriptors with the bus addresses, and kicks the device; the device reads the buffers, transmits, and updates the descriptor status. The whole protocol runs in host memory, synchronised through `bus_dmamap_sync` calls and completion interrupts. Chapter 21's driver is a single-transfer simplification of this pattern; Part 6 (Chapter 28) generalises to full rings.

A **storage controller** (SATA, SAS, NVMe) uses DMA for every block transfer. The driver issues a command block containing a physical-address list (a scatter-gather list) that describes the pages making up the host buffer. The device walks the list, reads or writes each page, and signals completion. Modern NVMe controllers use a **Physical Region Page (PRP)** or **Scatter Gather List (SGL)** structure to describe the transfer, and the controller writes the completion into a completion queue that is itself in host memory via DMA. The `nvme(4)` driver is about four thousand lines of DMA-aware code; the structure is comfortable to read once Chapter 21's primitives are in place.

A **USB3 host controller** uses DMA for transfer descriptors, completion events, and bulk data. The driver hands the device a pointer to a transfer descriptor in host memory; the device fetches it via DMA, performs the transfer, writes the completion into an event ring, and raises an interrupt. USB is particularly interesting because each USB device's data goes through the host controller's DMA engine, so the driver for a USB NIC is actually two drivers stacked: the USB host controller does the DMA, and the USB device driver runs on top.

A **GPU** uses DMA for command buffers, texture uploads, and sometimes display scanout. A single frame may involve tens of megabytes of data moving between system RAM and GPU VRAM, orchestrated by the GPU's own DMA engine under the direction of the driver's command stream. FreeBSD's drm-kmod ports of Linux's DRM drivers use `bus_dma` to set up the command buffer mappings; the translation from Linux's DMA API is a thin layer because the abstractions are similar.

A **sound card** uses DMA for the audio buffer. The driver allocates a circular buffer, programs the device with its bus address and length, and the device reads samples in real time, wrapping around at the end. The driver refills the buffer ahead of the device's read pointer and relies on a position interrupt (or periodic timer) to schedule the refill. `sound(4)` uses `bus_dma` for this purpose; the pattern is a nice intermediate between single-transfer DMA and full scatter-gather rings.

Each of these examples follows the same abstract pattern: tag, map, memory, sync, trigger, complete, sync, consume, unload. The details differ; the shape is constant. Chapter 21 teaches the shape on a simulated device; later chapters apply the shape to the real subsystems.

### Why Direct Memory Access Cannot Be Unrestricted

At first glance, "the device writes to memory" sounds straightforward. The device has an internal register; the register holds a bus address; the device writes to that address. Why does the driver need to do anything at all beyond telling the device an address?

Three hardware realities make unrestricted DMA unsafe, and those realities are what `bus_dma(9)` exists to hide.

**Reality one: the device may not be able to reach all of host memory.** Older PCI devices are 32-bit bus masters: they can only address the lower four gigabytes of bus address space. On a system with sixteen gigabytes of RAM, a buffer at physical address 0x4_0000_0000 is invisible to such a device; handing it that address would be a silent corruption (the write goes somewhere, but not where the driver expected). Some newer devices have 36-bit or 40-bit DMA engines and can reach more of RAM but not all of it. The driver has to describe the device's range to the kernel, and the kernel has to ensure every buffer the device sees is inside the range. When a buffer happens to be outside the range, the kernel silently inserts a **bounce buffer**: a region inside the range, into which the kernel copies the data before the DMA read (or out of which it copies after the DMA write). Bounce buffers are correct but expensive; a driver that allocates its buffers inside the device's range avoids them.

**Reality two: the bus address the device sees is not always the physical address the CPU uses.** On modern amd64 systems with an IOMMU enabled (Intel VT-d, AMD-Vi), the memory controller inserts a translation layer between the device's bus address and the host's physical address. The device writes to bus address X; the IOMMU translates that to physical address Y; the memory controller writes physical address Y. The translation is per-device, and by default the IOMMU only permits a device to access memory the driver has explicitly mapped. This is a correctness and security win (a buggy or compromised device cannot write to arbitrary memory), but it requires the driver to participate: the driver tells the kernel "this buffer should be visible to this device", the kernel programs the IOMMU accordingly, and only then can the device reach the buffer. Without `bus_dma`, the driver would have to know whether an IOMMU is present, what its page tables look like, and how to program them.

**Reality three: the CPU and the device may see the same memory differently at the same moment.** Every CPU has caches; the caches hold copies of memory at the granularity of cache lines (typically sixty-four bytes on amd64, sometimes more on other platforms). When the CPU writes a value, the write goes into the cache first; the cache line is marked dirty, and the write only reaches the memory controller when the line is evicted or a coherence protocol synchronises it. When a DMA-capable device writes a value, the write goes directly to the memory controller (unless the platform is fully coherent, which amd64 typically is but ARM sometimes is not), so the CPU's cached copy may be stale. Conversely, when the device reads a value that the CPU has recently written and not flushed, the device reads a stale value. On fully coherent platforms, the hardware handles this automatically; on partially coherent or non-coherent platforms, the driver has to tell the kernel "the CPU is about to read this buffer, please ensure the caches are invalidated first" or "the CPU just wrote this buffer, please flush the caches before the device reads". This is what `bus_dmamap_sync` does.

These three realities are not visible to driver authors on amd64 with a modern IOMMU and a fully coherent device; the `bus_dma` API is still required, but most of its work is transparent. On 32-bit hardware, on non-coherent ARM platforms, or on systems where the IOMMU is configured restrictively, the API actually does something at every call. A driver written correctly against `bus_dma` works on all of them; a driver that bypasses the API works only on the subset of platforms where the naive model happens to be correct.

### Why bus_dma(9) Exists

FreeBSD's `bus_dma(9)` interface is the portability layer that hides these three realities behind a single API. It was inherited and adapted from NetBSD's equivalent, and refined in FreeBSD over the 5.x and later releases. The design decisions that make it distinctive are worth understanding at the conceptual level before the code examples.

The API **separates description from execution**. A DMA **tag** describes the constraints of a group of transfers: what address range is reachable, what alignment is required, how large a transfer may be, how many segments it may span, what the boundary rules are. A DMA **map** represents one specific transfer's mapping onto that tag: which bus addresses the specific buffer occupies, which bounce buffers (if any) are in play. The driver creates the tag once at attach time; it creates or loads maps at transfer time. The separation lets the kernel cache expensive setup (parent tags, constraint checks) while keeping per-transfer work cheap.

The API **uses callbacks to hand back mapping information**. `bus_dmamap_load` does not return the list of bus addresses directly; it calls a callback function the driver provides, passing the segment list as an array. The reason is historical and practical: on some platforms the load may need to wait for bounce buffers to become available, and in that case the callback runs later when the buffers are free. The driver's load code returns immediately with `EINPROGRESS`; the callback eventually runs and completes the mapping. This pattern is the one that trips most first-time readers, and Section 3 walks through it carefully. For simple cases (descriptor rings allocated at attach time, where the driver is willing to wait), the load completes synchronously and the callback runs before `bus_dmamap_load` returns.

The API **makes synchronisation explicit**. `bus_dmamap_sync` with a PRE flag says "the CPU is about to stop accessing this buffer and hand it to the device; please flush". With a POST flag it says "the device has stopped accessing this buffer and the CPU is about to access it; please invalidate". On coherent platforms, `bus_dmamap_sync` is sometimes a no-op; on non-coherent platforms, it runs the cache flush or invalidate. The driver writes the same code in both cases; the API handles the difference.

The API **supports hierarchies of constraints**. A tag can inherit from a parent tag, and the child's constraints are restrictions of the parent's. This matches the hardware: a PCI device's DMA capabilities are constrained by its parent bridge's capabilities, which are constrained by the platform's memory controller. `bus_get_dma_tag(9)` returns the device's parent tag, and the driver passes that to `bus_dma_tag_create` as the parent of any tag it creates. The kernel composes the constraints automatically; the driver describes only its own device's requirements.

These four design choices (separation, callbacks, explicit sync, hierarchies) are visible in every `bus_dma`-using driver, and the chapter's code follows them closely. The benefit of understanding the design is that real drivers become much easier to read; the six-thousand-line `nvme(4)` driver, for example, follows the same pattern as Chapter 21's toy driver.

### The Concrete Flow Chapter 21 Will Build

Concretely, the Chapter 21 driver will learn to do the following sequence, in order:

1. **Create a parent-inheriting tag.** The driver calls `bus_get_dma_tag(sc->dev)` to obtain the device's parent tag and passes it to `bus_dma_tag_create` along with the `myfirst` device's own constraints (4 KB alignment, 4 KB buffer size, 1 segment, `BUS_SPACE_MAXADDR` for the address range because the simulation has no architectural limit).
2. **Allocate DMA memory.** The driver calls `bus_dmamem_alloc` with the tag. The kernel returns a kernel virtual address pointing at a four-kilobyte buffer that is both mapped for the CPU and suitable for the device. The call also returns a `bus_dmamap_t` handle that represents the mapping.
3. **Load the memory into the map.** The driver calls `bus_dmamap_load` with the kernel virtual address and a callback. The callback receives the segment list (one segment for this simple case) and stashes the bus address in the softc.
4. **Program the device.** The driver writes the bus address, the length, and the direction to the simulated DMA engine's registers.
5. **Sync PREWRITE (for host-to-device) or PREREAD (for device-to-host).** The driver calls `bus_dmamap_sync` with the appropriate flag, signalling that the CPU has finished touching the buffer and the device is about to access it.
6. **Kick the engine.** The driver writes the DMA_CTRL register's START bit; the simulated engine schedules a `callout(9)` a few milliseconds in the future.
7. **Wait for completion.** Via the Chapter 20 rx filter, which fires when the simulated engine raises `DMA_COMPLETE`. The filter enqueues the task; the task runs.
8. **Sync POSTREAD or POSTWRITE.** The task calls `bus_dmamap_sync` with the POST flag before accessing the buffer.
9. **Read and verify.** The task compares the buffer contents against the expected pattern and updates a softc counter.
10. **Tear down.** On detach, the driver unloads the map, frees the memory, destroys the tag. The order is the reverse of setup.

Every step maps to a single `bus_dma` call. The chapter's job is to teach each call in context, show how it fits into the driver's existing lifecycle, and explain what the kernel itself does on each call so the reader can debug when things go wrong. A driver that has internalised these ten steps can move on to descriptor rings, scatter-gather lists, and `bus_dmamap_load_mbuf_sg` without learning a new model; each of those is a variation on the same ten steps.

### Exercise: Identify DMA-Capable Devices on Your System

Before the next section, take five minutes to look at your own system. The exercise is simple and builds intuition: identify three devices on your lab machine that use DMA, and note one property each.

Start with `pciconf -lv`. The tool lists every PCI function, and most of them are DMA-capable in some way. For each function, note what subsystem it belongs to (network, storage, graphics, audio, USB). Then look for the line starting with `cmdreg:` in `pciconf -c` output; if the bit that reads `BUSMASTEREN` is set, the device has bus-mastering enabled and is actively using DMA.

```sh
pciconf -lvc | grep -B1 BUSMASTEREN
```

Pick one network function and look at it more carefully:

```sh
pciconf -lvbc <devname>
```

`pciconf -lvbc` shows the BAR regions, the capability list, and whether the PCIe device's config space reports any DMA-relevant capabilities (MSI/MSI-X, power management, PCIe DevCtl, ASPM). On most modern systems the output reveals that the device has a large MMIO BAR (for register access) and smaller MMIO BARs (for MSI-X table and PBA), but no I/O ports; the bulk of the device's memory is in RAM, reached by DMA, not in the device's own BAR.

Then look at an `nvme` device if you have one, or at `dmesg` for "mapped DMA" messages. Most storage drivers log a brief DMA setup banner at attach time; `nvme_ctrlr_setup` is a good one to grep for.

Write down in a lab notebook:

1. One network device with bus-mastering enabled, and a guess at what it uses DMA for.
2. One storage device (if present) with its DMA setup banner.
3. One device you did not expect to use DMA, but does. A surprise finding pays off: once you know what to look for, the picture changes.

The exercise takes about ten minutes and gives you a map of your own lab target's DMA landscape before Chapter 21's work begins.

### Wrapping Up Section 1

Section 1 established the hardware picture. DMA lets devices write to host memory directly, bypassing the CPU-by-word bottleneck of MMIO. The benefits are throughput, CPU offload, and determinism; the costs are the setup complexity and the need to synchronise between the CPU's cached view and the device's bus-visible view. FreeBSD's `bus_dma(9)` interface exists to hide three hardware realities (device addressing limits, IOMMU remapping, cache coherence) behind a single API, and the API's four design principles (tag/map separation, callback-based load, explicit sync, constraint inheritance) appear in every driver that uses it. Chapter 21's running example will exercise each principle in turn on a simulated device, with enough real FreeBSD grounding that the patterns carry over to production drivers unchanged.

Section 2 is the next step: the API's vocabulary in detail. What a tag actually contains, what a map actually is, what a segment is, what the PRE and POST flags mean at a finer level, and how the pieces fit together.



## Section 2: Understanding FreeBSD's bus_dma(9) Interface

Section 1 established that `bus_dma(9)` is the portability layer between the driver and the platform's DMA realities. Section 2 opens the layer and looks at its pieces. The goal is to give the reader the vocabulary to talk about a DMA tag, a DMA map, a segment, a sync operation, and a callback without mystery. No Stage 1 code is written yet; Section 3 does that. This section is the mental map.

### The Four Pieces of the API

Every `bus_dma` driver deals with four objects. Understand the four and the rest of the API falls into place.

**The tag** is a description. It is an opaque kernel object of type `bus_dma_tag_t`, created once and reused for many transfers. A tag carries:

- An optional parent tag, inherited from the parent bridge through `bus_get_dma_tag(dev)`.
- An alignment constraint in bytes (the starting address of every mapping made through this tag must be a multiple of this value).
- A boundary constraint in bytes (a mapping that crosses an address boundary of this size is not allowed).
- A low address and a high address that together describe a window of bus address space the device **cannot** reach.
- A maximum mapping size (the sum of segment lengths in one mapping).
- A maximum segment count (how many discontinuous pieces one mapping may span).
- A maximum segment size (the largest single piece).
- A set of flags, chiefly `BUS_DMA_WAITOK`, `BUS_DMA_NOWAIT`, and `BUS_DMA_COHERENT`.
- An optional lock function and its argument, used when the kernel needs to invoke the driver's load callback from a deferred context.

The tag is how the driver tells the kernel "this device has these constraints; please honour them on every mapping I make through this tag". The kernel consults the tag during every map operation and ensures the mapping respects the constraints (or reports an error if the requested buffer cannot satisfy them).

**The map** is a mapping context. It is an opaque kernel object of type `bus_dmamap_t`, created (often implicitly) per transfer. A map carries enough state to describe one specific mapping: which bus addresses the buffer occupies, whether any bounce pages are in use, whether the mapping is currently loaded or idle. Maps are cheap to create and cheap to load; the expensive setup work lives in the tag.

**The memory** is the kernel virtual address range the CPU uses to access the buffer. For static DMA regions (allocated at attach time, reused for many transfers), the memory is allocated by `bus_dmamem_alloc`, which returns a kernel virtual address and an implicitly-loaded map. For dynamic DMA (mapping arbitrary kernel buffers, mbufs, or user data), the memory already exists elsewhere and the driver uses `bus_dmamap_create` plus `bus_dmamap_load` to attach a map to it.

**The segment** is the bus-address-and-length pair the device actually sees. A `bus_dma_segment_t` is a small structure with two fields: `ds_addr` (a `bus_addr_t` giving the bus address) and `ds_len` (a `bus_size_t` giving the length). A single mapping may consist of one segment (physically contiguous) or several (scatter-gather). The driver programs the device with the segment list; that is the concrete hand-off.

Chapter 21's driver uses all four. The tag is created in `myfirst_dma_setup`. The map is returned by `bus_dmamem_alloc`. The memory is the four-kilobyte buffer returned by the same call. The segment is the one `bus_dma_segment_t` returned by the callback passed to `bus_dmamap_load`. Later chapters extend this to per-transfer maps and multi-segment scatter-gather, but the same four objects are always in play.

### Static Versus Dynamic Transactions

The `bus_dma(9)` manual page introduces a distinction between **static** and **dynamic** transactions. The distinction matters because it shapes which API calls a driver uses.

**Static transactions** use memory regions allocated by `bus_dma` itself, typically at attach time, and reused for the driver's lifetime. Descriptor rings are the classic example: a NIC driver allocates the receive ring once and uses it for every packet, never unloading and reloading. The driver calls:

- `bus_dma_tag_create` once, to describe the ring's constraints.
- `bus_dmamem_alloc` once, to allocate the ring's memory and get an implicitly-loaded map.
- `bus_dmamap_load` once, to obtain the ring's bus address. (The manual page's wording is "an initial load operation is required to obtain the bus address"; this is an API quirk you just remember.)
- `bus_dmamap_sync` many times, around each use of the ring.

On teardown, the driver calls `bus_dmamap_unload`, `bus_dmamem_free`, and `bus_dma_tag_destroy`. No `bus_dmamap_create` or `bus_dmamap_destroy` is needed because `bus_dmamem_alloc` returned the map and `bus_dmamem_free` frees it.

**Dynamic transactions** use memory regions allocated by something else (an mbuf from `m_getcl`, a kernel buffer from `malloc`, a user page pinned by `vm_fault_quick_hold_pages`), and the driver maps them into the device's address space per transfer. A NIC driver that is transmitting packets does this for each outgoing packet: the packet's mbuf already exists, the driver maps it, programs the device, waits for transmission, unmaps it. The driver calls:

- `bus_dma_tag_create` once, to describe the per-buffer constraints.
- `bus_dmamap_create` once per buffer slot, to get a map.
- `bus_dmamap_load_mbuf_sg` (or `bus_dmamap_load`) each time a packet is transmitted, to map the specific mbuf.
- `bus_dmamap_sync` around each use.
- `bus_dmamap_unload` after each transmission completes.
- `bus_dmamap_destroy` once per buffer slot, on detach.
- `bus_dma_tag_destroy` once, on detach.

Chapter 21's driver uses the static pattern: one buffer, allocated once, reused for every simulated DMA transfer. The dynamic pattern is introduced briefly in Section 7 as a contrast; Part 6's network chapter (Chapter 28) uses it in earnest.

Knowing which pattern a driver uses is the first thing to identify when reading DMA code. The call sequence is different, the teardown order is different, and the interpretation of the map is different. `bus_dmamem_alloc`'s "one static allocation" behaviour is what makes the static path shorter and simpler.

### The Sync Discipline

Section 1 described `bus_dmamap_sync` in general terms; Section 2 is the place to pin down the four operations precisely, because the next section will assume this vocabulary.

The four operations are:

- `BUS_DMASYNC_PREREAD`. Called **before** the device writes the buffer (from the host's point of view, before the buffer is read by the host). Tells the kernel "the CPU has finished whatever it was doing with this buffer; the device is about to write into it". On non-coherent platforms, this invalidates the CPU's cache copy so a later read will see the device's writes. On coherent platforms, it is often a no-op.
- `BUS_DMASYNC_PREWRITE`. Called **before** the device reads the buffer (from the host's point of view, before the buffer is written by the host). Tells the kernel "the CPU has just written the buffer; please flush any dirty cache lines so the device reads the current contents". On non-coherent platforms this is a cache flush; on coherent platforms it is often a memory barrier or no-op.
- `BUS_DMASYNC_POSTREAD`. Called **after** the device has written the buffer and **before** the CPU reads it. Tells the kernel "the device has finished writing; the CPU is about to read". On platforms with bounce buffers, this is the point at which the data is copied from the bounce region back to the driver's buffer.
- `BUS_DMASYNC_POSTWRITE`. Called **after** the device has read the buffer. Tells the kernel "the device is done reading; the CPU may reuse the buffer". Usually a no-op on coherent platforms; on systems with bounce buffers, this is when the bounce region may be released.

The names are worth internalising. "PRE" and "POST" refer to the DMA transaction: PRE is before, POST is after. "READ" and "WRITE" are from the **host's** perspective: READ means the host will read the result (the device writes), WRITE means the host has written what the device will read.

The pairs come together in the four common sequences:

- **Host-to-device (the driver sends data to the device):** write data → `PREWRITE` → device reads → device done → `POSTWRITE`.
- **Device-to-host (the device sends data to the driver):** `PREREAD` → device writes → device done → `POSTREAD` → host reads.
- **Descriptor ring where the driver updates an entry, the device reads it, updates status, and the driver reads the status:** the driver writes the entry → `PREWRITE` → device reads the entry → device updates status → device done → `POSTREAD | POSTWRITE` (combined flag) → host reads status.
- **Full ring shared between host and device:** at setup, `PREREAD | PREWRITE` marks the entire ring as hand-off to device, with both directions of data flow open.

Real drivers use the combined flags often because descriptor rings are bidirectional. Chapter 21 starts with the simple one-direction cases and shows the combined flag in the descriptor-ring sketch at the end of Section 4.

### The Load Callback Pattern

The most surprising part of `bus_dma` for a first-time reader is that `bus_dmamap_load` does not return the segment list directly. It takes a **callback function** as an argument, calls the callback with the segment list, and (normally) returns after the callback has run. Why this indirection?

The reason is that on platforms where the kernel may need to wait for bounce buffers, the load operation can be **deferred**. If bounce buffers are in short supply at the moment of the call, the kernel queues the request, returns `EINPROGRESS` to the caller, and runs the callback later when buffers are free. The driver has to be prepared for this case: the callback may run in a different context, possibly on a different thread, after the load call has already returned.

For most drivers on most platforms, the deferred case is rare. A driver that allocates its descriptor ring at attach time, on a system with enough bounce buffers and no IOMMU restriction, sees the callback run synchronously inside the load call, long before the load returns. The callback just stashes the bus address in a local variable that the driver reads immediately after the load returns.

A minimal callback looks like this:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error)
		return;

	KASSERT(nseg == 1, ("unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}
```

The driver calls:

```c
bus_addr_t bus_addr = 0;
int err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &bus_addr, BUS_DMA_NOWAIT);
if (err != 0) {
	device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
	return (err);
}
KASSERT(bus_addr != 0, ("single-segment callback did not set address"));
sc->dma_bus_addr = bus_addr;
```

The pattern is so common that many drivers define exactly this helper and name it similarly. `/usr/src/sys/dev/re/if_re.c` calls it `re_dma_map_addr`. `/usr/src/sys/dev/nvme/nvme_private.h` calls it `nvme_single_map`. Chapter 21's driver will call it `myfirst_dma_single_map`. The function body is almost identical across drivers.

The `BUS_DMA_NOWAIT` flag on the load call tells the kernel "do not defer; if bounce buffers are unavailable, return `ENOMEM` immediately". This is the common case for drivers that are willing to accept failure rather than wait. A driver that wants to wait passes `BUS_DMA_WAITOK` (or `0`, since `WAITOK` is the default zero value); in that case, if the load is deferred, the driver must return from the caller and expect the callback to run later.

When the load is deferred and runs later, the kernel may need to hold a driver-level lock while the callback runs. This is what the `lockfunc` and `lockfuncarg` parameters of `bus_dma_tag_create` are for: the driver provides a function that the kernel calls with `BUS_DMA_LOCK` before the callback and `BUS_DMA_UNLOCK` after. FreeBSD provides `busdma_lock_mutex` as a ready-made implementation for drivers that protect their DMA state with a standard mutex; the driver passes the mutex pointer as `lockfuncarg`. For drivers that never defer loads (because they use `BUS_DMA_NOWAIT` everywhere, or because they only load at attach time with no locks held), the kernel provides `_busdma_dflt_lock`, which panics if called; that is the safer default because it turns a silent threading bug into a loud one.

Chapter 21's driver loads exactly once at attach time, under no contended lock, with `BUS_DMA_NOWAIT`. The lockfunc does not matter because the callback always runs synchronously. Section 3 passes `NULL` for both `lockfunc` and `lockfuncarg`; the kernel substitutes `_busdma_dflt_lock` automatically, and the panic-on-unexpected-call acts as a safety net.

### Tag Hierarchies and Inheritance

Section 1 mentioned that a tag inherits from a parent tag, and that the hierarchy matches the hardware. Section 2 is the place to make this concrete.

When a PCI device is attached to a PCI bridge, the bridge's own tag carries the constraints that the bridge itself imposes on DMA traffic: the DMA addresses it can route, the alignment it enforces, whether an IOMMU is in front of it. The kernel creates this tag at bus-attach time, and a driver can retrieve it for its own device with:

```c
bus_dma_tag_t parent = bus_get_dma_tag(sc->dev);
```

The returned tag is the device's **parent DMA tag**. When the driver creates its own tag, it passes the parent:

```c
int err = bus_dma_tag_create(parent,
    /* alignment */    4,
    /* boundary */     0,
    /* lowaddr */      BUS_SPACE_MAXADDR,
    /* highaddr */     BUS_SPACE_MAXADDR,
    /* filtfunc */     NULL,
    /* filtfuncarg */  NULL,
    /* maxsize */      DMA_BUFFER_SIZE,
    /* nsegments */    1,
    /* maxsegsize */   DMA_BUFFER_SIZE,
    /* flags */        0,
    /* lockfunc */     NULL,
    /* lockfuncarg */  NULL,
    &sc->dma_tag);
```

The child inherits the parent's restrictions; the child can only add more restrictions, never remove them. If the parent says "this bridge cannot address above 4 GB", the child cannot say "actually my device can"; the kernel takes the intersection. This is what lets a driver write portable code: the driver describes only its device's own constraints, and the kernel composes them with whatever the platform imposes.

Drivers that need to describe multiple groups of transfers (for example, one alignment for descriptor rings and a different alignment for data buffers) create multiple tags, all inheriting from the same parent. The `if_re(4)` driver is a clean example: it creates `rl_parent_tag` from the bridge, then `rl_tx_mtag` and `rl_rx_mtag` (for mbuf payloads, 1-byte aligned, multi-segment) and `rl_tx_list_tag` and `rl_rx_list_tag` (for descriptor rings, `RL_RING_ALIGN`-aligned, single-segment) all inherit from `rl_parent_tag`. Each tag describes a different set of buffers; the hierarchy composes them.

Chapter 21's driver uses a single tag because it has a single buffer type. When the reader extends the driver with descriptor rings, the natural evolution is to add a second tag; Section 8's challenge exercises sketch the step.

### A Simple Hardware-to-Software Timeline

To solidify the vocabulary, here is a timeline for one complete DMA transfer on Chapter 21's device, with each line annotated by which object and which API call is involved.

1. **Attach time, once.** Create tag via `bus_dma_tag_create`. Allocate memory and implicit map via `bus_dmamem_alloc`. Load map via `bus_dmamap_load` with single-segment callback. Record bus address in softc. Result: `sc->dma_tag`, `sc->dma_vaddr`, `sc->dma_map`, `sc->dma_bus_addr` all populated.

2. **First transfer, host-to-device:**
   - Fill `sc->dma_vaddr` with the pattern to send.
   - Call `bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE)`.
   - Write `DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR=WRITE` via `bus_write_4` calls.
   - Write `DMA_CTRL = START`.
   - Wait for completion interrupt.
   - In the rx filter, read `DMA_STATUS`; if `DONE`, enqueue task.
   - In the task, call `bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE)`.
   - Update statistics.

3. **Second transfer, device-to-host:**
   - Call `bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)`.
   - Write `DMA_DIR=READ`, `DMA_CTRL=START`.
   - Wait for interrupt.
   - In the task, call `bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)`.
   - Read `sc->dma_vaddr` and verify the pattern.

4. **Detach time, once.** Unload map via `bus_dmamap_unload`. Free memory via `bus_dmamem_free` (which also frees the implicit map). Destroy tag via `bus_dma_tag_destroy`.

The timeline has four loops of sync: PREWRITE/POSTWRITE for host-to-device, PREREAD/POSTREAD for device-to-host. Each loop is one transfer. Each loop has exactly one sync call before and one after; neither may be omitted. Between the PRE sync and the POST sync, the CPU may not touch the buffer (the device owns it). Before the PRE sync and after the POST sync, the CPU owns the buffer freely.

The mental model "ownership passes between CPU and device, with sync calls marking each hand-off" is the single most useful intuition to take away from Section 2. Every real driver respects the ownership discipline, even on coherent platforms where the sync calls happen to be cheap or no-op.

### Exercise: Walk Through a Sample DMA Setup Without a Real Device

A paper exercise before Section 3. Imagine a simple DMA-capable device with the following properties:

- PCI device with one MMIO BAR.
- One DMA engine with these registers: `DMA_ADDR_LOW` at offset 0x20, `DMA_ADDR_HIGH` at 0x24, `DMA_LEN` at 0x28, `DMA_DIR` at 0x2C (0 = host-to-device, 1 = device-to-host), `DMA_CTRL` at 0x30 (write 1 = start), `DMA_STATUS` at 0x34 (bit 0 = done, bit 1 = error).
- 4 KB transfer buffer required.
- 32-bit DMA engine (cannot reach memory above 4 GB).
- 16-byte alignment required.
- Buffer must not cross a 64 KB boundary.

Write out, on paper, the exact `bus_dma_tag_create` call the driver would issue. Identify each of the fourteen parameters, and name what concrete value you would use for each. Then write out the `bus_dmamem_alloc` call and the `bus_dmamap_load` call that follow. Do not write C; write prose.

A sample answer (for comparison):

- Parent tag: `bus_get_dma_tag(sc->dev)`, the PCI parent.
- Alignment: 16.
- Boundary: 65536.
- Low address: `BUS_SPACE_MAXADDR_32BIT`. Remember the confusing naming: `lowaddr` is the highest address the device can reach, not a lower bound. For a 32-bit engine, any address above `BUS_SPACE_MAXADDR_32BIT` must be excluded, so that value goes here.
- High address: `BUS_SPACE_MAXADDR`. This is the upper bound of the excluded window; passing `BUS_SPACE_MAXADDR` extends the exclusion to infinity, which correctly expresses "nothing above 4 GB is reachable".
- Filter function: NULL (not needed, modern code avoids them).
- Filter argument: NULL.
- Max size: 4096.
- Nsegments: 1 (single-segment required).
- Max segment size: 4096.
- Flags: 0.
- Lockfunc: NULL.
- Lockfuncarg: NULL.
- Tag pointer: `&sc->dma_tag`.

Then `bus_dmamem_alloc` with `BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO` to allocate the buffer as coherent, zero-initialised, sleeping allowed (attach time). Then `bus_dmamap_load` with `BUS_DMA_NOWAIT`, the single-segment callback, and `&sc->dma_bus_addr` as the callback argument.

Writing this out on paper before seeing the code makes Section 3's walkthrough feel like a confirmation rather than an introduction. The exercise takes ten minutes and is worth it.

### Wrapping Up Section 2

Section 2 gave the reader the vocabulary: tag, map, memory, segment, PRE, POST, READ, WRITE, static, dynamic, parent, callback. The pieces fit together in a specific pattern: at attach time, create a tag, allocate memory, load a map, record the bus address; at transfer time, sync PRE, trigger the device, wait, sync POST, read the data; at detach time, unload the map, free the memory, destroy the tag. The pattern is the same across every DMA-capable driver in FreeBSD; the vocabulary is the same in every chapter of this book from now on.

Section 3 is the first running code. The driver creates a tag, allocates memory, loads it, and verifies that the bus address is sane. No transfers happen yet; the goal is to exercise the setup and teardown paths under `WITNESS` and confirm that the driver can stand up and tear down a DMA region cleanly across many kldload/kldunload cycles. That pair of operations is the foundation that every later section adds to.



## Section 3: Allocating and Mapping DMA Memory

Section 2 built the vocabulary. Section 3 writes the code. The first running stage of the Chapter 21 driver creates a DMA tag, allocates a single four-kilobyte buffer, loads it into a map, records the bus address, and adds the setup-plus-teardown path to the attach and detach sequences. No DMA transfers happen yet. The goal is to exercise the allocation path, watch the new softc fields populate under `kldload`, confirm that `kldunload` cleans up without complaint, and do so repeatedly under `WITNESS` to catch any lock-order violations.

Stage 1's version tag is `1.4-dma-stage1`. The compile target after the stage is `myfirst.ko`. No new files are added yet; the tag creation, allocation, and teardown live in `myfirst.c` and `myfirst_pci.c` alongside the existing attach logic. Section 8 moves the DMA code into its own file; here we keep everything in one place so the stage's scope is visible in a single view.

### New Softc Fields

The driver's softc grows by four fields:

```c
/* In myfirst.h, inside struct myfirst_softc. */
bus_dma_tag_t       dma_tag;
bus_dmamap_t        dma_map;
void               *dma_vaddr;
bus_addr_t          dma_bus_addr;
```

`dma_tag` is the tag created at attach time. `dma_map` is the map returned by `bus_dmamem_alloc`. `dma_vaddr` is the kernel virtual address the CPU uses to access the buffer. `dma_bus_addr` is the bus address the device uses.

The fields follow the Chapter 17/18 naming convention: lowercase, underscores, short. They are initialised to zero by the softc's implicit zeroing at attach time; the driver adds the initialisation by relying on `device_get_softc`'s zero-fill guarantee. No explicit `= 0` assignments are needed.

The softc includes `<machine/bus.h>` and `<sys/bus_dma.h>` (the latter through `<machine/bus.h>`); Chapter 18 already pulled these in for `bus_space` work, so the includes are present and no new ones are required.

### The Single-Segment Callback

Before the setup function, a one-function helper. The helper is the single-segment callback introduced in Section 2:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr = arg;

	if (error != 0) {
		printf("myfirst: dma load callback error %d\n", error);
		return;
	}

	KASSERT(nseg == 1, ("myfirst: unexpected DMA segment count %d", nseg));
	*addr = segs[0].ds_addr;
}
```

The function is the same pattern as `re_dma_map_addr` in `/usr/src/sys/dev/re/if_re.c`. Pass it a pointer to a `bus_addr_t`; on success, the callback writes the one segment's bus address into the target. The `KASSERT` confirms there is exactly one segment; `bus_dmamem_alloc` always returns a single segment, so this assertion is a safety net rather than a runtime check.

The callback is marked `static` because it is not used outside `myfirst.c`. It takes no state beyond the bus-address pointer, so it is thread-safe by construction and can run in any context the kernel chooses.

### The Setup Function

The Stage 1 setup function, named `myfirst_dma_setup`, lives in `myfirst.c`. It performs the four operations in order: create tag, allocate memory, load map, record bus address. Each step checks for error and unwinds prior steps on failure. The function returns `0` on success, a nonzero error code on failure, and leaves the softc's DMA fields in a consistent state in both cases.

```c
#define	MYFIRST_DMA_BUFFER_SIZE	4096u

int
myfirst_dma_setup(struct myfirst_softc *sc)
{
	int err;

	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
	    /* alignment */    4,
	    /* boundary */     0,
	    /* lowaddr */      BUS_SPACE_MAXADDR,
	    /* highaddr */     BUS_SPACE_MAXADDR,
	    /* filtfunc */     NULL,
	    /* filtfuncarg */  NULL,
	    /* maxsize */      MYFIRST_DMA_BUFFER_SIZE,
	    /* nsegments */    1,
	    /* maxsegsz */     MYFIRST_DMA_BUFFER_SIZE,
	    /* flags */        0,
	    /* lockfunc */     NULL,
	    /* lockfuncarg */  NULL,
	    &sc->dma_tag);
	if (err != 0) {
		device_printf(sc->dev, "bus_dma_tag_create failed: %d\n", err);
		return (err);
	}

	err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->dma_map);
	if (err != 0) {
		device_printf(sc->dev, "bus_dmamem_alloc failed: %d\n", err);
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err);
	}

	sc->dma_bus_addr = 0;
	err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
	    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
	    myfirst_dma_single_map, &sc->dma_bus_addr,
	    BUS_DMA_NOWAIT);
	if (err != 0 || sc->dma_bus_addr == 0) {
		device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
		return (err != 0 ? err : ENOMEM);
	}

	device_printf(sc->dev,
	    "DMA buffer %zu bytes at KVA %p bus addr %#jx\n",
	    (size_t)MYFIRST_DMA_BUFFER_SIZE,
	    sc->dma_vaddr, (uintmax_t)sc->dma_bus_addr);

	return (0);
}
```

A few details repay attention.

**The constants.** `MYFIRST_DMA_BUFFER_SIZE` is a `#define` named with the chapter's prefix, following the Chapter 17 naming convention. It is used three times in the setup function (in the tag's `maxsize`, the tag's `maxsegsz`, and the load's buffer length), so a symbolic name is worth it even if the value is small. Chapter 21 uses 4 KB because it matches a page and avoids any bounce-buffer corner cases; Section 7 revisits the choice.

**The parent tag.** `bus_get_dma_tag(sc->dev)` returns the parent DMA tag inherited from the PCI bridge. The kernel composes the parent's restrictions with the driver's own, so the child tag automatically respects any bridge-level limits. On amd64 with an IOMMU, the parent tag also carries the IOMMU's mapping requirements; the driver's tag inherits them transparently.

**The alignment.** 4 bytes is enough for a 32-bit-wide engine's DMA start address. A real device's datasheet would specify a value (NVMe uses the page size, `if_re` uses the descriptor size); Chapter 21's simulated engine happens to be content with 4.

**The boundary.** 0 means no boundary restriction. A device that cannot cross 64 KB boundaries (some DMA engines have this limitation because their internal counters are 16 bits) would pass 65536.

**The addresses.** `BUS_SPACE_MAXADDR` for both low and high means "no exclusion window". The simulation has no address-range limit; a real 32-bit-only device would pass `BUS_SPACE_MAXADDR_32BIT` for `lowaddr` to exclude addresses above 4 GB. Section 7 revisits this with a concrete walkthrough.

**The size and segment count.** A single 4 KB buffer that must be physically contiguous. `nsegments = 1` plus `maxsegsz = MYFIRST_DMA_BUFFER_SIZE` means a one-segment mapping; `bus_dmamem_alloc` always returns a single segment, so this matches.

**The flags.** `0` at tag-create time (no special tag flags). `BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO` at `bus_dmamem_alloc` time: the allocator may sleep if needed, the memory should be cache-coherent with the device (important on arm and arm64; a hint on amd64), and the buffer should be zero-initialised. `BUS_DMA_NOWAIT` at `bus_dmamap_load` time: the load must not defer; if bounce buffers are unavailable, the load should fail immediately. For Chapter 21's one-load-at-attach-time usage, the distinction does not matter in practice; the convention is to use `NOWAIT` anywhere the driver has a clean error path, because it makes the control flow easier to reason about.

**The lockfunc.** `NULL` means "default". On Chapter 21's single load at attach time, the callback always runs synchronously, so the lockfunc is never invoked; the default (`_busdma_dflt_lock`, which panics if called) is the correct safety net.

**The error handling.** Each step checks the return value and unwinds prior steps on failure. The order matters: tag was created first, so it is destroyed last; memory was allocated second, so it is freed second-to-last. The softc fields are cleared to NULL after free so detach can safely walk them whether setup partially failed or ran to completion.

**The final `device_printf`.** A single banner line logs the buffer size, kernel virtual address, and bus address. The line is useful in `dmesg` and doubles as a regression-test marker: a working driver prints it; a broken setup does not.

### The Teardown Function

The paired teardown function is:

```c
void
myfirst_dma_teardown(struct myfirst_softc *sc)
{
	if (sc->dma_bus_addr != 0) {
		bus_dmamap_unload(sc->dma_tag, sc->dma_map);
		sc->dma_bus_addr = 0;
	}
	if (sc->dma_vaddr != NULL) {
		bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
		sc->dma_vaddr = NULL;
		sc->dma_map = NULL;
	}
	if (sc->dma_tag != NULL) {
		bus_dma_tag_destroy(sc->dma_tag);
		sc->dma_tag = NULL;
	}
}
```

The function is guarded by zero-value checks so it is safe to call even when setup did not complete. This is the same pattern the Chapter 19 interrupt teardown uses, and for the same reason: detach may run after a partial attach failure, and the cleanup code must not assume any particular prefix of the setup succeeded.

The order is the reverse of setup:

1. Unload the map. Safe to call even if the load succeeded; idempotent.
2. Free the memory and the implicit map. `bus_dmamem_free` frees both the buffer and the map object returned by `bus_dmamem_alloc`; the driver never calls `bus_dmamap_destroy` for this path.
3. Destroy the tag. Returns `EBUSY` if any maps are still associated; the earlier unload prevents that.

The `bus_dmamap_unload` call is subtle. After `bus_dmamem_alloc`, the map is implicitly loaded (this is what the manual page calls "An initial load operation is required to obtain the bus address"). The load either happens implicitly or via the explicit `bus_dmamap_load` call the driver already made. Either way, the map is in the loaded state after setup; it must be unloaded before `bus_dmamem_free`. The chapter's pattern uses an explicit `bus_dmamap_load` to expose the callback and the bus address; `bus_dmamap_unload` is its paired teardown.

A reader who is curious about why `bus_dmamap_load` has to be called manually after `bus_dmamem_alloc` can read the answer in the `bus_dma(9)` manual page: the "STATIC TRANSACTIONS" section explicitly states "An initial load operation is required to obtain the bus address of the allocated memory". The allocator returns a valid KVA and a map object, but the bus address itself is only made available via the callback after the load. This is a historical quirk; you remember it once and the pattern falls into place for every static driver.

### Integrating Setup and Teardown Into Attach and Detach

The attach function from Chapter 20 ended with the MSI-X setup. Chapter 21 inserts the DMA setup just before it, so the tag and buffer are ready by the time the device starts firing interrupts:

```c
/* ... earlier attach work: BAR allocation, hw layer, cdev ... */

err = myfirst_dma_setup(sc);
if (err != 0)
	goto fail_dma;

err = myfirst_msix_setup(sc);
if (err != 0)
	goto fail_msix;

/* ... remaining attach work ... */

return (0);

fail_msix:
	myfirst_dma_teardown(sc);
fail_dma:
	/* ... earlier failure labels ... */
```

The exact labels depend on the driver's current layout, which is the Chapter 20 layout after Stage 4. The principle is: the DMA setup comes after the BAR is mapped (because the device's registers must be reachable to eventually trigger transfers), after the hardware layer is built, but before the interrupt setup runs. On failure, the driver unwinds to the labels in reverse.

The detach function mirrors the attach order. The MSI-X teardown runs first (to stop any further interrupts), then the DMA teardown runs, then the earlier cleanup. The ordering guarantees that no interrupt can fire on the DMA path after the tag is destroyed, which would be a use-after-free.

### Running Stage 1

Build and load:

```sh
make clean && make
kldload ./myfirst.ko
```

Watch `dmesg`:

```text
myfirst0: <myfirst DMA test device> ...
myfirst0: DMA buffer 4096 bytes at KVA 0xfffffe0010af9000 bus addr 0x10af9000
myfirst0: interrupt mode: MSI-X, 3 vectors
```

The KVA and bus address will differ on your machine; the format is what matters. The KVA is a typical kernel virtual address (well above the direct-map base on amd64). The bus address on amd64 with an IOMMU disabled is usually equal to the physical address; with an IOMMU enabled, it is an IOMMU-remapped address, often much lower than any physical address that could back it.

Verify the softc fields via sysctl or a `device_printf` in a debug build:

```sh
sysctl dev.myfirst.0
```

Unload the module:

```sh
kldunload myfirst
```

`dmesg` should show the detach:

```text
myfirst0: detaching
```

No panic, no `WITNESS` complaint, no `VFS` warning about leaking memory. If `WITNESS` is enabled and the driver's setup or teardown acquires a lock that `bus_dma` also acquires, the output would show a lock-order warning; a clean `kldunload` is the confirmation that the locking is right.

Repeat the load/unload cycle several times. DMA-related memory leaks are rare but catastrophic when they happen (a leak in `bus_dma` leaks pages, not just bytes); the regression check is worth running fifty times in a row with a `for` loop and confirming that `vmstat -m` shows `bus_dmamap` and `bus_dma_tag` returning to their idle counts.

### What Stage 1 Does and Does Not Do

Stage 1 exercises the allocation and cleanup paths. It does not perform any DMA transfers; `dma_vaddr` is an unused buffer (apart from its zero-fill), and `dma_bus_addr` is an unused address. The next sections will put both to work.

What Stage 1 proves is that:

- The driver can create a tag that inherits from the PCI parent.
- The driver can allocate a coherent, zero-initialised 4 KB buffer.
- The driver can obtain the bus address via the callback.
- The driver can tear everything down cleanly.
- The cycle can run many times without leaking resources.

That is a solid foundation. The rest of the chapter builds on it.

### Common Mistakes at This Stage

Five mistakes are common in Stage 1. Each is easy to make and easy to fix once recognised.

**Mistake one: forgetting to call `bus_dmamap_load` after `bus_dmamem_alloc`.** The symptom is that `dma_bus_addr` stays at zero; programming the device with bus address zero either silently corrupts memory or hits an IOMMU fault. The fix is to call `bus_dmamap_load` explicitly, as the Stage 1 code does. The `KASSERT(bus_addr != 0, ...)` in the driver catches this during development.

**Mistake two: passing an unaligned `maxsize` to `bus_dma_tag_create`.** The `alignment` parameter must be a power of two, and `maxsize` must be at least `alignment`. A size of 3 bytes with alignment 4 produces an error. The chapter's 4096-byte size with alignment 4 is safely aligned.

**Mistake three: using `BUS_DMA_NOWAIT` with `bus_dmamem_alloc` and then ignoring the error.** `BUS_DMA_NOWAIT` makes the allocator return `ENOMEM` if memory is tight; a driver that ignores the error and proceeds with `NULL` dereferences panics immediately. Use `BUS_DMA_WAITOK` at attach time (where sleeping is safe), and always check the return value.

**Mistake four: forgetting to zero the bus address before the load call.** If the load fails, the callback is never called, and `dma_bus_addr` retains whatever garbage was there. The chapter's code assigns `sc->dma_bus_addr = 0` before calling `bus_dmamap_load` so the post-load check (`if (err != 0 || sc->dma_bus_addr == 0)`) is reliable.

**Mistake five: skipping the tag destroy in the error-path unwind.** The unwind in the chapter's code destroys the tag in every failure branch. A driver that fails `bus_dmamem_alloc` and returns without destroying the tag leaks a tag on every failed attach; over many retries this adds up. The pattern is: on any failure after `bus_dma_tag_create` succeeded, destroy the tag before returning.

None of these mistakes panic immediately; all of them produce subtle bugs that manifest under load or after repeated load/unload cycles. Running Stage 1 fifty times with `WITNESS` and `INVARIANTS` enabled catches most of them.

### A Word on the `bus_dma` Template API

FreeBSD 13 and later also provide a template-based API for creating DMA tags, covered in the `bus_dma(9)` manual under `bus_dma_template_*`. The template API is an ergonomic alternative to `bus_dma_tag_create`'s fourteen-parameter list: the driver initialises a template from a parent tag, overrides individual fields with `BD_*` macros, and builds the tag with `bus_dma_template_tag`.

A template-based Stage 1 setup would look like:

```c
bus_dma_template_t t;

bus_dma_template_init(&t, bus_get_dma_tag(sc->dev));
BUS_DMA_TEMPLATE_FILL(&t,
    BD_ALIGNMENT(4),
    BD_MAXSIZE(MYFIRST_DMA_BUFFER_SIZE),
    BD_NSEGMENTS(1),
    BD_MAXSEGSIZE(MYFIRST_DMA_BUFFER_SIZE));
err = bus_dma_template_tag(&t, &sc->dma_tag);
```

The template API is preferred in new drivers because it makes the overridden fields explicit. Chapter 21 uses the classic `bus_dma_tag_create` in its running example because that is what the majority of the existing FreeBSD source tree uses, and the reader who can read a fourteen-parameter call can always translate it to the template form. Section 8's refactor exercise suggests trying the template API as a stylistic variation.

### Wrapping Up Section 3

Section 3 put the Chapter 21 driver into Stage 1: a tag, a map, a buffer, a bus address, and clean teardown. No transfers happen yet; the allocation path is the only thing exercised. That is enough to confirm that the pieces fit together, that the parent tag is inherited correctly, that the single-segment callback populates the bus address, and that `kldload`/`kldunload` cycles do not leak.

Section 4 is the sync discipline. Before the driver hands the buffer to a DMA engine, it needs to know exactly which sync calls happen where, why each one exists, and what would go wrong if one were omitted. Section 4 is a careful tour of `bus_dmamap_sync`, with concrete diagrams and the mental model that Section 5 then uses to build the simulated engine.



## Section 4: Synchronising and Using DMA Buffers

Section 3 produced a driver that can allocate and release a DMA buffer. Section 4 teaches the discipline that every actual transfer must respect: when to call `bus_dmamap_sync`, with which flag, and why. The discipline is what keeps the CPU's and the device's view of memory consistent. No new code is written in this section (the simulated engine that will consume the sync calls is built in Section 5), but the mental model laid down here is what every later section depends on.

### The Ownership Model

The single most useful intuition for DMA is this: at any moment, either the CPU owns the buffer or the device owns the buffer. Ownership passes back and forth through `bus_dmamap_sync` calls. Between a PRE sync and the next POST sync, the device owns the buffer and the CPU must not touch it. Between a POST sync and the next PRE sync, the CPU owns the buffer and the device must not touch it. The driver's job is to make every ownership boundary explicit with a sync call.

The ownership is physical, not logical. It does not mean the CPU or the device "knows" about the buffer in some abstract sense; it means the CPU's caches and the device's bus engine can actually observe different contents for the same memory at the same moment, and the sync calls are the kernel's opportunity to reconcile the difference. On a fully coherent platform (modern amd64 with a coherent PCIe root complex and no non-coherent DMA peers), the hardware reconciles the difference transparently and `bus_dmamap_sync` is often a memory barrier or a no-op. On a partially coherent platform (some arm64 systems, some RISC-V configurations, anything with `BUS_DMA_COHERENT` not honoured at allocation), the sync calls do cache flushes and invalidates. The driver writes the same code regardless; the kernel does the right thing.

A driver that respects the ownership boundaries is portable. A driver that skips sync calls "because my amd64 system happens to work without them" is not; the same driver will corrupt data on arm64 or in a virtualised environment where the hypervisor's IOMMU imposes its own coherence policy.

### The Four Sync Flags in Depth

Section 2 named the four sync flags. Section 4 pins down exactly what each one means, what the kernel does for each on different platforms, and when the driver calls each in practice.

**`BUS_DMASYNC_PREREAD`.** The CPU has finished touching the buffer and the device is about to read from host memory is **not** the intent here; the flag is about **device writes into host memory**, which the host then **reads**. The flag is called PREREAD because the *host* is going to read after the operation; it is called PRE because this is before the DMA. On non-coherent platforms, the kernel invalidates the CPU's cache copies of the buffer's cache lines. The reason is that after the device writes new data to memory, stale cache lines in the CPU would cause subsequent reads to see the old values. Invalidation ensures the next read fetches from memory, where the device's writes will land. On coherent platforms, invalidation is unnecessary because the hardware snoops the cache automatically; the kernel implements `PREREAD` as a memory barrier or no-op.

**`BUS_DMASYNC_PREWRITE`.** The CPU has just written the buffer and the device is about to read it. On non-coherent platforms, the kernel flushes the CPU's dirty cache lines to memory. The reason is that the CPU's writes might still sit in its cache; the device reads from memory directly and would see stale contents. Flushing ensures the memory controller has the current data before the device's read. On coherent platforms, a memory barrier is usually sufficient to enforce ordering; the cache coherence protocol handles the flush implicitly.

**`BUS_DMASYNC_POSTREAD`.** The device has finished writing to host memory and the CPU is about to read. On non-coherent platforms, this is often the actual cache invalidate (in some implementations the invalidate happens at POST rather than PRE); on coherent platforms, it is a barrier or no-op. On platforms with bounce buffers, this is when the kernel copies data from the bounce region back into the driver's buffer. That is why even on coherent platforms the POSTREAD call cannot be skipped: the coherence machinery may be coherent, but the bounce buffer machinery is not, and the POSTREAD call is the hook the bounce machinery uses to do its work.

**`BUS_DMASYNC_POSTWRITE`.** The device has finished reading from host memory. On most platforms this is a no-op because no host-visible state needs to change (the CPU already had the data it wrote; the device has finished reading and will not read again; nothing is out of sync). On bounce-buffer platforms, this is the hook where the bounce region may be released back to the pool.

Three observations that surface after the definitions:

- The PRE and POST of a pair are always both called. `PREREAD` is followed later by `POSTREAD`; `PREWRITE` is followed later by `POSTWRITE`. Skipping either breaks the contract.
- On coherent platforms many of these calls are cheap or free. That is an implementation detail the driver does not rely on. The driver writes them because they are required for portability and because bounce-buffer support is transparent only if the calls are there.
- The semantics of PRE and POST are "from the device's viewpoint": PRE means "before the device operation starts"; POST means "after the device operation completes". READ and WRITE are "from the host's viewpoint": READ means "the host is going to read the result"; WRITE means "the host wrote the content".

### The Four Common Transfer Patterns

Four patterns cover almost every DMA transfer a driver performs. Each has its own PRE/POST sequence, and the sequence is predictable once the pattern is recognised.

**Pattern A: Host-to-device data transfer.** The driver fills a buffer with data and asks the device to send it.

```text
CPU fills buffer at KVA
bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)
program device with bus_addr, length, direction
write doorbell
wait for completion
(device has read the buffer)
bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)
CPU may now reuse buffer
```

Example: a driver transmitting a packet to a NIC. The driver copies the packet into a DMA buffer, issues the PREWRITE sync, writes the descriptor, pokes the doorbell, and waits for the transmit-complete interrupt. In the interrupt's task, the driver issues the POSTWRITE sync and returns the buffer to the free pool.

**Pattern B: Device-to-host data transfer.** The driver asks the device to deliver data into a buffer; the driver then reads the buffer.

```text
bus_dmamap_sync(..., BUS_DMASYNC_PREREAD)
program device with bus_addr, length, direction
write doorbell
wait for completion
(device has written the buffer)
bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD)
CPU reads buffer at KVA
```

Example: a driver receiving a packet from a NIC. The driver hands the NIC a buffer's bus address, issues the PREREAD sync, pokes the doorbell, and waits for the receive-complete interrupt. In the interrupt's task, the driver issues the POSTREAD sync and processes the received data.

**Pattern C: Descriptor ring entry.** The driver writes a descriptor that the device reads (sees a new request), and the device later updates the same descriptor with a status field that the driver reads.

```text
CPU writes descriptor fields (bus_addr, length, status = PENDING)
bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE)
device reads descriptor
device processes the transfer
device writes descriptor status = DONE
bus_dmamap_sync(..., BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)
CPU reads descriptor status
```

The combined POST flag is the idiomatic way to pair the host-write-then-host-read when the host is both the producer and the consumer of different fields in the same memory region. The kernel does both operations in one call.

**Pattern D: Bidirectional shared ring.** The driver and the device share a descriptor ring indefinitely. At ring setup, both directions of data flow need to be synchronised.

```text
bus_dmamem_alloc returns the ring's memory
bus_dmamap_sync(..., BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)
(ring is now live; both sides may read and write)
for each ring use:
    bus_dmamap_sync with the appropriate PRE
    doorbell / completion
    bus_dmamap_sync with the appropriate POST
```

Production drivers like `if_re(4)` and `nvme(4)` use this pattern; the combined PRE sync at ring setup is followed by per-transaction PRE/POST pairs. Chapter 21's single-buffer driver does not use this pattern; it is mentioned for completeness and because the reader will see it in real driver code.

### The Simulated Transfer's Sync Sequence

Chapter 21's simulated DMA engine will run both host-to-device and device-to-host transfers. The test sequence the driver performs in Stage 2 is:

```c
/* Host-to-device transfer: driver writes a pattern, engine reads it. */
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
/* program engine */
CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, MYFIRST_DMA_DIR_WRITE);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
/* wait for completion interrupt */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE);

/* Device-to-host transfer: engine writes a pattern, driver reads it. */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREREAD);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, MYFIRST_DMA_DIR_READ);
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
/* wait for completion interrupt */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTREAD);
/* read sc->dma_vaddr and verify */
```

Two transfers, four sync calls, four flags. The order is the test's contract. Section 5 will thread this through the driver's code.

### What Coherence Flags Mean and Do Not Mean

`BUS_DMA_COHERENT` is a flag both `bus_dma_tag_create` and `bus_dmamem_alloc` accept. Its meaning differs subtly between the two, and the difference is worth understanding so the reader knows when to use which.

On `bus_dma_tag_create`, `BUS_DMA_COHERENT` is an **architectural hint** that the driver is willing to spend extra cost at allocation to reduce cost at sync. On arm64, the flag tells the allocator to place the buffer in a memory domain that is inherently coherent with the DMA path, which may be a separate reserved region.

On `bus_dmamem_alloc`, `BUS_DMA_COHERENT` tells the allocator to produce a buffer that does not need cache flushes for sync. On arm and arm64, this uses a different allocator path (uncached or write-combined memory). On amd64, where the PCIe root complex is coherent and DMA is automatically snooped, the flag has little effect but is still passed for portability.

The rule: always pass `BUS_DMA_COHERENT` at allocation for descriptor rings and small control structures that are on the hot path for both the CPU and the device. Do not rely on the flag to remove the need for `bus_dmamap_sync`; the sync is still required by the API contract even if the flag makes it cheap. Some arm64 implementations do require the sync even with `BUS_DMA_COHERENT` (the flag is called a "hint" in the manual page for a reason); the driver is portable only if the sync is always called.

For bulk data buffers (packet payloads, storage blocks), `BUS_DMA_COHERENT` is usually not passed because the cache-hit rate is high when the CPU is about to process the data anyway, and the coherent allocator may use a slower memory domain. Chapter 21 passes `BUS_DMA_COHERENT` on its single small buffer because the chapter's goal is to show the common case; a production driver would make the decision per-buffer based on access pattern.

### What Happens If a Sync Is Omitted

A concrete failure scenario clarifies the abstraction. Suppose the driver performs a host-to-device transfer but forgets the `PREWRITE` sync:

```c
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
/* MISSING: bus_dmamap_sync(..., BUS_DMASYNC_PREWRITE); */
CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);
```

On amd64 with a coherent root complex, the transfer succeeds: the hardware snoops the CPU's cache and the device reads the current contents. The bug is invisible.

On arm64 without a coherent PCIe fabric (some embedded systems), the transfer reads stale data: the CPU's write is still in the cache, the memory controller has old data, and the device reads 0x00 or garbage instead of 0xAA. The bug is visible immediately as data corruption.

On amd64 with bounce buffers (because the device had a 32-bit address limit and the buffer happened to be above 4 GB), the transfer reads stale data from the bounce region: the bounce machinery copies the driver's buffer into the bounce region at PREWRITE time, but without the PREWRITE call the bounce region is uninitialised (zero, or stale from a previous transfer). The bug is visible immediately as data corruption.

The pattern is consistent: the omission makes the driver work on the platform the author tested and fail on platforms with different coherence or addressing semantics. The sync calls are the portability guarantee. A driver that passes its test suite on amd64 and never has the sync calls exercised has a latent bug waiting for a different target.

### Lock Context and Sync Calls

The Chapter 19/20 lock discipline extends naturally to sync calls. The sync itself does not acquire any lock inside `bus_dma`; it is safe to call from any context as long as the caller has the appropriate concurrency discipline for the map.

The relevant rules:

- The same map must not be touched by two threads simultaneously. If two filters on two vectors can both sync the same map, the driver must hold a mutex during the sync. Chapter 21 uses a single map and a single vector's task for completion, so there is no contention on the single-buffer case.
- The sync call may be made from a filter (primary interrupt context), from a task (thread context), or from process context (attach, detach, sysctl handlers). All are safe.
- The sync call must not be made from inside the `bus_dmamap_load` callback. The callback itself is the kernel's signal that the load has completed; the driver syncs later, after the callback returns and after the driver has programmed the device.
- The sync call must be paired with the PRE/POST flag matching the transfer direction, not with a flag matching what the driver "plans to do next". `BUS_DMASYNC_POSTREAD` after a host-to-device transfer is a bug, even if the CPU happens to be about to read the buffer (for verification, say) right after. Use `POSTWRITE` after a host-to-device transfer to match the transfer's direction.

Chapter 21's driver calls sync in three contexts: from `myfirst_dma_do_transfer` (called from process context or from a sysctl handler), from the rx filter (only after confirming the transfer is complete, but the chapter defers the sync to the task to keep the filter minimal), and from the rx task (under the softc mutex, before and after touching the buffer). Section 5 shows the exact placements.

### The First Sync: When to Do It

One subtle question: does the driver need a sync call before the very first use of a freshly allocated buffer?

The answer is yes for bidirectional rings, and the required call is the combined PRE flag at setup time. The reason is that the allocator may not have zeroed the cache lines covering the buffer (even with `BUS_DMA_ZERO` setting the memory to zero, the cache lines may still hold whatever was there before the page was allocated), and without an initial sync the device might read garbage.

For Chapter 21's pattern (host-to-device transfer where the driver fills the buffer before PREWRITE), the initial sync is not needed: the driver writes the buffer, then issues PREWRITE, so the cache state before the write is irrelevant; PREWRITE handles any coherence concerns.

For device-to-host transfers, PREREAD serves the same purpose: the kernel does whatever invalidation is needed before the device writes, so any stale cache state is cleared.

The rule of thumb: as long as every hand-off to the device is preceded by a PRE sync, the buffer's initial state does not matter for correctness. The PRE sync is both an invalidate (for READ) and a flush (for WRITE), so it handles both directions. This is one of the API's small elegances; the driver does not need special "init" sync calls.

### Wrapping Up Section 4

Section 4 pinned down the sync discipline. Four flags (PREREAD, PREWRITE, POSTREAD, POSTWRITE), one mental model (ownership passes between CPU and device), four common patterns (host-to-device, device-to-host, descriptor ring, shared ring), and one rule (every transfer has one PRE call and one POST call, both in the driver's code even if one is a no-op on the current platform). The `BUS_DMA_COHERENT` flag reduces sync cost but does not remove the sync requirement; `bus_dmamap_sync` is always called.

Section 5 builds the simulated DMA engine that will consume these sync calls. The engine accepts a bus address and a length, runs a `callout(9)`-scheduled transfer a few milliseconds later, and sets a completion bit in the status register. The driver's job is to program the engine, wait for completion, and verify the result. Each stage of Section 5 matches one of Section 4's four patterns.



## Section 5: Building a Simulated DMA Engine

Section 4 laid down the sync discipline. Section 5 puts that discipline to work against a concrete DMA engine. The engine lives inside the Chapter 17 simulated backend and is built so the driver can drive it exactly as it would drive a real device. Tag, map, memory, sync, registers, completion, sync, verify: the full loop runs end to end, all on the simulator, inside the `myfirst` kernel module, with no real hardware required.

Stage 2's version tag is `1.4-dma-stage2`. The simulated engine is added to `myfirst_sim.c`. The driver's use of the engine is added to `myfirst.c`. The rx filter from Chapter 20 is unchanged at this stage; Section 6 wires the completion path through it. Here, the driver polls the engine's status register in a task because polling is the simpler path and isolates the DMA mechanics from the interrupt machinery for one stage.

### The DMA Register Set

The simulated backend's MMIO register map from Chapter 17 already includes `INTR_STATUS`, `INTR_MASK`, `DATA_OUT`, `DATA_AV`, and `ERROR` at fixed offsets. Stage 2 extends the map with five new DMA registers at offsets 0x20 through 0x34. The offsets and meanings are chosen to match a typical real device's DMA control block:

| Offset | Name             | R/W | Meaning                                                |
|--------|------------------|-----|--------------------------------------------------------|
| 0x20   | `DMA_ADDR_LOW`   | RW  | Low 32 bits of the DMA bus address.                    |
| 0x24   | `DMA_ADDR_HIGH`  | RW  | High 32 bits (zero on this device's 32-bit engine).    |
| 0x28   | `DMA_LEN`        | RW  | Transfer length in bytes.                              |
| 0x2C   | `DMA_DIR`        | RW  | 0 = host-to-device (engine reads), 1 = device-to-host. |
| 0x30   | `DMA_CTRL`       | RW  | Write 1 = start, write 2 = abort, bit 31 = reset.      |
| 0x34   | `DMA_STATUS`     | RO  | Bit 0 = DONE, bit 1 = ERR, bit 2 = RUNNING.            |

The `MYFIRST_REG_DMA_*` constants live in `myfirst.h`:

```c
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

#define	MYFIRST_DMA_DIR_WRITE		0u
#define	MYFIRST_DMA_DIR_READ		1u

#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)
```

The naming follows the Chapter 17 convention: `MYFIRST_REG_*` for offsets, `MYFIRST_DMA_*` for named bits. The constants are used by both the simulation backend (which implements the engine) and the driver (which programs the engine); the shared header is the single source of truth.

The engine also sets `MYFIRST_INTR_COMPLETE` in `INTR_STATUS` when a transfer finishes, using the existing Chapter 19/20 interrupt mechanism. That is what Section 6 wires up; for Stage 2, the driver polls `DMA_STATUS` directly from a task and does not use the interrupt yet.

### Extending the Simulation State

The simulated backend's `struct myfirst_sim` state grows with new fields to track the DMA engine:

```c
struct myfirst_sim_dma {
	uint32_t	addr_low;
	uint32_t	addr_high;
	uint32_t	len;
	uint32_t	dir;
	uint32_t	ctrl;
	uint32_t	status;
	struct callout	done_co;
	bool		armed;
};
```

The first six fields mirror the register contents. The `done_co` callout is the mechanism the engine uses to simulate transfer latency: when the driver writes `DMA_CTRL_START`, the engine schedules `done_co` to fire a few milliseconds later, which sets `DMA_STATUS_DONE` and raises `MYFIRST_INTR_COMPLETE`. The `armed` bool tracks whether the callout is scheduled so the abort path knows whether to call `callout_stop`.

The new state is embedded in the existing `struct myfirst_sim` structure:

```c
struct myfirst_sim {
	/* ... existing Chapter 17 fields ... */
	struct myfirst_sim_dma	dma;

	/* Backing store for the DMA engine's source or sink. */
	void			*dma_scratch;
	size_t			dma_scratch_size;

	/* Simulation back-channel: the host-visible KVA and bus address
	 * the driver registers at myfirst_dma_setup time. A real device
	 * never needs these; the simulator needs the KVA because it is
	 * software and cannot reach RAM through the memory controller. */
	void			*dma_host_kva;
	bus_addr_t		 dma_host_bus_addr;
	size_t			 dma_host_size;
};
```

`dma_scratch` is a buffer that the engine uses as the other side of the transfer: when the driver programs a host-to-device transfer, the engine reads from the driver's buffer and writes into `dma_scratch`; when the driver programs a device-to-host transfer, the engine reads from `dma_scratch` and writes into the driver's buffer. In a real device, the "other side" is the device's own hardware (the wire, the storage media, the audio output); in the simulation, it is an in-kernel buffer that stands in.

The `dma_host_*` fields are the back-channel mentioned above. They are populated by a new helper `myfirst_sim_register_dma_buffer(sim, kva, bus_addr, size)` called from `myfirst_dma_setup`, and cleared by `myfirst_sim_unregister_dma_buffer` called from `myfirst_dma_teardown`. The helpers sit inside `myfirst_sim.c` and expose the sim's internal state only to the extent the simulator needs; they are the one explicit coupling between the DMA layer and the sim layer.

The scratch buffer is allocated at sim init time:

```c
sc->sim.dma_scratch_size = 4096;
sc->sim.dma_scratch = malloc(sc->sim.dma_scratch_size,
    M_MYFIRST, M_WAITOK | M_ZERO);
```

and freed at sim teardown:

```c
free(sc->sim.dma_scratch, M_MYFIRST);
```

The scratch buffer is not itself a DMA buffer (the engine is simulated; no real DMA happens). It is just kernel memory the simulation uses to model the device's internal storage.

### The Register Access Hook

Chapter 17's simulation backend handles writes and reads on the simulated BAR by intercepting the `bus_read_4` and `bus_write_4` operations. The Stage 2 change extends the write hook to recognise the new DMA registers:

```c
static void
myfirst_sim_write_4(struct myfirst_sim *sim, bus_size_t off, uint32_t val)
{
	switch (off) {
	/* ... existing registers from Chapter 17 ... */
	case MYFIRST_REG_DMA_ADDR_LOW:
		sim->dma.addr_low = val;
		break;
	case MYFIRST_REG_DMA_ADDR_HIGH:
		sim->dma.addr_high = val;
		break;
	case MYFIRST_REG_DMA_LEN:
		sim->dma.len = val;
		break;
	case MYFIRST_REG_DMA_DIR:
		sim->dma.dir = val;
		break;
	case MYFIRST_REG_DMA_CTRL:
		sim->dma.ctrl = val;
		myfirst_sim_dma_ctrl_written(sim, val);
		break;
	default:
		/* ... existing default handling ... */
		break;
	}
}
```

The `MYFIRST_REG_DMA_STATUS` register is read-only, so the read hook handles it; the write hook ignores attempts to write it (or, in a defensive build, logs a warning and refuses). The `DMA_STATUS` read returns the current `sim->dma.status` value directly.

`myfirst_sim_dma_ctrl_written` is the engine's main decision point:

```c
static void
myfirst_sim_dma_ctrl_written(struct myfirst_sim *sim, uint32_t val)
{
	if ((val & MYFIRST_DMA_CTRL_RESET) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status = 0;
		sim->dma.addr_low = 0;
		sim->dma.addr_high = 0;
		sim->dma.len = 0;
		sim->dma.dir = 0;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_ABORT) != 0) {
		if (sim->dma.armed) {
			callout_stop(&sim->dma.done_co);
			sim->dma.armed = false;
		}
		sim->dma.status &= ~MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
		return;
	}
	if ((val & MYFIRST_DMA_CTRL_START) != 0) {
		if ((sim->dma.status & MYFIRST_DMA_STATUS_RUNNING) != 0) {
			/* New START while an old transfer is in flight. */
			sim->dma.status |= MYFIRST_DMA_STATUS_ERR;
			return;
		}
		sim->dma.status = MYFIRST_DMA_STATUS_RUNNING;
		sim->dma.armed = true;
		callout_reset(&sim->dma.done_co, hz / 100,
		    myfirst_sim_dma_done_co, sim);
		return;
	}
}
```

The engine treats RESET, ABORT, and START as three distinct commands and handles each accordingly. The `hz / 100` value schedules the completion roughly ten milliseconds in the future on a 1000 Hz kernel; the delay makes the transfer observably asynchronous and gives the driver a realistic window to poll or wait for the interrupt.

### The Callout Handler

When the callout fires, the engine performs the simulated transfer and sets the status:

```c
static void
myfirst_sim_dma_done_co(void *arg)
{
	struct myfirst_sim *sim = arg;
	bus_addr_t bus_addr;
	uint32_t len;
	void *kva;

	bus_addr = ((bus_addr_t)sim->dma.addr_high << 32) | sim->dma.addr_low;
	len = sim->dma.len;

	/* Back-channel lookup: find the KVA for this bus address. A real
	 * device would not need this; the device's own DMA engine would
	 * perform the memory-controller access. */
	if (sim->dma_host_kva == NULL ||
	    bus_addr != sim->dma_host_bus_addr ||
	    len == 0 || len > sim->dma_host_size ||
	    len > sim->dma_scratch_size) {
		sim->dma.status = MYFIRST_DMA_STATUS_ERR;
		sim->dma.armed = false;
		myfirst_sim_dma_raise_complete(sim);
		return;
	}
	kva = sim->dma_host_kva;

	if (sim->dma.dir == MYFIRST_DMA_DIR_WRITE) {
		/* Host-to-device: sim reads host KVA, writes scratch. */
		memcpy(sim->dma_scratch, kva, len);
	} else {
		/* Device-to-host: sim reads scratch, writes host KVA.
		 * Fill scratch with a recognisable pattern first so the
		 * test can verify the transfer. */
		memset(sim->dma_scratch, 0x5A, len);
		memcpy(kva, sim->dma_scratch, len);
	}

	sim->dma.status = MYFIRST_DMA_STATUS_DONE;
	sim->dma.armed = false;
	myfirst_sim_dma_raise_complete(sim);
}
```

The two helpers `myfirst_sim_dma_copy_from_host` and `myfirst_sim_dma_copy_to_host` do the actual byte movement. Here the simulation runs into a fundamental limit: it is software running inside the kernel, not a real device. A real device uses the bus address to reach RAM through the memory controller; a software simulator cannot physically take that path. The simulator needs a kernel virtual address it can dereference, not the bus address.

We solve this with a deliberate back-channel. At `myfirst_dma_setup` time, the driver calls a new helper `myfirst_sim_register_dma_buffer(sc, sc->dma_vaddr, sc->dma_bus_addr, MYFIRST_DMA_BUFFER_SIZE)`. The helper stores the (KVA, bus address, size) triple inside the sim's state. When the callout later fires, it reads the bus address the driver programmed into `DMA_ADDR_LOW`/`DMA_ADDR_HIGH`, looks it up in the stored triple, and recovers the KVA. The `memcpy` then works against the KVA.

This back-channel is a simulation-only crutch. Real hardware never receives a KVA; the device only ever uses the bus address, and the memory controller resolves it through the platform's address mapping (possibly via an IOMMU). The back-channel's sole purpose is to let the simulator pretend to do the DMA without actually programming the memory controller. Keeping the mechanism explicit and named is the best defence against confusion: the reader sees a clearly-labelled simulation bridge rather than a magical pointer cast, and the divide between "what the device sees" (the bus address) and "what the CPU sees" (the KVA) stays clean.

`myfirst_sim_dma_raise_complete` sets `MYFIRST_INTR_COMPLETE` in `INTR_STATUS` and, if `INTR_MASK` has the bit set, fires the simulated interrupt through the existing Chapter 19/20 path:

```c
static void
myfirst_sim_dma_raise_complete(struct myfirst_sim *sim)
{
	sim->intr_status |= MYFIRST_INTR_COMPLETE;
	if ((sim->intr_mask & MYFIRST_INTR_COMPLETE) != 0)
		myfirst_sim_raise_intr(sim);
}
```

For Stage 2 we do not enable the `MYFIRST_INTR_COMPLETE` bit in `INTR_MASK` yet; the driver will poll. Section 6 enables the bit and wires the completion path through the filter.

### The Driver's Side: Programming the Engine

On the driver side, a single helper function programs the engine and waits:

```c
int
myfirst_dma_do_transfer(struct myfirst_softc *sc, int direction,
    size_t length)
{
	uint32_t status;
	int timeout;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	if (direction == MYFIRST_DMA_DIR_WRITE) {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);
	}

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFF));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	/* Poll for completion. Stage 2 is polling-only. */
	for (timeout = 500; timeout > 0; timeout--) {
		pause("dma", hz / 1000); /* 1 ms. */
		status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
		if ((status & MYFIRST_DMA_STATUS_DONE) != 0)
			break;
		if ((status & MYFIRST_DMA_STATUS_ERR) != 0) {
			if (direction == MYFIRST_DMA_DIR_WRITE)
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTWRITE);
			else
				bus_dmamap_sync(sc->dma_tag, sc->dma_map,
				    BUS_DMASYNC_POSTREAD);
			return (EIO);
		}
	}

	if (timeout == 0) {
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		return (ETIMEDOUT);
	}

	/* Acknowledge the completion. */
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);

	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	return (0);
}
```

The function is sixty lines, and the shape is worth reading twice. The PRE sync happens once at the top; the POST sync happens exactly once on every return path (success, error, timeout). The polling loop uses `pause("dma", hz / 1000)` for one-millisecond sleeps; on a 1000 Hz kernel this gives five hundred one-millisecond tries before giving up at five hundred milliseconds total. `pause(9)` takes the global sleep mutex briefly but is safe from a process-context driver helper. A real device with an interrupt path would not poll; Section 6 replaces the polling with an interrupt wait.

The PRE and POST flag choice matches the direction. For host-to-device (DIR_WRITE), the driver has just written the buffer, so PREWRITE is required; after the transfer, POSTWRITE releases any bounce region. For device-to-host (DIR_READ), the driver will read the buffer, so PREREAD is required; after the transfer, POSTREAD completes the copy from bounce region (if any) and invalidates the cache (if needed).

### Exercising the Engine From User Space

The driver already exposes a sysctl tree under `dev.myfirst.N.` from Chapter 20. Stage 2 adds two new write-only sysctls that trigger a transfer:

```c
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
    myfirst_dma_sysctl_test_write, "IU",
    "Trigger a host-to-device DMA transfer");
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
    myfirst_dma_sysctl_test_read, "IU",
    "Trigger a device-to-host DMA transfer");
```

The handler for `dma_test_write`:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev, "dma_test_write: error %d\n", err);
	else
		device_printf(sc->dev,
		    "dma_test_write: pattern 0x%02x transferred\n",
		    pattern & 0xFF);
	return (err);
}
```

The user writes an integer to `dev.myfirst.0.dma_test_write`, the low byte becomes a fill pattern, the driver fills the buffer with that pattern, programs the engine, waits for completion, and logs the result. The handler for `dma_test_read` is symmetric: it triggers a device-to-host transfer, waits for completion, reads the first few bytes of the buffer, and logs them.

A user-space session runs like this:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0xAA
sudo dmesg | tail -1
# myfirst0: dma_test_write: pattern 0xaa transferred

sudo sysctl dev.myfirst.0.dma_test_read=1
sudo dmesg | tail -1
# myfirst0: dma_test_read: first bytes 5A 5A 5A 5A 5A 5A 5A 5A
```

The first transfer sends 0xAA to the simulated device; the second receives the device's 0x5A pattern back. Both transfers exercise the full PRE/POST discipline.

### Observability: Per-Transfer Counters

Stage 2 adds four softc counters to the DMA state:

```c
uint64_t dma_transfers_write;
uint64_t dma_transfers_read;
uint64_t dma_errors;
uint64_t dma_timeouts;
```

Each is incremented by `myfirst_dma_do_transfer` on the appropriate exit path, and each is exposed as a read-only sysctl. The counters are the driver's Stage 2 equivalent of the per-vector counters Chapter 20 added to the interrupt path; they make it easy to confirm at a glance that transfers are happening as expected and to detect silent failures.

The counters are read with atomic operations (`atomic_add_64`) so they can be updated safely even if Section 6's interrupt-driven completion path ends up running from a filter context. Chapter 20's Section 4 covered the atomic discipline; Stage 2's counters follow the same pattern.

### Verifying Stage 2

Build and load:

```sh
make clean && make
sudo kldload ./myfirst.ko
```

`dmesg` shows the DMA banner from Stage 1 plus the new DMA-enabled banner:

```text
myfirst0: DMA buffer 4096 bytes at KVA 0xfffffe... bus addr 0x...
myfirst0: DMA engine present, scratch 4096 bytes
myfirst0: interrupt mode: MSI-X, 3 vectors
```

Run the test:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0x33
sudo sysctl dev.myfirst.0.dma_test_read=1
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
```

Expected:

```text
dev.myfirst.0.dma_transfers_write: 1
dev.myfirst.0.dma_transfers_read: 1
```

Run it a thousand times in a loop:

```sh
for i in $(seq 1 1000); do
  sudo sysctl dev.myfirst.0.dma_test_write=$((i & 0xFF)) >/dev/null
  sudo sysctl dev.myfirst.0.dma_test_read=1 >/dev/null
done
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
sudo sysctl dev.myfirst.0.dma_errors
sudo sysctl dev.myfirst.0.dma_timeouts
```

Expected counts: 1000 write transfers, 1000 read transfers, 0 errors, 0 timeouts. Any timeout or error during the test indicates a bug in the simulation or a race in the driver; both are worth catching early.

Unload the module and verify that the callout is stopped, the scratch buffer is freed, and the tag is destroyed:

```sh
sudo kldunload myfirst
vmstat -m | grep myfirst || true
```

The line should be missing or show zero allocations.

### What Stage 2 Does and Does Not Do

Stage 2 is the first stage where actual DMA transfers run. The driver programs the engine; the engine copies bytes through the simulated path; the driver observes the result. The sync discipline is exercised in both directions.

What Stage 2 does not do is use the interrupt path. Polling is acceptable for a teaching stage but inappropriate for a real driver: the driver holds a kernel thread in `pause` for the duration of each transfer. Section 6 replaces the poll with the Chapter 19/20 interrupt mechanism; the rx filter sees `MYFIRST_INTR_COMPLETE`, acknowledges it, enqueues the task, and the task does the POST sync and verification. Stage 3 is the fully interrupt-driven version.

Stage 2 also does not handle every error cleanly. The timeout and abort paths are present but minimal; Section 7 walks through them carefully and extends the recovery logic. The single-transfer pattern is also limited; Section 8's refactor exposes helper functions that make multi-transfer patterns easy to add.

### Common Mistakes in Stage 2

Four mistakes are worth calling out.

**Mistake one: calling `bus_dmamap_sync` inside the callout.** The callout runs in softclock context, and the sync itself is safe from there, but the buffer accesses the simulation does (`myfirst_sim_dma_copy_from_host`) need the driver's map to be loaded. If the driver detaches while a callout is armed, the map is unloaded under the callout's feet. The fix is Chapter 21's detach pattern: stop the callout (`callout_drain`) before unloading the map. Section 7 revisits this carefully.

**Mistake two: forgetting to acknowledge `MYFIRST_INTR_COMPLETE`.** The simulation raises the bit; the driver must clear it by writing the bit back to `INTR_STATUS`. If the driver does not, the next transfer sees a stale DONE bit and the polling loop returns immediately with correct status but before the new transfer has run. The acknowledge is in the polling-loop version (`CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);`); Section 6 moves the acknowledge into the filter path where interrupt driven.

**Mistake three: using `DELAY` or `cpu_spinwait` instead of `pause`.** The Stage 2 polling loop uses `pause("dma", hz / 1000)` to yield. A tight spin with `DELAY(1000)` pegs a CPU for the entire transfer window, which is acceptable on a uniprocessor test but unacceptable in production. The `pause` call puts the thread to sleep and lets other work run. This matters more in the Section 6 version, where the driver calls `cv_timedwait` instead of polling, but the principle is the same: yield the CPU whenever you are waiting for external events.

**Mistake four: forgetting to cap `length` to the tag's `maxsize`.** The driver's helper accepts a `length` argument; if the caller passes a larger value than `MYFIRST_DMA_BUFFER_SIZE`, the engine writes past the end of the scratch buffer (in the simulation) or past the end of the host buffer (on real hardware). Chapter 21's code guards against this in the helper's first lines; forgetting the guard is a common source of subtle corruption.

### Wrapping Up Section 5

Section 5 made the simulated DMA engine real. The register map, the state machine, the callout-based latency, the driver's polling helper, the sysctl test interface, and the counters are all in place. The driver runs full transfers in both directions; the sync discipline from Section 4 is exercised on every one. Stage 2 is a working DMA driver even if it polls rather than handling interrupts.

Section 6 replaces the polling with the Chapter 19/20 interrupt path. The completion interrupt arrives through the rx filter, the filter enqueues the task, and the task performs the POST sync and the verification. The user-visible behaviour does not change; the driver's internals do. That change is what makes the driver scale: a polling driver pegs a CPU per transfer, an interrupt-driven driver handles the same transfers with negligible CPU cost.



## Section 6: Handling DMA Completion in the Driver

Section 5 produced a polling-based DMA driver. Section 6 rewrites the completion path to use the Chapter 19/20 interrupt machinery. The goals are: no CPU is held in `pause` while a transfer runs; the completion is delivered through a filter that runs in primary interrupt context; the task in thread context does the POST sync and the verification; and the user-visible behaviour is unchanged from Stage 2. The version tag becomes `1.4-dma-stage3`.

The Chapter 20 per-vector machinery already has the right shape for this. One of the three vectors (the rx vector, or in the legacy fallback case the single admin vector) handles `MYFIRST_INTR_COMPLETE`. The filter acknowledges; the task processes. Chapter 21's change is to make the task's processing include a DMA POST sync and a buffer read.

### Why Interrupt-Driven Is the Right Design

Section 5's polling path works, but it has four weaknesses that Stage 3 fixes.

**One CPU is tied up per in-flight transfer.** The `pause` call puts the thread to sleep on the `dma` wait channel, which is fine, but the thread's stack, cache state, and scheduling overhead still exist. A driver with many in-flight transfers would tie up many threads. Interrupt-driven completion uses one task per vector, reused across all transfers.

**Latency is coarse.** The polling loop wakes every millisecond. A real engine might complete in microseconds; the driver waits up to a full millisecond before noticing. Interrupt-driven completion is delivered at the hardware's natural latency.

**The polling loop does not compose with multiple in-flight transfers.** To issue two transfers back-to-back, the driver must either wait for the first to complete (serialising) or maintain its own per-transfer state (ad-hoc bookkeeping). Interrupt-driven completion naturally composes: each completion fires its own interrupt, the filter routes it to the task, and the task handles it.

**Polling cannot run in interrupt context.** If the driver ever needs to complete a transfer from inside an interrupt handler (for example, a receive-complete interrupt that immediately triggers a new descriptor fill), the polling loop will not work because `pause` cannot be called from the filter. The interrupt-driven path is the only composable design.

Stage 3 keeps the polling helper as a fallback for sysctl-driven tests (it is useful for deterministic testing) and adds an interrupt-driven helper for the real use case.

### The Interrupt Path Modifications

Chapter 20's rx filter handled `MYFIRST_INTR_DATA_AV`. Stage 3 extends it (or the legacy-fallback admin filter) to handle `MYFIRST_INTR_COMPLETE` as well:

```c
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;
	bool handled = false;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);

	if ((status & MYFIRST_INTR_DATA_AV) != 0) {
		atomic_add_64(&vec->fire_count, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		atomic_add_64(&sc->intr_data_av_count, 1);
		handled = true;
	}

	if ((status & MYFIRST_INTR_COMPLETE) != 0) {
		atomic_add_64(&sc->dma_complete_intrs, 1);
		ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		handled = true;
	}

	if (!handled) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}
```

The filter reads `INTR_STATUS` once, checks for both bits, acknowledges each one it sees, increments per-bit counters, and enqueues the task if any bit was set. The pattern is exactly Chapter 19's generalised to two bits per vector; the only new thing is the `MYFIRST_INTR_COMPLETE` counter.

The task picks up the state the filter recorded:

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	bool did_complete;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}

	/* Data-available path (Chapter 19/20). */
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);

	/* DMA-complete path (Chapter 21). */
	did_complete = false;
	if (sc->dma_in_flight) {
		sc->dma_in_flight = false;
		did_complete = true;
		if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_last_status = CSR_READ_4(sc,
		    MYFIRST_REG_DMA_STATUS);
		atomic_add_64(&sc->dma_complete_tasks, 1);
		cv_broadcast(&sc->dma_cv);
	}
	MYFIRST_UNLOCK(sc);

	(void)did_complete;
}
```

The task takes the softc mutex (Chapter 11 discipline; the sleep mutex is held while accessing shared state). The `dma_in_flight` flag, set by the transfer helper before the doorbell write, tells the task that a DMA completion is pending. If it is set, the task clears it, issues the POST sync matching the recorded direction, reads the final status, and broadcasts `dma_cv` so any waiter can wake up.

The sync call from the task is safe: the task runs in thread context under the softc mutex, and the sync call does not acquire any extra locks. The Chapter 19 lock discipline carries through.

### The New Interrupt-Driven Transfer Helper

The Stage 3 version of the helper starts the transfer, arms the callout or the real hardware, and sleeps until the completion task signals:

```c
int
myfirst_dma_do_transfer_intr(struct myfirst_softc *sc, int direction,
    size_t length)
{
	int err;

	if (length == 0 || length > MYFIRST_DMA_BUFFER_SIZE)
		return (EINVAL);

	MYFIRST_LOCK(sc);
	if (sc->dma_in_flight) {
		MYFIRST_UNLOCK(sc);
		return (EBUSY);
	}

	/* Issue the PRE sync before touching the device. */
	if (direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_PREREAD);

	sc->dma_last_direction = direction;
	sc->dma_in_flight = true;

	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_LOW,
	    (uint32_t)(sc->dma_bus_addr & 0xFFFFFFFF));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_ADDR_HIGH,
	    (uint32_t)(sc->dma_bus_addr >> 32));
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_LEN, (uint32_t)length);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_DIR, direction);
	CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_START);

	/* Wait for the task to set dma_in_flight = false. */
	err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz); /* 1 s timeout */
	if (err == EWOULDBLOCK) {
		/* Abort the engine and issue the POST sync so we do not
		 * leave the map in an inconsistent state. */
		CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
		if (direction == MYFIRST_DMA_DIR_WRITE)
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTWRITE);
		else
			bus_dmamap_sync(sc->dma_tag, sc->dma_map,
			    BUS_DMASYNC_POSTREAD);
		sc->dma_in_flight = false;
		atomic_add_64(&sc->dma_timeouts, 1);
		MYFIRST_UNLOCK(sc);
		return (ETIMEDOUT);
	}

	/* The task has issued the POST sync and recorded dma_last_status. */
	if ((sc->dma_last_status & MYFIRST_DMA_STATUS_ERR) != 0) {
		atomic_add_64(&sc->dma_errors, 1);
		MYFIRST_UNLOCK(sc);
		return (EIO);
	}

	if (direction == MYFIRST_DMA_DIR_WRITE)
		atomic_add_64(&sc->dma_transfers_write, 1);
	else
		atomic_add_64(&sc->dma_transfers_read, 1);

	MYFIRST_UNLOCK(sc);
	return (0);
}
```

The helper is longer than Stage 2's because it coordinates with the task: the task issues the POST sync; the helper only does so on the timeout path where the task will never see completion. The coordination is the point of the redesign; the helper has moved from "poll until done" to "signal the device, wait on a condition variable, let the task complete me".

The `cv_timedwait` call takes the softc mutex as its second argument; that is the standard `cv_timedwait` contract (mutex held going in, released for the wait, re-acquired on wake). The mutex scope covers every access to the shared DMA state (`dma_in_flight`, `dma_last_direction`, `dma_last_status`).

One subtle point: the PRE sync runs before the mutex is released for the wait, and the POST sync runs after the wake. Both are inside the critical section of the transfer, so no other thread can see the buffer in a partially-synchronised state. This is the locking payoff of Chapter 11.

### The `MYFIRST_INTR_COMPLETE` Bit Goes Live

Stage 2 masked `MYFIRST_INTR_COMPLETE` out of `INTR_MASK`. Stage 3 enables it:

```c
CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR | MYFIRST_INTR_COMPLETE);
```

The change is a single line in the interrupt setup. The simulation's engine raises the bit at completion; the driver's filter sees it and enqueues the task; the task handles it. The entire pipeline lights up with this one change.

### Verifying Stage 3

Build and load:

```sh
make clean && make
sudo kldload ./myfirst.ko
```

Run the same sysctl-driven test as Stage 2, but wire it to the interrupt-driven helper:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	unsigned int pattern;
	int err;

	err = sysctl_handle_int(oidp, &pattern, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	memset(sc->dma_vaddr, (int)(pattern & 0xFF),
	    MYFIRST_DMA_BUFFER_SIZE);
	err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
	    MYFIRST_DMA_BUFFER_SIZE);
	if (err != 0)
		device_printf(sc->dev,
		    "dma_test_write: intr err %d\n", err);
	return (err);
}
```

Run a thousand transfers and observe the counters:

```sh
for i in $(seq 1 1000); do
  sudo sysctl dev.myfirst.0.dma_test_write=$((i & 0xFF)) >/dev/null
  sudo sysctl dev.myfirst.0.dma_test_read=1 >/dev/null
done
sudo sysctl dev.myfirst.0.dma_complete_intrs
sudo sysctl dev.myfirst.0.dma_complete_tasks
sudo sysctl dev.myfirst.0.dma_transfers_write
sudo sysctl dev.myfirst.0.dma_transfers_read
```

Expected: `dma_complete_intrs` equals 2000 (one per transfer in both directions), `dma_complete_tasks` equals 2000 (the task processed each completion), `dma_transfers_write` and `dma_transfers_read` each equal 1000.

Run `vmstat -i` during the test to see the vector receiving interrupts. With MSI-X, the rx vector's count should climb visibly. With legacy fallback, the single vector's count climbs.

### Stopping In-Flight Transfers at Detach

Stage 3 has a new detach hazard: the task may be running the POST sync at the moment `myfirst_dma_teardown` is called. The teardown must not unload the map while the task is still holding references to it.

The fix is one line added to the detach path before `myfirst_dma_teardown`:

```c
taskqueue_drain(sc->intr_tq, &sc->vectors[MYFIRST_VECTOR_RX].task);
```

`taskqueue_drain` waits for the task to finish running; after it returns, the driver is guaranteed that no further task invocations will happen on that task (subject to the caveat that another filter might re-enqueue it; the detach path has already masked `INTR_MASK` so no more interrupts will fire). With the drain in place, the teardown is safe.

The MSI-X teardown from Chapter 20 already drains the task; Chapter 21 extends the Stage 3 teardown to also stop the simulation callout before unloading the map:

```c
/* In myfirst_sim_destroy, called from detach: */
callout_drain(&sc->sim.dma.done_co);
```

`callout_drain` waits for the callout to finish running; after it returns, the engine is guaranteed not to raise any more completions. Combined with the taskqueue drain, the detach is safe against every race.

### What Stage 3 Produces

Stage 3 is a fully interrupt-driven DMA driver. Every transfer programs the engine, sleeps on a condition variable, wakes when the completion task signals, and returns with the transferred data synced and verified. The driver never holds the CPU while a transfer runs. The sysctl test interface works as before; the internals match production DMA code.

Stage 3 does not yet cover error recovery in depth. Section 7 walks through the specific failure modes and the patterns that handle each.

### Common Mistakes in Stage 3

Four more mistakes are worth pointing out.

**Mistake one: forgetting the `cv_broadcast` in the task.** If the task does the POST sync but does not broadcast `dma_cv`, the helper waits the full timeout and returns `ETIMEDOUT` even though the transfer succeeded. The fix is the single `cv_broadcast` line; missing it is a subtle bug that manifests as every transfer timing out.

**Mistake two: calling `cv_timedwait` without holding the mutex.** `cv_timedwait` requires the mutex to be held at entry; the kernel panics under `INVARIANTS` if it is not. The helper's structure (lock, do work, wait, handle result, unlock) keeps the mutex held throughout; the wait releases it briefly during the sleep and re-acquires it on wake. Breaking this pattern (releasing the mutex before `cv_timedwait`) is a race that `WITNESS` catches.

**Mistake three: handling the timeout path without a POST sync.** The timeout path in the helper aborts the engine and then still issues the POST sync. The sync is required because `BUS_DMA_NOWAIT` may have inserted bounce buffers at PRE time; without POST, the bounce region is not released. Forgetting it leaks bounce pages slowly.

**Mistake four: leaving `dma_in_flight` set on an error.** Every exit path in the helper clears `dma_in_flight`. Forgetting to clear it on the error path means the next transfer returns `EBUSY` even though no transfer is running. The helper's structure (set at the top, cleared either by the task or explicitly on timeout/error) is the robust pattern.

### Wrapping Up Section 6

Section 6 replaced Stage 2's polling with Stage 3's interrupt-driven completion. The filter sees the completion bit, the task issues the POST sync and signals, the helper wakes and returns. Throughput and latency both improve; CPU cost drops; the driver composes with other work naturally. The code is longer than Stage 2's because it coordinates across two contexts (task and helper), but the pattern is a direct application of Chapter 11's lock discipline and Chapter 20's per-vector task design; no new ideas are needed beyond the DMA-specific sync placement.

Section 7 is the error-handling tour. Every failure mode the chapter addresses gets a walkthrough: what happens, what the symptom looks like, and what the driver does about it. The patterns are the same ones every production DMA driver has to handle; the chapter's simulated environment is a good place to rehearse them.



## Section 7: DMA Error Handling and Edge Cases

Section 6 produced a working interrupt-driven DMA driver. Section 7 is the chapter where the driver stops assuming success. Every step of the DMA pipeline can fail: the tag creation can fail, the memory allocation can fail, the load can fail, the load can be deferred, the engine can report an error, the engine can never complete (timeout), the detach can race with a running transfer. Each failure mode has a pattern that handles it; Section 7 walks through each in turn.

The goal is not to make Chapter 21's driver resistant to every conceivable failure; it is to teach the patterns so the reader recognises them in production code and applies them when writing real drivers. Many of the patterns are short; the explanation is the learning object, not the code.

### Failure Mode 1: `bus_dma_tag_create` Returns `ENOMEM`

`bus_dma_tag_create` can return `ENOMEM` if the kernel cannot allocate the tag structure itself. The allocation is small (the tag is a few hundred bytes) and only happens once at attach, so the failure is rare in practice, but the driver must still handle it.

The pattern: check the return value, log, return the error to the caller, do not touch any of the downstream DMA state. The Stage 1 code already does this:

```c
err = bus_dma_tag_create(bus_get_dma_tag(sc->dev), ...);
if (err != 0) {
    device_printf(sc->dev, "bus_dma_tag_create failed: %d\n", err);
    return (err);
}
```

The higher-level attach function sees the error, runs its failure-path unwind, and the kernel reports the probe failure cleanly. No DMA resources are leaked because none were allocated.

A `BUS_DMA_ALLOCNOW` flag tells the kernel to pre-allocate bounce buffer resources at tag creation time, so some kinds of allocation failure move from load time to tag time. This is useful for drivers that cannot tolerate a later load failure; Chapter 21's driver does not use it (the simulation does not need bounce buffers), but production drivers that interface with 32-bit hardware often do.

### Failure Mode 2: `bus_dmamem_alloc` Returns `ENOMEM`

`bus_dmamem_alloc` can return `ENOMEM` if the allocator cannot satisfy the tag's constraints at the moment of the call. For large buffers, this is more likely than it sounds: a request for a contiguous four-megabyte buffer with a 4 KB alignment on a fragmented system can genuinely fail. For Chapter 21's 4 KB buffer, the failure is extremely unlikely, but the code still checks.

The pattern: check, log, destroy the tag that was just created, propagate the error:

```c
err = bus_dmamem_alloc(sc->dma_tag, &sc->dma_vaddr,
    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->dma_map);
if (err != 0) {
    device_printf(sc->dev, "bus_dmamem_alloc failed: %d\n", err);
    bus_dma_tag_destroy(sc->dma_tag);
    sc->dma_tag = NULL;
    return (err);
}
```

The key point is the tag destroy in the error path. Forgetting it leaks the tag across every failed attach; over many retries it becomes a noticeable leak. The Stage 1 code follows this pattern; every new allocation-path call in later sections must too.

### Failure Mode 3: `bus_dmamap_load` Returns `EINVAL`

`bus_dmamap_load` returns `EINVAL` in two related cases:

1. The buffer is larger than the tag's `maxsize`. For example, loading an 8 KB buffer with a tag whose `maxsize` is 4 KB fails immediately with `EINVAL`. The callback is called with `error = EINVAL` as a confirmation.
2. The buffer's properties violate the tag's constraints in a way the kernel can detect statically (alignment, boundary). These are rarer because the tag's constraints usually match the buffer's characteristics by design.

The pattern: check, log, free the memory, destroy the tag, propagate:

```c
err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &sc->dma_bus_addr, BUS_DMA_NOWAIT);
if (err != 0) {
    device_printf(sc->dev, "bus_dmamap_load failed: %d\n", err);
    bus_dmamem_free(sc->dma_tag, sc->dma_vaddr, sc->dma_map);
    sc->dma_vaddr = NULL;
    bus_dma_tag_destroy(sc->dma_tag);
    sc->dma_tag = NULL;
    return (err);
}
```

The Stage 1 code has this pattern. Note that `EINVAL` is distinct from `EFBIG`: `EINVAL` is passed to the callback if the arguments are invalid; `EFBIG` comes back in the callback when the mapping cannot be achieved within the tag's segment constraints. Chapter 21's single-segment load is too simple to see `EFBIG`; scatter-gather loads of large buffers can.

### Failure Mode 4: `bus_dmamap_load` Returns `EINPROGRESS`

`EINPROGRESS` means the kernel has queued the load because bounce buffers (or other mapping resources) are not currently available. The callback will run later, in a different context, when resources free up. Chapter 21 uses `BUS_DMA_NOWAIT`, which forbids this behaviour (the kernel returns `ENOMEM` instead), so `EINPROGRESS` does not occur in the chapter's driver. A driver that uses `BUS_DMA_WAITOK` or zero flags and sees `EINPROGRESS` has to do more work:

```c
err = bus_dmamap_load(sc->dma_tag, sc->dma_map, buf, len,
    my_callback, sc, 0);
if (err == EINPROGRESS) {
    /* Do not free buf or destroy the tag here; the callback will
     * run later. The caller must be prepared to handle the load
     * completing at any time. */
    return (0);
}
```

The driver then has to arrange for the callback to record the result in a place the rest of the driver can see (often a softc field guarded by the tag's lockfunc), and the caller that is "waiting" for the load has to sleep on a condition variable the callback broadcasts. This is the deferred-load pattern; it is complex enough that most drivers prefer `BUS_DMA_NOWAIT` and retry-on-failure over the deferred path.

Chapter 21's driver does not use this pattern. Section 7 mentions it for completeness; the reader who encounters `EINPROGRESS` in another driver will know what it means and where to look.

### Failure Mode 5: Engine Reports Partial Transfer

The simulated engine does not report partial transfers in its baseline behaviour; it either completes the full `DMA_LEN` or sets `DMA_STATUS_ERR`. Real engines sometimes report a partial transfer: the engine copied fewer bytes than requested and signalled completion with the partial count in a length register.

A driver that sees a partial transfer must decide what to do: retry, treat as fatal, pass the partial result to user space with an error flag. The chapter's simulation can be extended to model partial transfers by having the callout set a smaller transferred-length field:

```c
uint32_t actual_len = len;
/* For the lab in Section 7, force a partial transfer every 100 tries. */
if ((sim->dma_transfer_count++ % 100) == 0)
    actual_len = len / 2;
/* ... perform memcpy of actual_len bytes ... */
sim->dma.transferred_len = actual_len;
sim->dma.status = MYFIRST_DMA_STATUS_DONE;
```

The driver reads the length after the sync:

```c
uint32_t xferred;
xferred = CSR_READ_4(sc, MYFIRST_REG_DMA_XFERRED);
if (xferred < expected_len) {
    device_printf(sc->dev,
        "partial DMA: requested %u, got %u\n",
        expected_len, xferred);
    atomic_add_64(&sc->dma_partials, 1);
    /* Decide: retry, report, ignore. */
}
```

Chapter 21's baseline code does not implement partial-transfer reporting; the simulated engine always completes the full length. Lab 5 in the Section 7 challenges adds the partial-transfer behaviour and walks through a retry strategy. The pattern is what matters, not the specific `xferred` register; every partial transfer has the same driver-side shape.

### Failure Mode 6: Engine Never Signals Completion (Timeout)

The engine might set `STATUS_RUNNING` and never advance to `DONE`. In the simulation this happens if the callout is dropped (for example, the callout pool is exhausted or the callout is cancelled by a different path). On real hardware it happens if the device is wedged, the PCIe link is down, or the firmware has a bug.

Chapter 21's interrupt-driven helper (`myfirst_dma_do_transfer_intr`) uses `cv_timedwait` with a one-second timeout. If the wait returns `EWOULDBLOCK`, the transfer is considered hung. The driver's response:

```c
if (err == EWOULDBLOCK) {
    CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL, MYFIRST_DMA_CTRL_ABORT);
    if (direction == MYFIRST_DMA_DIR_WRITE)
        bus_dmamap_sync(sc->dma_tag, sc->dma_map,
            BUS_DMASYNC_POSTWRITE);
    else
        bus_dmamap_sync(sc->dma_tag, sc->dma_map,
            BUS_DMASYNC_POSTREAD);
    sc->dma_in_flight = false;
    atomic_add_64(&sc->dma_timeouts, 1);
    MYFIRST_UNLOCK(sc);
    return (ETIMEDOUT);
}
```

Four things happen: the engine is aborted (so it does not complete after the helper returns), the POST sync is issued (so the map is not left in a PRE state), `dma_in_flight` is cleared (so the next transfer can start), a counter is incremented (for observability). The helper then returns `ETIMEDOUT` to the caller.

A real driver would also consider whether to reset the device at this point. If timeouts are rare, abort-and-continue is appropriate; if they are frequent, a full reset may be needed. Chapter 21's pattern is abort-and-continue; the reset path is a Section 8 refactor exercise.

The chapter's simulation can be extended to never complete, to let the reader exercise the timeout path:

```c
/* Comment out the callout_reset to make transfers hang: */
// callout_reset(&sim->dma.done_co, hz / 100, myfirst_sim_dma_done_co, sim);
```

After this change, every transfer hits the timeout. The driver's counters climb, the caller sees `ETIMEDOUT`, and `kldunload` still succeeds (because the abort path cleared `dma_in_flight` and the teardown has no in-flight transfers to wait for). Running this experiment once gives the reader a concrete sense of what a "stuck device" driver looks like from the outside.

### Failure Mode 7: Detach While a Transfer Is In Flight

The detach path has to handle the case where a transfer is in flight at the moment the driver is unloaded. The risks are:

1. The callout fires after `myfirst_dma_teardown` has unloaded the map, writing to freed memory.
2. The task runs after `myfirst_dma_teardown` has destroyed the tag, syncing against a freed tag.
3. The helper is waiting on `dma_cv` at the moment the device is disappearing.

The Chapter 20 teardown ordering already addresses several of these. Chapter 21 adds two more barriers:

```c
void
myfirst_detach_dma_path(struct myfirst_softc *sc)
{
    /* 1. Tell callers no new transfers may start. */
    MYFIRST_LOCK(sc);
    sc->detaching = true;
    MYFIRST_UNLOCK(sc);

    /* 2. If a transfer is in flight, wait for it to complete or time out. */
    MYFIRST_LOCK(sc);
    while (sc->dma_in_flight) {
        cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
    }
    MYFIRST_UNLOCK(sc);

    /* 3. Mask the completion interrupt at the device. */
    CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
        CSR_READ_4(sc, MYFIRST_REG_INTR_MASK) &
        ~MYFIRST_INTR_COMPLETE);

    /* 4. Drain callouts and tasks. */
    callout_drain(&sc->sim.dma.done_co);
    taskqueue_drain(sc->intr_tq, &sc->vectors[MYFIRST_VECTOR_RX].task);

    /* 5. Tear down the DMA resources. */
    myfirst_dma_teardown(sc);
}
```

The five steps are in the order that keeps each subsequent step safe. Setting `detaching = true` prevents new transfers from starting (the helper checks it before arming). Waiting for in-flight transfers to finish or time out ensures no callout is scheduled and no task is pending. Masking the interrupt prevents any late completion from firing the filter. Draining the callout and the task ensures that any in-progress callback has finished. Tearing down the DMA resources is safe because nothing else can touch them now.

The first step (setting `detaching = true`) requires the transfer helper to check the flag:

```c
if (sc->detaching) {
    MYFIRST_UNLOCK(sc);
    return (ENXIO);
}
```

This is a small addition to the helper and prevents the rare race where a user-space test fires a transfer while detach is running.

### Failure Mode 8: Bounce Buffer Exhaustion

On 32-bit systems or on 64-bit systems with 32-bit-only devices, `bus_dma` may need to allocate bounce buffers to satisfy the address constraint. The bounce pool has a fixed size; if it is exhausted, `bus_dmamap_load` with `BUS_DMA_NOWAIT` returns `ENOMEM`.

Chapter 21's simulation has no real address constraints and the chapter's primary path does not hit this mode. Production drivers for 32-bit-capable devices on 64-bit systems must handle it:

```c
err = bus_dmamap_load(sc->dma_tag, map, buf, len,
    my_callback, sc, BUS_DMA_NOWAIT);
if (err == ENOMEM) {
    /* The bounce pool is exhausted. Options:
     * - Retry later (queue the request).
     * - Fail this transfer and let the caller retry.
     * - Allocate a fresh buffer inside the device's address range. */
    ...
}
```

The practical mitigation is usually to allocate all buffers inside the device's addressable range (so bounce buffers are never needed). That is what `bus_dmamem_alloc` with an appropriate tag does automatically: the allocator sees the tag's `highaddr` (or `lowaddr`, depending on which side of the window the address constraint is expressed) and allocates inside the range. A driver that uses `bus_dmamem_alloc` for its DMA buffers never sees bounce-buffer exhaustion; a driver that uses `bus_dmamap_load` on arbitrary kernel memory (say, an mbuf from the network stack) can.

Chapter 21's static pattern is immune. The dynamic pattern discussed in Part 6 (Chapter 28) is not, and the network-driver chapter there covers the retry strategies in detail.

### Failure Mode 9: Unaligned Buffer

The tag's `alignment` parameter describes the required alignment of DMA addresses. If the buffer's address is not aligned, the load fails.

For Chapter 21's `bus_dmamem_alloc`-allocated buffer, the allocator always returns an aligned buffer; the failure does not occur on the static path. For dynamic loads of arbitrary buffers, the driver must either ensure the buffer is aligned (by copying it into an aligned temp buffer) or rely on bounce-buffer machinery to do the alignment automatically. The kernel handles this transparently on `bus_dmamap_load_mbuf_sg` (the mbuf machinery produces aligned segments via bounce); for raw `bus_dmamap_load` the driver is on its own.

The chapter's simulation does not model alignment constraints beyond accepting `alignment = 4`. A Section 7 challenge invites the reader to set `alignment = 64`, observe that `bus_dmamem_alloc` still returns an aligned buffer (because allocators are alignment-aware), and then try loading a deliberately misaligned buffer to see the failure.

### Failure Mode 10: The Load Callback Runs with Non-Zero `error`

`bus_dmamap_load` may call the callback with `error = EFBIG` if the load is logically valid but cannot be achieved with the tag's segment constraints. The callback should handle this:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr = arg;

    if (error != 0) {
        printf("myfirst: dma load callback error %d\n", error);
        /* Do not write *addr; the caller checks for zero. */
        return;
    }

    KASSERT(nseg == 1, ("myfirst: unexpected DMA segment count %d", nseg));
    *addr = segs[0].ds_addr;
}
```

The caller then detects the failure by checking the bus-address output:

```c
sc->dma_bus_addr = 0;
err = bus_dmamap_load(...);
if (err != 0 || sc->dma_bus_addr == 0) {
    /* Failure: either the load returned non-zero, or the callback
     * ran with error != 0 and did not populate the address. */
    ...
}
```

The two checks together cover all failure modes: synchronous error from the load (`err != 0`), callback error with the load returning zero (`dma_bus_addr == 0`). Chapter 21's Stage 1 code has both checks and handles both cases uniformly.

### Failure Mode 11: A Sync Call Is Skipped

A driver that skips a sync call does not fail immediately on coherent platforms; the bug is latent. Section 4 already covered this. The key point for Section 7 is that there is no automatic detection for missing syncs; the driver must be structured so that syncs are impossible to forget. Chapter 21's pattern (the PRE is always the first action in the helper; the POST is always the last) is one way. Another is to use a wrapper function that does the sync around a device-specific callback; this is what iflib and some other frameworks do internally.

`WITNESS` does catch certain sync-related mistakes. If the driver holds the wrong lock when calling a `bus_dmamap_*` function that the tag's lockfunc expects, `WITNESS` warns. If the driver calls a sync from a context where `busdma_lock_mutex` would try to acquire a lock, `WITNESS` catches the mismatch. Running the driver under `WITNESS` regularly is the best defence against sync-related latent bugs.

### Failure Mode 12: The Callback Sets `*addr` to Zero and the Driver Proceeds

A subtle variant of Failure Mode 10. The callback runs, the load returns zero, the `error` argument to the callback happens to be zero, but `nseg == 0`. This cannot happen for `bus_dmamem_alloc`-backed buffers (which always produce one segment), but it can happen for some corner cases of arbitrary loads. The pattern is:

```c
static void
myfirst_dma_single_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr = arg;

    if (error != 0) {
        printf("myfirst: dma load callback error %d\n", error);
        *addr = 0;
        return;
    }

    if (nseg != 1) {
        printf("myfirst: unexpected DMA segment count %d\n", nseg);
        *addr = 0;
        return;
    }

    *addr = segs[0].ds_addr;
}
```

Explicit zeroing on every failure path keeps the caller's error detection reliable. The stricter version is the right default; the `KASSERT` in the Stage 1 code is a debug-only check, and the runtime check is what production code should use. The chapter's example code uses both: `KASSERT` in debug builds, explicit zeroing as a fallback.

### A Pattern: The `dma_setup` / `dma_teardown` Pair

Section 8 moves the DMA code into its own file, but Section 7 is the right place to name the pattern that motivates the refactor. Every DMA-capable driver ends up with a pair of functions: one that performs all the tag/map/memory/load operations in the correct order with full error unwinding, and one that reverses them in the correct order. The pair is the driver's DMA ABI: the rest of the driver only needs to call `dma_setup` at attach and `dma_teardown` at detach. The functions themselves handle all the intermediate error paths.

Chapter 21's pair is `myfirst_dma_setup` and `myfirst_dma_teardown`. Production drivers often have per-subsystem pairs (`re_dma_alloc` and `re_dma_free` for `if_re`, `nvme_qpair_dma_create` and `nvme_qpair_dma_destroy` for NVMe). The shape is always the same; the depth is what varies with the driver's complexity.

### Wrapping Up Section 7

Section 7 walked through twelve failure modes, each with its pattern. The patterns are: check every return value, unwind every prior allocation, explicitly zero output fields on failure, drain callouts and tasks before tearing down resources, mask interrupts before destroying associated DMA state, and keep the setup/teardown pair symmetric so the detach path is the reverse of the attach path. Every production DMA driver follows these patterns; the chapter's code provides a template for applying them.

Section 8 is the final refactor. The DMA code moves into `myfirst_dma.c` and `myfirst_dma.h`; the Makefile updates; the version bumps to `1.4-dma`; the `DMA.md` document captures the design. The chapter's running driver is then ready for Chapter 22's power-management work and Chapter 28's descriptor-ring extension.



## Section 8: Refactoring and Versioning Your DMA-Capable Driver

Section 7 closed the functional scope. Section 8 is the housekeeping pass. The DMA code has accumulated in `myfirst.c`, the simulation backend's DMA extension has grown inside `myfirst_sim.c`, the softc has grown several new fields, the sysctls have grown, and the interrupt filter has gained a `MYFIRST_INTR_COMPLETE` branch. This section collects the DMA-specific code into `myfirst_dma.c` and `myfirst_dma.h`, cleans up the naming, updates the Makefile, bumps the version, adds the `DMA.md` document, and runs the final regression pass. Version tag `1.4-dma`.

The refactor is small in absolute terms (about 200 lines of code moved, plus a new header with public function prototypes and macros), but the structural benefit is large: the driver's architecture now shows, at a glance, that DMA is a first-class subsystem with its own file, its own public API, and its own documentation.

### The Final File Layout

After Section 8, the driver's file layout is:

```text
myfirst.c          # Top-level: attach/detach, cdev, ioctl
myfirst.h          # Shared macros, softc struct, public prototypes
myfirst_hw.c       # Chapter 16: register accessor layer
myfirst_hw.h
myfirst_hw_pci.c   # Chapter 18: real PCI backend
myfirst_sim.c      # Chapter 17: simulated backend (now includes DMA engine)
myfirst_sim.h
myfirst_pci.c      # Chapter 18: PCI attach/detach
myfirst_pci.h
myfirst_intr.c     # Chapter 19: legacy interrupt path
myfirst_intr.h
myfirst_msix.c     # Chapter 20: MSI/MSI-X path
myfirst_msix.h
myfirst_dma.c      # Chapter 21: DMA setup/teardown/transfer
myfirst_dma.h
myfirst_sync.h     # Chapter 11: locking macros
cbuf.c             # Chapter 15: circular buffer
cbuf.h
```

Fifteen source files plus shared headers. Each file has a narrow responsibility. The refactor maintains this separation: `myfirst_dma.c` is self-contained and only depends on `myfirst.h` and the kernel's public headers.

### The `myfirst_dma.h` Header

The header declares the public DMA API and the shared constants:

```c
/* myfirst_dma.h */
#ifndef _MYFIRST_DMA_H_
#define _MYFIRST_DMA_H_

/* DMA buffer size used by myfirst. Matches the Chapter 21 simulated
 * engine's scratch size. A real device would use a value from the
 * hardware's documented capabilities. */
#define	MYFIRST_DMA_BUFFER_SIZE		4096u

/* DMA register offsets (relative to the BAR base). */
#define	MYFIRST_REG_DMA_ADDR_LOW	0x20
#define	MYFIRST_REG_DMA_ADDR_HIGH	0x24
#define	MYFIRST_REG_DMA_LEN		0x28
#define	MYFIRST_REG_DMA_DIR		0x2C
#define	MYFIRST_REG_DMA_CTRL		0x30
#define	MYFIRST_REG_DMA_STATUS		0x34

/* DMA_DIR values. */
#define	MYFIRST_DMA_DIR_WRITE		0u	/* host-to-device */
#define	MYFIRST_DMA_DIR_READ		1u	/* device-to-host */

/* DMA_CTRL bits. */
#define	MYFIRST_DMA_CTRL_START		(1u << 0)
#define	MYFIRST_DMA_CTRL_ABORT		(1u << 1)
#define	MYFIRST_DMA_CTRL_RESET		(1u << 31)

/* DMA_STATUS bits. */
#define	MYFIRST_DMA_STATUS_DONE		(1u << 0)
#define	MYFIRST_DMA_STATUS_ERR		(1u << 1)
#define	MYFIRST_DMA_STATUS_RUNNING	(1u << 2)

/* Public API. */
struct myfirst_softc;

int	myfirst_dma_setup(struct myfirst_softc *sc);
void	myfirst_dma_teardown(struct myfirst_softc *sc);
int	myfirst_dma_do_transfer(struct myfirst_softc *sc,
	    int direction, size_t length);
void	myfirst_dma_handle_complete(struct myfirst_softc *sc);
void	myfirst_dma_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_DMA_H_ */
```

Five public functions. `myfirst_dma_setup` is called once from attach; `myfirst_dma_teardown` is called once from detach; `myfirst_dma_do_transfer` is called by sysctl handlers or by other parts of the driver that want to trigger a DMA; `myfirst_dma_handle_complete` is called from the rx task when `MYFIRST_INTR_COMPLETE` was observed; `myfirst_dma_add_sysctls` registers the DMA counters and the test sysctls.

The header uses forward declaration (`struct myfirst_softc`) to avoid circular includes. The implementation sees the full softc definition via `myfirst.h`.

### The `myfirst_dma.c` File

The file gathers the DMA code that has accumulated across Sections 3 through 7. The top of the file includes the standard headers the DMA API needs:

```c
/* myfirst_dma.c */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
```

The implementation then defines the single-segment callback, `myfirst_dma_setup`, `myfirst_dma_teardown`, `myfirst_dma_do_transfer`, `myfirst_dma_handle_complete`, and `myfirst_dma_add_sysctls`. Each function is lifted from its earlier section with no behaviour change; the refactor is purely a location move.

The one notable addition is `myfirst_dma_handle_complete`, which centralises the POST sync that Section 6's task performed inline:

```c
void
myfirst_dma_handle_complete(struct myfirst_softc *sc)
{
	MYFIRST_ASSERT_LOCKED(sc);

	if (!sc->dma_in_flight)
		return;

	sc->dma_in_flight = false;
	if (sc->dma_last_direction == MYFIRST_DMA_DIR_WRITE)
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTWRITE);
	else
		bus_dmamap_sync(sc->dma_tag, sc->dma_map,
		    BUS_DMASYNC_POSTREAD);

	sc->dma_last_status = CSR_READ_4(sc, MYFIRST_REG_DMA_STATUS);
	atomic_add_64(&sc->dma_complete_tasks, 1);
	cv_broadcast(&sc->dma_cv);
}
```

The rx task's body becomes:

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || !sc->pci_attached) {
		MYFIRST_UNLOCK(sc);
		return;
	}
	sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_task_invocations++;
	cv_broadcast(&sc->data_cv);
	myfirst_dma_handle_complete(sc);
	MYFIRST_UNLOCK(sc);
}
```

The task is shorter by four lines, and the DMA-specific logic lives entirely in `myfirst_dma.c`. If a later chapter (or a future contributor) wants to change the POST-sync or completion behaviour, the change touches one file.

### The Updated Makefile

The Makefile from Chapter 20 was:

```makefile
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.3-msi\"

.include <bsd.kmod.mk>
```

The Chapter 21 Stage 4 Makefile adds `myfirst_dma.c` and bumps the version string:

```makefile
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.4-dma\"

.include <bsd.kmod.mk>
```

The change is two lines: the added source file in `SRCS` and the updated version string. Both are required; a Makefile missing the new source fails to link because `myfirst.c` calls `myfirst_dma_setup` which is not in any compiled object.

### The Version String in `kldstat`

The `MYFIRST_VERSION_STRING` macro is used in `MODULE_VERSION` and in an `SYSCTL_STRING` exposing the version. After the refactor, `kldstat -v | grep myfirst` reports:

```text
myfirst 1.4-dma (pseudo-device)
```

and `sysctl dev.myfirst.0.version` returns the same string. Operators who see the driver attached can check the version at a glance; the string is a useful diagnostic when mixing development and production driver versions on the same test system.

### The `DMA.md` Document

Chapter 20 introduced `MSIX.md`. Chapter 21 adds `DMA.md`, a one-file reference that documents the DMA subsystem's public API, the register layout, the flow diagrams, and the counters. A sample outline:

```markdown
# DMA Subsystem

## Purpose

The DMA layer allows the driver to transfer data between host memory
and the device without CPU-per-byte involvement. It is used for every
transfer larger than a few words; smaller register reads and writes
still use MMIO directly.

## Public API

- `myfirst_dma_setup(sc)`: called from attach. Creates the tag,
  allocates the buffer, loads the map, populates `sc->dma_bus_addr`.
- `myfirst_dma_teardown(sc)`: called from detach. Reverses setup.
- `myfirst_dma_do_transfer(sc, dir, len)`: triggers one DMA transfer
  and waits for completion.
- `myfirst_dma_handle_complete(sc)`: called from the rx task when
  `MYFIRST_INTR_COMPLETE` was observed.
- `myfirst_dma_add_sysctls(sc)`: registers the DMA counters and test
  sysctls under `dev.myfirst.N.`.

## Register Layout

... (table from Section 5) ...

## Flow Diagrams

Host-to-device:
    ... (diagram from Section 4, Pattern A) ...

Device-to-host:
    ... (diagram from Section 4, Pattern B) ...

## Counters

- `dev.myfirst.N.dma_transfers_write`: successful host-to-device transfers.
- `dev.myfirst.N.dma_transfers_read`: successful device-to-host transfers.
- `dev.myfirst.N.dma_errors`: transfers that returned EIO.
- `dev.myfirst.N.dma_timeouts`: transfers that hit the 1-second timeout.
- `dev.myfirst.N.dma_complete_intrs`: completion-bit observations in the filter.
- `dev.myfirst.N.dma_complete_tasks`: completion processing in the task.

## Observability

`sysctl dev.myfirst.N.dma_*` returns the full counter set. A healthy
driver has `dma_complete_intrs == dma_complete_tasks` and both equal
to `dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

## Testing

The sysctls `dma_test_write` and `dma_test_read` trigger transfers
from user space. Writing any value to `dma_test_write` fills the
buffer with the low byte of the value and runs a host-to-device
transfer; writing any value to `dma_test_read` runs a device-to-host
transfer and logs the first eight bytes to `dmesg`.

## Known Limitations

- Single buffer, single transfer at a time.
- No descriptor-ring support (Part 6, Chapter 28).
- No per-NUMA-node allocation (Part 7, Chapter 33).
- No partial-transfer reporting (exercise for the reader).

## See Also

- `bus_dma(9)`, `/usr/src/sys/sys/bus_dma.h`.
- `/usr/src/sys/dev/re/if_re.c` for a production descriptor-ring driver.
- `INTERRUPTS.md`, `MSIX.md` for the interrupt path the completion uses.
```

The document is about a page long and useful both as a contributor reference and as a reader's recap. Real production drivers often have their `README` files structured similarly: subsystem-by-subsystem, with a short flow diagram and a counter list.

### The Regression Pass

The chapter's regression test from Chapter 20 is extended with DMA-specific checks. The full test (call it `full_regression_ch21.sh`) runs these steps on a fresh boot:

1. Kernel is in the expected state (`uname -v` shows `1.4-dma` in the `myfirst` line, `INVARIANTS`/`WITNESS` in the config).
2. Load the module (`kldload ./myfirst.ko`). `dmesg` shows the DMA banner and the interrupt-mode banner.
3. Verify initial counter state (`dma_transfers_write == 0`, etc.).
4. Run 1000 write transfers via the sysctl. Assert counter increments.
5. Run 1000 read transfers via the sysctl. Assert counter increments.
6. Run 100 transfers with the simulated error injection enabled. Assert `dma_errors` counter climbs.
7. Run 10 transfers with the callout disabled (engine hangs). Assert `dma_timeouts` counter climbs and `dma_in_flight` is cleared after each timeout.
8. Unload the module (`kldunload myfirst`). Assert clean unload with no `WITNESS` warnings.
9. Repeat the load/unload cycle 50 times to detect leaks.

The full script lives under `examples/part-04/ch21-dma/labs/full_regression_ch21.sh`. A successful run prints a single line per step with the observed counters and an overall `PASS` at the end. Any step that fails prints a `FAIL` line with the actual versus expected values.

### What the Refactor Accomplished

After Section 8:

- The DMA code lives in its own file with a documented public API.
- The Makefile knows about the new source.
- The version tag reflects the chapter's work.
- A `DMA.md` document serves as a reference for contributors and future chapters.
- The regression test covers every DMA path the chapter teaches.

The driver is now `1.4-dma`, and the diff from `1.3-msi` is about four hundred lines added plus fifty lines moved. Every line is traceable: either it implements a concept from one of the earlier sections or it is housekeeping (headers, Makefile, documentation).

### Exercise: Create a Utility That Verifies DMA Data

A hands-on exercise for Section 8. Write a small user-space utility that exercises the DMA path and verifies the data end-to-end.

The utility's job:
1. Open the `dev.myfirst.0.dma_test_write` sysctl, write a known pattern (say, 0xA5).
2. Open `dev.myfirst.0.dma_test_read`, trigger a read.
3. Check `dmesg` (or a custom sysctl that exposes the first bytes of the buffer) for the expected pattern (0x5A from the simulator's scratch).
4. Repeat 100 times with different patterns; report a summary.

The utility is a shell script for simplicity:

```sh
#!/bin/sh
fail=0
for pat in $(jot 100 1); do
    hex=$(printf "0x%02x" $pat)
    sysctl -n dev.myfirst.0.dma_test_write=$pat >/dev/null
    sysctl -n dev.myfirst.0.dma_test_read=1 >/dev/null
    # ... check the result ...
done
echo "failures: $fail"
```

The exercise is open-ended: the reader can extend it with timing measurements, error-rate estimates, or a comparison against `dma_errors` counter values. Section 8's challenge list suggests a few directions.

### Wrapping Up Section 8

Section 8 is the finish line. The DMA code has its own file; the Makefile compiles it; the version tag says `1.4-dma`; the documentation captures the design; the regression test exercises every path. The chapter's running driver is now ready for Chapter 22's power-management work, which will use the DMA teardown path to quiesce transfers during suspend, and ready for Chapter 28, which will extend the single-buffer pattern to a full descriptor ring.

The chapter's sections are complete. The remaining material is reference and practice: a walkthrough of a production DMA driver (`if_re`), deeper looks at bounce buffers, 32-bit devices, IOMMU, coherent flags, and parent-tag inheritance, the labs, the challenges, the troubleshooting reference, and the final wrap-up.



## Reading a Real Driver Together: if_re.c

A tour of `/usr/src/sys/dev/re/if_re.c` that maps its DMA code back to the concepts in Chapter 21. `if_re(4)` is the driver for the RealTek 8139C+ / 8169 / 8168 family of Gigabit Ethernet chips, which is common enough to be present in many lab environments and compact enough (around four thousand lines total) to read in a week of commute-length sessions. Its DMA code is about four hundred lines and sits inside a few well-named functions, so it is a good first real-driver read.

> **Reading this walkthrough.** The listings in the subsections that follow are abbreviated excerpts from `re_allocmem()`, `re_dma_map_addr()`, `re_encap()`, and `re_detach()` in `/usr/src/sys/dev/re/if_re.c`. We have preserved each call's argument list and the surrounding control flow, but we show only the fragments that illustrate a `bus_dma(9)` idea under discussion; the real functions include more error handling, more child tags, and more bookkeeping. Every symbol the listings name, from `bus_dma_tag_create` to `bus_dmamap_load` to `re_dma_map_addr`, is a real FreeBSD identifier you can find with a symbol search. The same abbreviation convention appears in the `nvme(4)` driver's `nvme_single_map` in `/usr/src/sys/dev/nvme/nvme_private.h` and in any driver that packages a single-segment callback as a helper.

### The Parent Tag

`re_allocmem` begins by creating the parent tag:

```c
lowaddr = BUS_SPACE_MAXADDR;
if ((sc->rl_flags & RL_FLAG_PCIE) == 0)
    lowaddr = BUS_SPACE_MAXADDR_32BIT;
error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
    lowaddr, BUS_SPACE_MAXADDR, NULL, NULL,
    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0,
    NULL, NULL, &sc->rl_parent_tag);
```

Two things worth noticing.

First, the `lowaddr` decision. On PCIe variants of the chip, DMA can reach all of memory (`BUS_SPACE_MAXADDR`). On older non-PCIe variants, the chip is 32-bit-only (`BUS_SPACE_MAXADDR_32BIT`, i.e. the device cannot reach memory above 4 GB). The driver probes this at attach time and sets `lowaddr` accordingly. Any child tag created from `rl_parent_tag` inherits this limit automatically. On a 64-bit amd64 system with 16 GB of RAM, the driver's tags ensure that DMA buffers are allocated below 4 GB for the non-PCIe variants; bounce buffers kick in only if the kernel cannot satisfy that allocation.

Second, the `maxsize = BUS_SPACE_MAXSIZE_32BIT` and `nsegments = 0`. These values produce a very permissive parent tag: any child with any segment count is valid. This is a common idiom; the parent carries only the addressing constraint, and children carry the specifics.

### The Per-Buffer Child Tags

The driver creates four child tags:

- `rl_tx_mtag`: for transmit mbuf payloads. Alignment 1, max size `MCLBYTES * RL_NTXSEGS`, up to `RL_NTXSEGS` segments per mapping.
- `rl_rx_mtag`: for receive mbuf payloads. Alignment 8, max size `MCLBYTES`, 1 segment per mapping.
- `rl_tx_list_tag`: for the transmit descriptor ring. Alignment `RL_RING_ALIGN` (256 bytes), max size `tx_list_size`, 1 segment.
- `rl_rx_list_tag`: for the receive descriptor ring. Alignment `RL_RING_ALIGN`, max size `rx_list_size`, 1 segment.

Each child passes `sc->rl_parent_tag` as the parent. Each carries its own alignment and segment constraints. The kernel composes the parent's 32-bit addressing limit with each child's own constraints; the driver does not have to repeat the addressing limit in every child tag.

The descriptor-ring tags are single-segment: the ring is allocated contiguously. The mbuf-payload tags are multi-segment: a single received packet may be a chain of mbufs, and the NIC's DMA engine is fine with a scatter-gather list of segments as long as the total fits within the maximum size.

### The Ring Allocation

The descriptor ring allocation uses `bus_dmamem_alloc` with `BUS_DMA_COHERENT`:

```c
error = bus_dmamem_alloc(sc->rl_ldata.rl_tx_list_tag,
    (void **)&sc->rl_ldata.rl_tx_list,
    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
    &sc->rl_ldata.rl_tx_list_map);
```

The flags are `WAITOK | COHERENT | ZERO`: allow sleeping, prefer coherent memory, zero-initialise. The coherent hint is appropriate because the ring is on the hot path for both the CPU (producing TX entries) and the NIC (consuming them, writing back status). The zero-initialise is important because the ring's initial state must have all entries marked "not in use"; uninitialised memory could cause the NIC to attempt DMA based on garbage descriptor contents.

After `bus_dmamem_alloc`, the driver calls `bus_dmamap_load` with a single-segment callback:

```c
sc->rl_ldata.rl_tx_list_addr = 0;
error = bus_dmamap_load(sc->rl_ldata.rl_tx_list_tag,
     sc->rl_ldata.rl_tx_list_map, sc->rl_ldata.rl_tx_list,
     tx_list_size, re_dma_map_addr,
     &sc->rl_ldata.rl_tx_list_addr, BUS_DMA_NOWAIT);
```

And the callback:

```c
static void
re_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *addr;

    if (error)
        return;

    KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
    addr = arg;
    *addr = segs->ds_addr;
}
```

The callback is almost identical to Chapter 21's `myfirst_dma_single_map`. The driver's code is portable across every DMA-capable FreeBSD platform because the callback, the tag, and the allocator all respect the `bus_dma` discipline.

### The Per-Transfer Pattern

For each transmitted packet, the driver calls `bus_dmamap_load_mbuf_sg` on the packet's mbuf:

```c
error = bus_dmamap_load_mbuf_sg(sc->rl_ldata.rl_tx_mtag, txd->tx_dmamap,
    *m_head, segs, &nsegs, BUS_DMA_NOWAIT);
```

The `_mbuf_sg` variant fills a pre-allocated segment array, avoiding the callback for the common case. If the load returns `EFBIG`, the driver tries to compact the mbuf chain with `m_collapse` and retries.

Once the load succeeds, the driver syncs:

```c
bus_dmamap_sync(sc->rl_ldata.rl_tx_mtag, txd->tx_dmamap,
    BUS_DMASYNC_PREWRITE);
```

Then writes the descriptors with the segment addresses, pokes the doorbell, and lets the NIC transmit. On completion, the driver syncs again:

```c
bus_dmamap_sync(sc->rl_ldata.rl_tx_list_tag, sc->rl_ldata.rl_tx_list_map,
    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
```

The combined POSTREAD|POSTWRITE sync is the descriptor-ring variant from Section 4: the driver both wrote the descriptor (the address, length, flags) and wants to read the device's status update (transmitted, error, etc.).

### The Cleanup

The detach path (`re_dma_free`) mirrors the allocation order, in reverse:

```c
if (sc->rl_ldata.rl_rx_list_tag) {
    if (sc->rl_ldata.rl_rx_list_addr)
        bus_dmamap_unload(sc->rl_ldata.rl_rx_list_tag,
            sc->rl_ldata.rl_rx_list_map);
    if (sc->rl_ldata.rl_rx_list)
        bus_dmamem_free(sc->rl_ldata.rl_rx_list_tag,
            sc->rl_ldata.rl_rx_list, sc->rl_ldata.rl_rx_list_map);
    bus_dma_tag_destroy(sc->rl_ldata.rl_rx_list_tag);
}
```

The per-buffer dmamaps created with `bus_dmamap_create` are destroyed with `bus_dmamap_destroy` in a loop, and finally the tags are destroyed.

### What the Walkthrough Teaches

`if_re`'s DMA code is a near-textbook application of Chapter 21's concepts. Every call the chapter teaches appears in the driver. The only concepts not in the chapter are the scatter-gather variant (`bus_dmamap_load_mbuf_sg`) and the per-buffer dynamic maps (`bus_dmamap_create`/`bus_dmamap_destroy`), and both are natural extensions of the static pattern.

A reader who finishes Chapter 21 and then reads `if_re.c` for half an hour sees the same shapes. The chapter's investment pays off against real driver code.



## A Deeper Look at Bounce Buffers

Section 1 introduced bounce buffers; Section 7 described the error mode. This deeper look pins down what bounce buffers are, when they are used, and what the cost looks like.

### What Bounce Buffers Are

A bounce buffer is a region of physical memory inside the device's addressable range, used to stage data when the driver's actual buffer is outside the range. The `bus_dma` layer manages a pool of bounce pages as an internal resource; a driver that allocates buffers with `bus_dmamem_alloc` inside the device's range never touches the pool.

When a driver loads a buffer via `bus_dmamap_load` and the buffer is outside the device's range (say, a mbuf allocated above 4 GB on a 32-bit-only device), the kernel:

1. Allocates a bounce page inside the device's range.
2. At `PREWRITE` time, copies the driver's buffer into the bounce page.
3. Programs the device with the bounce page's address.
4. At `POSTREAD` time, copies the bounce page back to the driver's buffer.
5. Releases the bounce page.

The driver never knows that a bounce happened. The `bus_addr_t` returned by the callback is the bounce page's address; the sync operations move the data back and forth.

### When Bounce Buffers Matter

Bounce buffers are needed in three situations:

1. **32-bit-only devices on systems with more than 4 GB of RAM.** The device cannot reach above 4 GB; any buffer up there needs a bounce below 4 GB.
2. **Devices with addressing holes.** Some legacy devices have unusable address regions (for example, the ISA-style 16 MB DMA limit). A buffer inside the hole needs to bounce.
3. **IOMMU-enabled systems with devices that are not IOMMU-registered.** If the IOMMU is in enforcing mode and a device has not been mapped, the DMA target is bounced to a mapped region.

On modern amd64 with 64-bit-capable devices and 64-bit-capable drivers (which most are), bounce buffers are rare. On embedded systems or in compatibility contexts, they are common.

### The Performance Cost

Each bounce involves a `memcpy` from the driver's buffer to the bounce region (at `PREWRITE`) and another `memcpy` back (at `POSTREAD`). For a 1500-byte mbuf this is about 3 KB of memory traffic per packet; on a 10 Gbps NIC receiving a million packets per second, that is 3 GB/s of bounce traffic, which measurably loads the memory bus.

The bounce buffer pool is also sized (tunable via `hw.busdma.*` sysctls, with defaults that scale with physical memory). On a system where many drivers are bouncing, the pool can be exhausted, at which point loads with `BUS_DMA_NOWAIT` return `ENOMEM` and loads without `NOWAIT` defer.

### How to Avoid Bouncing

Three strategies:

1. **Allocate DMA memory with `bus_dmamem_alloc`.** The allocator respects the tag's addressing constraints by construction.
2. **Set the tag's `highaddr`/`lowaddr` to match the device's true range.** Do not pass `BUS_SPACE_MAXADDR` for a 32-bit-only device; the incorrect value leads the allocator to hand out too-high buffers and forces bouncing.
3. **Use `BUS_DMA_ALLOCNOW`.** This pre-allocates the bounce pool at tag creation, turning a later allocation failure into an immediate failure the driver can handle at attach time.

Chapter 21's simulation does not exercise bouncing, but the concept is important for understanding real driver behaviour. A driver that "works on my laptop" but corrupts data on a server with 64 GB of RAM almost certainly has an address-range misconfiguration that is triggering silent bouncing on the laptop and failing differently on the server.

### Observability

The `hw.busdma` sysctl tree exposes bounce-related counters:

```sh
sysctl hw.busdma
```

The interesting lines include `total_bpages`, `free_bpages`, `total_bounced`, `total_deferred`, `lowpriority_bounces`. `total_bounced` is the total number of pages bounced since boot; a non-zero value on a system where no bouncing should happen is a clue that some driver's tag is misconfigured.



## A Deeper Look at 32-Bit-Capable Devices

Section 1 and the bounce-buffer discussion both touched on this; this deeper look collects the practical guidance.

### The Setting

Some PCI and PCIe devices have 32-bit DMA engines. The engines accept only 32-bit bus addresses, so the DMA traffic is confined to the lower 4 GB of bus address space. On a 64-bit host with more than 4 GB of RAM, every DMA buffer must be below the 4 GB boundary.

### The Tag Setting

The relevant constants:

- `BUS_SPACE_MAXADDR`: no addressing limit. Use for 64-bit-capable devices.
- `BUS_SPACE_MAXADDR_32BIT`: 0xFFFFFFFF. Use as the `lowaddr` for 32-bit-only devices.
- `BUS_SPACE_MAXADDR_24BIT`: 0x00FFFFFF. Use as the `lowaddr` for legacy devices with a 16 MB limit (rare).

Together, `lowaddr` and `highaddr` describe the *excluded* window of bus address space. The manual page wording is: "The window contains all addresses greater than `lowaddr` and less than or equal to `highaddr`." Addresses inside the window cannot be reached by the device and must be bounced; addresses outside the window are reachable.

For a 32-bit device, the excluded window is everything above 4 GB, so `lowaddr = BUS_SPACE_MAXADDR_32BIT` and `highaddr = BUS_SPACE_MAXADDR`. The window becomes `(0xFFFFFFFF, BUS_SPACE_MAXADDR]`, which captures exactly "anything above 4 GB".

For a 64-bit-capable device with no restrictions, the excluded window should be empty. The idiomatic way to express that is `lowaddr = BUS_SPACE_MAXADDR` and `highaddr = BUS_SPACE_MAXADDR`, which collapses the window to `(BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR]`, an empty range.

The naming is confusing at first: `lowaddr` sounds like a lower bound but is actually the *upper end* of the device's reachable range (and the *lower end* of the excluded window). The naming is historical: `lowaddr` is the low end of the excluded region, and the excluded region is above the device's range. The mnemonic: `lowaddr` is "the last address the device can reach".

### Double-Addressing-Cycle (DAC)

PCI has a mechanism called Double Address Cycle (DAC) that allows 32-bit slots to address 64 bits by issuing two cycles. Some devices that appear 32-bit actually support DAC and can reach 64-bit addresses. The `if_re` driver checks a chip-family flag to decide: PCIe variants use full 64-bit; non-PCIe variants use 32-bit only. A driver that does not know whether its device supports DAC should default to 32-bit (safer) and enable 64-bit if the datasheet confirms support.

### The Impact on Bounce Traffic

A 32-bit-only device on a 64 GB server:
- Most buffers are allocated above 4 GB by the default kernel allocator (since most memory is up there).
- Every load triggers a bounce.
- The bounce pool caps the driver's throughput at whatever the pool can churn.

The fix is always to use `bus_dmamem_alloc` for the driver's static buffers and to ensure the tag's `lowaddr` is set correctly so the allocator places the buffers below 4 GB. For the dynamic case (mbufs, user buffers), the network stack provides pre-allocated mbuf clusters below 4 GB for drivers that request them via `bus_dma`-aware mbuf types, though this path is inside iflib and not a concern for Chapter 21.



## A Deeper Look at IOMMU Integration

Section 1 mentioned the IOMMU. This deeper look covers what the IOMMU does, how to tell if it is active, and why the `bus_dma` API is designed to be transparent to it.

### What the IOMMU Does

An IOMMU (Input-Output Memory Management Unit) is a translation layer between a device's bus addresses and the host's physical addresses. With the IOMMU active:

- The device sees bus address X.
- The IOMMU translates X to physical address Y.
- The memory controller writes physical address Y.

The translation is per-device (each device has its own mapping table), and by default the IOMMU only allows a device to access memory the kernel has explicitly mapped for it. This provides two benefits:

1. **Security.** A compromised device cannot DMA to arbitrary memory; it can only reach memory the kernel has explicitly mapped.
2. **Flexibility.** The kernel can present a 32-bit-capable device with a 32-bit view of any region of memory, including regions above 4 GB, by setting up an appropriate mapping. The bounce-buffer strategy becomes unnecessary when the IOMMU can just remap.

### How the Kernel Integrates

FreeBSD's `busdma_iommu` (built when `DEV_IOMMU` is set, typically on x86 with VT-d or AMD-Vi) integrates with the `bus_dma` API transparently. When a driver calls `bus_dmamap_load`, the busdma layer asks the IOMMU to allocate IOVA (I/O virtual address) space and program the mapping; the `bus_addr_t` returned to the driver is the IOVA, not the physical address. The driver programs the device with the IOVA; the device issues a DMA to the IOVA; the IOMMU translates to the physical address; the memory controller completes the transfer.

From the driver's perspective, nothing changes. The same `bus_dma` calls do the right thing on IOMMU systems as on non-IOMMU systems. This is the portability promise of `bus_dma`: drivers written against the API work on whatever the platform provides.

### Detecting the IOMMU

```sh
sysctl hw.vmm.ppt.devices  # shows pass-through devices (bhyve)
sysctl hw.dmar             # Intel VT-d if enabled
sysctl hw.iommu            # generic IOMMU flag
```

On a system where the kernel booted with the IOMMU active, `dmesg` shows a line like `dmar0: <DMAR>` near boot.

### Performance Implications

IOMMU translation is not free. Each translation goes through the IOMMU's page tables; the IOMMU caches recently-used translations in a TLB-like structure, but misses cost real time. For drivers that make millions of small mappings per second, the IOMMU can become a bottleneck.

The mitigations are:

1. **Batch mappings.** A driver that keeps a long-lived buffer mapped once has no recurring IOMMU cost. Chapter 21's static pattern is ideal.
2. **Use the IOMMU's super-page support.** Larger pages mean fewer TLB entries.
3. **Disable the IOMMU for trusted devices.** Not generally advised, but possible for specific deployments.

Chapter 21's driver is not affected either way; the static buffer is mapped once at attach and never remapped. Drivers with high remap rates (storage controllers processing many small transfers per second) are affected more.



## A Deeper Look at BUS_DMA_COHERENT vs Explicit Sync

Section 4 covered the `BUS_DMA_COHERENT` flag at the API level. This deeper look explains the interaction with the sync discipline, which is subtle on some platforms.

### The Relationship

`BUS_DMA_COHERENT` is a hint passed to `bus_dma_tag_create` (architectural preference) and `bus_dmamem_alloc` (allocation mode). It asks the allocator to place the buffer in a memory domain that is coherent with the DMA path: on arm64 with write-combining or uncached regions, this is a specific memory type; on amd64, the allocator chooses normal memory because the PCIe root complex is already coherent.

The flag does not eliminate the requirement to call `bus_dmamap_sync`. The API contract requires the sync; `BUS_DMA_COHERENT` makes the sync cheap or free, not unnecessary.

### Why the Sync Is Still Required

Two reasons. First, the driver is portable: the same source file compiles on every platform FreeBSD supports. On a platform where `BUS_DMA_COHERENT` is honoured but bounce buffers are in play (for an addressing constraint, say), the sync is the hook that triggers the bounce copy. Without it, the bounce data is stale.

Second, the driver is forward-compatible: a future platform may not honour the flag, or may honour it only for certain allocators. The explicit sync is what makes the driver correct against any future platform.

The rule: always pass `BUS_DMA_COHERENT` when the access pattern justifies it, and always call `bus_dmamap_sync` regardless. The flag makes sync cheap; the sync makes the driver correct.

### When to Use the Flag

For static buffers (descriptor rings, control structures): always pass `BUS_DMA_COHERENT`. The buffer is accessed often by both the CPU and the device; coherent memory reduces the per-access cost.

For dynamic buffers (packet payloads, disk blocks): usually do not pass it. The cache-hit rate when the CPU processes the data is usually high enough that coherent memory's slower access patterns hurt more than they help.

The `if_re` driver follows exactly this: descriptor tags use `BUS_DMA_COHERENT`, payload tags do not. NVMe's per-qpair tag uses `BUS_DMA_COHERENT` for the same reason.



## A Deeper Look at Parent and Child Tag Inheritance

Section 3's setup function passed `bus_get_dma_tag(sc->dev)` as the parent. This deeper look covers why inheritance matters and when a driver should create an explicit parent tag rather than inheriting from the bridge.

### Inheritance Semantics

A child tag inherits every constraint from its parent that is more restrictive than the child's own. The intersection semantics mean:

- The child's `lowaddr` is `min(parent_lowaddr, child_lowaddr)`. The tighter limit wins.
- The child's alignment is `max(parent_alignment, child_alignment)`. More alignment wins.
- The child's `maxsize` is `min(parent_maxsize, child_maxsize)`. The smaller win.
- The child's `nsegments` is `min(parent_nsegments, child_nsegments)`.
- Flags are composed: `BUS_DMA_COHERENT` propagates, `BUS_DMA_ALLOCNOW` does not.

The kernel applies these rules at tag-create time; the child's internal constraints are the intersection of the parent's and its own.

### Why Drivers Create Explicit Parent Tags

A driver that has several per-subsystem tags often finds it cleaner to create one explicit parent tag that carries the device-level constraints (addressing limits, platform-level alignment), then create children for each specific purpose (rings, payloads). The parent is then destroyed at detach after all children are destroyed.

`if_re` does this: `rl_parent_tag` is the device-level parent, inheriting from the PCI bridge and adding the device's 32-bit limit (for non-PCIe chips). The four child tags (`rl_tx_mtag`, `rl_rx_mtag`, `rl_tx_list_tag`, `rl_rx_list_tag`) inherit from `rl_parent_tag`. Destroying `rl_parent_tag` is the last DMA cleanup step, because all four children must be destroyed first (`bus_dma_tag_destroy` returns `EBUSY` if any child still exists).

### When to Skip the Explicit Parent

For drivers with a single tag (like Chapter 21's), creating an explicit parent is overkill. The driver's single tag inherits from `bus_get_dma_tag(sc->dev)` directly and applies its own constraints; the bridge's tag is the de-facto parent.

For drivers with two or three related tags that share constraints, an explicit parent reduces duplication and makes the design clearer.

For drivers with many tags (descriptor rings, multiple buffer pools, shared memory regions), an explicit parent is always the right choice.



## A Deeper Look at Descriptor Rings as a Future Topic

Chapter 21's driver uses a single DMA buffer. Production drivers use descriptor rings. This deeper look previews what a descriptor ring is, so the reader who wants to extend the chapter's work has a shape to aim at, but the detailed teaching is deferred.

A descriptor ring is an array of fixed-size entries in DMA-coherent memory. Each entry contains at least a bus address and a length, plus flags that describe the transfer (direction, type, status). The driver and the device communicate through the ring: the driver writes entries, the device reads them, performs the transfer, and writes status back.

The ring has two indexes: a **producer** index (the next entry the writer will fill) and a **consumer** index (the next entry the reader will process). For a transmit ring, the driver is the producer and the device is the consumer. For a receive ring, the roles are reversed. Both indexes wrap modulo the ring size.

The driver signals new entries by writing a **doorbell** register (an MMIO write that the device treats as "look at the ring"). The device signals completions by raising an interrupt; the driver then walks the ring from the last consumer index to the current one, processes each entry, and advances the consumer index.

The complications that make rings their own topic are: head-of-line blocking when the ring is full, partial-transfer handling across ring entries, reorder tolerance (does the device complete entries in order, or out of order?), flow control between driver and device, wrap-around correctness, and the interaction with multi-queue hardware where each queue has its own ring.

Extending Chapter 21's driver to use a small ring is a natural exercise. The challenges at the end of the chapter include a sketch. The full descriptor-ring topic is the network chapter in Part 6 (Chapter 28).



## A Mental Model for Chapter 21's Sync Discipline

A paragraph-length mental model that captures the sync discipline in one sentence: *the PRE and POST sync calls mark every moment a DMA buffer changes ownership between the CPU and the device, and the driver writes them into its code even when the current platform makes them free.* Chapter 21's driver respects this model everywhere. Every `bus_dmamap_sync` call in `myfirst_dma.c` is paired with a partner: PRE marks the CPU releasing ownership, POST marks the CPU re-acquiring it. Between the two, the buffer belongs to the device, and no driver code reads or writes it. The discipline sounds absolute because it is; every production driver follows it; every exception the reader might imagine is actually a buggy driver that happens to work on the test platform.



## Patterns From Real FreeBSD Drivers

A short catalogue of DMA patterns as they appear in the real tree. Each pattern has a representative file; reading the file's DMA-related functions after this chapter is the recommended follow-up practice.

### Pattern: `bus_dmamem_alloc` for Static Descriptor Rings

**Where:** `/usr/src/sys/dev/re/if_re.c`, `re_allocmem`; `/usr/src/sys/dev/nvme/nvme_qpair.c`, `nvme_qpair_construct`.

The driver allocates a contiguous region at attach time, loads it with a single-segment callback, uses it for the driver's lifetime, unloads it on detach. This is Chapter 21's static pattern and the foundation for almost every DMA-capable driver.

### Pattern: `bus_dmamap_load_mbuf_sg` for Per-Packet Mappings

**Where:** `/usr/src/sys/dev/re/if_re.c`, `re_encap`; `/usr/src/sys/dev/e1000/if_em.c`, `em_xmit`.

For each outgoing packet, the driver loads the packet's mbuf into a dynamic map, programs the descriptor, transmits, and unloads. This is the dynamic pattern Chapter 21 mentions but does not implement.

### Pattern: Parent-Child Tag Hierarchy

**Where:** `/usr/src/sys/dev/re/if_re.c`, `re_allocmem`; `/usr/src/sys/dev/bce/if_bce.c`, `bce_dma_alloc`.

The driver creates a parent tag for device-level constraints and child tags for per-subsystem allocations. The hierarchy composes constraints automatically and separates the driver's cleanup into clear steps.

### Pattern: `BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE` for Shared Rings

**Where:** `/usr/src/sys/dev/nvme/nvme_qpair.c`, around `nvme_qpair_reset` and the submission queue; `/usr/src/sys/dev/ahci/ahci.c`, around the command table.

For rings where both the driver and the device read and write, the combined PRE flag at setup and the combined POST flag at transaction boundaries make the sync discipline explicit.

### Pattern: Single-Segment Callback as a Universal Helper

**Where:** `/usr/src/sys/dev/nvme/nvme_private.h`, `nvme_single_map`; `/usr/src/sys/dev/re/if_re.c`, `re_dma_map_addr`; many others.

Almost every driver defines a single-segment callback named in its own style. Chapter 21's `myfirst_dma_single_map` is a clean example; the real-driver variants are all functionally identical.

### Pattern: Coherent Tag for Rings, Non-Coherent for Payloads

**Where:** `/usr/src/sys/dev/re/if_re.c` and many others.

The driver passes `BUS_DMA_COHERENT` for descriptor-ring tags and omits it for payload tags. The allocator handles both; the driver makes the right choice per tag.



## Hands-On Labs

Five hands-on labs walk the reader through the chapter's stages on a lab target. Each lab is self-contained; completing all five produces a working `1.4-dma` driver.

### Lab 1: Identify DMA-Capable Devices on Your System

**Goal:** build a map of your lab machine's DMA landscape before the chapter's coding work begins.

**Time:** 20 minutes.

**Steps:**

1. Run `pciconf -lv` on your lab host and note the devices.
2. Run `pciconf -lvc | grep -B1 BUSMASTEREN` to identify devices with bus-mastering enabled.
3. Pick one device, run `pciconf -lvbc <devname>`, and identify its BAR layout and capability list.
4. Run `sysctl hw.busdma` and note the bounce-buffer counters.
5. Write in your lab notebook: three devices, one property each, your current bounce-buffer usage.

**Expected:** a short list of devices with their bus-mastering status, and an understanding of whether your system is bouncing.

### Lab 2: Stage 1, Allocate and Map a DMA Buffer

**Goal:** build the Stage 1 driver that creates a tag, allocates a buffer, loads a map, and cleans up.

**Time:** 1.5 hours.

**Steps:**

1. Start from the Chapter 20 Stage 4 driver (`1.3-msi`).
2. Add the four softc fields (`dma_tag`, `dma_map`, `dma_vaddr`, `dma_bus_addr`).
3. Add the single-segment callback (`myfirst_dma_single_map`).
4. Add `myfirst_dma_setup` and `myfirst_dma_teardown` to `myfirst.c`.
5. Call them from attach and detach.
6. Build and load. Verify the `dmesg` banner shows the KVA and bus address.
7. Unload and reload 50 times; check that `vmstat -m` does not show leaks.

**Expected:** a driver that stands up and tears down a DMA region cleanly. `dmesg` shows the banner on each load; no panic on unload.

**Hint:** if the load fails immediately with `EINVAL`, check that `MYFIRST_DMA_BUFFER_SIZE` is both the tag's `maxsize` and the argument to `bus_dmamap_load`; a mismatch is the most common cause.

### Lab 3: Stage 2, Simulated DMA Engine with Polling

**Goal:** extend the driver with the simulated DMA engine and the polling-based transfer helper.

**Time:** 2 hours.

**Steps:**

1. Add the `MYFIRST_REG_DMA_*` constants to `myfirst.h`.
2. Extend `struct myfirst_sim` with the DMA engine state.
3. Implement the engine's callout handler and the write hook.
4. Implement `myfirst_dma_do_transfer` (polling version).
5. Add the `dma_test_write` and `dma_test_read` sysctls.
6. Build and load.
7. Run `sudo sysctl dev.myfirst.0.dma_test_write=0xAA` and verify the `dmesg` output.
8. Run the 1000-iteration test from Section 5 and check the counters.

**Expected:** 1000 successful transfers in each direction; zero errors; zero timeouts.

**Hint:** if the transfer times out every time, the simulation's callout is probably not scheduling. Check `callout_reset` is called and that `hz / 100` matches your kernel's `hz` (use `sysctl kern.hz` to verify).

### Lab 4: Stage 3, Interrupt-Driven Completion

**Goal:** replace the polling helper with the interrupt-driven helper.

**Time:** 2 hours.

**Steps:**

1. Extend the rx filter to handle `MYFIRST_INTR_COMPLETE`.
2. Extend the rx task to call `myfirst_dma_handle_complete`.
3. Add `myfirst_dma_do_transfer_intr` to `myfirst.c`.
4. Enable `MYFIRST_INTR_COMPLETE` in the `INTR_MASK` write.
5. Rewire the sysctl handlers to use the interrupt-driven version.
6. Build and load.
7. Run the 1000-iteration test. Verify `dma_complete_intrs` and `dma_complete_tasks` climb together.
8. Verify no CPU is pegged during the test (`top` or `vmstat` should show low system CPU).

**Expected:** 1000 interrupts, 1000 tasks, 1000 completions. No CPU pegged.

**Hint:** if the completion counter increments but the helper hangs in `cv_timedwait` until the 1-second timeout, the task is not calling `cv_broadcast` on `dma_cv`. The wake-up is what unblocks the helper.

### Lab 5: Stage 4, Refactor and Regression

**Goal:** complete the refactor into `myfirst_dma.c` and `myfirst_dma.h`; run the final regression test.

**Time:** 1.5 hours.

**Steps:**

1. Create `myfirst_dma.h` with the public API.
2. Create `myfirst_dma.c` with the DMA code moved from `myfirst.c`.
3. Update the Makefile to include `myfirst_dma.c` and bump the version to `1.4-dma`.
4. Create `DMA.md` with the subsystem reference.
5. Run `make clean && make` and confirm a clean build.
6. Load the driver, verify the version string.
7. Run the full regression script (`labs/full_regression_ch21.sh`) and confirm it prints `PASS`.

**Expected:** clean build, clean load, clean regression pass, clean unload.

**Hint:** if the link fails with an undefined symbol referring to one of the DMA functions, the `SRCS` line in the Makefile has not been updated to include `myfirst_dma.c`. Run `make clean` after changing the Makefile to ensure the new source is compiled.



## Challenge Exercises

Challenge exercises that stretch Chapter 21's scope without introducing foundations from later chapters. Pick the ones that interest you; the list is meant to be a menu, not a checklist.

### Challenge 1: Multi-Buffer Rotation

Extend the driver to maintain a pool of four DMA buffers instead of one. Each transfer picks the next buffer in rotation. The test program runs four transfers back-to-back without waiting for completions and observes that all four complete before the fifth is submitted.

**What it exercises:** the allocation and load paths in a loop; per-buffer softc state; the interaction with a single completion vector when multiple transfers are in flight.

### Challenge 2: Scatter-Gather Load

Extend the driver to accept a scatter-gather list of up to three segments per transfer. Use `bus_dmamap_load` with a multi-segment callback; store the segment list in the softc; program the simulated engine with three (address, length) pairs.

**What it exercises:** the non-single-segment callback; the tag's `nsegments` parameter; the engine's ability to handle multiple segments.

### Challenge 3: Descriptor Ring Sketch

Extend the driver with a small descriptor ring (eight entries). Each entry is a struct with a bus address, a length, a direction, and a status. The driver fills entries, writes the ring's head doorbell, and the simulated engine walks the ring and updates status.

**What it exercises:** the full producer-consumer ring pattern; the combined sync flag for bidirectional rings; the head/tail doorbell protocol.

### Challenge 4: Partial Transfer Reporting

Modify the simulated engine to occasionally (say, every fifth transfer) report a partial transfer. Modify the driver to detect the partial result, log it, and retry the remainder.

**What it exercises:** the partial-transfer pattern from Section 7; retry logic; per-transfer state tracking.

### Challenge 5: IOMMU Awareness

On a system with the IOMMU active, add sysctls that expose the difference between the driver's bus address and the underlying physical address. Use `pmap_kextract` to get the physical address of the buffer's KVA, and compare it against `sc->dma_bus_addr`.

**What it exercises:** understanding the IOMMU's transparent remapping; observability of the abstraction.

### Challenge 6: Bounce Buffer Observability

Create a tag with `lowaddr = BUS_SPACE_MAXADDR_32BIT` to force 32-bit-only addressing. Allocate buffers above 4 GB (via `contigmalloc` with appropriate flags) and observe that `bus_dmamap_load` triggers bouncing. Expose counters for `total_bounced` from the driver's perspective.

**What it exercises:** understanding when bouncing happens; observability of the bounce path.

### Challenge 7: Tag Template Refactor

Rewrite the Stage 4 setup function to use the `bus_dma_template_*` API instead of `bus_dma_tag_create`. The behaviour should be identical; only the syntax changes.

**What it exercises:** the modern template API; the equivalence between the two tag-creation styles.

### Challenge 8: DTrace-Based Transfer Profiling

Write a DTrace script that measures the latency of DMA transfers from the helper's perspective. Attach to `myfirst_dma_do_transfer_intr` entry and exit; print a histogram of elapsed times; compare against the one-millisecond engine delay.

**What it exercises:** DTrace FBT probes; DMA timing observability; understanding of the cost breakdown.



## Troubleshooting and Common Mistakes

A reference guide to the most common problems and their fixes. Each entry has the symptom, the likely cause, and the fix.

### "bus_dma_tag_create fails with EINVAL"

**Symptom:** `device_printf` reports `bus_dma_tag_create failed: 22`.

**Likely cause:** the tag parameters are inconsistent. The `alignment` must be a power of two; the `boundary` must be a power of two and at least as large as `maxsegsz`; `maxsize` must be at least `alignment`.

**Fix:** check the parameters against the `bus_dma(9)` manual page's constraint descriptions. Common mistakes: alignment of 3 (not a power of two); boundary smaller than maxsegsz.

### "bus_dmamap_load callback runs with error != 0"

**Symptom:** the single-segment callback's `error` argument is non-zero; `dma_bus_addr` is zero.

**Likely cause:** the buffer is larger than the tag's `maxsize`, or the mapping cannot satisfy the tag's segment constraints (`EFBIG`).

**Fix:** check that the load's `buflen` matches the tag's `maxsize`; check that the buffer is contiguous if the tag allows only one segment.

### "dma_bus_addr is zero after bus_dmamap_load returned zero"

**Symptom:** the load returns success but the bus address is not populated.

**Likely cause:** the callback was not called (unlikely, but possible if the kernel's internal state is inconsistent), or the callback did not write `*addr` because of an internal error it swallowed.

**Fix:** check the callback for early returns that do not populate the output. Chapter 21's pattern is to zero `dma_bus_addr` before the load and check for zero after.

### "Transfers succeed but the buffer contents are wrong"

**Symptom:** the transfer completes successfully according to the counters, but the data in the buffer is not what the sender sent.

**Likely cause:** a missing or wrong-flavour `bus_dmamap_sync` call. On a coherent platform the bug may be invisible; on a non-coherent platform or with bounce buffers active it is visible.

**Fix:** verify every transfer has exactly one PRE sync and one POST sync matching the transfer direction. `PREWRITE`/`POSTWRITE` for host-to-device; `PREREAD`/`POSTREAD` for device-to-host.

### "bus_dmamap_unload panics with 'map not loaded'"

**Symptom:** `kldunload` panics (or prints a `WITNESS` warning) at `bus_dmamap_unload`.

**Likely cause:** the driver's teardown runs `bus_dmamap_unload` twice, or runs it when the map was never loaded.

**Fix:** guard the call with `if (sc->dma_bus_addr != 0)` and zero the field after unload. Chapter 21's teardown pattern is correct; verify the driver's teardown matches.

### "bus_dmamem_free panics with 'no allocation'"

**Symptom:** `kldunload` panics at `bus_dmamem_free`.

**Likely cause:** `dma_vaddr` is stale (was freed but not zeroed), and the teardown is running again on a second unload path.

**Fix:** set `dma_vaddr = NULL` immediately after `bus_dmamem_free`. Chapter 21's teardown pattern is correct; verify.

### "dma_complete_intrs is zero even though transfers succeed"

**Symptom:** the transfer helper returns success (via the polling path), but the completion-interrupt counter stays at zero.

**Likely cause:** `MYFIRST_INTR_COMPLETE` is not enabled in the `INTR_MASK` write, or the filter is not checking the bit.

**Fix:** verify the mask write enables `MYFIRST_INTR_COMPLETE`; verify the filter has the second `if` for the complete bit. Stage 3's changes include both.

### "cv_timedwait always returns EWOULDBLOCK"

**Symptom:** every interrupt-driven transfer times out after one second.

**Likely cause:** the task never calls `cv_broadcast(&sc->dma_cv)`, or the task is not running because the filter did not enqueue it.

**Fix:** verify the task calls `cv_broadcast(&sc->dma_cv)` in `myfirst_dma_handle_complete`. Verify the filter calls `taskqueue_enqueue` after detecting the complete bit.

### "Driver hangs on kldunload"

**Symptom:** `kldunload myfirst` blocks indefinitely.

**Likely cause:** a transfer is in flight and the helper is waiting on `dma_cv`; the detach path is waiting for `dma_in_flight` to clear; but the device is not completing.

**Fix:** the detach path must call `callout_drain` before waiting for `dma_in_flight`. If the callout has not been drained, the simulation can complete the transfer and the detach proceeds. Verify the order: set `detaching = true`, drain callout, wait for in-flight to clear, tear down.

### "WITNESS warns about lock order reversal"

**Symptom:** `dmesg` shows a `WITNESS` warning involving `sc->mtx`, `dma_cv`, or the taskqueue lock.

**Likely cause:** the helper holds `sc->mtx` while calling `cv_timedwait`, which is correct; but an assertion may be triggered if a different path also holds a conflicting lock.

**Fix:** review the lock ordering. The Chapter 11/19/20 discipline: `sc->mtx` before `dma_cv`; no taskqueue locks held by the driver; no `dma_cv` wait while holding an atomic lock. Chapter 21's patterns are correct; the warning likely indicates a driver-local issue.

### "Counters climb but the buffer is not populated"

**Symptom:** `dma_transfers_read` counter increases, but `sc->dma_vaddr` is all zeros after the transfer.

**Likely cause:** the simulation's `dma_scratch` pattern is not being copied into the host buffer, or the POST sync is skipped.

**Fix:** verify the simulated engine's `myfirst_sim_dma_copy_to_host` is called with the correct length; verify the POST sync runs. If the simulation uses a simplified mapping (bus address == KVA), verify that the host's KVA actually corresponds to the tag's allocated buffer.

### "Load-unload cycle leaks memory"

**Symptom:** after 50 load-unload cycles, `vmstat -m` shows `bus_dmamap` or `bus_dma_tag` with non-zero counts.

**Likely cause:** a failure path in setup did not destroy the tag or free the memory before returning.

**Fix:** review every failure return in `myfirst_dma_setup`. Chapter 21's pattern destroys the tag on any post-tag-create failure, frees memory on any post-alloc failure, unloads the map on any post-load failure. Verify each failure path unwinds correctly.

### "Transfers are slow compared to expected"

**Symptom:** the 1000-transfer test takes many seconds instead of milliseconds.

**Likely cause:** the polling loop's `pause` is too coarse, or the interrupt path is mis-configured so every transfer hits the 1-second timeout.

**Fix:** if using the polling helper, reduce the `pause` interval; if using the interrupt helper, verify the complete bit is reaching the filter (Stage 3's `dma_complete_intrs` should match the transfer count exactly).



## Worked Example: Tracing a DMA Transfer End to End

A detailed walk through a single host-to-device DMA transfer in Chapter 21's Stage 4 driver, annotated at every step with which line of code runs, what the kernel is doing underneath, what `WITNESS` would check, and what an operator would see in `dmesg` or a counter. The goal is to give the reader a mental picture of the full pipeline as a sequence of concrete events.

### The Starting State

The driver has attached. `dma_tag`, `dma_map`, `dma_vaddr`, and `dma_bus_addr` are populated. The interrupt mask has `MYFIRST_INTR_COMPLETE` set. The rx vector's task is initialised. All counters are at zero. A user logs in and types:

```sh
sudo sysctl dev.myfirst.0.dma_test_write=0xAA
```

### Event 1: The sysctl Handler Runs

The sysctl framework calls `myfirst_dma_sysctl_test_write`. The handler parses the value (0xAA), fills `sc->dma_vaddr` with 0xAA bytes, and calls `myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE, MYFIRST_DMA_BUFFER_SIZE)`.

Context: process context, the user's `sysctl` binary's kernel side. No locks held yet. `WITNESS` has nothing to check.

### Event 2: The Helper Takes the Softc Lock

`myfirst_dma_do_transfer_intr` acquires `sc->mtx` via `MYFIRST_LOCK(sc)`. `WITNESS` checks the lock order: the softc mutex is an `MTX_DEF` lock, no higher-ordered locks are held, the acquisition is valid.

The helper checks `sc->dma_in_flight`. It is false (no transfer is running). The helper proceeds.

### Event 3: The PRE Sync

`bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE)` runs. On amd64, this is a memory barrier (`mfence`). On arm64 with `BUS_DMA_COHERENT` honoured, it is a data-memory barrier. On older arm without coherent DMA, it would be a cache flush of the buffer's lines.

The kernel's internal state: the tag's map tracker marks the map as pending PRE. If bounce pages were in use (they are not on Chapter 21's amd64 test), the bounce data would be copied now.

### Event 4: The Device Programming

The helper writes four registers in sequence: `DMA_ADDR_LOW`, `DMA_ADDR_HIGH`, `DMA_LEN`, `DMA_DIR`. Each write goes through `bus_write_4`, which dispatches through the Chapter 16 accessor to the simulation's write hook. The simulation records the values in `sim->dma`.

The helper writes `DMA_CTRL = MYFIRST_DMA_CTRL_START`. The write triggers `myfirst_sim_dma_ctrl_written` in the simulation.

### Event 5: The Simulation Arms the Callout

Inside the simulation, `myfirst_sim_dma_ctrl_written` sees `MYFIRST_DMA_CTRL_START`, checks that no transfer is already running, sets `status = RUNNING`, sets `armed = true`, and calls `callout_reset(&sim->dma.done_co, hz / 100, myfirst_sim_dma_done_co, sim)`.

The callout is scheduled ten milliseconds in the future. The simulation returns; the helper's `CSR_WRITE_4` returns.

### Event 6: The Helper Waits

The helper sets `sc->dma_last_direction = MYFIRST_DMA_DIR_WRITE`, sets `sc->dma_in_flight = true`, and calls `cv_timedwait(&sc->dma_cv, &sc->mtx, hz)`.

`cv_timedwait` releases `sc->mtx`, puts the thread to sleep on `dma_cv`, and schedules a wakeup at `hz` (1 second) in the future. The thread is now off the run queue.

Context: the kernel scheduler is free to run other work. `WITNESS` tracks that the thread went to sleep while holding no non-sleepable locks; this is correct because the mutex was released.

### Event 7: The Callout Fires

Ten milliseconds later, the callout subsystem runs `myfirst_sim_dma_done_co` on a softclock thread. The function extracts the bus address (`((sim->dma.addr_high << 32) | sim->dma.addr_low)`), confirms `len <= scratch_size`, and calls `myfirst_sim_dma_copy_from_host` to copy the 0xAA pattern from the host buffer into the scratch buffer.

The function sets `sim->dma.status = DONE` and calls `myfirst_sim_dma_raise_complete`.

Context: softclock thread. No driver lock held. `WITNESS` checks that the callout's callback is not acquiring driver locks in a way that conflicts with the callout_mtx; the sim's data is protected by per-sim state or atomic updates.

### Event 8: The Simulation Raises the Completion Bit

`myfirst_sim_dma_raise_complete` sets `MYFIRST_INTR_COMPLETE` in `sim->intr_status` and, since the bit is enabled in `intr_mask`, calls `myfirst_sim_raise_intr`. The latter queues the simulated interrupt event through the Chapter 19/20 path.

### Event 9: The Filter Runs

The kernel's interrupt machinery dispatches the rx vector. The filter (`myfirst_msix_rx_filter`) runs in primary interrupt context. It reads `INTR_STATUS`, sees `MYFIRST_INTR_COMPLETE` set, acknowledges the bit, increments `sc->dma_complete_intrs` via `atomic_add_64`, and enqueues the rx task.

The filter returns `FILTER_HANDLED`. The kernel's interrupt dispatch ends.

Context: primary interrupt context. No sleep, no malloc, no lock above the spinlock level. `WITNESS` checks that the atomic increment does not imply any lock.

### Event 10: The Task Runs

The taskqueue machinery schedules the rx task on its thread. The thread acquires `sc->mtx` via `MYFIRST_LOCK(sc)`. It reads `DATA_OUT` (from the Chapter 19/20 pipeline) and broadcasts `data_cv`. Then it calls `myfirst_dma_handle_complete(sc)`.

### Event 11: The POST Sync

`myfirst_dma_handle_complete` sees `sc->dma_in_flight == true`. It clears the flag, checks the direction (`DIR_WRITE`), issues `bus_dmamap_sync(..., BUS_DMASYNC_POSTWRITE)`, reads `DMA_STATUS` (which shows `DONE`), increments `dma_complete_tasks`, and calls `cv_broadcast(&sc->dma_cv)`.

The POST sync on amd64 is a barrier. On arm64 with coherent memory, a barrier. On systems with bounce buffers, the bounce data would be copied back now (for a POSTREAD; POSTWRITE usually just releases the bounce page).

### Event 12: The Task Releases the Lock

The task's body completes. It releases `sc->mtx` via `MYFIRST_UNLOCK(sc)`. The task returns.

### Event 13: The Helper Wakes

`cv_broadcast(&sc->dma_cv)` in Event 11 scheduled the helper's thread back to the run queue. The helper wakes inside `cv_timedwait`, re-acquires `sc->mtx`, and returns from the wait with `err = 0` (not `EWOULDBLOCK`).

### Event 14: The Helper Examines the Status

The helper reads `sc->dma_last_status`. It is `DONE` with `ERR = 0`. The helper increments `dma_transfers_write` via `atomic_add_64`. It releases `sc->mtx` and returns 0.

### Event 15: The Sysctl Handler Returns

`myfirst_dma_sysctl_test_write` sees the successful return, prints a banner (`dma_test_write: pattern 0xaa transferred`), and returns 0 to the sysctl framework.

### Event 16: The User Sees the Result

The user's `sysctl` command returns. A subsequent `dmesg | tail` shows the banner. The counters now show:

```text
dma_transfers_write: 1
dma_complete_intrs: 1
dma_complete_tasks: 1
```

### The Total Time

The critical path's wall-clock time is roughly: 10 ms for the callout delay, plus a few microseconds for the filter dispatch, plus a few microseconds for the task, plus the context-switch latency for the helper's wakeup. Total: about 10-11 ms.

If the helper had used the Stage 2 polling path instead, the wall-clock would be similar (the callout delay dominates), but the helper would hold a CPU busy polling for the entire ten milliseconds. The interrupt-driven version frees the CPU to do other work.

### The Lesson

Sixteen events across five contexts: process (helper), softclock (callout), primary interrupt (filter), taskqueue thread (task), process (helper wake). Each context has its own lock discipline; the transitions between contexts happen via the kernel's scheduling and interrupt machinery, not through driver code. The driver's code lives inside each context; the kernel moves the execution between them.

A reader who can trace this sequence from beginning to end understands Chapter 21. A reader who cannot can re-read Sections 5 and 6 with the sequence in mind; the individual steps become concrete when mapped to the narrative.



## A Deeper Look at Locking and DMA

Chapter 11 and Chapter 19 established the driver's lock discipline. Chapter 21 applies it to the DMA path, which introduces a few specifics worth calling out.

### The Softc Mutex Covers the DMA State

The softc mutex (`sc->mtx`) protects every DMA field that is read or written outside of attach/detach: `dma_in_flight`, `dma_last_direction`, `dma_last_status`, the per-transfer counters. Every code path that touches these fields acquires `sc->mtx` first.

The counters are updated with `atomic_add_64` so the filter (which cannot acquire a sleep mutex) can increment them without taking the lock. The same counters are read with `atomic_load_64` (implicitly via the sysctl handler's copy) when the reader wants a consistent snapshot. Using atomics instead of the mutex for counters is the Chapter 11 pattern; it is faster and filter-safe.

The condition variable `dma_cv` is waited on and broadcast under `sc->mtx`. This is standard `cv_*` discipline.

### The Tag's Lockfunc

The `lockfunc` parameter of `bus_dma_tag_create` is for deferred loads. Chapter 21 passes `NULL`, which substitutes `_busdma_dflt_lock`, which panics if called. The panic is the safety net: it tells the developer that a deferred load happened (which Chapter 21's `BUS_DMA_NOWAIT` usage should prevent), at which point the developer would add a proper lockfunc.

For drivers that do use deferred loads, the common pattern is:

```c
err = bus_dma_tag_create(parent, align, bdry, lowaddr, highaddr,
    NULL, NULL, maxsize, nseg, maxsegsz, 0,
    busdma_lock_mutex, &sc->mtx, &sc->tag);
```

`busdma_lock_mutex` is a kernel-provided implementation that acquires and releases the mutex passed as the last argument. The kernel calls it before and after the deferred callback.

### No Locks in the Sync Path

`bus_dmamap_sync` does not internally acquire any driver lock. It is safe to call from filter, task, or process context, as long as the caller has the appropriate concurrency discipline for the map. Chapter 21 calls sync from the helper (with `sc->mtx` held) and from the task (with `sc->mtx` held, inside `myfirst_dma_handle_complete`). Both are safe because no thread outside the lock scope can concurrently sync the same map.

### The Detach Race

The detach path's race is between "transfer is completing" and "driver is being unloaded". Chapter 21's pattern uses the `detaching` flag plus the `dma_in_flight` wait to ensure the detach cannot tear down resources while a transfer is in progress. The `callout_drain` and `taskqueue_drain` ensure no callback is pending.

This pattern is the Chapter 11 detach discipline generalised to one more kind of in-flight work (DMA transfers). The pattern composes: interrupts were drained in Chapter 19, per-vector tasks in Chapter 20, DMA transfers in Chapter 21. Chapter 22 will add one more (power-transition callbacks).



## A Deeper Look at Observability for DMA Drivers

Chapter 21's driver exposes several observability hooks. This deeper look covers what each one provides and how an operator would use them.

### The Per-Transfer Counters

```text
dev.myfirst.N.dma_transfers_write
dev.myfirst.N.dma_transfers_read
dev.myfirst.N.dma_errors
dev.myfirst.N.dma_timeouts
dev.myfirst.N.dma_complete_intrs
dev.myfirst.N.dma_complete_tasks
```

Six counters, each a 64-bit atomic. The invariants:

- `dma_complete_intrs == dma_complete_tasks`: every filter-observed completion produced a task invocation.
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`: every completion interrupt eventually produced a transfer outcome.

Violations of either invariant indicate a bug. The regression test checks both.

### The Ring Buffer of Recent Transfers

A challenge exercise would add a small circular buffer of recent transfers, each recording the direction, length, result, and time. A read-only sysctl exposes the last N transfers as a kernel-formatted string. This is useful for post-mortem debugging (when the driver has hung, the ring buffer shows what was happening just before).

### DTrace Probes

The driver has no explicit DTrace probes, but FBT (function boundary tracing) works automatically. Useful one-liners:

```console
# Count DMA transfer calls
dtrace -n 'fbt::myfirst_dma_do_transfer_intr:entry { @[probefunc] = count(); }'

# Histogram of transfer durations
dtrace -n 'fbt::myfirst_dma_do_transfer_intr:entry { self->start = vtimestamp; } fbt::myfirst_dma_do_transfer_intr:return /self->start/ { @[probefunc] = quantize(vtimestamp - self->start); self->start = 0; }'

# Interrupt rate per vector
dtrace -n 'fbt::myfirst_msix_rx_filter:entry { @ = count(); } tick-1s { printa(@); clear(@); }'
```

DTrace is the best tool for understanding driver behaviour under load. The probes run with low overhead (tens of nanoseconds per hit on amd64) and can be enabled on a production system.

### `vmstat -i` and `systat -vmstat`

`vmstat -i` shows the interrupt count per vector. For Chapter 21's driver with MSI-X, three vectors appear with their `bus_describe_intr` labels (`admin`, `rx`, `tx`). The `rx` line's count should equal `dma_complete_intrs` plus `data_av_count` (the combined activity on that vector).

`systat -vmstat` provides a live view. Running it in a second terminal while the test runs shows the vector's rate changing in real time.

### `procstat -kke`

`procstat -kke` shows the kernel stack of every thread. The rx task's thread, when running the DMA handler, has a stack containing `myfirst_dma_handle_complete` and `bus_dmamap_sync`. Observing the stack confirms the thread is in the expected place; an unexpected stack suggests a hang or deadlock.

### `sysctl hw.busdma`

The `hw.busdma` sysctl tree exposes DMA-subsystem-wide counters:

```text
hw.busdma.total_bpages
hw.busdma.free_bpages
hw.busdma.reserved_bpages
hw.busdma.active_bpages
hw.busdma.total_bounced
hw.busdma.total_deferred
```

`total_bounced` is the most useful for Chapter 21 purposes: on a system where no driver should be bouncing, this stays at zero. A climbing value indicates some driver's tag constraints are forcing bouncing; this is usually a configuration issue.



## A Deeper Look at Performance of DMA Transfers

Chapter 21's simulation runs every transfer in about ten milliseconds (the callout delay). Real hardware is much faster. This deeper look covers what the measurements tell us and what they do not.

### The Latency Budget

A modern PCIe 3.0 device's DMA latency is dominated by:

1. **The PCIe posted-write latency.** The device writes to the CPU's memory controller via a posted write, which takes a few hundred nanoseconds round-trip.
2. **The cache flush/invalidate overhead.** On coherent systems this is tens of nanoseconds via snooping; on non-coherent systems it is microseconds.
3. **The interrupt delivery latency.** MSI-X delivery to a CPU takes microseconds including the ithread wakeup.
4. **The task dispatch latency.** The rx task's wakeup and dispatch takes microseconds.

Total: a DMA transfer of a 4 KB buffer on a modern amd64 system takes about 5-10 microseconds from kick to completion, for a sustained throughput of several gigabytes per second.

### The Throughput Ceiling

With a 4 KB buffer and 10 microseconds per transfer, the throughput is 400 MB/s. To reach the link's 3.5 GB/s, the driver has to batch: multiple transfers in flight simultaneously. A descriptor ring with N entries can have N transfers in flight, and if each takes 10 microseconds but the driver issues one per microsecond, the aggregate throughput is 4 GB/s.

Chapter 21's single-buffer driver cannot batch; it issues one transfer at a time. The descriptor-ring extension (challenge exercise) can. Part 6's network driver (Chapter 28) uses this pattern in full.

### Measuring the Chapter's Driver

A simple timing test:

```c
struct timespec start, end;
nanouptime(&start);
for (int i = 0; i < 1000; i++) {
    myfirst_dma_do_transfer_intr(sc, DIR_WRITE, 4096);
}
nanouptime(&end);
elapsed = timespec_sub(end, start);
```

With the simulation's ten-millisecond callout, 1000 transfers should take about 10 seconds total, for a throughput of 400 KB/s. Small, because the simulation is designed to be observably asynchronous rather than fast. Real hardware would be orders of magnitude faster.

### What the Measurement Tells Us

The measurement confirms the pipeline is end-to-end functional. The absolute number is not interesting (it is a simulation, not real hardware). What matters is that 1000 transfers complete without error, without timeout, without the helper hanging, and without the counters diverging from their invariants. That is the regression criterion for Chapter 21.

On a real DMA-capable device, replacing the simulation backend with the real hardware driver would swap the 10 ms callout delay for the hardware's actual latency. The Chapter 21 code is otherwise unchanged; this is the portability payoff of building against `bus_dma`.



## Additional Troubleshooting: Ten More Failure Patterns

Ten more common problems that surface in Chapter 21-style DMA drivers, each with diagnosis and fix.

### "Kernel panics on first transfer with 'null pointer dereference'"

The likely cause is that `sc->dma_tag` or `sc->dma_map` is NULL because the setup failed silently. The Stage 1 setup's error paths must propagate failure up so attach fails cleanly; if they do not, attach appears to succeed but the transfer path hits the NULL pointer.

The fix: review every return path in `myfirst_dma_setup`. Each post-allocation failure must free the preceding allocations and set the softc fields to NULL.

### "First transfer works, subsequent transfers hang"

Likely cause: `dma_in_flight` is not being cleared on the first transfer's completion. The task's `dma_handle_complete` must clear it; the helper's timeout path must also clear it.

The fix: verify `dma_in_flight = false` is set in both `dma_handle_complete` and the helper's `EWOULDBLOCK` branch.

### "Transfers succeed but dma_complete_intrs is zero"

Likely cause: the polling helper is in use, not the interrupt helper. The polling path does not increment `dma_complete_intrs`.

The fix: verify the sysctl handler calls `myfirst_dma_do_transfer_intr`, not the Stage 2 polling version. The Stage 3 refactor should have replaced the callers.

### "Buffer contents are correct but verification fails"

Likely cause: a stride mismatch between what the simulator wrote and what the driver expected. The driver expects 0x5A bytes; the simulator writes 0x5A; but the driver checks a different offset.

The fix: verify the sim's `memset` matches the driver's verification. This is almost always a test-code bug rather than a DMA bug.

### "dma_errors counter climbs but the driver does not propagate the error"

Likely cause: the helper increments the counter but returns 0 instead of EIO.

The fix: verify the error branch returns EIO and the sysctl handler propagates the error to the caller.

### "taskqueue_drain hangs on kldunload"

Likely cause: a task is pending, but something keeps re-enqueueing it. Most commonly, the filter is still running because the interrupt mask was not set to zero.

The fix: mask the interrupt before draining the taskqueue. The detach order matters: mask, drain callout, drain task, unload map.

### "bus_dmamem_alloc returns a buffer above 4 GB despite lowaddr = MAXADDR_32BIT"

Likely cause: the tag's `highaddr` is `BUS_SPACE_MAXADDR_32BIT` instead of `BUS_SPACE_MAXADDR`. The meaning of `lowaddr` is "the lowest address in the excluded window"; `highaddr` is "the highest address in the excluded window". To exclude addresses above 4 GB, set `lowaddr = BUS_SPACE_MAXADDR_32BIT` and `highaddr = BUS_SPACE_MAXADDR`.

The fix: swap the addresses. The idiom is: `lowaddr` is "the last address the device can reach"; `highaddr` is usually `BUS_SPACE_MAXADDR`.

### "Test script runs fine once, fails after module reload"

Likely cause: the module's module-level state is not being reset on reload. A static variable in `myfirst_sim.c` that holds the sim state across loads would cause this.

The fix: verify every module-level variable is initialised at `module_init` time and cleared at `module_fini`. Chapter 17's simulation backend should be doing this correctly.

### "WITNESS warns about 'DMA map used while not loaded' or similar"

Likely cause: the sync call runs after the map has been unloaded, or before it has been loaded. The order of operations must be: load, sync, unload.

The fix: review the teardown path; the unload must come after the last sync and before the tag destroy.

### "Transfers work on my amd64 test VM but fail on the arm64 build server"

Likely cause: platform-dependent coherence behaviour. The driver may be skipping a sync call that is a no-op on amd64 but required on arm64.

The fix: verify every transfer has both PRE and POST syncs; re-run the test with `INVARIANTS` on arm64 and inspect any panic.



## Reference: The Complete Stage 4 `myfirst_dma.c` Walkthrough

A section-by-section tour of the refactored `myfirst_dma.c` file. The file is about 250 lines; this walkthrough explains each function's shape.

### The Includes and Macros

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_dma.h"
```

Standard set. `<sys/bus_dma.h>` is included transitively through `<machine/bus.h>`; no need to include it explicitly.

### The Single-Segment Callback

Covered in Section 3. Ten lines.

### The Setup Function

Covered in Section 3. About 50 lines with extensive error handling.

### The Teardown Function

Covered in Section 3. About 15 lines.

### The Polling Transfer Helper

Covered in Section 5. About 60 lines. Kept in Stage 4 as a fallback or a debug path; the sysctl handlers can be pointed at it for comparative testing.

### The Interrupt-Driven Transfer Helper

Covered in Section 6. About 70 lines.

### The Completion Handler

Covered in Section 8. About 20 lines.

### The Sysctl Registration

```c
void
myfirst_dma_add_sysctls(struct myfirst_softc *sc)
{
    struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
    struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);

    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_write",
        CTLFLAG_RD, &sc->dma_transfers_write, 0,
        "Successful host-to-device DMA transfers");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_transfers_read",
        CTLFLAG_RD, &sc->dma_transfers_read, 0,
        "Successful device-to-host DMA transfers");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_errors",
        CTLFLAG_RD, &sc->dma_errors, 0,
        "DMA transfers that returned EIO");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_timeouts",
        CTLFLAG_RD, &sc->dma_timeouts, 0,
        "DMA transfers that timed out");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_intrs",
        CTLFLAG_RD, &sc->dma_complete_intrs, 0,
        "DMA completion interrupts observed");
    SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "dma_complete_tasks",
        CTLFLAG_RD, &sc->dma_complete_tasks, 0,
        "DMA completion task invocations");
    SYSCTL_ADD_UQUAD(ctx, kids, OID_AUTO, "dma_bus_addr",
        CTLFLAG_RD, &sc->dma_bus_addr,
        "Bus address of the DMA buffer");

    SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_write",
        CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
        myfirst_dma_sysctl_test_write, "IU",
        "Trigger a host-to-device DMA transfer");
    SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "dma_test_read",
        CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE, sc, 0,
        myfirst_dma_sysctl_test_read, "IU",
        "Trigger a device-to-host DMA transfer");
}
```

Seven read-only counters, one UQUAD bus address (for operator visibility), two write-only test sysctls.

### The Sysctl Handlers

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
    struct myfirst_softc *sc = arg1;
    unsigned int pattern;
    int err;

    err = sysctl_handle_int(oidp, &pattern, 0, req);
    if (err != 0 || req->newptr == NULL)
        return (err);

    memset(sc->dma_vaddr, (int)(pattern & 0xFF),
        MYFIRST_DMA_BUFFER_SIZE);
    err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_WRITE,
        MYFIRST_DMA_BUFFER_SIZE);
    if (err != 0)
        device_printf(sc->dev,
            "dma_test_write: err %d\n", err);
    else
        device_printf(sc->dev,
            "dma_test_write: pattern 0x%02x transferred\n",
            pattern & 0xFF);
    return (err);
}

static int
myfirst_dma_sysctl_test_read(SYSCTL_HANDLER_ARGS)
{
    struct myfirst_softc *sc = arg1;
    unsigned int ignore;
    int err;
    uint8_t *bytes;

    err = sysctl_handle_int(oidp, &ignore, 0, req);
    if (err != 0 || req->newptr == NULL)
        return (err);

    err = myfirst_dma_do_transfer_intr(sc, MYFIRST_DMA_DIR_READ,
        MYFIRST_DMA_BUFFER_SIZE);
    if (err != 0) {
        device_printf(sc->dev, "dma_test_read: err %d\n", err);
        return (err);
    }

    bytes = (uint8_t *)sc->dma_vaddr;
    device_printf(sc->dev,
        "dma_test_read: first bytes %02x %02x %02x %02x "
        "%02x %02x %02x %02x\n",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7]);
    return (0);
}
```

The write handler fills the buffer and triggers host-to-device. The read handler triggers device-to-host and logs the first eight bytes. Both are small; both make the DMA subsystem user-accessible without requiring a custom ioctl.

### Lines of Code

The full `myfirst_dma.c` file is about 280 lines. For comparison, Chapter 20's `myfirst_msix.c` was 330 lines; Chapter 19's `myfirst_intr.c` was 250 lines. Chapter 21's file is in the middle of that range, which matches the complexity: DMA setup is more intricate than single-vector interrupt setup, but less than multi-vector routing.

Taken together with the 330-line `myfirst_msix.c` and the 250-line `myfirst_intr.c`, the three Chapter 19-21 files account for about 860 lines. A reader who has written and understood them has written three production-grade driver subsystems in miniature.



## Reference: FreeBSD Manual Pages for Chapter 21

The manual pages Chapter 21 has drawn on directly. Each is worth a read after the chapter's material has settled.

- `bus_dma(9)`: the central reference for the bus-DMA API.
- `bus_space(9)`: the accessor layer the driver builds on (Chapter 16).
- `contigmalloc(9)`: the contiguous allocator used by `bus_dma` underneath.
- `busdma(9)` cross-references, including the `bus_dma_tag_template(9)` entry.
- `callout(9)`: the timer subsystem the simulation's engine uses.
- `condvar(9)`: the `cv_*` discipline the helper uses.
- `device(9)`: the general device framework.

The manual pages are maintained with the kernel and track changes to the API. Reading them once per major release (or after a significant change in the book's coverage) is a useful habit.



## A Deeper Look at How DMA and Interrupts Fit Together

Chapters 19, 20, and 21 have built three interlocking pieces: interrupts, multiple vectors, DMA. This deeper look explains how they fit together in a complete high-performance driver, so the reader has a mental model of the whole before Chapter 22's power-management discipline arrives.

### The Three-Piece Architecture

A modern DMA-capable driver has three cooperating subsystems:

1. **The DMA subsystem** (Chapter 21) owns the data path. It sets up buffers, programs the device, synchronises caches, and handles the transfer's data. Any time the driver moves data of non-trivial size, DMA is involved.

2. **The interrupt subsystem** (Chapters 19 and 20) owns the signalling path. The device signals events (data arrival, transfer completion, errors) via interrupts. The filter handles the signal in primary interrupt context; the task handles the follow-up in thread context.

3. **The coordination layer** (the softc and its mutex plus condition variables) owns the state between the two. The DMA subsystem writes `dma_in_flight` before triggering a transfer; the interrupt subsystem reads it during completion processing. The condition variable lets the DMA subsystem wait for the interrupt subsystem to signal completion.

The three pieces are designed to compose. A driver can have many DMA operations in flight, each signalling its completion through its own interrupt vector; the per-vector machinery from Chapter 20 handles this naturally, and the per-transfer state from Chapter 21 plugs into the per-vector tasks.

### The Common Flow

For every DMA transfer in a production driver, the flow is:

1. Driver code wants to transfer data.
2. Driver locks the softc, sets up the per-transfer state.
3. Driver issues the PRE sync on the map.
4. Driver programs the device's registers with the bus address, length, direction, and flags.
5. Driver writes the doorbell.
6. Driver releases the lock (or sleeps on a condition variable under the lock).
7. Kernel does other work.
8. Device completes the transfer and raises its interrupt.
9. Filter runs, acknowledges, enqueues a task.
10. Task runs, acquires the softc lock, processes the completion: POST sync, status read, result recording.
11. Task broadcasts the condition variable and releases the lock.
12. Driver code (or its waiter) wakes up, reads the result, and proceeds.

The flow is the same for a simple single-buffer driver and for a multi-queue network driver with thousands of transfers in flight. The difference is scale: the network driver has many concurrent transfers, each with its own per-transfer state, but each one follows the same twelve-step flow.

### The Scale Story

On Chapter 21's single-buffer simulation, the flow runs once per ten milliseconds. On a modern NIC, the flow runs millions of times per second across many queues. The machinery scales because:

- Interrupts are per-queue via MSI-X (Chapter 20).
- DMA buffers are per-queue via descriptor rings (a future chapter).
- The filter is short and filter-safe (Chapter 19).
- The task is per-queue and CPU-bound to a NUMA-local core (Chapter 20).
- The softc state is partitioned per-queue to avoid contention.

The cumulative design decisions Parts 4 and 5 teach are what make this scaling possible. A driver that respects the discipline scales naturally; a driver that violates it (a global lock instead of per-queue locks, a shared DMA map instead of per-transfer maps) hits scaling limits early.

### The Interaction Patterns

Three specific interaction patterns worth recognising:

**Pattern A: Interrupt delivers "new data is in the DMA ring".** The filter reads a completion index register, acknowledges, enqueues the task. The task walks the ring from the last index to the current, processes each entry, and updates its per-queue state. This is how a NIC's receive path works.

**Pattern B: Interrupt delivers "DMA transfer N is complete".** The filter looks up transfer N in a per-transfer state array, enqueues the task. The task looks at transfer N's recorded state, performs the POST sync, and calls the completion callback. This is how a storage controller's command-completion path works.

**Pattern C: Interrupt delivers "error occurred on DMA engine".** The filter may need more care here because errors often require device-level reset, which cannot happen in filter context. The filter enqueues a task with an error-specific payload; the task quiesces the engine, logs the error, and decides whether to reset.

Chapter 21's driver exercises a simplified Pattern B. Real drivers exercise all three, sometimes on separate vectors.



## A Deeper Look at Comparisons with Other Kernel DMA APIs

For readers who come from other kernel contexts, a brief comparison between FreeBSD's `bus_dma(9)` and the equivalent APIs elsewhere. Not exhaustive; just enough to orient a reader who needs to translate between mental models.

### Linux's DMA API

Linux uses `dma_alloc_coherent` / `dma_map_single` / `dma_sync_single_for_cpu` / `dma_sync_single_for_device` / `dma_unmap_single` for what FreeBSD calls `bus_dmamem_alloc` / `bus_dmamap_load` / `bus_dmamap_sync` (POST) / `bus_dmamap_sync` (PRE) / `bus_dmamap_unload`.

The semantic model is nearly identical:

- `dma_alloc_coherent` returns a CPU-visible virtual address and a DMA-visible bus address for a coherent buffer; `bus_dmamem_alloc` plus `bus_dmamap_load` does the same in two steps.
- `dma_map_single` maps an arbitrary buffer to a bus address, similar to `bus_dmamap_load` with a single-segment callback.
- `dma_sync_single_for_cpu` corresponds to `bus_dmamap_sync(..., POSTREAD)`; `for_device` corresponds to `PREWRITE` or `PREREAD`.
- `dma_unmap_single` is the Linux version of `bus_dmamap_unload`.

The FreeBSD API's distinctive features are the tag (an explicit constraint descriptor; Linux's approach is more implicit), the callback (for deferred loads; Linux uses DMA fence APIs instead), and the hierarchical tag inheritance (which Linux does not formalise the same way).

For Linux driver code being ported to FreeBSD, the rough translation is:

- Define a `bus_dma_tag_t` tag with the device's constraints.
- Replace `dma_alloc_coherent` with `bus_dma_tag_create` (once) plus `bus_dmamem_alloc` plus `bus_dmamap_load`.
- Replace `dma_map_single` per-transfer with `bus_dmamap_create` plus `bus_dmamap_load` (for dynamic maps).
- Replace every `dma_sync_*_for_cpu` with `bus_dmamap_sync(..., POSTREAD)` or `POSTWRITE`.
- Replace every `dma_sync_*_for_device` with `bus_dmamap_sync(..., PREREAD)` or `PREWRITE`.
- Replace `dma_unmap_single` with `bus_dmamap_unload`.

The DRM-kmod ports of Linux GPU drivers to FreeBSD use this translation extensively; a driver ported from Linux to FreeBSD typically gains explicit tag setup and explicit PRE/POST pairs where Linux had implicit ones.

### NetBSD's bus_dma

NetBSD has the original `bus_dma` API that FreeBSD's is derived from. The function names are almost identical; the semantics are almost identical. The differences are mostly in peripheral APIs (template support, IOMMU integration, platform-specific extensions).

A driver written for NetBSD's `bus_dma` generally compiles on FreeBSD with minor adjustments. The portability is not accidental; it is the API's design goal.

### Windows's DMA Abstractions

Windows uses a different abstraction (`AllocateCommonBuffer`, `IoMapTransfer`, `FlushAdapterBuffers`) with different semantics. Translating a Windows driver to FreeBSD is a more involved task because the Windows model does not have FreeBSD's tag concept and its synchronisation is less explicit.

A reader who works in both ecosystems benefits from understanding that the underlying hardware realities are the same everywhere; only the API surface differs. The `bus_dma` discipline from Chapter 21 transfers, albeit with a translation layer, to every kernel environment.



## Reference: A Short Tour of `/usr/src/sys/kern/subr_bus_dma.c`

The kernel's implementation of the `bus_dma` API lives under `/usr/src/sys/kern/subr_bus_dma.c` and the architecture-specific `busdma_*.c` files. A short tour of the central file gives the curious reader a sense of where the machinery actually runs.

The file contains generic helpers that the architecture-specific backends call. The key entry points:

- `bus_dmamap_load_uio`: load a `struct uio` (user I/O).
- `bus_dmamap_load_mbuf_sg`: load an mbuf chain with a pre-allocated segment array.
- `bus_dmamap_load_ccb`: load a CAM control block.
- `bus_dmamap_load_bio`: load a `struct bio`.
- `bus_dmamap_load_crp`: load a crypto operation.

Each of these is a thin wrapper around the platform's `bus_dmamap_load_ma` (load array-of-mappings) or `bus_dmamap_load_phys` (load physical address), which are the architecture-specific primitives.

The file also contains the template API (`bus_dma_template_*`) and its helpers. The template code is about 200 lines and straightforward to read.

The generic file does not contain the per-architecture sync or bounce logic; that lives in `/usr/src/sys/x86/x86/busdma_bounce.c` for amd64 and i386, or in the equivalent platform directory. A reader who wants to understand what `bus_dmamap_sync` actually does on amd64 can read `busdma_bounce.c`'s `bounce_bus_dmamap_sync` function; it is about 100 lines and shows the bounce-copy logic plus the memory barriers.



## Reference: Driver Memorable Phrases

A short list of the phrases worth memorising because they compress the chapter's discipline into a few words.

- "Tag describes, map is specific, sync signals ownership, unload reverses load."
- "Every PRE has a POST; every setup has a teardown."
- "`BUS_DMA_COHERENT` makes sync cheap, not unnecessary."
- "The bus address is not always the physical address."
- "`BUS_SPACE_MAXADDR_32BIT` is `lowaddr` for 32-bit devices; `highaddr` stays `BUS_SPACE_MAXADDR`."
- "The callback may run later; use `BUS_DMA_NOWAIT` to avoid that case."
- "The tag lockfunc is for deferred loads; `NULL` panics if deferred, which is the safer default."
- "Single-segment callbacks look the same in every driver; the pattern is universal."
- "Drain callouts before unloading maps; drain tasks before destroying tags."



## Reference: A Comparison Table Across Part 4

A table that summarises what each Part 4 chapter added, how the driver's version tag changed, and what new resource type the driver gained access to.

| Chapter | Version      | Added Subsystem                         | New Resource Types                          |
|---------|--------------|-----------------------------------------|---------------------------------------------|
| 16      | 0.9-mmio     | Register access via bus_space           | bus_space_tag_t, bus_space_handle_t        |
| 17      | 1.0-sim      | Simulated hardware backend              | sim backend, simulated register map         |
| 18      | 1.1-pci      | Real PCI attach                         | BAR resource, PCI config access             |
| 19      | 1.2-intr     | Legacy interrupt handling               | IRQ resource, filter, task                  |
| 20      | 1.3-msi      | MSI/MSI-X multi-vector                  | per-vector IRQ resources, per-vector tasks  |
| 21      | 1.4-dma      | DMA with bus_dma(9)                     | DMA tag, DMA map, DMA memory, bus address   |

Each chapter adds exactly one subsystem; the driver's complexity grows monotonically with its capability. A reader tracing the version history can see at a glance what the driver can do at any stage.

Part 4 closes with Chapter 22's power-management work, which does not add a new subsystem but adds discipline across all existing ones: every subsystem must be quiescable and resumable.



## Reference: What the Reader Should Now Be Able to Do

A one-page self-check. After finishing Chapter 21 and completing the labs, the reader should be able to:

1. Open a random DMA-capable driver in `/usr/src/sys/dev/` and identify its tag creation, memory allocation, map loading, sync pattern, and teardown.
2. Explain why a driver calls `bus_dmamap_sync` with a PRE flag before triggering the device and a POST flag after the device completes.
3. Write a tag-creation call for a hypothetical device with documented constraints (alignment, size, addressing range).
4. Write a single-segment callback and use it to extract the bus address from a `bus_dmamap_load` call.
5. Recognise when a driver is using the static pattern versus the dynamic pattern.
6. Identify and explain the three portability realities `bus_dma(9)` hides: addressing limits, IOMMU remapping, cache coherence.
7. Debug a transfer that succeeds in terms of counters but produces corrupt data, by checking for missing or wrong-flavour syncs.
8. Construct a clean detach path that drains callouts, drains tasks, masks interrupts, unloads maps, frees memory, and destroys tags in the right order.
9. Distinguish between `BUS_DMA_COHERENT` at tag-create time (architectural hint) and at `bus_dmamem_alloc` time (allocation mode).
10. Explain why `BUS_DMA_NOWAIT` is the safer default for most drivers and when `BUS_DMA_WAITOK` is appropriate.
11. Write a correct single-buffer DMA driver from scratch, given a datasheet that specifies the register layout and the engine's behaviour.
12. Explain why the `bus_dma(9)` layer exists and what happens when a driver bypasses it.

A reader who checks off ten or more of these has internalised the chapter. The remaining items come with practice on real drivers.



## Reference: When to Reach for Each API Variant

The `bus_dma(9)` API has several load variants; a quick reference to help readers choose correctly in future work:

- **`bus_dmamap_load`**: a generic kernel buffer at a known KVA and length. The simplest and most common call. Used by Chapter 21.
- **`bus_dmamap_load_mbuf_sg`**: an mbuf chain, common in network drivers. The `_sg` variant fills a pre-allocated segment array and avoids the callback.
- **`bus_dmamap_load_ccb`**: a CAM control block, used by storage drivers.
- **`bus_dmamap_load_uio`**: a `struct uio`, used when mapping user-supplied buffers.
- **`bus_dmamap_load_bio`**: a block I/O request, used in GEOM consumers.
- **`bus_dmamap_load_crp`**: a crypto operation, used by OpenCrypto transforms.

The chapter uses only the first. The reader who moves to networking, storage, or crypto later will see the specialised variants; each is a thin wrapper around the same underlying mechanism, with a helper that unpacks the subsystem's preferred buffer representation into segments.

For dynamic transactions where the driver maintains its own pool of maps, `bus_dmamap_create` and `bus_dmamap_destroy` complement the load calls. Chapter 21's static pattern does not use these; the dynamic pattern in Part 6's network chapter (Chapter 28) does.

A subtle point worth remembering: `bus_dmamem_alloc` both allocates memory and creates an implicit map. The map does not need `bus_dmamap_create` or `bus_dmamap_destroy`. The driver still calls `bus_dmamap_load` once to obtain the bus address (via the callback), and `bus_dmamap_unload` once at teardown; but the map itself is managed by the allocator, not by the driver. A driver that calls `bus_dmamap_destroy` on an allocator-returned map triggers a panic. Keep the two lifecycles distinct.



## Wrapping Up

Chapter 21 gave the driver the ability to move data. At the start, `myfirst` at version `1.3-msi` could listen to its device through multiple interrupt vectors but touched every byte through `bus_read_4`. At the end, `myfirst` at version `1.4-dma` has a `bus_dma(9)` setup and teardown path, a simulated DMA engine with a callout-driven completion, an interrupt-driven transfer helper that sleeps on a condition variable while the device works, a full PRE/POST sync discipline on every transfer, per-transfer counters for observability, a refactored `myfirst_dma.c` file, a `DMA.md` document, and a regression test that exercises every code path the chapter teaches.

The eight sections walked the full progression. Section 1 established the hardware picture: what DMA is, why it matters, what the real-world examples look like. Section 2 pinned down the `bus_dma(9)` vocabulary: tag, map, memory, segment, PRE, POST, static, dynamic, parent, callback. Section 3 wrote the first running code: tag creation, memory allocation, map load, clean teardown. Section 4 laid down the sync discipline: the ownership model, the four flags, the four common patterns. Section 5 built the simulated DMA engine with register map, state machine, callout, and a polling-based driver helper. Section 6 rewrote the completion path to use the Chapter 20 per-vector interrupt machinery. Section 7 walked through twelve failure modes and the patterns that handle each. Section 8 refactored the code into `myfirst_dma.c`, updated the Makefile, bumped the version, added documentation, and closed the chapter's scope.

What Chapter 21 did not do is scatter-gather, descriptor rings, iflib integration, or user-space buffer mapping. Each of those is a natural extension built on Chapter 21's primitives, and each belongs in a later chapter (Part 6 for networking specifics, Part 7 for performance tuning). The foundation is in place; the specialisations add vocabulary without needing to rebuild the foundation.

The file layout has grown: 15 source files (including `cbuf`), 7 documentation files (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`), and an extended regression suite. The driver is structurally parallel to production FreeBSD drivers; a reader who has worked through Chapters 16 through 21 can open `if_re.c`, `nvme_qpair.c`, or `ahci.c` and recognise the architectural parts: register accessors, simulation backend, PCI attach, interrupt filter and task, per-vector machinery, DMA setup and teardown, sync discipline, clean detach.

### A Reflection Before Chapter 22

Chapter 21 was the last chapter in Part 4 that introduced a new hardware primitive. Chapters 16 through 21 took the driver from "no hardware awareness" to "fully functional hardware-backed DMA driver". Chapter 22 is the discipline chapter: how to make this driver survive suspend and resume cycles, how to save and restore state around power transitions, how to quiesce every subsystem (interrupts, DMA, timers, tasks) before the device loses power and resume it cleanly afterwards. The DMA teardown path Chapter 21 built is what Chapter 22's suspend handler will call; the DMA setup path is what Chapter 22's resume handler will call.

Chapter 21's teaching also generalises. A reader who has internalised the tag-map-memory-sync discipline, the PRE/POST ownership model, the single-segment callback, and the clean setup/teardown pattern will find similar shapes in every DMA-capable FreeBSD driver. The specific device differs; the structure does not.

### What to Do If You Are Stuck

Three suggestions.

First, focus on the Stage 2 polling path. If `sudo sysctl dev.myfirst.0.dma_test_write=0xAA` produces a correct banner in `dmesg`, the polling path is working. Every other piece of the chapter is optional in the sense that it decorates the pipeline, but if the pipeline fails, the whole chapter is not working and Section 5 is the right place to diagnose.

Second, open `/usr/src/sys/dev/re/if_re.c` and re-read `re_allocmem` slowly. It is about one hundred fifty lines of tag and memory setup code. Every line maps to a Chapter 21 concept. Reading it once after completing the chapter should feel like familiar territory; the real driver's patterns will look like elaborations of the chapter's simpler ones.

Third, skip the challenges on first pass. The labs are calibrated for Chapter 21's pace; the challenges assume the chapter's material is solid. Come back to them after Chapter 22 if they feel out of reach now.

Chapter 21's goal was to give the driver the ability to move data. If it has, Chapter 22's power-management machinery becomes a specialisation rather than an entirely new topic.



## Bridge to Chapter 22

Chapter 22 is titled *Power Management*. Its scope is the discipline of saving and restoring a driver's state around system suspend and resume. Modern systems suspend aggressively: laptops suspend when the lid closes; servers suspend individual devices that are idle (PCIe's D0, D1, D2, D3 power states); virtual machines migrate between hosts. A driver that does not handle these transitions cleanly leaves its device in an inconsistent state, causes stuck resumes, or corrupts in-flight data.

Chapter 21 prepared the ground in three specific ways.

First, **you have a complete teardown path**. The DMA resources can be torn down and rebuilt cleanly; Chapter 22's suspend handler will call `myfirst_dma_teardown`, and the resume handler will call `myfirst_dma_setup`. The versioning of state (attaching, detaching, and now suspending) is an extension of the patterns Chapter 21 already introduced.

Second, **you have an in-flight transfer tracker**. Chapter 21's `dma_in_flight` field and the `cv_timedwait` wait protocol are exactly what Chapter 22's suspend handler needs to ensure no transfer is pending when the device loses power. Reusing the tracker keeps the code uniform.

Third, **you have a clean interrupt mask API**. The driver masks and unmasks `MYFIRST_INTR_COMPLETE` through the Chapter 19/20 machinery. Chapter 22's suspend handler will mask all interrupts before the power transition; Chapter 22's resume handler will unmask them after the device has stabilised.

Specific topics Chapter 22 will cover:

- What ACPI suspend states mean (S1, S3, S4) and what they require of drivers.
- PCIe device power states (D0, D1, D2, D3hot, D3cold) and how FreeBSD transitions between them.
- The `device_suspend` and `device_resume` methods; how to implement them.
- Quiescing DMA: how to ensure no in-flight transfer is outstanding when power is lost.
- Re-attaching after resume: re-initialising hardware state, reloading tables, restoring interrupts.
- Handling devices that reset across suspend: detecting the reset and rebuilding state.
- Integrating with the rest of the Chapter 16-21 machinery (bus_space, simulation, PCI, interrupts, DMA).

You do not need to read ahead. Chapter 21 is sufficient preparation. Bring your `myfirst` driver at `1.4-dma`, your `LOCKING.md`, your `INTERRUPTS.md`, your `MSIX.md`, your `DMA.md`, your `WITNESS`-enabled kernel, and your regression script. Chapter 22 starts where Chapter 21 ended.

Part 4 is nearly complete. Chapter 22 closes the part by adding the one remaining discipline that separates a prototype driver from a production driver: the ability to survive the power transitions that real systems impose.

The vocabulary is yours; the structure is yours; the discipline is yours. Chapter 22 adds the next missing piece: the driver's ability to gracefully stop and gracefully start, in response to events the driver itself did not initiate.



## Reference: Chapter 21 Quick-Reference Card

A compact summary of the vocabulary, APIs, flags, and procedures Chapter 21 introduced.

### Vocabulary

- **DMA (Direct Memory Access):** device reading or writing host memory without CPU per-byte involvement.
- **Bus-master:** a device capable of initiating bus transactions to host memory.
- **Tag (`bus_dma_tag_t`):** a description of DMA constraints for a group of transfers.
- **Map (`bus_dmamap_t`):** a mapping context for one specific transfer.
- **Segment (`bus_dma_segment_t`):** a (bus_addr, length) pair describing one contiguous piece of a mapping.
- **Bounce buffer:** a kernel-managed staging region used when the device cannot reach the driver's actual buffer.
- **Coherent memory:** memory allocated with `BUS_DMA_COHERENT`; sync operations are cheap.
- **Callback:** the function `bus_dmamap_load` calls with the segment list.
- **Static transaction:** a long-lived mapping allocated at attach time.
- **Dynamic transaction:** a per-transfer mapping created and destroyed per use.
- **Parent tag:** the tag inherited from the parent bridge; constraints compose via intersection.
- **PREWRITE/POSTWRITE/PREREAD/POSTREAD:** the four sync flags.
- **IOMMU:** input-output MMU; transparent remapping between device and host address spaces.

### Essential APIs

- `bus_dma_tag_create(parent, align, bdry, low, high, filt, filtarg, maxsz, nseg, maxsegsz, flags, lockfn, lockarg, &tag)`: create a DMA tag.
- `bus_dma_tag_destroy(tag)`: destroy a DMA tag; fails if children remain.
- `bus_get_dma_tag(dev)`: return the device's parent DMA tag.
- `bus_dmamem_alloc(tag, &vaddr, flags, &map)`: allocate DMA-capable memory and get a map.
- `bus_dmamem_free(tag, vaddr, map)`: free `bus_dmamem_alloc` memory.
- `bus_dmamap_create(tag, flags, &map)`: create a map for dynamic loads.
- `bus_dmamap_destroy(tag, map)`: destroy a map.
- `bus_dmamap_load(tag, map, buf, len, callback, cbarg, flags)`: load a buffer into a map.
- `bus_dmamap_unload(tag, map)`: unload a map.
- `bus_dmamap_sync(tag, map, op)`: synchronise caches for `op` (one of the PRE/POST flags).

### Essential Flags

- `BUS_DMA_WAITOK`: allocator may sleep.
- `BUS_DMA_NOWAIT`: allocator must not sleep; return `ENOMEM` if resources unavailable.
- `BUS_DMA_COHERENT`: prefer cache-coherent memory.
- `BUS_DMA_ZERO`: zero-initialise allocated memory.
- `BUS_DMA_ALLOCNOW`: pre-allocate bounce resources at tag-create time.

### Essential Sync Operations

- `BUS_DMASYNC_PREREAD`: before device writes, host will read.
- `BUS_DMASYNC_PREWRITE`: before device reads, host has written.
- `BUS_DMASYNC_POSTREAD`: after device writes, before host reads.
- `BUS_DMASYNC_POSTWRITE`: after device reads, host may reuse.

### Common Procedures

**Allocate static DMA buffer:**

```c
bus_dma_tag_create(bus_get_dma_tag(dev), ..., &sc->tag);
bus_dmamem_alloc(sc->tag, &sc->vaddr, BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->map);
bus_dmamap_load(sc->tag, sc->map, sc->vaddr, size, single_map_cb, &sc->bus_addr, BUS_DMA_NOWAIT);
```

**Free static DMA buffer:**

```c
bus_dmamap_unload(sc->tag, sc->map);
bus_dmamem_free(sc->tag, sc->vaddr, sc->map);
bus_dma_tag_destroy(sc->tag);
```

**Host-to-device transfer:**

```c
/* Fill sc->vaddr. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_PREWRITE);
/* Program device, trigger, wait for completion. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_POSTWRITE);
```

**Device-to-host transfer:**

```c
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_PREREAD);
/* Program device, trigger, wait for completion. */
bus_dmamap_sync(sc->tag, sc->map, BUS_DMASYNC_POSTREAD);
/* Read sc->vaddr. */
```

### Useful Commands

- `sysctl hw.busdma`: DMA subsystem statistics.
- `vmstat -m | grep bus_dma`: memory use.
- `pciconf -lvbc <dev>`: device capability listing.
- `procstat -kke`: thread states and stacks.

### Files to Keep Bookmarked

- `/usr/src/sys/sys/bus_dma.h`: the public header.
- `/usr/src/share/man/man9/bus_dma.9`: the manual page.
- `/usr/src/sys/dev/re/if_re.c`: production descriptor-ring driver.
- `/usr/src/sys/dev/nvme/nvme_qpair.c`: NVMe queue construction.



## Reference: Glossary of Chapter 21 Terms

A short glossary of the chapter's new terms.

- **Alignment constraint:** the requirement that a buffer's starting address be a multiple of a specific value.
- **Boundary constraint:** the requirement that a buffer not cross a specific address boundary.
- **Bounce page:** a single page of bounce buffer used to stage data for a mapping.
- **Bus address:** the address a device uses to reach a memory region; may differ from the physical address.
- **Bus-master:** a device that can initiate transactions on the bus (i.e. do DMA).
- **Callback (DMA):** the function `bus_dmamap_load` calls with the segment list after the load completes.
- **Coherent memory:** memory in a domain where CPU and DMA see the same data without explicit synchronisation.
- **Deferred load:** a load that returned `EINPROGRESS` and will have its callback called later.
- **Descriptor:** a small structure (in DMA memory) that describes one transfer: address, length, flags, status.
- **Descriptor ring:** an array of descriptors used for producer-consumer communication.
- **DMA tag:** see `bus_dma_tag_t`.
- **Doorbell:** an MMIO register a driver writes to signal new ring entries to the device.
- **Dynamic transaction:** see Section 2.
- **IOMMU:** input-output MMU; translates device-side addresses to host physical addresses.
- **KVA:** kernel virtual address; the pointer the CPU uses.
- **Load callback:** synonym for Callback (DMA).
- **Mapping:** a binding between a kernel buffer and a set of bus-visible segments.
- **Parent tag:** a tag from which another inherits constraints.
- **PREREAD, PREWRITE, POSTREAD, POSTWRITE:** sync flags; see Section 4.
- **Scatter-gather:** a mapping of multiple discontinuous segments as a single logical transfer.
- **Segment:** see `bus_dma_segment_t`.
- **Static transaction:** see Section 2.
- **Sync:** `bus_dmamap_sync`; the hand-off between CPU and device ownership of a buffer.



## Reference: A Closing Note on DMA Philosophy

A paragraph to close the chapter with.

DMA is the primitive that turns a driver from a byte-at-a-time controller into a data-moving subsystem. Before DMA, every byte of data the driver handled went through the CPU's hands, one MMIO transaction at a time; after DMA, the CPU handles only policy and bookkeeping, and the device moves the bytes. The difference is the difference between a driver that can keep up with a 10 Mbit line and a driver that can keep up with a 100 Gbit line.

Chapter 21's lesson is that DMA is disciplined, not magical. The `bus_dma(9)` API hides three hardware realities (addressing limits, IOMMU remapping, cache coherence) behind a single set of calls, and the calls follow a predictable pattern: create a tag, allocate memory, load a map, sync PRE, trigger the device, wait, sync POST, read the result, eventually unload and free. The pattern is the same across every DMA-capable driver in FreeBSD; internalising it once pays off across dozens of later chapters and thousands of lines of real driver code.

For this reader and for this book's future readers, the Chapter 21 DMA pattern is a permanent part of the `myfirst` driver's architecture and a permanent tool in the reader's toolkit. Chapter 22 assumes it: suspend needs to quiesce DMA; resume needs to reinitialise it. Part 6's networking chapters assume it: every packet path uses DMA. Part 7's performance chapters (Chapter 33) assume it: every tuning measurement is against DMA throughput. The vocabulary is the vocabulary every high-performance FreeBSD driver shares; the patterns are the patterns production drivers live by; the discipline is the discipline that keeps coherent platforms coherent and non-coherent platforms correct.

The skill Chapter 21 teaches is not "how to set up a single 4 KB DMA buffer". It is "how to think about memory ownership between the CPU and the device, how to describe a device's constraints to the kernel, and how to move data under a sync discipline that works portably on every platform FreeBSD supports". That skill applies across every DMA-capable device the reader will ever work on.



---
