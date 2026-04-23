---
title: "Advanced Interrupt Handling"
description: "Chapter 20 extends the Chapter 19 interrupt driver with MSI and MSI-X support. It teaches the difference between legacy INTx, MSI, and MSI-X; how to query capability counts with pci_msi_count(9) and pci_msix_count(9); how to allocate vectors with pci_alloc_msi(9) and pci_alloc_msix(9); how to build the fallback ladder from MSI-X down to legacy INTx; how to register per-vector filter handlers with separate driver_filter_t functions; how to design interrupt-safe per-vector data structures; how to give each vector a specific role and a specific CPU affinity; and how to tear down a multi-vector driver safely. The driver grows from 1.2-intr to 1.3-msi, gains a new msix-specific file, and leaves Chapter 20 ready for DMA in Chapter 21."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 20
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 165
---

# Advanced Interrupt Handling

## Reader Guidance & Outcomes

Chapter 19 ended with a driver that could listen to its device. The `myfirst` module at version `1.2-intr` has one filter handler registered on the legacy PCI INTx line, one deferred-work task on a taskqueue, a simulated-interrupt sysctl for testing on the bhyve lab target, a strict teardown order, and a new `myfirst_intr.c` file that keeps the interrupt code tidy. The Chapter 11 locking discipline carries through: atomics in the filter, sleep locks in the task, `INTR_MPSAFE` on the handler, shared-IRQ safety via `FILTER_STRAY`. The driver behaves like a small real driver that happens to have a single source of interrupts.

What the driver does not yet do is take advantage of everything PCIe offers. A modern PCIe device does not need to share a single line with its neighbours. It can request a dedicated interrupt through the message-signalled mechanism PCI 2.2 introduced (MSI) or the richer per-function table MSI-X added in PCIe. A device with several queues (a NIC with receive and transmit queues, an NVMe controller with admin and I/O submission queues, a modern USB3 host controller with event queues) usually wants an interrupt per queue rather than one shared interrupt for the whole device. Chapter 20 teaches the driver how to ask.

The chapter's scope is precisely this transition: what MSI and MSI-X are at the hardware level, how FreeBSD represents them in terms of additional IRQ resources, how a driver queries capability counts and allocates vectors, how the fallback ladder from MSI-X down to MSI down to legacy INTx works in practice, how to register several different filter functions on the same device, how to design per-vector data structures so each vector's handler touches its own state, how to give each vector a CPU affinity that matches the device's NUMA placement, how to label each vector with `bus_describe_intr(9)` so `vmstat -i` tells the operator which vector does what, and how to tear every one of these things down in the correct order. The chapter stops short of DMA, which is Chapter 21; per-queue receive and transmit vectors become especially valuable once a descriptor ring is in the picture, but teaching both at once would dilute both.

Chapter 20 keeps several neighbouring topics at arm's length. Full DMA (`bus_dma(9)` tags, descriptor rings, bounce buffers, cache coherence) is Chapter 21. Iflib's multi-queue framework, which wraps MSI-X with a layer of per-queue iflib machinery, is a Part 6 topic (Chapter 28) for readers who want the iflib-style networking path. The richer per-function MSI-X mask table operations (steering specific message addresses to specific CPUs through the MSI-X table directly) are discussed but not implemented end to end. Platform-specific interrupt remapping through the IOMMU, SR-IOV vector sharing, and PCIe AER-driven interrupt recovery are left for later chapters. Chapter 20 stays inside the ground it can cover well and hands off explicitly when a topic deserves its own chapter.

Multi-vector work rests on every earlier Part 4 layer. Chapter 16 gave the driver a vocabulary of register access. Chapter 17 taught it to think like a device. Chapter 18 introduced it to a real PCI device. Chapter 19 gave it ears on a single IRQ. Chapter 20 gives it a set of ears, one per conversation the device wants to have. Chapter 21 will teach those ears to cooperate with the device's own ability to reach into RAM. Each chapter adds one layer. Each layer depends on the ones before it. Chapter 20 is where the driver stops pretending the device has only one thing to say and starts treating it as the multi-queue machine it really is.

### Why MSI-X Earns a Chapter of Its Own

At this point you may be asking why MSI and MSI-X need a chapter of their own. The Chapter 19 driver has a working interrupt handler on the legacy IRQ line. If the filter-plus-task pipeline is already right, why not keep using it? Does MSI-X really justify a full chapter of new material?

Three reasons.

The first is scale. A single IRQ line on a shared system forces every driver on that line to serialise through one `intr_event`. On a host with dozens of PCIe devices, the legacy INTx mechanism would bottleneck the whole system if it were the only option. MSI-X lets each device (and each queue within a device) have its own dedicated `intr_event`, served by its own ithread or filter handler, pinned to its own CPU. The difference between a modern server handling ten million packets per second with MSI-X and the same workload on legacy INTx is the difference between "possible" and "impossible"; MSI-X is what makes the former a reality.

The second is locality. With a single interrupt line, the kernel has one choice of CPU to route the interrupt to, and that choice is global for the device. With MSI-X, each vector can be pinned to a different CPU, and good drivers pin each vector to a CPU that is NUMA-local to the queue it serves. The cache-line advantages of doing this are real: a receive queue whose interrupt fires on the same CPU that eventually consumes the packet avoids cross-socket cache traffic that dominates on legacy setups.

The third is cleanliness. Even for a driver that does not need high throughput, MSI or MSI-X can simplify the handler. With a dedicated line, the filter does not need to handle the shared-IRQ case. With a dedicated vector per event class (admin, receive, transmit, error), each handler is smaller and more specialised, and the whole driver becomes easier to read. Good drivers use MSI-X even when performance does not require it, because the code becomes better.

Chapter 20 earns its place by teaching all three of these benefits concretely. A reader finishes the chapter able to allocate vectors, route them, describe them, and tear them down, with a working driver that demonstrates the pattern end to end.

### Where Chapter 19 Left the Driver

A brief recap of where you should stand. Chapter 20 extends the driver produced at the end of Chapter 19 Stage 4, tagged as version `1.2-intr`. If any of the items below feels uncertain, return to Chapter 19 before starting this chapter.

- Your driver compiles cleanly and identifies itself as `1.2-intr` in `kldstat -v`.
- On a bhyve or QEMU guest that exposes a virtio-rnd device, the driver attaches, allocates BAR 0 as `SYS_RES_MEMORY`, allocates the legacy IRQ as `SYS_RES_IRQ` with `rid = 0`, registers a filter handler through `bus_setup_intr(9)` with `INTR_TYPE_MISC | INTR_MPSAFE`, creates `/dev/myfirst0`, and supports the `dev.myfirst.N.intr_simulate` sysctl.
- The filter reads `INTR_STATUS`, increments per-bit counters, acknowledges, enqueues the deferred task for `DATA_AV`, and returns the right `FILTER_*` value.
- The task (`myfirst_intr_data_task_fn`) runs in thread context on a taskqueue named `myfirst_intr` at `PI_NET` priority, reads `DATA_OUT`, updates the softc, and broadcasts `sc->data_cv`.
- The detach path clears `INTR_MASK`, calls `bus_teardown_intr`, drains and frees the taskqueue, releases the IRQ resource, detaches the hardware layer, and releases the BAR.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, and `INTERRUPTS.md` are current.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel.

That driver is what Chapter 20 extends. The additions are noticeable in scope: one new file (`myfirst_msix.c`), one new header (`myfirst_msix.h`), several new softc fields to track per-vector state, a new family of per-vector filter functions, a new fallback ladder in the setup helper, per-vector `bus_describe_intr` calls, optional CPU binding, a version bump to `1.3-msi`, a new `MSIX.md` document, and updates to the regression test. The mental model grows too: the driver starts thinking of interrupts as a vector of sources rather than a single event stream.

### What You Will Learn

By the time you move on to the next chapter, you will be able to:

- Describe what an MSI and an MSI-X interrupt is at the hardware level, how each is signalled over PCIe (as a memory write rather than an electrical level change), and why the two mechanisms coexist with legacy INTx.
- Explain the key differences between MSI and MSI-X: the MSI vector count (1 to 32 from a contiguous block), the MSI-X vector count (up to 2048 independently-addressable vectors), and the per-vector address and mask capabilities MSI-X offers that MSI does not.
- Query a device's MSI and MSI-X capabilities through `pci_msi_count(9)` and `pci_msix_count(9)`, and know what the returned count means.
- Allocate MSI or MSI-X vectors through `pci_alloc_msi(9)` and `pci_alloc_msix(9)`, handle the case where the kernel allocates fewer vectors than requested, and recover from allocation failures.
- Build a three-tier fallback ladder: MSI-X first (if available), then MSI (if MSI-X is unavailable or allocation failed), then legacy INTx. Each tier uses the same `bus_setup_intr` pattern at its core, but the rid and handler-per-vector structure differ.
- Allocate per-vector IRQ resources with the correct rid (rid=0 for legacy INTx; rid=1, 2, 3, ... for MSI and MSI-X vectors).
- Register a distinct filter handler per vector so that each vector has its own purpose (admin, receive-queue-N, transmit-queue-N, error).
- Design per-vector state (per-queue counters, per-queue task, per-queue lock) so that handlers running concurrently on different CPUs do not contend on shared data.
- Describe each vector with `bus_describe_intr(9)` so that `vmstat -i` and `devinfo -v` show each vector with a meaningful name.
- Bind each vector to a specific CPU with `bus_bind_intr(9)`, and query the device's NUMA-local CPU set with `bus_get_cpus(9)` using `LOCAL_CPUS` or `INTR_CPUS`.
- Handle partial-allocation failures: the device has eight vectors, the kernel gave us three; adjust the driver to use the three and do the remaining work through polling or scheduled tasks.
- Tear down a multi-vector driver correctly: per-vector `bus_teardown_intr`, per-vector `bus_release_resource`, then a single `pci_release_msi(9)` at the end.
- Log a single clear dmesg summary line at attach time stating the interrupt mode (MSI-X / N vectors, MSI / K vectors, or legacy INTx), so the operator instantly sees which tier the driver ended up using.
- Split the multi-vector code into `myfirst_msix.c`, update the module's `SRCS` line, tag the driver as `1.3-msi`, and produce `MSIX.md` documenting per-vector purposes and observed counter patterns.

The list is long; each item is narrow. The point of the chapter is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 20 stays focused.

- **DMA.** `bus_dma(9)` tags, `bus_dmamap_load(9)`, scatter-gather lists, bounce buffers, cache coherence around DMA descriptors, and the way the device writes completions to RAM are Chapter 21. Chapter 20 gives the driver multiple vectors; Chapter 21 gives the device the ability to move data. Each half is independently valuable; together they are the backbone of every modern performance driver.
- **iflib(9) and the multi-queue networking framework.** iflib is a thick, opinionated framework that wraps MSI-X with per-queue ithreads, per-queue DMA pools, and a lot of machinery a generic driver does not need. Chapter 20 teaches the raw pattern; Part 6's networking chapter (Chapter 28) revisits it in iflib's vocabulary.
- **PCIe AER recovery through MSI-X vectors.** Advanced Error Reporting can signal through its own MSI-X vector on some devices. Chapter 20 mentions the possibility; the full recovery path is a later-chapter topic.
- **SR-IOV and per-VF interrupts.** A Single-Root IO Virtualization virtual function has its own MSI-X capability and its own per-VF vectors. Chapter 20's driver is a physical function; the VF story is a later-chapter specialisation.
- **Thread priority tuning per vector.** A driver can pass a different priority to each vector's `bus_setup_intr` flags, or use `taskqueue_start_threads` at different priorities per vector. Chapter 20 uses `INTR_TYPE_MISC | INTR_MPSAFE` for each vector and does not tune priorities; Part 7's performance chapters (Chapter 33) cover the tuning story.
- **Modern-virtio-PCI transport using PCIe capabilities.** The `virtio_pci_modern(4)` driver puts the virtqueue notifications inside capability structures and uses MSI-X vectors for virtqueue completions. Chapter 20's driver still targets a legacy virtio-rnd BAR; a reader adapting it to a real production device would follow the Chapter 20 pattern but read from the modern virtio PCI layout.

Staying inside those lines keeps Chapter 20 a chapter about multi-vector interrupt handling. The vocabulary is what transfers; the specific chapters that follow apply the vocabulary to DMA, iflib, AER, and SR-IOV.

### Estimated Time Investment

- **Reading only**: four to five hours. The MSI/MSI-X conceptual model is not complex, but the per-vector discipline, the fallback ladder, and the CPU-affinity story benefit from a careful read.
- **Reading plus typing the worked examples**: ten to twelve hours over two or three sessions. The driver evolves in four stages: fallback ladder, multiple vectors, per-vector handlers, refactor. Each stage is small but requires careful attention to per-vector state.
- **Reading plus all labs and challenges**: sixteen to twenty hours over four or five sessions, including reading real drivers (`virtio_pci.c`, `if_em.c`'s MSI-X code, `nvme.c`'s admin+IO vector split), setting up a bhyve or QEMU guest with MSI-X exposed, and running the chapter's regression test.

Sections 3, 5, and 6 are the densest. If the per-vector handler pattern feels unfamiliar on first pass, that is normal. Stop, re-read Section 3's diagram, and continue when the shape has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 19 Stage 4 (`1.2-intr`). The starting point assumes every Chapter 19 primitive: the filter plus task pipeline, the simulated-interrupt sysctl, the `ICSR_*` accessor macros, the clean teardown.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- `bhyve(8)` or `qemu-system-x86_64` is available. For MSI-X labs the guest must expose a device whose MSI-X capability is enabled. QEMU's `virtio-rng-pci` has MSI-X; bhyve's `virtio-rnd` uses legacy virtio and does not expose MSI-X on the host driver as a matter of default configuration. The chapter calls out which labs require which environment.
- The `devinfo(8)`, `vmstat(8)`, `pciconf(8)`, and `cpuset(1)` tools are in your path.

If any item above is shaky, fix it now. MSI-X tends to expose any latent weakness in the driver's interrupt-context discipline because multiple handlers can run on multiple CPUs at the same time; the debug kernel's `WITNESS` is especially valuable during Chapter 20 development.

### How to Get the Most Out of This Chapter

Four habits will pay off quickly.

First, keep `/usr/src/sys/dev/pci/pcireg.h` and `/usr/src/sys/dev/pci/pcivar.h` bookmarked along with the new files `/usr/src/sys/dev/pci/pci.c` and `/usr/src/sys/dev/virtio/pci/virtio_pci.c`. The first two are from Chapter 18 and define the capability constants (`PCIY_MSI`, `PCIY_MSIX`, `PCIM_MSIXCTRL_*`) and the accessor wrappers. The third is the kernel's implementation of `pci_msi_count_method`, `pci_alloc_msi_method`, `pci_alloc_msix_method`, and `pci_release_msi_method`. The fourth is a clean real-driver example of the full MSI-X allocation ladder with fallback. Each file rewards half an hour of reading.

Second, run `pciconf -lvc` on your lab host and a guest. The `-c` flag tells `pciconf` to print each device's capability list, and you will see which devices expose MSI, MSI-X, or both. Looking at your own machine is the quickest way to understand why MSI-X is the default everywhere in modern PCIe.

Third, type the changes by hand and run each stage. MSI-X code is where subtle per-vector mistakes produce bugs that only appear under concurrent load. Typing carefully, watching `dmesg` for attach banners, and running the regression test after each stage catches these mistakes at the point where they are cheap to fix.

Fourth, read `/usr/src/sys/dev/nvme/nvme_ctrlr.c`'s MSI-X setup (look for `nvme_ctrlr_allocate_bar` and `nvme_ctrlr_construct_admin_qpair`) after Section 5. `nvme(4)` is a clean real-driver example of the admin-plus-N-queues pattern that Chapter 20 teaches. The file is long but the MSI-X code is a small fraction of it; the rest of the reading is optional but educational.

### Roadmap Through the Chapter

The sections in order are:

1. **What Are MSI and MSI-X?** The hardware picture: how message-signalled interrupts work over PCIe, the difference between MSI and MSI-X, and why modern devices prefer them.
2. **Enabling MSI in Your Driver.** The simpler of the two modes. Query count, allocate, register a handler. Stage 1 of the Chapter 20 driver (`1.3-msi-stage1`).
3. **Managing Multiple Interrupt Vectors.** The core of the chapter. Per-vector rid, per-vector filter function, per-vector softc state, per-vector `bus_describe_intr`. Stage 2 (`1.3-msi-stage2`).
4. **Designing Interrupt-Safe Data Structures.** Why multi-vector means multi-CPU, what locks each vector's handler may and may not touch, how to structure per-queue state. A discipline, not a stage bump.
5. **Using MSI-X for High Flexibility.** The fuller mechanism. Table layout, per-vector binding, NUMA-aware placement with `bus_get_cpus`. Stage 3 (`1.3-msi-stage3`).
6. **Handling Vector-Specific Events.** Per-vector handler functions, per-vector deferred work, a pattern the `nvme(4)` driver uses at scale.
7. **Teardown and Cleanup With MSI/MSI-X.** Per-vector teardown, then a single `pci_release_msi` call. The ordering rules that keep everything safe.
8. **Refactoring and Versioning Your Multi-Vector Driver.** The final split into `myfirst_msix.c`, the new `MSIX.md`, the version bump to `1.3-msi`, and the regression pass. Stage 4.

After the eight sections come hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 20's story and opens Chapter 21's, and a bridge to Chapter 21. The reference-and-cheat-sheet material at the end of the chapter is meant to be re-read as you work through Chapter 21; Chapter 20's vocabulary (vector, rid, per-vector softc, affinity, teardown ordering) is the foundation Chapter 21's DMA work assumes.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3 and 5 stand alone and make good single-sitting reads.



## Section 1: What Are MSI and MSI-X?

Before the driver code, the hardware picture. Section 1 teaches what message-signalled interrupts are at the level of the PCIe bus and the interrupt controller, without any FreeBSD-specific vocabulary. A reader who understands Section 1 can read the rest of the chapter with the kernel's MSI/MSI-X path as a concrete object rather than a vague abstraction.

### The Problem With Legacy INTx

Chapter 19 taught the legacy PCI INTx interrupt model: each PCI function has one interrupt line (usually one of INTA, INTB, INTC, INTD), the line is level-triggered, and multiple devices on the same physical line share it. Chapter 19's driver handled the shared case correctly by reading INTR_STATUS first and returning `FILTER_STRAY` when nothing was set.

INTx works. But it has three problems that grow in importance as systems scale.

The first is **sharing overhead**. A shared line with ten devices requires every driver to be called on every interrupt, just to read its own status register and discover the interrupt was not for it. On a system where most interrupts are legitimate (the line is busy), this is a few extra `bus_read_4` calls per event; on a system where one device storms, every other driver's filter runs unnecessarily. The CPU cost is small per event but adds up across millions of events per second.

The second is **no per-queue separation**. A modern NIC has four, eight, sixteen, or sixty-four receive queues and a matching number of transmit queues. Each queue wants its own interrupt: when receive queue 3 has packets, only the handler for receive queue 3 should run, on a CPU close to the memory that queue uses. With INTx the device has only one line, so either the driver polls every queue from one handler (expensive and slow) or the device only supports one queue (unacceptable for a ten-gigabit NIC).

The third is **no CPU affinity per event type**. A shared line fires on one CPU (whichever the interrupt controller routes it to). On a NUMA system where the device is attached to socket 0, firing the interrupt on a CPU in socket 1 is worse than firing it on a CPU in socket 0: the handler's code runs on socket 1 but the device's memory lives on socket 0, and every register read crosses the inter-socket fabric. With INTx the driver cannot say "please fire this interrupt on CPU 3"; the kernel picks and the driver has no influence per event type.

MSI and MSI-X fix all three. The mechanism is fundamentally different from INTx: instead of electrical signalling over a dedicated wire, the device performs a memory write to a specific address, and the CPU's interrupt controller treats the write as an interrupt. This decouples the number of interrupts from the number of physical wires, lets each message-signalled interrupt have its own destination address (and therefore its own CPU), and eliminates the shared-line problem entirely.

### How an MSI-Triggered Interrupt Actually Happens

Physically, an MSI interrupt is a write transaction on the PCIe fabric. The device issues a write of a specific value to a specific address. The memory controller recognises the address as belonging to the interrupt controller's MSI region, and routes the write to the APIC (or GIC, or whatever the platform's interrupt controller is). The interrupt controller decodes the address to determine which CPU should receive the interrupt, and decodes the written value to determine which vector (which entry in the IDT or equivalent) the CPU should run. The CPU then dispatches as it would for any interrupt: save state, jump to the vector's handler, run the kernel's interrupt dispatch.

From the driver's perspective, the flow is almost identical to legacy INTx:

1. The device has an event.
2. The interrupt fires.
3. The kernel calls the driver's filter handler.
4. The filter reads status, acknowledges, handles or defers, returns.
5. If deferred, the task runs later.
6. Teardown proceeds in the detach path.

What differs is the mechanism at step 2 and the allocation model at setup time. The device is not asserting a wire; it is writing to memory. The kernel does not need to have a pre-arranged destination wire; it has a pool of message addresses and message values. Each vector corresponds to one (address, value) pair. The device stores these pairs in its MSI capability structure and issues a write using them when it needs an interrupt.

### MSI: The Simpler of the Two

MSI (Message Signalled Interrupts) is the older and simpler of the two mechanisms. Introduced in PCI 2.2 in 1999, MSI lets a device request between 1 and 32 interrupt vectors, allocated as a contiguous power-of-two block (1, 2, 4, 8, 16, or 32). The device has a single MSI capability structure in its configuration space, which contains:

- A message address register (the write's destination address, typically the APIC's MSI region).
- A message data register (the value written, which encodes the vector number).
- A message control register (enable bit, function mask bit, number of vectors requested, and so on).

When the device wants to signal vector N (where N is 0 through count-1), it writes the message data register's base value OR'd with N to the message address. The interrupt controller demultiplexes the written value to dispatch the correct vector.

Key properties of MSI:

- **Single capability block.** The device has one MSI capability, not one per vector.
- **Contiguous vectors.** The block is power-of-two and allocated as a unit.
- **Limited count.** Maximum 32 vectors per function.
- **No per-vector masking.** The whole block is masked or unmasked as a group (via the function-mask bit if supported).
- **No per-vector address.** All vectors share a single message address register; the vector number goes in the low bits of the data written.

MSI is a significant improvement over legacy INTx, but it has limits: no per-vector masking and a 32-vector cap. Most drivers that want multiple vectors end up preferring MSI-X.

### MSI-X: The Fuller Mechanism

MSI-X, introduced in PCI 3.0 in 2004 and extended in PCIe, removes MSI's limits. The device has an MSI-X capability structure plus an MSI-X **table** (an array of per-vector entries) and a **pending bit array** (PBA). The capability structure points into one or more of the device's BARs, where the table and PBA live.

Each MSI-X table entry contains:

- A message address register (per-vector).
- A message data register (per-vector).
- A vector control register (per-vector mask bit).

When the device wants to signal vector N, it looks up entry N in the table, reads that entry's address and data, and performs the write. The interrupt controller dispatches based on what was written.

Key properties of MSI-X:

- **Per-vector address and data.** Each vector can be routed to a different CPU by programming a different address.
- **Per-vector mask.** Individual vectors can be disabled without disabling the whole block.
- **Up to 2048 vectors per function.** An NVMe controller with many queues is happy here; a NIC with 64 receive queues plus 64 transmit queues plus some admin vectors fits.
- **Table in a BAR.** The table's location is discoverable through the MSI-X capability registers; `pci_msix_table_bar(9)` and `pci_msix_pba_bar(9)` return which BAR holds each.
- **More complex setup.** The driver has to allocate the table, program each entry, and then enable.

In practice, modern PCIe devices prefer MSI-X for any multi-vector use case, and reserve MSI for backwards compatibility or single-vector simple devices. The kernel handles most of the table programming internally; the driver's job is to query the count, allocate, and register per-vector handlers.

### How FreeBSD Abstracts the Difference

The kernel hides most of the MSI-vs-MSI-X difference behind a small set of accessor functions. From `/usr/src/sys/dev/pci/pcivar.h`:

- `pci_msi_count(dev)` returns the MSI vector count the device advertises (0 if no MSI capability).
- `pci_msix_count(dev)` returns the MSI-X vector count (0 if no MSI-X capability).
- `pci_alloc_msi(dev, &count)` and `pci_alloc_msix(dev, &count)` allocate vectors. The `count` is an input-output: input is the desired count, output is the actual count allocated.
- `pci_release_msi(dev)` releases both MSI and MSI-X vectors (it handles either case internally).

The driver does not interact with the MSI-X table directly; the kernel does that on the driver's behalf. What the driver does see is that after a successful allocation, the device appears to have additional IRQ resources available through `bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, using `rid = 1, 2, 3, ...` for the allocated vectors. The driver then registers a filter handler for each resource the same way Chapter 19 registered one for the legacy line.

The symmetry is deliberate. The same `bus_setup_intr(9)` call that handled the legacy IRQ at `rid = 0` handles each MSI or MSI-X vector at `rid = 1, 2, 3, ...`. Every `INTR_MPSAFE` rule, every `FILTER_*` return-value convention, every shared-IRQ discipline (for MSI, where vectors can technically share an `intr_event` in corner cases), and every teardown ordering from Chapter 19 carries through.

### The Fallback Ladder

A robust driver tries the mechanisms in order of preference and falls back to the next one when allocation fails. The canonical ladder:

1. **MSI-X first.** If `pci_msix_count(dev)` is non-zero, try `pci_alloc_msix(dev, &count)`. If it succeeds, use MSI-X. On a modern PCIe device this is the preferred path.
2. **MSI second.** If MSI-X is unavailable or the allocation failed, check `pci_msi_count(dev)`. If it is non-zero, try `pci_alloc_msi(dev, &count)`. If it succeeds, use MSI.
3. **Legacy INTx last.** If both MSI-X and MSI are unavailable, fall back to the Chapter 19 legacy path with `rid = 0`.

Real drivers implement this ladder so they work on every system they might land on, from a brand-new NVMe drive that only supports MSI-X to a legacy chipset that only supports INTx. The Chapter 20 driver does the same; Section 2 writes the MSI path, Section 5 writes the MSI-X path, and Section 8 ties them together into a single fallback ladder.

### Real-World Examples

A short tour of devices that use MSI and MSI-X.

**Modern NICs.** A typical 10 or 25 Gbps NIC exposes 16 to 64 MSI-X vectors: one per receive queue, one per transmit queue, and a handful for admin, error, and link-state events. Intel's `igc(4)`, `em(4)`, `ix(4)`, and `ixl(4)` all follow this pattern; Broadcom's `bnxt(4)`, Mellanox's `mlx4(4)` and `mlx5(4)`, and Chelsio's `cxgbe(4)` do the same. The `iflib(9)` framework wraps the MSI-X allocation for many drivers.

**NVMe storage controllers.** An NVMe controller has one admin queue and up to 65535 I/O queues. In practice, drivers allocate one MSI-X vector for the admin queue and one per I/O queue up to `NCPU`. FreeBSD's `nvme(4)` driver does exactly this; the code is readable and worth studying.

**Modern USB host controllers.** An xHCI (USB 3) host controller typically advertises one MSI-X vector for the command-completion event ring and several more for per-slot event rings on high-performance variants. The `xhci(4)` driver's setup path shows the admin-plus-events pattern.

**GPUs.** A modern discrete GPU has many MSI-X vectors: one for the command buffer, one or more for display, one per engine, one for power management, and others. The out-of-tree drm-kmod drivers exercise MSI-X extensively.

**Virtio devices in VMs.** When a FreeBSD guest runs under bhyve, KVM, or VMware, the modern virtio-PCI transport uses MSI-X: one vector for configuration-change events, and one per virtqueue. The `virtio_pci_modern(4)` driver implements this.

Each of these drivers follows the same pattern Chapter 20 teaches: query, allocate, register per-vector handlers, bind to CPUs, describe. The specifics differ (how many vectors, how they are assigned to events, how they are bound to CPUs), but the structure is constant.

### Why MSI-X and Not MSI

A reader might ask: given that MSI-X is strictly more capable than MSI, why does MSI even exist any more? Two reasons.

The first is backward compatibility. Devices and motherboards predating PCI 3.0 may support MSI but not MSI-X. A driver that wants to work on old hardware needs an MSI fallback. Most of the ecosystem has moved forward, but the long tail of older devices still exists.

The second is simplicity. MSI with one or two vectors is simpler to set up than MSI-X (no table to program, no BAR to consult). For devices whose interrupt needs fit within MSI's 32-vector cap and do not need per-vector masking, MSI is the lighter-weight choice. Many simple PCIe devices expose only MSI for this reason.

The practical answer for Chapter 20's driver: always try MSI-X first, fall back to MSI if MSI-X is unavailable, fall back to legacy INTx if neither is available. Every real FreeBSD driver written in the last decade uses this ladder.

### A Diagram of the MSI-X Flow

```text
  Device    Config space    MSI-X table (in BAR)     Interrupt controller     CPU
 --------   ------------   ---------------------    --------------------    -----
   |             |                 |                         |                |
   | event N    |                 |                         |                |
   | occurs     |                 |                         |                |
   |            |                 |                         |                |
   | read       |                 |                         |                |
   | entry N   -+---------------->|                         |                |
   | from table |   address_N,    |                         |                |
   |            |   data_N        |                         |                |
   |<-----------+-----------------|                         |                |
   |                              |                         |                |
   | memory-write to address_N                             |                |
   |-----------------------------+------------------------->|                |
   |                              |                         |                |
   |                              |                         | steer to CPU  |
   |                              |                         |-------------->|
   |                              |                         |               | filter_N
   |                              |                         |               | runs
   |                              |                         |               |
   |                              |                         | EOI           |
   |                              |                         |<--------------|
```

The diagram elides the MSI-X-table reads (which the device performs internally before issuing the write) and the interrupt-controller's demultiplexing logic, but it captures the mechanism's essence: the device's event triggers a memory write, the memory write becomes an interrupt, the interrupt dispatches to a filter. The filter does the same work Chapter 19's filter did. The only difference is that on MSI-X, there is a different filter for each vector.

### Exercise: Finding MSI-Capable Devices on Your System

Before moving to Section 2, a short exercise to make the capability picture concrete.

On your lab host, run:

```sh
sudo pciconf -lvc
```

The `-c` flag tells `pciconf(8)` to print each device's capability list. You will see entries like:

```text
vgapci0@pci0:0:2:0: ...
    ...
    cap 05[d0] = MSI supports 1 message, 64 bit
    cap 10[a0] = PCI-Express 2 endpoint max data 128(128)
em0@pci0:0:25:0: ...
    ...
    cap 01[c8] = powerspec 2  supports D0 D3  current D0
    cap 05[d0] = MSI supports 1 message, 64 bit
    cap 11[e0] = MSI-X supports 4 messages
```

Each `cap 05` is an MSI capability. Each `cap 11` is an MSI-X capability. The description after the equals sign tells you how many messages (vectors) the device supports in that mode.

Pick three devices from your output. For each, note:

- The MSI count (if any).
- The MSI-X count (if any).
- Which one the driver is currently using. (You can deduce this from `vmstat -i` entries for the device: if you see multiple `name:queueN` lines, the driver is using MSI-X.)

A host without many PCIe devices may show only MSI capabilities; laptops often have limited MSI-X use. A modern server with multiple NICs and an NVMe drive shows many MSI-X capabilities with high vector counts (64 or more for some NICs).

Keep this output open while you read Section 2. The vocabulary of "cap 11[XX] = MSI-X supports N messages" is what the kernel's `pci_msix_count(9)` returns to the driver, and what the allocation ladder queries at attach time.

### Wrapping Up Section 1

MSI and MSI-X are the modern, message-signalled successors to legacy INTx. MSI offers up to 32 vectors allocated as a contiguous block with a single destination address; MSI-X offers up to 2048 vectors with per-vector addresses, per-vector data, and per-vector masking. Both are signalled over PCIe as memory writes that the interrupt controller decodes into vector dispatches.

The kernel abstracts the difference behind `pci_msi_count(9)`, `pci_msix_count(9)`, `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, and `pci_release_msi(9)`. Each allocated vector becomes an IRQ resource at `rid = 1, 2, 3, ...` that the driver registers a filter handler for via `bus_setup_intr(9)`, exactly as Chapter 19 did for the legacy IRQ at `rid = 0`.

A robust driver implements a three-tier fallback ladder: MSI-X preferred, MSI as fallback, legacy INTx as last resort. Section 2 writes the MSI part of that ladder. Section 5 writes the MSI-X part. Section 8 assembles the full ladder.



## Section 2: Enabling MSI in Your Driver

Section 1 established the hardware model. Section 2 puts the driver to work. The task is narrow: extend Chapter 19's attach path so that before falling back to the legacy IRQ at `rid = 0`, the driver tries to allocate an MSI vector. If the allocation succeeds, the driver uses the MSI vector instead of the legacy line. If the allocation fails (either because the device does not support MSI or the kernel cannot allocate), the driver falls back to the legacy path exactly as Chapter 19's code did.

The point of Section 2 is to introduce the MSI API in isolation, before the multi-vector complications of MSI-X make the picture busier. A single-vector MSI path is essentially the same as a single-vector legacy INTx path; only the allocation call and the rid change. That minimal change makes a good first stage.

### What Stage 1 Produces

Stage 1 extends the Chapter 19 Stage 4 driver with a two-tier fallback: MSI first, legacy INTx as fallback. The filter handler is the same Chapter 19 filter. The taskqueue is the same. The sysctls are the same. What changes is the allocation path: `myfirst_intr_setup` first checks `pci_msi_count(9)` and, if non-zero, calls `pci_alloc_msi(9)` for one vector. If that succeeds, the IRQ resource is at `rid = 1`; if it fails, the driver falls through to `rid = 0` for legacy INTx.

The driver also logs the interrupt mode in a single `dmesg` line so the operator knows at a glance which tier the driver ended up using. This is a small but important observability feature that every real FreeBSD driver implements; Chapter 20 follows the convention.

### The MSI Count Query

The first step is to ask the device how many MSI vectors it advertises:

```c
int msi_count = pci_msi_count(sc->dev);
```

The return value is 0 if the device has no MSI capability; otherwise it is the number of vectors the device advertises in its MSI capability control register. Typical values are 1, 2, 4, 8, 16, or 32 (MSI requires a power-of-two count up to 32).

A return of 0 does not mean the device has no interrupts; it means the device does not expose MSI. The driver should fall through to the next tier.

### The MSI Allocation Call

The second step is to ask the kernel to allocate the vectors:

```c
int count = 1;
int error = pci_alloc_msi(sc->dev, &count);
```

The `count` is an input-output parameter. On input, it is the number of vectors the driver wants. On output, it is the number the kernel actually allocated. The kernel is allowed to allocate fewer than requested; a driver that needs at least a specific count must check the returned value.

For Chapter 20 Stage 1, the driver requests one vector. If the kernel returns 1, the driver proceeds. If the kernel returns 0 (rare but possible on a contended system) or returns an error, the driver releases any allocation and falls back to legacy INTx.

A subtle point: even when `pci_alloc_msi` returns non-zero, the driver **must** call `pci_release_msi(dev)` to undo the allocation at teardown time. Unlike `bus_alloc_resource_any` / `bus_release_resource`, the MSI family uses a single `pci_release_msi` call that undoes all vectors allocated via either `pci_alloc_msi` or `pci_alloc_msix` on the device.

### The Per-Vector Resource Allocation

With MSI vectors allocated at the device level, the driver must now allocate a `SYS_RES_IRQ` resource for each vector. For a single MSI vector, the rid is 1:

```c
int rid = 1;  /* MSI vectors start at rid 1 */
struct resource *irq_res;

irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
if (irq_res == NULL) {
	/* Release the MSI allocation and fall back. */
	pci_release_msi(sc->dev);
	goto fallback;
}
```

Note two differences from Chapter 19's legacy allocation:

First, **rid is 1, not 0**. MSI vectors are numbered starting from 1, leaving rid 0 for legacy INTx. If the driver ever used both (which it should not), the rids would not overlap.

Second, **RF_SHAREABLE is not set**. MSI vectors are per-function; they are not shared with other drivers. The `RF_SHAREABLE` flag is only relevant for legacy INTx. Setting it on an MSI resource allocation does no harm, but it is not meaningful.

### The Filter Handler on an MSI Vector

The filter handler function is identical to Chapter 19's:

```c
int myfirst_intr_filter(void *arg);
```

The kernel calls the filter when the vector fires, exactly as it called the Chapter 19 filter when the legacy line asserted. The filter reads `INTR_STATUS`, acknowledges, enqueues the task for `DATA_AV`, and returns `FILTER_HANDLED` (or `FILTER_STRAY` on no-bit). Nothing about the filter's body needs to change.

`bus_setup_intr(9)` is called identically:

```c
error = bus_setup_intr(sc->dev, irq_res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    myfirst_intr_filter, NULL, sc,
    &sc->intr_cookie);
```

The function signature, the flags, the argument (`sc`), and the out-cookie are all the Chapter 19 pattern.

A small improvement: `bus_describe_intr(9)` can now label the vector with a mode-specific name:

```c
bus_describe_intr(sc->dev, irq_res, sc->intr_cookie, "msi");
```

After this, `vmstat -i` shows the handler as `irq<N>: myfirst0:msi` (for some N the kernel chose). The operator instantly sees the driver is using MSI.

### Building the Fallback

Putting it all together, the Stage 1 `myfirst_intr_setup` becomes a fallback ladder with two tiers: try MSI first, fall back to legacy INTx. The code:

```c
int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error, msi_count, count;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	/*
	 * Tier 1: attempt MSI.
	 */
	msi_count = pci_msi_count(sc->dev);
	if (msi_count > 0) {
		count = 1;
		if (pci_alloc_msi(sc->dev, &count) == 0 && count == 1) {
			sc->irq_rid = 1;
			sc->irq_res = bus_alloc_resource_any(sc->dev,
			    SYS_RES_IRQ, &sc->irq_rid, RF_ACTIVE);
			if (sc->irq_res != NULL) {
				error = bus_setup_intr(sc->dev, sc->irq_res,
				    INTR_TYPE_MISC | INTR_MPSAFE,
				    myfirst_intr_filter, NULL, sc,
				    &sc->intr_cookie);
				if (error == 0) {
					bus_describe_intr(sc->dev,
					    sc->irq_res, sc->intr_cookie,
					    "msi");
					sc->intr_mode = MYFIRST_INTR_MSI;
					device_printf(sc->dev,
					    "interrupt mode: MSI, 1 vector\n");
					goto enabled;
				}
				bus_release_resource(sc->dev,
				    SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
				sc->irq_res = NULL;
			}
			pci_release_msi(sc->dev);
		}
	}

	/*
	 * Tier 2: fall back to legacy INTx.
	 */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate legacy IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->irq_rid, sc->irq_res);
		sc->irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (rid=0)\n");

enabled:
	/* Enable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

The code has three distinct blocks:

1. The MSI attempt block (lines inside the `if (msi_count > 0)` guard).
2. The legacy fallback block.
3. The enabling block at `enabled:` that runs regardless of tier.

The MSI attempt does the full sequence: count query, allocate, allocate the IRQ resource at rid 1, register the handler. If any step fails, the code releases whatever succeeded (the resource if allocated, the MSI if successfully allocated) and falls through.

The legacy fallback is essentially the Chapter 19 setup, unchanged.

The `enabled:` block writes `INTR_MASK` at the device. Whether we got MSI or legacy, the device-side mask is the same.

The fallback structure is what real drivers do. A reader looking at `virtio_pci.c`'s setup code will see the same pattern at larger scale: several attempts with successive fallbacks.

### The intr_mode Field and the dmesg Summary

The softc gains a new field:

```c
enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

struct myfirst_softc {
	/* ... existing fields ... */
	enum myfirst_intr_mode intr_mode;
};
```

The field records which tier the driver ended up using. The attach-time `device_printf` prints it:

```text
myfirst0: interrupt mode: MSI, 1 vector
```

or:

```text
myfirst0: interrupt mode: legacy INTx (rid=0)
```

Operators reading `dmesg` see this line and know which path is active. The reader debugging their driver sees it too; if the driver is falling back to legacy when the reader expected MSI, the line flags the problem immediately.

The `intr_mode` field is also exposed through a read-only sysctl so that user-space tools can read it:

```c
SYSCTL_ADD_INT(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_mode",
    CTLFLAG_RD, &sc->intr_mode, 0,
    "Interrupt mode: 0=legacy, 1=MSI, 2=MSI-X");
```

A script that wants to know whether any `myfirst` instance is using MSI-X can sum the `intr_mode` values across all units.

### What the Teardown Needs to Change

The teardown path in Chapter 19 called `bus_teardown_intr`, drained and freed the taskqueue, and released the IRQ resource. For Stage 1, one additional call is needed: if the driver used MSI, it must call `pci_release_msi` after releasing the IRQ resource:

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release MSI if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

The `pci_release_msi` call is conditional: only call it if the driver actually allocated MSI or MSI-X. Calling it when the driver used only legacy INTx is a no-op in modern FreeBSD, but the condition is clearer.

Notice the ordering: IRQ resource release first, then `pci_release_msi`. This is the opposite of the allocation order (where `pci_alloc_msi` preceded `bus_alloc_resource_any`). The rule is the general teardown rule from Chapter 18 and 19: undo in reverse order of setup.

### Verifying Stage 1

On a guest where the device supports MSI (QEMU's `virtio-rng-pci` does; bhyve's `virtio-rnd` does not), the Stage 1 driver should attach with MSI:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: BAR0 allocated: 0x20 bytes at 0xfebf1000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: MSI, 1 vector
```

On a guest where the device supports only legacy (bhyve's `virtio-rnd` typically):

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: legacy INTx (rid=0)
```

Both cases are correct. The driver runs in either mode; the behaviour (filter, task, counters, simulated-interrupt sysctl) is identical.

`sysctl dev.myfirst.0.intr_mode` returns 0 (legacy), 1 (MSI), or 2 (MSI-X, once Section 5 adds it). The regression script uses this to verify the expected mode.

### What Stage 1 Does Not Do

Stage 1 adds MSI with one vector but does not yet use MSI's multi-vector potential. A single MSI vector is functionally almost identical to a single legacy IRQ (it only gains the scalability benefits on a system with many devices, which a single-device lab rarely shows). The value of Stage 1 is that it introduces the fallback ladder idiom and establishes the `intr_mode` observability; Stage 2 and beyond use those foundations to add multi-vector handling.

### Common Mistakes at This Stage

A short list.

**Using rid = 0 for MSI.** The MSI vector's rid is 1, not 0. Requesting `rid = 0` on a device that has allocated MSI vectors returns the legacy INTx resource, which is not the MSI vector. The driver ends up with a handler on the wrong line. Fix: `rid = 1` for the first MSI or MSI-X vector.

**Forgetting `pci_release_msi` at teardown.** The kernel's MSI allocation state survives `bus_release_resource` on the IRQ resource. Without `pci_release_msi`, the next attach attempt will fail because the kernel still thinks the driver owns the MSI vectors. Fix: always call `pci_release_msi` in teardown when MSI or MSI-X was used.

**Forgetting the INTx fallback.** A driver that only tries MSI and returns an error on failure works on systems that support MSI but fails on older systems. Fix: always provide a legacy INTx fallback.

**Forgetting to restore sc->intr_mode on teardown.** The `intr_mode` field records the tier. Without resetting, a future reattach could read a stale value. Not a serious bug (attach always sets it), but cleanliness matters. Fix: reset to `LEGACY` (or a neutral value) in teardown.

**Mismatching the count.** `pci_alloc_msi` can allocate fewer vectors than requested; if the driver assumes `count == 1` when it is 0, the code dereferences an un-allocated resource. Fix: always check the returned count.

**Calling `pci_alloc_msi` twice without release.** Only one MSI (or MSI-X) allocation can be active per device at a time. Attempting a second allocation without releasing the first returns an error. Fix: if the driver wants to change its allocation (from MSI to MSI-X, say), call `pci_release_msi` first.

### Checkpoint: Stage 1 Working

Before Section 3, confirm Stage 1 is in place:

- `kldstat -v | grep myfirst` shows version `1.3-msi-stage1`.
- `dmesg | grep myfirst` shows the attach banner with an `interrupt mode:` line indicating either MSI or legacy.
- `sysctl dev.myfirst.0.intr_mode` returns 0 or 1.
- `vmstat -i | grep myfirst` shows the handler with either `myfirst0:msi` or `myfirst0:legacy` as the descriptor.
- `sudo sysctl dev.myfirst.0.intr_simulate=1` still drives the Chapter 19 pipeline.
- `kldunload myfirst` runs cleanly; no leaks.

If the MSI path fails on your guest, try QEMU instead of bhyve. If the MSI path works on one and not the other, verify the device's MSI capability is exposed through `pciconf -lvc`.

### Wrapping Up Section 2

Enabling MSI in a driver is three new calls (`pci_msi_count`, `pci_alloc_msi`, `pci_release_msi`), one change to the IRQ resource allocation (rid = 1 instead of 0), and one new softc field (`intr_mode`). The fallback ladder adds a second tier: try MSI, fall back to legacy. Every `bus_setup_intr`, every filter, every taskqueue task, and every teardown step from Chapter 19 carries through unchanged.

Stage 1 handles a single MSI vector. Section 3 moves to multi-vector: several different filter functions, several per-vector softc states, and the beginning of the per-queue handler pattern that modern drivers use extensively.



## Section 3: Managing Multiple Interrupt Vectors

Stage 1 added MSI with one vector. Section 3 extends the driver to handle multiple vectors, each with its own role. The motivating example is the device that has more than one thing to say: a NIC with a receive queue and a transmit queue, an NVMe controller with admin and I/O queues, a UART with receive-ready and transmit-empty events.

The Chapter 20 driver does not have a real multi-queue device; the virtio-rnd target has at most a single event class per interrupt. For teaching purposes, we simulate multi-vector behaviour the same way Chapter 19 simulated interrupts: the sysctl interface lets the reader fire simulated interrupts against specific vectors, and the driver's filter and task machinery demonstrates how real drivers handle multi-vector cases.

By the end of Section 3 the driver is at version `1.3-msi-stage2` and has three MSI-X vectors: an admin vector, an "rx" vector, and a "tx" vector. Each vector has its own filter function, its own deferred task, and its own counters. The filter reads `INTR_STATUS` and acknowledges only the bits relevant to its vector; the task does the vector-specific work.

An important note about the three-vector count. MSI is constrained to a power-of-two vector count (1, 2, 4, 8, 16, or 32), so a request for exactly 3 vectors is rejected by `pci_alloc_msi(9)` with `EINVAL` (see `/usr/src/sys/dev/pci/pci.c`'s `pci_alloc_msi_method`). MSI-X has no such restriction and readily allocates 3 vectors. The MSI tier of the fallback ladder therefore requests a single MSI vector and falls back to the Chapter 19 single-handler pattern; only the MSI-X tier gives the driver its three per-vector filters. Section 5 makes this explicit and Section 8's refactor keeps the MSI tier simple.

### The Per-Vector Design

The design has three vectors:

- **Admin vector (vector 0, rid 1).** Handles `ERROR` and configuration-change events. Low rate; runs rarely.
- **RX vector (vector 1, rid 2).** Handles `DATA_AV` events (receive-ready). Runs at data-path rate.
- **TX vector (vector 2, rid 3).** Handles `COMPLETE` events (transmit-complete). Runs at data-path rate.

Each vector has:

- A separate `struct resource *` (the IRQ resource for that vector).
- A separate `void *intr_cookie` (the kernel's opaque handle for the handler).
- A separate filter function (`myfirst_admin_filter`, `myfirst_rx_filter`, `myfirst_tx_filter`).
- A separate set of counters (so per-CPU concurrent filter runs do not fight over a single shared counter).
- A separate `bus_describe_intr` name (`admin`, `rx`, `tx`).
- A separate deferred task (for RX; admin and TX handle their work inline).

The per-vector state lives in an array of per-vector structures inside the softc:

```c
#define MYFIRST_MAX_VECTORS 3

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX,
	MYFIRST_VECTOR_TX,
};

struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

struct myfirst_softc {
	/* ... existing fields ... */
	struct myfirst_vector	vectors[MYFIRST_MAX_VECTORS];
	int			num_vectors;   /* actually allocated */
};
```

A few design notes worth unpacking.

**Per-vector `struct myfirst_softc *sc` back-pointer.** The argument each filter receives via `bus_setup_intr` is the per-vector structure (`struct myfirst_vector *`), not the global softc. The per-vector structure contains a back-pointer to the softc so the filter can reach shared state when needed. This is the pattern `nvme(4)` uses for per-queue vectors and the pattern every multi-queue driver follows.

**Per-vector counters.** Each vector has its own `fire_count` and `stray_count`. Two filters running on two CPUs can increment their own counters without atomic contention; atomics are still used, but each atomic hits a different cache line.

**Per-vector filter pointer.** The `filter` field stores a pointer to the vector's filter function. This is not strictly required (we could have a switch in a single generic filter), but it makes the per-vector specialisation explicit: each vector's filter is statically known.

**Per-vector task.** Not every vector needs a task. Admin and TX do their work inline (increment a counter, update a flag, maybe wake a waiter). RX defers to a task because it wants to broadcast a condition variable, which requires thread context. The `has_task` flag makes the per-vector difference explicit.

### The Filter Functions

Three distinct filter functions, one per vector:

```c
int
myfirst_admin_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & (MYFIRST_INTR_ERROR)) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_ERROR);
	atomic_add_64(&sc->intr_error_count, 1);
	return (FILTER_HANDLED);
}

int
myfirst_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_DATA_AV) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_DATA_AV);
	atomic_add_64(&sc->intr_data_av_count, 1);
	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}

int
myfirst_tx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_COMPLETE) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);
	atomic_add_64(&sc->intr_complete_count, 1);
	return (FILTER_HANDLED);
}
```

Each filter has the same shape: read status, check the bit this vector cares about, acknowledge, update counters, optionally enqueue a task, return. The differences are which bit each filter cares about and which counters each increments.

Several details worth noticing.

**The stray check is per-vector.** Each filter checks for its own bit, not for any bit. If the filter is called for an event it does not handle (because the bit set is for a different vector), the filter returns `FILTER_STRAY`. This matters less for MSI-X (where each vector has its own dedicated message, so the device never fires the "wrong" vector), but it matters more for MSI with multiple vectors sharing a single capability.

**Counter sharing.** The per-vector counters (`vec->fire_count`, `vec->stray_count`) are specific to the vector. The global counters (`sc->intr_data_av_count`, etc.) are shared and still used for the chapter's per-bit observability. Having both gives the reader a way to cross-check: the RX filter's fire count should approximately equal the global `data_av_count`.

**The filter does not sleep.** All of the Chapter 19 filter-context rules carry through: no sleep locks, no `malloc(M_WAITOK)`, no blocking. The filter uses only atomics and direct BAR accesses.

### The Per-Vector Task

Only RX has a task; admin and TX handle their work in the filter. The RX task is essentially the Chapter 19 task:

```c
static void
myfirst_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

The task runs in thread context on the shared `intr_tq` taskqueue (Chapter 19 created it at `PI_NET` priority). The same taskqueue serves all per-vector tasks; for a driver with truly independent per-queue work, each vector might have its own taskqueue, but Chapter 20 uses one.

### Allocating Multiple Vectors

The setup code for Stage 2 is longer than Stage 1 because it handles multiple vectors:

```c
int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error, wanted, allocated, i;

	TASK_INIT(&sc->vectors[MYFIRST_VECTOR_RX].task, 0,
	    myfirst_rx_task_fn, &sc->vectors[MYFIRST_VECTOR_RX]);
	sc->vectors[MYFIRST_VECTOR_RX].has_task = true;
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_admin_filter;
	sc->vectors[MYFIRST_VECTOR_RX].filter = myfirst_rx_filter;
	sc->vectors[MYFIRST_VECTOR_TX].filter = myfirst_tx_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].name = "admin";
	sc->vectors[MYFIRST_VECTOR_RX].name = "rx";
	sc->vectors[MYFIRST_VECTOR_TX].name = "tx";
	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		sc->vectors[i].id = i;
		sc->vectors[i].sc = sc;
	}

	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	/*
	 * Try to allocate a single MSI vector. MSI requires a power-of-two
	 * count (PCI specification and /usr/src/sys/dev/pci/pci.c's
	 * pci_alloc_msi_method enforces this), so we cannot request the
	 * MYFIRST_MAX_VECTORS = 3 we want; we ask for 1 and fall back to
	 * the Chapter 19 single-handler pattern at rid=1, the same way
	 * sys/dev/virtio/pci/virtio_pci.c's vtpci_alloc_msi() does.
	 *
	 * MSI-X, covered in Section 5, is the tier where we actually
	 * obtain three distinct vectors; MSI-X is not constrained to
	 * power-of-two counts.
	 */
	allocated = 1;
	if (pci_msi_count(sc->dev) >= 1 &&
	    pci_alloc_msi(sc->dev, &allocated) == 0 && allocated >= 1) {
		sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
		sc->vectors[MYFIRST_VECTOR_ADMIN].name = "msi";
		error = myfirst_intr_setup_vector(sc, MYFIRST_VECTOR_ADMIN, 1);
		if (error == 0) {
			sc->intr_mode = MYFIRST_INTR_MSI;
			sc->num_vectors = 1;
			device_printf(sc->dev,
			    "interrupt mode: MSI, 1 vector "
			    "(single-handler fallback)\n");
			goto enabled;
		}
		pci_release_msi(sc->dev);
	}

	/*
	 * MSI allocation failed or was unavailable. Fall back to legacy
	 * INTx with a single vector-0 handler that handles every event
	 * class in one place.
	 */

fallback_legacy:
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid = 0;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = bus_alloc_resource_any(
	    sc->dev, SYS_RES_IRQ,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res == NULL) {
		device_printf(sc->dev, "cannot allocate legacy IRQ\n");
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res);
		sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	sc->num_vectors = 1;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (1 handler for all events)\n");

enabled:
	/* Enable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

The code has three phases: MSI attempt, MSI fallback cleanup, and legacy fallback. The MSI attempt loops through the vectors, calling a helper (`myfirst_intr_setup_vector`) to allocate and register each one. On failure at any vector, the code unwinds in reverse order and falls through to legacy.

The helper:

```c
static int
myfirst_intr_setup_vector(struct myfirst_softc *sc, int idx, int rid)
{
	struct myfirst_vector *vec = &sc->vectors[idx];
	int error;

	vec->irq_rid = rid;
	vec->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &vec->irq_rid, RF_ACTIVE);
	if (vec->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, vec->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    vec->filter, NULL, vec, &vec->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
	    "%s", vec->name);
	return (0);
}
```

The helper is small and symmetric: allocate the resource, set up the handler, describe it. The argument to `bus_setup_intr` is the per-vector structure (`vec`), not the softc. The filter receives `vec` as its `void *arg` and uses `vec->sc` when it needs the softc.

The per-vector teardown helper:

```c
static void
myfirst_intr_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

Teardown is the inverse of setup: tear down the handler, release the resource.

### The Full Teardown Path

The multi-vector teardown calls the per-vector helper for each active vector, then releases the MSI allocation once:

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Per-vector teardown. */
	for (i = 0; i < sc->num_vectors; i++)
		myfirst_intr_teardown_vector(sc, i);

	/* Drain tasks. */
	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release MSI if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

The ordering is the now-familiar one: mask at device, tear down handlers, drain tasks, release MSI. The per-vector loop does the per-vector work.

### Simulating Interrupts Per Vector

Chapter 19's simulated-interrupt sysctl fires one handler at a time. Stage 2 extends the concept: a sysctl per vector, or a single sysctl with a vector-index field. The chapter's code goes with the simpler single-sysctl-per-vector form:

```c
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_admin",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    &sc->vectors[MYFIRST_VECTOR_ADMIN], 0,
    myfirst_intr_simulate_vector_sysctl, "IU",
    "Simulate admin vector interrupt");
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_rx", ...);
SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_tx", ...);
```

The handler:

```c
static int
myfirst_intr_simulate_vector_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_vector *vec = arg1;
	struct myfirst_softc *sc = vec->sc;
	uint32_t mask = 0;
	int error;

	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	MYFIRST_LOCK(sc);
	if (sc->hw == NULL || sc->bar_res == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	/*
	 * Invoke this vector's filter if it has one (MSI-X). On single-
	 * handler tiers (MSI with 1 vector, or legacy INTx) only slot 0
	 * has a registered filter, so we fall through to it. The Chapter 19
	 * myfirst_intr_filter handles all three status bits in one pass.
	 */
	if (vec->filter != NULL)
		(void)vec->filter(vec);
	else if (sc->vectors[MYFIRST_VECTOR_ADMIN].filter != NULL)
		(void)sc->vectors[MYFIRST_VECTOR_ADMIN].filter(
		    &sc->vectors[MYFIRST_VECTOR_ADMIN]);
	return (0);
}
```

From user space:

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2  # ERROR bit, admin vector
sudo sysctl dev.myfirst.0.intr_simulate_rx=1     # DATA_AV bit, rx vector
sudo sysctl dev.myfirst.0.intr_simulate_tx=4     # COMPLETE bit, tx vector
```

The `intr_count` counters per vector tick independently. A reader can verify per-vector behaviour by firing each sysctl and watching the corresponding `vec->fire_count` counter rise.

### What Happens on Legacy Fallback

When the driver falls back to legacy INTx (because MSI was unavailable or failed), there is only one handler to cover all three event classes. The code assigns the Chapter 19 `myfirst_intr_filter` to the admin vector's slot and uses that single filter on `rid = 0`. The admin vector's filter becomes a multi-event handler that looks at all three status bits and dispatches accordingly.

This is a small but important detail: the Chapter 19 filter still exists and is reused on the legacy path, while the per-vector filters are only used when MSI or MSI-X is available. A reader inspecting the driver sees both, and the difference is explained in Section 3's comments.

### The Stage 2 dmesg Banner

On a guest where the driver lands on the MSI-X tier (this is the only tier that delivers three vectors; the MSI tier falls back to a single-handler setup for the reasons explained earlier):

```text
myfirst0: BAR0 allocated: 0x20 bytes at 0xfebf1000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: interrupt mode: MSI-X, 3 vectors
```

`vmstat -i | grep myfirst` shows three separate lines:

```text
irq256: myfirst0:admin                 12         1
irq257: myfirst0:rx                    98         8
irq258: myfirst0:tx                    45         4
```

(The exact IRQ numbers vary by platform; MSI-allocated IRQs on x86 start in the 256 range once the I/O APIC range is exhausted.)

On a guest where only MSI is available, the driver reports a single-handler fallback:

```text
myfirst0: interrupt mode: MSI, 1 vector (single-handler fallback)
```

and `vmstat -i` shows one line, because the driver uses the Chapter 19 pattern on that one MSI vector.

The per-vector breakdown (three lines) is what makes the multi-vector driver observable. An operator watching the counters can tell which vector is active and how often.

### Common Mistakes in Stage 2

A short list.

**Passing the softc (not the vector) as the filter argument.** If you pass `sc` instead of `vec`, the filter cannot tell which vector it is serving. Fix: pass `vec` to `bus_setup_intr`; the filter reaches `sc` through `vec->sc`.

**Forgetting to initialise `vec->sc`.** The per-vector structures are zero-initialised by `myfirst_init_softc`; `vec->sc` stays NULL unless explicitly set. Without it, the filter's `vec->sc->mtx` access is a null dereference. Fix: set `vec->sc = sc` during setup, before any handler is registered.

**Using the same rid for multiple vectors.** MSI rids are 1, 2, 3, ...; reusing rid 1 for both the admin and RX vectors means only one handler is actually registered. Fix: assign rids in sequence per vector.

**Per-vector handler that touches shared state without locking.** Two filters running on different CPUs both try to write to a single `sc->counter`. Without atomic operations or a spin lock, the increment loses updates. Fix: use per-vector counters where possible, and atomic ops for any shared counter.

**Per-vector task stored in the wrong location.** If the task is in the softc instead of in the vector structure, two vectors enqueueing the "same" task collide. Fix: store the task in the vector structure, and pass the vector as the task argument.

**Missing per-vector teardown on partial setup failure.** The goto cascade must undo exactly the vectors that succeeded. Missing cleanup leaves IRQ resources allocated. Fix: use the per-vector teardown helper and iterate backward on the partial-failure case.

### Wrapping Up Section 3

Managing multiple interrupt vectors is a set of three new patterns: per-vector state (in a `struct myfirst_vector` array), per-vector filter functions, and per-vector `bus_describe_intr` names. Each vector has its own IRQ resource at its own rid, its own filter function that reads only the status bits relevant to its vector, its own counters, and (optionally) its own deferred task. The single MSI or MSI-X allocation call handles the device-side state; the per-vector `bus_alloc_resource_any` and `bus_setup_intr` calls handle each handler individually.

The fallback ladder from Section 2 extends naturally: try MSI with N vectors first; on partial failure, release and try legacy INTx with a single handler. The per-vector teardown helper makes the partial-failure unwind and the clean teardown symmetric.

Section 4 is the locking-and-data-structures section. It examines what happens when multiple filters run on multiple CPUs at the same time, and what synchronisation discipline keeps the shared state sane.



## Section 4: Designing Interrupt-Safe Data Structures

Section 3 added multiple vectors, each with its own handler. Section 4 examines the consequences: multiple handlers can run concurrently on multiple CPUs, and any data the handlers share must be protected accordingly. The discipline is not new; it is the multi-CPU specialisation of Chapter 11's locking model. What is new is that Chapter 20's driver has three (or more) concurrent filter-context paths instead of one.

Section 4 is the section where multi-vector changes the shape of the driver's state, not just the count of its handlers.

### The New Concurrency Picture

Chapter 19's driver had one filter and one task. The filter ran on whichever CPU the kernel routed the interrupt to; the task ran on the taskqueue's worker thread. Two of them could in principle run simultaneously: the filter on CPU 0 and the task on CPU 3, for example. The atomic counters and the `sc->mtx` (held by the task, not by the filter) gave the needed synchronisation.

Chapter 20's multi-vector driver has three filters and one task. On an MSI-X system, each filter has its own `intr_event`, so each can fire on a different CPU independently. A burst of three interrupts arriving within a microsecond can see three filters running on three CPUs at the same time. The single task is still serialised through the taskqueue, but the filters are not.

The data the filters touch falls into three categories:

1. **Per-vector state.** Each vector's own counters, its own cookie, its own resource. No sharing between vectors. No synchronisation needed.
2. **Shared counters.** Counters updated by any filter (the global `intr_data_av_count`, `intr_error_count`, etc.). Must be atomic.
3. **Shared device state.** The BAR itself, the softc's `sc->hw` pointer, `sc->pci_attached`, the mutex-protected fields. Access rules depend on the context.

The discipline is to keep per-vector state truly per-vector, to use atomics for shared counters, and to obey the Chapter 11 locking rules for anything requiring a sleep mutex.

### Per-Vector State: The Default

The easiest synchronisation is no synchronisation. If a piece of state is touched by only one vector's filter (and by nothing else), no lock is needed. This is the case for:

- `vec->fire_count`: incremented by this vector's filter only, read by sysctl handlers via the sysctl-reader path. Atomic add suffices; no lock between filter and sysctl because the sysctl reads atomically.
- `vec->stray_count`: same pattern.
- `vec->intr_cookie`: written once at setup, read at teardown. Single-writer, ordered access.
- `vec->irq_res`: same pattern.

Most per-vector state falls into this category. The `struct myfirst_vector` array in the softc is the key pattern: each vector's state lives in its own slot, touched only by its own filter.

### Shared Counters: Atomic Operations

The global per-bit counters (Chapter 19 introduced `sc->intr_data_av_count`, etc.) are updated by the corresponding vector's filter. Only one filter updates each counter, so technically they are per-vector-except-by-name. But the reader can imagine a scenario where a bit pattern appears in `INTR_STATUS` that requires both the RX and the admin vectors to increment shared counters. The safer approach: make each update atomic.

Chapter 20 uses `atomic_add_64` throughout the filter paths:

```c
atomic_add_64(&sc->intr_data_av_count, 1);
```

This is cheap (one locked instruction on x86, a barrier-plus-add on arm64), and it lets the filter run on any CPU without worrying about lost updates.

The cost of `atomic_add_64` on a heavily-shared counter is cache-line bouncing: every increment from a different CPU invalidates the cache line on the other CPUs. For a counter incremented a million times per second from several CPUs, this is a measurable performance hit. The mitigation is to make counters truly per-CPU (using `counter(9)` or `DPCPU_DEFINE`) and sum them only when read; Chapter 20's driver is not at that scale, so plain atomics are fine.

### Shared Device State: The Mutex Discipline

`sc->hw`, `sc->pci_attached`, `sc->bar_res`: these are set during attach and torn down during detach. During steady state, they are read-only. The filters access them without a lock because the lifetime discipline (attach before enabling, disable before detach) ensures the pointers are valid whenever the filter can run.

The rule: a filter that accesses `sc->hw` or `sc->bar_res` without a lock must be confident the attach-detach ordering guarantees the pointer is valid. Chapter 20's Section 7 walks the ordering in detail. For Section 4's purposes, trust the discipline: when the filter runs, the device is attached and the pointers are valid.

### The Per-Vector Lock: When You Need It

Sometimes the per-vector state is richer than a counter. A vector that reads from a receive queue and updates a per-queue data structure (a ring of mbufs, say) needs a spin lock to protect the ring from two simultaneous firings of the same vector. Wait, can the same vector fire twice simultaneously on an MSI-X system?

On MSI-X, the kernel guarantees that each `intr_event` delivers to one CPU at a time; a single vector does not re-enter itself. Two different vectors can run on two CPUs at once, but vector N cannot run on CPU 3 and CPU 5 simultaneously.

This means: **per-vector state does not need a per-vector lock** for concurrent access from the same vector. It might need one for communication between the filter and the task (the task runs on a different CPU, possibly concurrently with the filter), but a spin lock suffices there, and the communication is usually through atomic operations anyway.

A spin lock becomes useful when:

- A driver uses a single filter function for multiple vectors, and the kernel can dispatch two vectors' worth of that filter concurrently. (Chapter 20's Stage 2 has separate filters per vector, so this does not apply.)
- A driver shares a receive ring between the filter (which fills it) and a task (which drains it). A spin lock protects the ring index; the filter acquires the spin lock, adds to the ring, releases. The task acquires, drains, releases.

Chapter 20's driver does not use spin locks in the filter; the per-vector counters are atomic and the shared state is handled through the existing `sc->mtx` in the task. Real drivers may need spin locks in richer scenarios.

### Per-CPU Data: The Advanced Option

For very-high-rate drivers, even atomic counters on shared data become a bottleneck. The solution is per-CPU data: each CPU has its own copy of the counter, the filter increments its own CPU's copy (no cross-CPU traffic), and the sysctl reader sums the per-CPU values.

FreeBSD's `counter(9)` API provides this: a `counter_u64_t` is a handle to a per-CPU array, `counter_u64_add(c, 1)` increments the current CPU's slot, and `counter_u64_fetch(c)` sums all CPUs' slots on read. The implementation uses per-CPU data regions (`DPCPU_DEFINE` under the hood) and is as cheap as a normal non-atomic increment on the hot path.

Chapter 20's driver does not use `counter(9)`; plain atomics are enough for the demo's scale. Real high-throughput drivers (ten gigabit NICs, NVMe controllers at a million IOPS) use `counter(9)` extensively. A reader writing such a driver should study `counter(9)` after Chapter 20.

### Lock Ordering and Multi-Vector Complications

Chapter 15 established the driver's lock order: `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`. Chapter 19's filter took no locks (atomics only); the task took `sc->mtx`. Chapter 20's per-vector filters still take no locks (atomics only), so the filter path does not contribute new lock-order edges. The per-vector tasks still take `sc->mtx`, same as Chapter 19's single task.

Lock ordering with multiple tasks running concurrently requires a small extension. When the admin task and the RX task both acquire `sc->mtx`, they serialise on the mutex. That is fine as long as each task releases the mutex promptly; if the admin task held `sc->mtx` while waiting on something slow, the RX task would stall. The Chapter 15 rule "no long-held mutexes" applies here too.

WITNESS catches most lock-order problems. For Chapter 20, the lock-order story is essentially unchanged from Chapter 19 because the filter paths are lock-free and the task paths all acquire the same single `sc->mtx`.

### The Memory Model: Why Atomics Matter

A subtle point worth making explicit. In a multi-CPU system, writes from one CPU are not visible to other CPUs instantly. A write on CPU 0 to `sc->intr_count++` (without atomics) might land in CPU 0's store buffer and take nanoseconds or microseconds to propagate to CPU 3's view of the same memory. In that window, CPU 3 could read the pre-write value.

`atomic_add_64` includes a memory barrier that forces the write to become globally visible before the instruction returns. This is what makes the counter's value "consistent" across CPUs: any reader after the increment sees the new value.

For counter state, this level of consistency is sufficient. The counter's absolute value at any instant is not important; what matters is that the value grows monotonically and reaches the correct total. `atomic_add_64` guarantees both.

For richer shared state (say, a shared data-structure index that multiple filters update), the memory model gets more subtle. The driver would need a spin lock, which provides both mutual exclusion and a memory barrier. Chapter 20's driver does not need this level of machinery; Chapter 19's atomic discipline carries through.

### Observability: Per-Vector Counters in sysctl

Each vector gets its own sysctl subtree so the operator can query it:

```c
char name[32];
for (int i = 0; i < MYFIRST_MAX_VECTORS; i++) {
	snprintf(name, sizeof(name), "vec%d_fire_count", i);
	SYSCTL_ADD_U64(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, name,
	    CTLFLAG_RD, &sc->vectors[i].fire_count, 0,
	    "Fire count for this vector");
}
```

From user space:

```sh
sysctl dev.myfirst.0 | grep vec
```

```text
dev.myfirst.0.vec0_fire_count: 42    # admin
dev.myfirst.0.vec0_stray_count: 0
dev.myfirst.0.vec1_fire_count: 9876  # rx
dev.myfirst.0.vec1_stray_count: 0
dev.myfirst.0.vec2_fire_count: 4523  # tx
dev.myfirst.0.vec2_stray_count: 0
```

The operator can see at a glance which vectors are firing and at what rates. The stray counts should stay at zero on MSI-X (each vector has its own dedicated message) but may tick on MSI or legacy when a shared filter sees an event for a different vector.

### Wrapping Up Section 4

Multi-vector drivers change the concurrency picture: several filters can run on several CPUs simultaneously. The discipline is to design state per-vector where possible, use atomics for shared counters, and obey the existing Chapter 11 lock order for anything needing a sleep mutex. Per-CPU counters (`counter(9)`) are available for very-high-rate drivers but are overkill for Chapter 20.

The driver's lock order does not gain new edges because the filter path remains lock-free (atomics only) and the tasks all take `sc->mtx`. WITNESS still catches lock-order problems; the atomic discipline still catches the rest.

Section 5 moves to the more capable mechanism: MSI-X. The API is very similar (`pci_msix_count` + `pci_alloc_msix` instead of the MSI pair), but the scalability and CPU-affinity options are richer.



## Section 5: Using MSI-X for High Flexibility

Section 2 introduced MSI with one vector, Section 3 extended to multiple MSI vectors, Section 4 walked through the concurrency implications. Section 5 moves to MSI-X: the fuller mechanism that modern PCIe devices use when they have more than a handful of interrupts to manage. The API is parallel to MSI's, so the code change is small; the conceptual change is that MSI-X lets the driver bind each vector to a specific CPU, through `bus_bind_intr(9)` and `bus_get_cpus(9)`, and that matters for real performance.

### The MSI-X Count and Allocation API

The API mirrors MSI's:

```c
int msix_count = pci_msix_count(sc->dev);
```

`pci_msix_count(9)` returns the number of MSI-X vectors the device advertises (0 if no MSI-X capability). The count comes from the MSI-X capability's `Table Size` field plus one; a device with `Table Size = 7` advertises 8 vectors.

Allocating is similar:

```c
int count = desired;
int error = pci_alloc_msix(sc->dev, &count);
```

Same input-output `count` parameter, same semantics: the kernel may allocate fewer than requested. Unlike MSI, MSI-X allows a non-power-of-two count, so if the driver asks for 3 vectors, the kernel can give 3.

The same `pci_release_msi(9)` call releases MSI-X vectors; there is no separate `pci_release_msix`. The function name is a historical artefact; it handles both MSI and MSI-X.

### The Fallback Ladder Extended

The Chapter 20 driver's full fallback ladder is:

1. **MSI-X** with the desired vector count.
2. **MSI** with the desired vector count, if MSI-X is unavailable or allocation failed.
3. **Legacy INTx** with a single handler for everything, if both MSI-X and MSI fail.

The code structure parallels Section 3's two-tier ladder, extended with a third tier at the top:

```c
/* Tier 0: MSI-X. */
wanted = MYFIRST_MAX_VECTORS;
if (pci_msix_count(sc->dev) >= wanted) {
	allocated = wanted;
	if (pci_alloc_msix(sc->dev, &allocated) == 0 &&
	    allocated == wanted) {
		for (i = 0; i < wanted; i++) {
			error = myfirst_intr_setup_vector(sc, i, i + 1);
			if (error != 0)
				goto fail_msix;
		}
		sc->intr_mode = MYFIRST_INTR_MSIX;
		sc->num_vectors = wanted;
		device_printf(sc->dev,
		    "interrupt mode: MSI-X, %d vectors\n", wanted);
		myfirst_intr_bind_vectors(sc);
		goto enabled;
	}
	if (allocated > 0)
		pci_release_msi(sc->dev);
}

/* Tier 1: MSI. */
/* ... Section 3 MSI code ... */

fail_msix:
for (i -= 1; i >= 0; i--)
	myfirst_intr_teardown_vector(sc, i);
pci_release_msi(sc->dev);
/* fallthrough to MSI attempt, then legacy. */
```

The structure is straightforward: each tier has the same pattern (query count, allocate, set up vectors, mark mode, describe). The code follows the well-worn waterfall.

### Vector Binding With bus_bind_intr

Once MSI-X is allocated, the driver has the option of binding each vector to a specific CPU. The API is:

```c
int bus_bind_intr(device_t dev, struct resource *r, int cpu);
```

The `cpu` is an integer CPU ID from 0 to `mp_ncpus - 1`. On success, the interrupt is routed to that CPU. On failure, the function returns an errno; the driver treats it as a non-fatal hint and continues without binding.

For Chapter 20's three-vector driver, a reasonable binding is:

- **Admin vector**: CPU 0 (control work, any CPU is fine).
- **RX vector**: CPU 1 (cache locality benefits for a real RX queue).
- **TX vector**: CPU 2 (similar locality benefit).

On a two-CPU system, the bindings would collapse; on a many-CPU system, the driver should use `bus_get_cpus(9)` to query which CPUs are local to the device's NUMA node and distribute vectors accordingly.

The bind helper:

```c
static void
myfirst_intr_bind_vectors(struct myfirst_softc *sc)
{
	int i, cpu, ncpus;
	int err;

	if (mp_ncpus < 2)
		return;  /* nothing to bind */

	ncpus = mp_ncpus;
	for (i = 0; i < sc->num_vectors; i++) {
		cpu = i % ncpus;
		err = bus_bind_intr(sc->dev, sc->vectors[i].irq_res, cpu);
		if (err != 0) {
			device_printf(sc->dev,
			    "bus_bind_intr vector %d to CPU %d: %d\n",
			    i, cpu, err);
		}
	}
}
```

The code is a round-robin bind: vector 0 to CPU 0, vector 1 to CPU 1, and so on, wrapping modulo the CPU count. On a two-CPU system with three vectors, vector 0 and vector 2 both land on CPU 0; on a four-CPU system, each vector gets its own CPU.

A more sophisticated driver uses `bus_get_cpus(9)`:

```c
cpuset_t local_cpus;
int ncpus_local;

if (bus_get_cpus(sc->dev, LOCAL_CPUS, sizeof(local_cpus),
    &local_cpus) == 0) {
	/* Use only CPUs in local_cpus for binding. */
	ncpus_local = CPU_COUNT(&local_cpus);
	/* ... pick from local_cpus ... */
}
```

The `LOCAL_CPUS` argument returns the CPUs that are in the same NUMA domain as the device. The `INTR_CPUS` argument returns CPUs suitable for handling device interrupts (usually excluding CPUs pinned to critical work). A driver that cares about NUMA performance uses these to place vectors on NUMA-local CPUs.

Chapter 20's driver does not use `bus_get_cpus(9)` by default; the simpler round-robin bind is enough for the lab. A challenge exercise adds NUMA-aware binding.

### The MSI-X dmesg Summary

The Chapter 20 driver prints a line like:

```text
myfirst0: interrupt mode: MSI-X, 3 vectors
```

With per-vector CPU bindings visible in `vmstat -i` (the per-CPU totals in vmstat -i are not per-vector; they are aggregated) and in `cpuset -g -x <irq>` output (one query per vector):

```sh
for irq in 256 257 258; do
    echo "IRQ $irq:"
    cpuset -g -x $irq
done
```

Typical output:

```text
IRQ 256:
irq 256 mask: 0
IRQ 257:
irq 257 mask: 1
IRQ 258:
irq 258 mask: 2
```

(The IRQ numbers depend on the platform's assignment.)

An operator inspecting the driver's interrupt setup can see which vectors fire where.

### Per-Vector bus_describe_intr

Each MSI-X vector should have a description. The code from Section 3 already sets them via `bus_describe_intr(9)`:

```c
bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
    "%s", vec->name);
```

After this, `vmstat -i` shows each vector with its role:

```text
irq256: myfirst0:admin                 42         4
irq257: myfirst0:rx                 12345      1234
irq258: myfirst0:tx                  5432       543
```

The operator sees which vector is the admin, which is the RX, which is the TX, and how busy each is. This is essential observability for a multi-vector driver.

### MSI-X Table and BAR Considerations

A detail worth mentioning, though the driver does not interact with it directly. The MSI-X capability structure points to a **table** and a **pending bit array** (PBA), each living in one of the device's BARs. The BAR that holds each is discoverable through `pci_msix_table_bar(9)` and `pci_msix_pba_bar(9)`:

```c
int table_bar = pci_msix_table_bar(sc->dev);
int pba_bar = pci_msix_pba_bar(sc->dev);
```

Each returns the BAR index (0 through 5) or -1 if the device has no MSI-X capability. For most devices, the table and PBA are in BAR 0 or BAR 1; for some devices, they share a BAR with memory-mapped registers (the driver's BAR 0).

The kernel handles the table programming internally. The driver's only interaction is:

- Ensure the BAR containing the table is allocated (so the kernel can reach it). On some devices this requires the driver to allocate additional BARs.
- Call `pci_alloc_msix` and let the kernel do the rest.

For Chapter 20's driver, the virtio-rnd target (or its QEMU equivalent with MSI-X) has the table in BAR 1 or a dedicated region. The Chapter 18 code allocated BAR 0; the kernel handles the MSI-X table BAR implicitly through the allocation infrastructure.

A driver that wants to inspect the table bar:

```c
device_printf(sc->dev, "MSI-X table in BAR %d, PBA in BAR %d\n",
    pci_msix_table_bar(sc->dev), pci_msix_pba_bar(sc->dev));
```

This is useful for diagnostic purposes.

### Allocating Fewer Vectors Than Requested

A subtle case: the device advertises 3 MSI-X vectors and the driver asks for 3, but the kernel only allocates 2. What does the driver do?

The answer depends on the driver's design. Options:

1. **Fail the attach.** If the driver cannot function with fewer vectors, return an error. This is rare for flexible drivers but possible for drivers with tight hardware requirements.
2. **Use what we got.** If the driver can function with 2 vectors (folding RX and TX into one, for example), use the 2 and adjust the configuration. This is common for drivers targeting ranges of hardware.
3. **Release and fall back.** If 2 vectors is worse than 1 MSI vector for some reason, release MSI-X and try MSI. This is uncommon.

Chapter 20's driver goes with option 1: if it does not get exactly `MYFIRST_MAX_VECTORS` (3) vectors, it releases MSI-X and falls back to MSI. A more sophisticated driver would use option 2; the Chapter 20 teaching focuses on the simpler pattern.

Real FreeBSD drivers often use option 2 with a helper function that figures out how to distribute the allocated vectors across the desired roles. The `nvme(4)` driver is an example: if it asks for N I/O queues worth of vectors and gets fewer, it reduces the number of I/O queues accordingly.

### Testing MSI-X on bhyve vs QEMU

A practical detail about the lab. bhyve's legacy virtio-rnd device (what Chapter 18 and 19 used) does not expose MSI-X; it is a legacy-only virtio transport. To exercise MSI-X in a guest, the reader needs one of:

- **QEMU with `-device virtio-rng-pci`** (not `-device virtio-rng`, which is legacy). The modern virtio-rng-pci exposes MSI-X.
- **Modern bhyve emulation** of a non-virtio-rnd device that has MSI-X. Chapter 20 does not use this path.
- **Real hardware** that supports MSI-X (most modern PCIe devices).

QEMU is the practical choice for Chapter 20's labs. The driver's fallback ladder ensures it still works on bhyve (falling back to legacy); testing MSI-X specifically requires QEMU or real hardware.

### Common Mistakes in MSI-X Setup

A short list.

**Using `pci_release_msix`.** This function does not exist in FreeBSD; the release is handled by `pci_release_msi(9)` which works for both MSI and MSI-X. Fix: use `pci_release_msi`.

**Binding to a CPU the device cannot reach.** Some platforms (rarely) have CPUs that are not in the interrupt-controller's routable set. The `bus_bind_intr` call returns an error; ignore it and continue. Fix: log the error but do not fail attach.

**Expecting vmstat -i to show per-CPU breakdown.** `vmstat -i` aggregates per-event counts. The per-CPU breakdown is available via `cpuset -g -x <irq>` (or `sysctl hw.intrcnt` in raw form). The operator has to look in the right place. Fix: document the observability path for your driver.

**Failing to check `allocated` against `wanted`.** Accepting a partial allocation when the driver cannot handle it leads to subtle bugs (vectors that should fire never fire). Fix: decide the strategy upfront (fail, adapt, or release) and code accordingly.

### Wrapping Up Section 5

MSI-X is the fuller mechanism: a per-vector addressable table that the kernel programs on behalf of the driver, with per-vector CPU affinity and per-vector masking available to drivers that need them. The API mirrors MSI's closely (`pci_msix_count` + `pci_alloc_msix` + `pci_release_msi`), and the per-vector resource allocation is the same as Section 3's MSI code. The new piece is `bus_bind_intr(9)` for CPU affinity and `bus_get_cpus(9)` for NUMA-local CPU queries.

For Chapter 20's driver, MSI-X is the preferred tier; the fallback ladder tries MSI-X first, falls back to MSI, and finally to legacy INTx. The per-vector handlers, counters, and tasks from Section 3 work unchanged on MSI-X; only the allocation call changes.

Section 6 is where vector-specific events become explicit. Each vector has its own purpose, its own filter logic, and its own observable behaviour. Chapter 20's Stage 3 is the stage where the driver looks like a real multi-queue device even though the underlying silicon (the virtio-rnd target) is simpler.



## Section 6: Handling Vector-Specific Events

Sections 2 through 5 built the infrastructure for multi-vector handling. Section 6 is the section where the per-vector roles become explicit. Each vector has a specific event class it handles; each filter has a specific check it makes; each task has a specific wake-up it performs. The driver at Stage 3 treats vectors as named, purposeful entities rather than interchangeable slots.

The Chapter 20 driver's three vectors have distinct responsibilities:

- **Admin vector** handles `ERROR` events. The filter reads status, acknowledges, and (on real errors) logs a message. Admin work is infrequent but must not be dropped.
- **RX vector** handles `DATA_AV` (receive-available) events. The filter acknowledges and defers the data-handling work to a per-vector task that broadcasts a condition variable.
- **TX vector** handles `COMPLETE` (transmit-complete) events. The filter acknowledges and optionally wakes a thread waiting for transmit completion. The filter handles the bookkeeping inline.

Each vector is independently testable through the simulated-interrupt sysctl, independently observable through its counters, and independently bound to a CPU. The driver is starting to look like a small real multi-queue device.

### The Admin Vector

The admin vector handles rare, important events: configuration changes, errors, link state changes (for a NIC), temperature alerts (for a sensor). Its work is usually small: log the event, update a state flag, wake a user-space waiter that polls the state.

For the Chapter 20 driver, the admin vector handles the Chapter 17 `ERROR` bit. The filter:

```c
int
myfirst_admin_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_ERROR) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_ERROR);
	atomic_add_64(&sc->intr_error_count, 1);
	return (FILTER_HANDLED);
}
```

On a real device, the admin filter might also examine a secondary register (an error code register, say) and decide based on the severity whether to schedule a recovery task. Chapter 20's driver keeps it simple: count and acknowledge.

### The RX Vector

The RX vector is the data-path vector. For a NIC, it would handle received packets. For an NVMe drive, completions of read requests. For the Chapter 20 driver, it handles the Chapter 17 `DATA_AV` bit.

The filter is small (acknowledge and enqueue a task); the task does the real work. Section 3 showed both. The task:

```c
static void
myfirst_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

In a real driver, the task would walk a receive descriptor ring, build mbufs, and pass them up the network stack. For Chapter 20's demo, it reads `DATA_OUT`, stores the value, broadcasts the condition variable, and lets any waiting cdev reader wake up.

The `npending` argument is the number of times the task was enqueued since its last run. For a high-rate RX path, a task that ran once and saw `npending = 5` knows it is behind (5 interrupts coalesced into 1 task run) and can size its batch accordingly. Chapter 20's task ignores `npending`; real drivers use it for batching.

### The TX Vector

The TX vector is the transmit-completion vector. For a NIC, it signals that a packet the driver handed to hardware has been transmitted and the buffer can be reclaimed. For an NVMe drive, it signals that a write request has completed.

For the Chapter 20 driver, it handles the Chapter 17 `COMPLETE` bit. The filter does the work inline (no task needed):

```c
int
myfirst_tx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_COMPLETE) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_COMPLETE);
	atomic_add_64(&sc->intr_complete_count, 1);
	return (FILTER_HANDLED);
}
```

The TX filter's inline-only design is a deliberate choice. On a real TX completion path, the filter might record the completion count and the task might walk the TX descriptor ring to reclaim buffers. For the Chapter 20 demo, the completion is just counted.

An alternative design would have TX also use a task. Whether to do this depends on the work the task would do: if it is substantial (walking a ring, reclaiming dozens of buffers), a task is worth it; if it is trivial (a single decrement of an in-flight counter), inline in the filter is fine. Chapter 20 picks inline for TX to illustrate that not every vector needs a task.

### Vector-to-Event Mapping

On MSI-X, each vector is independent; firing vector 1 delivers to the RX filter, firing vector 2 delivers to the TX filter. The mapping from vector to event is part of the driver's design, not a kernel choice.

On MSI with multiple vectors, the kernel may in principle dispatch multiple vectors in rapid succession if multiple events fire simultaneously. The driver's filters must each read the status register and claim only the bits belonging to their vector.

On legacy INTx, there is only one vector and one filter. The filter handles all three event classes in one pass.

Chapter 20's code handles all three cases: the per-vector filter on MSI-X reads only its own bit, the per-vector filter on MSI does the same (with the same bit-check logic), and the single filter on legacy INTx handles all three bits.

### Simulated Per-Vector Events

The simulation sysctl from Section 3 lets a reader exercise each vector independently. From user space:

```sh
# Simulate an admin interrupt (ERROR).
sudo sysctl dev.myfirst.0.intr_simulate_admin=2

# Simulate an RX interrupt (DATA_AV).
sudo sysctl dev.myfirst.0.intr_simulate_rx=1

# Simulate a TX interrupt (COMPLETE).
sudo sysctl dev.myfirst.0.intr_simulate_tx=4
```

Each sysctl writes its specific bit to `INTR_STATUS` and invokes the corresponding vector's filter. On the MSI-X tier all three filters exist, so each sysctl hits its own per-vector filter and its per-vector counter ticks. On the MSI tier (single vector at slot 0) and on the legacy INTx tier (single vector at slot 0), slots 1 and 2 have no registered filter, so the simulation helper routes the call through slot 0's filter. The Chapter 19 `myfirst_intr_filter` handles all three bits inline, so the global `intr_count`, `intr_data_av_count`, `intr_error_count`, and `intr_complete_count` counters still move correctly. The per-vector counters on slots 1 and 2 stay at zero in single-handler tiers, which is the right observability signal that the driver is not running with three vectors.

A reader can watch the pipeline from user space:

```sh
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate_rx=1
    sleep 0.1
done &
watch sysctl dev.myfirst.0 | grep -E "vec|intr_"
```

The counters tick at roughly 10 per second. The RX vector's counter matches `intr_data_av_count`; the task's invocation count matches.

### Dynamic Vector Assignment

A subtle but important point. The driver's design has three vectors in a fixed array with fixed roles. A more flexible driver might discover the number of available vectors at runtime and assign roles dynamically. The pattern looks like:

```c
/* Discover how many vectors we got. */
int nvec = actually_allocated_msix_vectors(sc);

/* Assign roles based on nvec. */
if (nvec >= 3) {
	/* Full design: admin, rx, tx. */
	sc->vectors[0].filter = myfirst_admin_filter;
	sc->vectors[1].filter = myfirst_rx_filter;
	sc->vectors[2].filter = myfirst_tx_filter;
	sc->num_vectors = 3;
} else if (nvec == 2) {
	/* Compact: admin+tx share one vector, rx has its own. */
	sc->vectors[0].filter = myfirst_admin_tx_filter;
	sc->vectors[1].filter = myfirst_rx_filter;
	sc->num_vectors = 2;
} else if (nvec == 1) {
	/* Minimal: one filter handles everything. */
	sc->vectors[0].filter = myfirst_intr_filter;
	sc->num_vectors = 1;
}
```

This dynamic adaptation is what production drivers do. Chapter 20's driver uses the simpler fixed approach; a challenge exercise adds the dynamic variant.

### A Pattern From nvme(4)

For a real example, the `nvme(4)` driver handles the admin queue separately from the I/O queues. Its filter functions differ per queue type; its interrupt counts are tracked per queue. The pattern is:

```c
/* In nvme_ctrlr_construct_admin_qpair: */
qpair->intr_idx = 0;  /* vector 0 for admin */
qpair->intr_rid = 1;
qpair->res = bus_alloc_resource_any(ctrlr->dev, SYS_RES_IRQ,
    &qpair->intr_rid, RF_ACTIVE);
bus_setup_intr(ctrlr->dev, qpair->res, INTR_TYPE_MISC | INTR_MPSAFE,
    NULL, nvme_qpair_msix_handler, qpair, &qpair->tag);

/* For each I/O queue: */
for (i = 0; i < ctrlr->num_io_queues; i++) {
	ctrlr->ioq[i].intr_rid = i + 2;  /* I/O vectors at rid 2, 3, ... */
	/* ... similar bus_alloc_resource_any + bus_setup_intr ... */
}
```

Each queue has its own `intr_rid`, its own resource, its own tag (cookie), its own handler argument. The admin queue uses one vector; each I/O queue uses its own vector. The pattern scales linearly with the number of queues.

Chapter 20's driver is a small version of this: three fixed vectors instead of one admin plus N I/O. The scaling story transfers directly.

### Observability: Per-Vector Rate

A useful diagnostic: compute each vector's rate over a sliding window:

```sh
#!/bin/sh
prev_admin=$(sysctl -n dev.myfirst.0.vec0_fire_count)
prev_rx=$(sysctl -n dev.myfirst.0.vec1_fire_count)
prev_tx=$(sysctl -n dev.myfirst.0.vec2_fire_count)
sleep 1
curr_admin=$(sysctl -n dev.myfirst.0.vec0_fire_count)
curr_rx=$(sysctl -n dev.myfirst.0.vec1_fire_count)
curr_tx=$(sysctl -n dev.myfirst.0.vec2_fire_count)

echo "admin: $((curr_admin - prev_admin)) /s"
echo "rx:    $((curr_rx    - prev_rx   )) /s"
echo "tx:    $((curr_tx    - prev_tx   )) /s"
```

The output is per-vector rates over the last second. A reader running the simulated-interrupt sysctl in a loop can see the rates tick up; a reader observing a real workload sees which vector is busy.

### Wrapping Up Section 6

Handling vector-specific events means each vector has its own filter function, its own counters, its own (optional) task, and its own observable behaviour. The pattern scales: three vectors for the Chapter 20 demo, dozens for a production NIC, hundreds for an NVMe controller. The per-vector separation makes each piece small, specific, and maintainable.

Section 7 is the teardown section. Multi-vector drivers need to tear down each vector individually, in the right order, and then call `pci_release_msi` once at the end. The order is strict but not complex; Section 7 walks through it.



## Section 7: Teardown and Cleanup With MSI/MSI-X

Chapter 19's teardown was a single pair: `bus_teardown_intr` on the one vector, then `bus_release_resource` on the one IRQ resource. Chapter 20's teardown is the same pair repeated per vector, followed by a single `pci_release_msi` call that undoes the MSI or MSI-X device-level allocation.

Section 7 makes the ordering precise, walks through the partial-failure cases, and highlights the observability checks that confirm a clean teardown.

### The Required Order

For a multi-vector driver, the detach sequence is:

1. **Refuse if busy.** Same as Chapter 19: return `EBUSY` if the driver has open descriptors or in-flight work.
2. **Mark as no longer attached.**
3. **Destroy the cdev.**
4. **Disable interrupts at the device.** Clear `INTR_MASK` so the device stops asserting.
5. **For each vector in reverse order:**
   a. `bus_teardown_intr` on the vector's cookie.
   b. `bus_release_resource` on the vector's IRQ resource.
6. **Drain all per-vector tasks.** Each task that was initialised.
7. **Destroy the taskqueue.**
8. **Call `pci_release_msi`** once, unconditionally if `intr_mode` is MSI or MSI-X.
9. **Detach the hardware layer and release the BAR** as usual.
10. **Deinit the softc.**

The new steps are 5 (per-vector loop instead of a single pair) and 8 (`pci_release_msi`). Steps 1-4 and 9-10 are unchanged from Chapter 19.

### Why Reverse Order Per Vector

The per-vector reverse-order loop is a defensive measure against dependencies between vectors. On a simple driver like Chapter 20's, the vectors are independent: tearing down vector 2 before vector 1 is fine. On a driver where vector 2's filter reads state that vector 1's filter writes, the order matters: tear down the writer (vector 1) first, then the reader (vector 2).

For correctness on Chapter 20's driver, forward and reverse order are both safe. For robustness against future changes, reverse order is preferred.

### The Per-Vector Teardown Code

From Section 3, the per-vector teardown helper:

```c
static void
myfirst_intr_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, vec->irq_rid,
		    vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

The helper is robust against partial setup: if the vector never had a cookie (setup failed before `bus_setup_intr`), the `if` check skips the teardown call. If the resource was never allocated, the second `if` check skips the release. The same helper works for partial-failure unwind during setup and for full teardown during detach.

### The Full Teardown

```c
void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down each vector's handler, in reverse. */
	for (i = sc->num_vectors - 1; i >= 0; i--)
		myfirst_intr_teardown_vector(sc, i);

	/* Drain and destroy the taskqueue, including per-vector tasks. */
	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Release the MSI/MSI-X allocation if used. */
	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

The code structure is straightforward: disable at the device, per-vector teardown in reverse, per-vector task drain, free the taskqueue, release MSI if used. The pattern transfers directly to any multi-vector driver.

### Partial-Attach Failure Unwind

During setup, if vector N fails to register, the code must unwind vectors 0 through N-1 that succeeded. The pattern:

```c
for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
	error = myfirst_intr_setup_vector(sc, i, i + 1);
	if (error != 0)
		goto fail_vectors;
}

/* Success, continue. */

fail_vectors:
	/* Undo vectors 0 through i-1. */
	for (i -= 1; i >= 0; i--)
		myfirst_intr_teardown_vector(sc, i);
	pci_release_msi(sc->dev);
	/* Fall through to next tier or final failure. */
```

The `i -= 1` is important: after the `goto`, `i` is the vector that failed (it is past the successful setups). We undo vectors 0 through i-1, which is the successfully-registered set. The per-vector teardown helper is safe to call on the failing vector's slot too, because its fields are NULL (the setup did not get far enough to populate them).

### Observability: Verifying a Clean Teardown

After `kldunload myfirst`, the following should hold:

- `kldstat -v | grep myfirst` returns nothing.
- `devinfo -v | grep myfirst` returns nothing.
- `vmstat -i | grep myfirst` returns nothing.
- `vmstat -m | grep myfirst` shows zero live allocations.

Any failure points to a cleanup bug:

- A `vmstat -i` entry remaining means `bus_teardown_intr` was not called for that vector.
- A `vmstat -m` leak means a per-vector task was not drained or the taskqueue was not freed.
- A `devinfo -v` entry remaining (rare) means the device's detach did not complete.

### MSI Resource Leak Across Load-Unload Cycles

A specific concern for MSI/MSI-X drivers: forgetting `pci_release_msi` leaves the device's MSI state allocated. The next `kldload` of the same driver (or a different driver for the same device) will fail to allocate MSI vectors because the kernel thinks they are already in use.

The symptom in `dmesg`:

```text
myfirst0: pci_alloc_msix returned EBUSY
```

or similar. The fix is to ensure `pci_release_msi` runs on every teardown path, including the partial-failure unwind.

A useful test: load, unload, load. If the second load succeeds with the same MSI mode, the teardown is correct. If the second load falls back to a lower tier, the teardown leaked.

### Common Mistakes in Teardown

A short list.

**Forgetting `pci_release_msi`.** The most common bug. Symptom: next MSI allocation attempts fail. Fix: always call it when MSI or MSI-X was used.

**Calling `pci_release_msi` on a driver that used legacy INTx only.** Technically a no-op, but explicit check makes the intent clearer. Fix: check `intr_mode` before calling.

**Wrong per-vector teardown order.** For drivers with inter-vector dependencies, the reverse-order loop matters. For Chapter 20's driver, the order is not dependency-critical, but the reverse-order discipline is cheap and worth keeping.

**Draining a task that was never initialised.** If a vector does not have `has_task`, draining its uninitialised `task` field produces garbage. Fix: check `has_task` before draining.

**Leaking the taskqueue.** `taskqueue_drain` does not free the taskqueue; `taskqueue_free` does. Both are needed. Fix: call both.

**Partial-setup unwind that undoes too much.** If vector 2 fails and the unwind code also tears down vector 2 (which was never set up), NULL dereferences follow. The per-vector helper's NULL checks protect against this, but the cascade logic should also be careful. Fix: use `i -= 1` to start the unwind at the correct vector.

### Wrapping Up Section 7

Teardown for a multi-vector driver is per-vector in a loop, followed by a single `pci_release_msi` at the end. The per-vector helper is shared between full-teardown and partial-failure-unwind. The observability checks after unload are the same ones Chapter 19 used; any leak points to a specific bug.

Section 8 is the refactor section: split the multi-vector code into `myfirst_msix.c`, update `INTERRUPTS.md` to reflect the new capabilities, bump the version to `1.3-msi`, and run the regression pass. The driver is functionally complete after Section 7; Section 8 makes it maintainable.



## Section 8: Refactoring and Versioning Your Multi-Vector Driver

The multi-vector interrupt handler is working. Section 8 is the housekeeping section. It splits the MSI/MSI-X code into its own file, updates the module metadata, extends the `INTERRUPTS.md` document with the new multi-vector details, bumps the version to `1.3-msi`, and runs the regression pass.

This is the fourth chapter running in which the chapter closes with a refactor section. The refactors accumulate: Chapter 16 split out the hardware layer, Chapter 17 the simulation, Chapter 18 the PCI attach, Chapter 19 the legacy interrupt. Chapter 20 adds the MSI/MSI-X layer. Each responsibility has its own file; the main `myfirst.c` stays roughly constant in size; the driver scales.

### The Final File Layout

At the end of Chapter 20:

```text
myfirst.c           - Main driver
myfirst.h           - Shared declarations
myfirst_hw.c        - Ch16 hardware access layer
myfirst_hw_pci.c    - Ch18 hardware-layer extension
myfirst_hw.h        - Register map
myfirst_sim.c       - Ch17 simulation backend
myfirst_sim.h       - Simulation interface
myfirst_pci.c       - Ch18 PCI attach
myfirst_pci.h       - PCI declarations
myfirst_intr.c      - Ch19 interrupt handler (legacy + filter+task)
myfirst_intr.h      - Ch19 interrupt interface + ICSR macros
myfirst_msix.c      - Ch20 MSI/MSI-X multi-vector layer (NEW)
myfirst_msix.h      - Ch20 multi-vector interface (NEW)
myfirst_sync.h      - Part 3 synchronisation
cbuf.c / cbuf.h     - Ch10 circular buffer
Makefile            - kmod build
HARDWARE.md, LOCKING.md, SIMULATION.md, PCI.md, INTERRUPTS.md, MSIX.md (NEW)
```

`myfirst_msix.c` and `myfirst_msix.h` are new. `MSIX.md` is new. The Chapter 19 `myfirst_intr.c` stays; it now handles the legacy-INTx fallback while `myfirst_msix.c` handles the MSI and MSI-X path.

### The myfirst_msix.h Header

```c
#ifndef _MYFIRST_MSIX_H_
#define _MYFIRST_MSIX_H_

#include <sys/taskqueue.h>

struct myfirst_softc;

enum myfirst_intr_mode {
	MYFIRST_INTR_LEGACY = 0,
	MYFIRST_INTR_MSI = 1,
	MYFIRST_INTR_MSIX = 2,
};

enum myfirst_vector_id {
	MYFIRST_VECTOR_ADMIN = 0,
	MYFIRST_VECTOR_RX,
	MYFIRST_VECTOR_TX,
	MYFIRST_MAX_VECTORS
};

struct myfirst_vector {
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	enum myfirst_vector_id	 id;
	struct myfirst_softc	*sc;
	uint64_t		 fire_count;
	uint64_t		 stray_count;
	const char		*name;
	driver_filter_t		*filter;
	struct task		 task;
	bool			 has_task;
};

int  myfirst_msix_setup(struct myfirst_softc *sc);
void myfirst_msix_teardown(struct myfirst_softc *sc);
void myfirst_msix_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_MSIX_H_ */
```

The public API is three functions: setup, teardown, add_sysctls. The enum types and the per-vector struct are exported so `myfirst.h` can include them and the softc can have the per-vector array.

### The Full Makefile

```makefile
# Makefile for the Chapter 20 myfirst driver.

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

One additional source file in the SRCS list; the version string bumped.

### The Version String

`1.2-intr` to `1.3-msi`. The bump reflects a significant capability addition: multi-vector interrupt handling. A minor-version bump is appropriate; the user-visible interface (the cdev) did not change.

### The MSIX.md Document

A new document lives next to the source:

```markdown
# MSI and MSI-X Support in the myfirst Driver

## Summary

The driver probes the device's MSI-X, MSI, and legacy INTx capabilities
in that order, and uses the first one that allocates successfully. The
driver's interrupt counters, data path, and cdev behaviour are
independent of which tier the driver ends up using.

## Setup Sequence

`myfirst_msix_setup()` tries three tiers:

1. MSI-X with MYFIRST_MAX_VECTORS (3) vectors. On success:
   - Allocates per-vector IRQ resources at rid=1, 2, 3.
   - Registers a distinct filter function per vector.
   - Calls bus_describe_intr with the per-vector name.
   - Binds each vector to a CPU (round-robin or NUMA-aware).
2. MSI with MYFIRST_MAX_VECTORS vectors. Same per-vector pattern.
3. Legacy INTx with a single handler that covers all three event
   classes at rid=0.

## Per-Vector Assignment

| Vector | Purpose | Handles                         | Inline/Deferred |
|--------|---------|---------------------------------|-----------------|
| 0      | admin   | INTR_STATUS.ERROR                | Inline          |
| 1      | rx      | INTR_STATUS.DATA_AV              | Deferred (task) |
| 2      | tx      | INTR_STATUS.COMPLETE             | Inline          |

On MSI-X, each vector has its own intr_event, its own CPU affinity
(via bus_bind_intr), and its own bus_describe_intr label ("admin",
"rx", "tx"). On MSI, the driver obtains a single vector and falls
back to the Chapter 19 single-handler pattern because MSI requires
a power-of-two vector count (pci_alloc_msi rejects count=3 with
EINVAL). On legacy INTx, a single filter covers all three bits.

## sysctls

- `dev.myfirst.N.intr_mode`: 0 (legacy), 1 (MSI), 2 (MSI-X).
- `dev.myfirst.N.vec{0,1,2}_fire_count`: per-vector fire counts.
- `dev.myfirst.N.vec{0,1,2}_stray_count`: per-vector stray counts.
- `dev.myfirst.N.intr_simulate_admin`, `.intr_simulate_rx`,
  `.intr_simulate_tx`: simulate per-vector interrupts.

## Teardown Sequence

1. Disable interrupts at the device (clear INTR_MASK).
2. Per-vector in reverse: bus_teardown_intr, bus_release_resource.
3. Drain and free per-vector tasks and the taskqueue.
4. If intr_mode is MSI or MSI-X, call pci_release_msi once.

## dmesg Summary Line

A single line on attach:

- "interrupt mode: MSI-X, 3 vectors"
- "interrupt mode: MSI, 1 vector (single-handler fallback)"
- "interrupt mode: legacy INTx (1 handler for all events)"

## Known Limitations

- MYFIRST_MAX_VECTORS is hardcoded at 3. A dynamic design that
  adapts to the allocated count is a Chapter 20 challenge exercise.
- CPU binding is round-robin. NUMA-aware binding via bus_get_cpus
  is a challenge exercise.
- DMA is Chapter 21.
- iflib integration is out of scope.

## See Also

- `INTERRUPTS.md` for the Chapter 19 legacy path details.
- `HARDWARE.md` for the register map.
- `LOCKING.md` for the full lock discipline.
- `PCI.md` for the PCI attach behaviour.
```

The document gives a future reader the full picture of the multi-vector design in one page.

### The Regression Pass

The Chapter 20 regression is a superset of Chapter 19's:

1. Compile cleanly. `make` produces `myfirst.ko` without warnings.
2. Load. `kldload` shows the attach banner including the `interrupt mode:` line.
3. Verify mode. `sysctl dev.myfirst.0.intr_mode` returns 0, 1, or 2 (depending on guest).
4. Per-vector attach. `vmstat -i | grep myfirst` shows N lines (1 for legacy, 3 for MSI or MSI-X).
5. Per-vector description. Each entry has the correct name (`admin`, `rx`, `tx`, or `legacy`).
6. Simulated interrupts. Each vector's counter ticks independently.
7. Task runs. RX vector's simulated interrupt drives `intr_task_invocations`.
8. Clean detach. `devctl detach myfirst0` tears down all vectors.
9. Load after unload. A second `kldload` uses the same tier (tests that `pci_release_msi` worked).
10. vmstat -m shows no leaks. After unload, no myfirst allocations remain.

The regression script runs all ten checks. On QEMU with virtio-rng-pci, the test exercises the MSI-X path; on bhyve with virtio-rnd, it exercises the legacy-INTx fallback. The driver's fallback ladder ensures it works on either.

### What the Refactor Accomplished

At the start of Chapter 20, `myfirst` at `1.2-intr` had one interrupt handler on the legacy line. At the end of Chapter 20, `myfirst` at `1.3-msi` has a three-tier fallback ladder (MSI-X → MSI → legacy), three per-vector filters on MSI or MSI-X, per-vector counters, per-vector CPU affinity, and a single clean teardown path. The driver's file count has grown by two; its documentation has grown by one; its functional capabilities have grown substantially.

The code is recognisably FreeBSD. A contributor opening the driver for the first time finds a familiar structure: a per-vector array, per-vector filter functions, a three-tier setup ladder, a bus_describe_intr for each vector, a single `pci_release_msi` at teardown. These patterns appear in every multi-queue FreeBSD driver.

### Wrapping Up Section 8

The refactor follows the established shape: a new file for the new layer, a new header exporting the public interface, a new document explaining the behaviour, a version bump, a regression pass. Chapter 20's layer is multi-vector interrupt handling; Chapter 19's remains the single-vector legacy fallback. Together they form the full interrupt story the driver needs.

The instructional body of Chapter 20 is complete. Labs, challenges, troubleshooting, a wrap-up, and the bridge to Chapter 21 follow.



## Reading a Real Driver Together: virtio_pci.c

Before the labs, a short walk through a real FreeBSD driver that uses MSI-X extensively. `/usr/src/sys/dev/virtio/pci/virtio_pci.c` is the shared core of both the legacy and modern virtio-PCI transports; it holds the interrupt-allocation ladder that every virtio device uses. Reading this file after Chapter 20 is a short exercise in pattern recognition; almost everything in the interrupt section maps to something Chapter 20 just taught.

### The Allocation Ladder

`virtio_pci.c` has a helper called `vtpci_alloc_intr_resources` (the exact name varies slightly by FreeBSD version). Its structure is:

```c
static int
vtpci_alloc_intr_resources(struct vtpci_common *cn)
{
	int error;

	/* Tier 0: MSI-X. */
	error = vtpci_alloc_msix(cn, nvectors);
	if (error == 0) {
		cn->vtpci_flags |= VTPCI_FLAG_MSIX;
		return (0);
	}

	/* Tier 1: MSI. */
	error = vtpci_alloc_msi(cn);
	if (error == 0) {
		cn->vtpci_flags |= VTPCI_FLAG_MSI;
		return (0);
	}

	/* Tier 2: legacy INTx. */
	return (vtpci_alloc_intx(cn));
}
```

The three tiers are exactly the Chapter 20 ladder. Each tier, on success, sets a flag on the common state and returns 0. On failure, the next tier is tried.

### The MSI-X Allocation Helper

`vtpci_alloc_msix` queries the count, decides how many vectors to request (based on the number of virtqueues the device uses), and calls `pci_alloc_msix`:

```c
static int
vtpci_alloc_msix(struct vtpci_common *cn, int nvectors)
{
	int error, count;

	if (pci_msix_count(cn->vtpci_dev) < nvectors)
		return (ENOSPC);

	count = nvectors;
	error = pci_alloc_msix(cn->vtpci_dev, &count);
	if (error != 0)
		return (error);
	if (count != nvectors) {
		pci_release_msi(cn->vtpci_dev);
		return (ENXIO);
	}
	return (0);
}
```

The pattern: check count, allocate, verify the allocation matched the request, release on mismatch. If the device advertises fewer vectors than wanted, `ENOSPC` is returned immediately. If `pci_alloc_msix` allocates a smaller count than requested, the code releases and returns `ENXIO`.

Chapter 20's code follows this exact logic (Section 5 showed the full version).

### The Per-Vector Resource Allocation

Once MSI-X is allocated, virtio walks the vectors and registers a handler per vector:

```c
static int
vtpci_register_msix_vectors(struct vtpci_common *cn)
{
	int i, rid, error;

	rid = 1;  /* MSI-X vectors start at rid 1 */
	for (i = 0; i < cn->vtpci_num_vectors; i++) {
		cn->vtpci_vectors[i].res = bus_alloc_resource_any(
		    cn->vtpci_dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
		if (cn->vtpci_vectors[i].res == NULL)
			/* ... fail ... */;
		rid++;
		error = bus_setup_intr(cn->vtpci_dev,
		    cn->vtpci_vectors[i].res,
		    INTR_TYPE_MISC | INTR_MPSAFE,
		    NULL, vtpci_vq_handler,
		    &cn->vtpci_vectors[i], &cn->vtpci_vectors[i].cookie);
		if (error != 0)
			/* ... fail ... */;
	}
	return (0);
}
```

Two things match Chapter 20:

- `rid = 1` for the first vector, incrementing per vector.
- The filter (here `NULL`) and handler (`vtpci_vq_handler`) pattern. Note that virtio uses an ithread-only handler (filter=NULL), not a filter-plus-task pipeline. This is a simpler option that works for virtio's per-vector work.

The `vtpci_vq_handler` function is the per-vector worker. Each vector gets its own argument (`&cn->vtpci_vectors[i]`), and the handler uses that argument to identify which virtqueue to service.

### The Teardown

Virtio's teardown follows the Chapter 20 pattern:

```c
static void
vtpci_release_intr_resources(struct vtpci_common *cn)
{
	int i;

	for (i = 0; i < cn->vtpci_num_vectors; i++) {
		if (cn->vtpci_vectors[i].cookie != NULL) {
			bus_teardown_intr(cn->vtpci_dev,
			    cn->vtpci_vectors[i].res,
			    cn->vtpci_vectors[i].cookie);
		}
		if (cn->vtpci_vectors[i].res != NULL) {
			bus_release_resource(cn->vtpci_dev, SYS_RES_IRQ,
			    rman_get_rid(cn->vtpci_vectors[i].res),
			    cn->vtpci_vectors[i].res);
		}
	}

	if (cn->vtpci_flags & (VTPCI_FLAG_MSI | VTPCI_FLAG_MSIX))
		pci_release_msi(cn->vtpci_dev);
}
```

Per-vector teardown (bus_teardown_intr + bus_release_resource), then a single `pci_release_msi` at the end. The order matches Chapter 20's `myfirst_msix_teardown`.

A detail worth noting: virtio uses `rman_get_rid` to recover the rid from the resource, rather than storing it separately. Chapter 20's driver stores the rid in the per-vector struct; both approaches are fine, but the storage approach is clearer and easier to debug.

### What the Virtio Walkthrough Teaches

Three lessons transfer directly to Chapter 20's design:

1. **The three-tier fallback ladder is the standard pattern**. Every driver that wants to work on a range of hardware implements it the same way.
2. **Per-vector resource management uses incrementing rids starting at 1**. This is universal in FreeBSD's PCI infrastructure.
3. **`pci_release_msi` is called once, regardless of the number of vectors**. The per-vector teardown releases IRQ resources; the device-level release handles the MSI state.

A reader who can follow `vtpci_alloc_intr_resources` end to end has internalised the Chapter 20 vocabulary. For a richer example, `/usr/src/sys/dev/nvme/nvme_ctrlr.c` shows the same pattern at scale, with one admin vector plus up to `NCPU` I/O vectors.



## A Deeper Look at Vector-to-CPU Placement

Section 5 introduced `bus_bind_intr(9)` briefly. This section goes a level deeper into why CPU placement matters, how real drivers choose CPUs, and what the trade-offs are.

### The NUMA Picture

On a single-socket system, all CPUs share a single memory controller and a single cache hierarchy. Placement between CPUs matters only for cache affinity (the handler's code and data will be warm on whichever CPU last ran it). The performance difference between "CPU 0" and "CPU 3" is small.

On a multi-socket NUMA system, the picture changes. Each socket has its own memory controller, its own L3 cache, and its own PCIe root complex. A PCIe device attached to socket 0 sits on that socket's root complex; its registers are memory-mapped to an address range handled by socket 0's controller. An interrupt from that device fires; the handler reads `INTR_STATUS`; the read goes to the device's BAR, which is on socket 0; the CPU that runs the handler must be on socket 0, or the read crosses the inter-socket interconnect.

The inter-socket interconnect (on Intel systems: UPI or earlier QPI; on AMD: Infinity Fabric) is much slower than intra-socket cache access. A handler running on the wrong socket sees register reads that take tens of nanoseconds instead of ones; a receive queue whose data lives on the wrong socket sees every packet crossing the interconnect on the way to user space.

Well-placed vectors keep the handler's work on the socket the device lives on.

### Querying NUMA Locality

FreeBSD exposes the NUMA topology to drivers through `bus_get_cpus(9)`. The API:

```c
int bus_get_cpus(device_t dev, enum cpu_sets op, size_t setsize,
    struct _cpuset *cpuset);
```

The `op` argument selects which set to query:

- `LOCAL_CPUS`: CPUs in the same NUMA domain as the device.
- `INTR_CPUS`: CPUs suitable for handling device interrupts (usually `LOCAL_CPUS` unless the operator has excluded some).

The `cpuset` is an output parameter; on success, it contains the bitmap of CPUs in the queried set.

Example use:

```c
cpuset_t local_cpus;
int num_local;

if (bus_get_cpus(sc->dev, INTR_CPUS, sizeof(local_cpus),
    &local_cpus) == 0) {
	num_local = CPU_COUNT(&local_cpus);
	device_printf(sc->dev, "device has %d interrupt-suitable CPUs\n",
	    num_local);
}
```

The driver uses `CPU_FFS(&local_cpus)` to find the first CPU in the set, `CPU_CLR(cpu, &local_cpus)` to mark it used, and iterates.

A round-robin bind that respects NUMA locality:

```c
static void
myfirst_msix_bind_vectors_numa(struct myfirst_softc *sc)
{
	cpuset_t local_cpus;
	int cpu, i;

	if (bus_get_cpus(sc->dev, INTR_CPUS, sizeof(local_cpus),
	    &local_cpus) != 0) {
		/* No NUMA info; round-robin across all CPUs. */
		myfirst_msix_bind_vectors_roundrobin(sc);
		return;
	}

	if (CPU_EMPTY(&local_cpus))
		return;

	for (i = 0; i < sc->num_vectors; i++) {
		if (CPU_EMPTY(&local_cpus))
			bus_get_cpus(sc->dev, INTR_CPUS,
			    sizeof(local_cpus), &local_cpus);
		cpu = CPU_FFS(&local_cpus) - 1;  /* FFS returns 1-based */
		CPU_CLR(cpu, &local_cpus);
		(void)bus_bind_intr(sc->dev,
		    sc->vectors[i].irq_res, cpu);
	}
}
```

The code grabs the local CPU set, picks the lowest-numbered CPU, binds vector 0 to it, clears that CPU from the set, picks the next lowest, binds vector 1 to it, and so on. If the set is exhausted (more vectors than local CPUs), it refreshes and continues.

Chapter 20's driver does not include this NUMA-aware binding; a challenge exercise asks the reader to add it.

### The Operator View

An operator can override the kernel's placement with `cpuset`:

```sh
# Get current placement for IRQ 257.
sudo cpuset -g -x 257

# Bind IRQ 257 to CPU 3.
sudo cpuset -l 3 -x 257

# Bind to a set of CPUs (kernel picks one when the interrupt fires).
sudo cpuset -l 2,3 -x 257
```

These commands override whatever the driver set with `bus_bind_intr`. An operator might do this to pin critical interrupts away from user-workload CPUs (for real-time applications) or to concentrate traffic on specific CPUs (for diagnostic purposes).

The driver's `bus_bind_intr` call sets the initial placement; the operator can override. A well-behaved driver sets a sensible default and respects operator changes (which it does automatically, because `bus_bind_intr` just writes to an OS-managed CPU-affinity state that the operator then modifies).

### Measuring the Effect

A concrete way to see NUMA locality's value: run a high-interrupt-rate workload with the handler pinned to a local CPU, then to a remote CPU, and compare latencies. On a two-socket system, the remote CPU's handler typically takes 1.5x to 3x longer per interrupt, measured in CPU cycles.

FreeBSD's DTrace provider can measure this:

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_intr_filter:return /self->ts/ {
    @[cpu] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

The output is a per-CPU histogram of filter latencies. A reader can run this while observing vector placements and confirm the latency difference.

### When Vector Placement Matters

- High interrupt rates (more than a few thousand per second per vector).
- Large cache-line footprint in the handler (the handler's code and data occupy multiple cache lines).
- Shared receive paths with downstream processing on the same socket.
- NUMA systems with more than one socket and PCIe devices attached to specific sockets.

### When Vector Placement Doesn't Matter

- Low-rate interrupts (dozens per second or fewer).
- Single-socket systems.
- Handlers that do minimal work (Chapter 20's admin vector).
- Drivers that run on a single CPU regardless (single-CPU embedded systems).

Chapter 20's driver is in the "does not really matter" category for normal testing, but the patterns the chapter teaches transfer directly to drivers where it does.



## A Deeper Look at Vector Assignment Strategies

Section 6 showed the fixed-assignment pattern (vector 0 = admin, 1 = rx, 2 = tx). This section explores other assignment strategies real drivers use.

### One-Vector-Per-Queue

The simplest strategy and the most common. Each queue (rx queue, tx queue, admin queue, etc.) has its own dedicated vector. The driver allocates `N+M+1` vectors for `N` receive queues, `M` transmit queues, and 1 admin.

Pros:
- Simple per-vector handler logic.
- Each queue's interrupt rate is independent.
- CPU affinity is per-queue (easy to pin to the NUMA-local CPU).

Cons:
- Consumes many vectors for drivers with many queues.
- Each queue's ithread adds overhead on low-rate queues.

This is the pattern `nvme(4)` uses.

### Coalesced RX+TX Vector

Some drivers coalesce the RX and TX of a single queue-pair into a single vector. A NIC with 8 queue pairs would use 8 coalesced vectors plus a few for admin. When the vector fires, the filter checks both RX and TX status bits and dispatches accordingly.

Pros:
- Half the vectors per queue-pair.
- RX and TX for the same queue pair tend to be NUMA-local to each other (they share the same descriptor-ring memory).

Cons:
- The filter is slightly more complex.
- RX and TX can interfere under load (a burst of RX fills the handler's time, delaying TX completions).

This is a middle-ground design, used by some consumer NICs.

### One Vector For All Queues

Some very-constrained devices (low-cost NICs, small embedded devices) have only one or two MSI-X vectors total. The driver uses a single vector for all queues and dispatches to each queue based on a status register.

Pros:
- Works on hardware with few vectors.
- Simple allocation.

Cons:
- No per-queue affinity.
- The filter does more work to decide what to dispatch.

This is the pattern a driver on very low-end hardware uses.

### Dynamic Per-CPU Assignment

A clever design: allocate one vector per CPU, and assign queues to vectors dynamically. An RX queue is "owned" by one CPU at a time; it processes on that CPU's vector. If the workload shifts, the driver can remap queues to different CPUs.

Pros:
- Optimal per-CPU cache affinity.
- Adapts to workload changes.

Cons:
- Complex allocation and remapping logic.
- Not easy to reason about.

Some high-end NIC drivers (Mellanox ConnectX series, Intel 800 Series) use variants of this.

### Chapter 20's Strategy

Chapter 20's driver uses the fixed-assignment strategy with three vectors. It is the simplest strategy that illustrates multi-vector design without getting into NUMA details or dynamic remapping. Real drivers often start with this design and evolve to more sophisticated patterns as requirements demand.

A challenge exercise asks the reader to implement the dynamic per-CPU-allocation strategy as an extension.



## A Deeper Look at Interrupt Moderation and Coalescing

A concept adjacent to MSI-X that deserves a brief mention. Modern high-throughput devices often support **interrupt moderation** or **coalescing**: the device buffers events (incoming packets, completions) and fires a single interrupt for multiple events, either at a time threshold or a count threshold.

### Why Moderation Matters

A NIC receiving ten million packets per second would fire ten million interrupts if each packet triggered one. That is far too many; the CPU would spend all its time entering and exiting interrupt handlers. The solution is to batch: the NIC fires one interrupt every 50 microseconds, and during those 50 microseconds the NIC accumulates whatever packets arrived. The handler processes all the accumulated packets in one go.

Coalescing trades latency for throughput: each packet takes up to 50 microseconds longer to be delivered to user space, but the CPU handles millions of packets per second with a manageable interrupt rate.

### How Drivers Control Moderation

The mechanism is device-specific. Common forms:

- **Time-based:** the device fires after a configured interval (e.g., 50 microseconds).
- **Count-based:** the device fires after N events (e.g., 16 packets).
- **Combined:** whichever threshold is reached first.
- **Adaptive:** the device (or the driver) tunes the thresholds based on observed rates.

The driver typically programs the thresholds through device registers. The MSI-X mechanism itself does not provide moderation; it is a device feature that works with MSI-X because MSI-X allows per-vector assignment.

### Chapter 20's Driver Does Not Moderate

The Chapter 20 driver has no moderation. Each simulated interrupt produces one filter call. On real hardware this would be a problem at high rates; on the lab it is fine.

Real drivers like `em(4)`, `ix(4)`, `ixl(4)`, and `mgb(4)` all have moderation parameters. The `sysctl` interface exposes them as tunable values:

```sh
sysctl dev.em.0 | grep itr
```

A reader who adapts the chapter's driver to a real device should study the moderation controls for that device. The mechanism is orthogonal to MSI-X; the two combine to give high-performance interrupt handling.



## Patterns From Real FreeBSD Drivers

A tour of the multi-vector patterns that appear in `/usr/src/sys/dev/`. Each pattern is a short snippet from a real driver, with a note on what it teaches for Chapter 20.

### Pattern: nvme(4) Admin + I/O Vector Split

`/usr/src/sys/dev/nvme/nvme_ctrlr.c` has the canonical admin-plus-N pattern:

```c
/* Allocate one vector for admin + N for I/O. */
num_trackers = MAX(1, MIN(mp_ncpus, ctrlr->max_io_queues));
num_vectors_requested = num_trackers + 1;  /* +1 for admin */
num_vectors_allocated = num_vectors_requested;
pci_alloc_msix(ctrlr->dev, &num_vectors_allocated);

/* Admin queue uses vector 0 (rid 1). */
ctrlr->adminq.intr_rid = 1;
ctrlr->adminq.res = bus_alloc_resource_any(ctrlr->dev, SYS_RES_IRQ,
    &ctrlr->adminq.intr_rid, RF_ACTIVE);
bus_setup_intr(ctrlr->dev, ctrlr->adminq.res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    NULL, nvme_qpair_msix_handler, &ctrlr->adminq, &ctrlr->adminq.tag);

/* I/O queues use vectors 1..N (rid 2..N+1). */
for (i = 0; i < ctrlr->num_io_queues; i++) {
	ctrlr->ioq[i].intr_rid = i + 2;
	/* same pattern ... */
}
```

Why it matters: the admin-plus-N pattern is the right choice when one vector handles infrequent, high-priority work (errors, async events) and N vectors handle rate-limited, per-queue work. Chapter 20's admin/rx/tx split is a miniature version of this.

### Pattern: ixgbe's Queue-Pair Vector

`/usr/src/sys/dev/ixgbe/ix_txrx.c` uses a queue-pair design where each vector handles both the RX and TX of one queue pair:

```c
/* One vector per queue pair + 1 for link. */
for (i = 0; i < num_qpairs; i++) {
	que[i].rid = i + 1;
	/* Filter checks both RX and TX status bits and dispatches. */
	bus_setup_intr(..., ixgbe_msix_que, &que[i], ...);
}
/* Link-state vector is the last one. */
link.rid = num_qpairs + 1;
bus_setup_intr(..., ixgbe_msix_link, sc, ...);
```

Why it matters: the coalesced RX+TX-per-queue-pair design halves the vector count without sacrificing per-queue affinity. Suitable when the device has many queues but few vectors.

### Pattern: virtio_pci's Per-Virtqueue Vector

`/usr/src/sys/dev/virtio/pci/virtio_pci.c` has one vector per virtqueue:

```c
int nvectors = ... /* count of virtqueues + 1 for config */;
pci_alloc_msix(dev, &nvectors);
for (i = 0; i < nvectors; i++) {
	vec[i].rid = i + 1;
	/* Each vector gets the per-virtqueue data as its arg. */
	bus_setup_intr(dev, vec[i].res, ..., virtio_vq_intr, &vec[i], ...);
}
```

Why it matters: virtio's per-virtqueue assignment is the model for any paravirtualised device. The vector count equals the virtqueue count plus admin/config.

### Pattern: ahci's Per-Port Vector

`/usr/src/sys/dev/ahci/ahci_pci.c` uses one vector per SATA port:

```c
for (i = 0; i < ahci->nports; i++) {
	ahci->ports[i].rid = i + 1;
	/* ... */
}
```

Why it matters: storage controllers often use per-port vector assignments so that I/O completions on different ports can be processed concurrently on different CPUs.

### Pattern: iflib's Hidden Vector Management

Drivers using `iflib(9)` (such as `em(4)`, `igc(4)`, `ix(4)`, `ixl(4)`, `mgb(4)`) do not manage vectors directly. Instead, they register per-queue handler functions with iflib's registration table, and iflib does the allocation and binding:

```c
static struct if_shared_ctx em_sctx_init = {
	/* ... */
	.isc_driver = &em_if_driver,
	.isc_tx_maxsize = EM_TSO_SIZE,
	/* ... */
};

static int
em_if_msix_intr_assign(if_ctx_t ctx, int msix)
{
	struct e1000_softc *sc = iflib_get_softc(ctx);
	int error, rid, i, vector = 0;

	/* iflib has already called pci_alloc_msix; sc knows the count. */
	for (i = 0; i < sc->rx_num_queues; i++, vector++) {
		rid = vector + 1;
		error = iflib_irq_alloc_generic(ctx, ..., rid, IFLIB_INTR_RXTX,
		    em_msix_que, ...);
	}
	return (0);
}
```

Why it matters: iflib abstracts MSI-X allocation and per-queue binding behind a clean API. Drivers using iflib are simpler than bare MSI-X drivers but give up some flexibility. The iflib pattern is the right choice for new FreeBSD network drivers; the bare MSI-X pattern is the right choice for non-network devices or when iflib does not fit.

### What the Patterns Teach

All of these drivers follow the same structural pattern Chapter 20 teaches:

1. Query vector count.
2. Allocate vectors.
3. For each vector: allocate IRQ resource at rid=i+1, register handler, describe.
4. Bind vectors to CPUs.
5. On teardown: per-vector teardown in reverse, then `pci_release_msi`.

The differences among drivers are:

- How many vectors (1, a handful, dozens, or hundreds).
- How vectors are assigned (admin+N, queue-pair, per-port, per-virtqueue).
- Whether iflib handles the allocation.
- What each filter function does (admin vs data-path).

A reader who has Chapter 20's vocabulary can recognise these differences immediately.



## A Performance Observation: Measuring the MSI-X Benefit

A section that grounds the chapter's performance claims in a concrete measurement.

### The Test Setup

Suppose you have the Chapter 20 driver running on QEMU with `virtio-rng-pci` (so MSI-X is active) and a multi-CPU guest. The `intr_simulate_rx` sysctl lets you trigger interrupts from a user-space loop:

```sh
# In one shell, drive simulated RX interrupts as fast as possible.
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate_rx=1 >/dev/null 2>&1
done
```

### Measuring With DTrace

In another shell, measure the filter's CPU-time per invocation and which CPU it runs on:

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; self->c = cpu; }
fbt::myfirst_rx_filter:return /self->ts/ {
    @lat[self->c] = quantize(vtimestamp - self->ts);
    self->ts = 0;
    self->c = 0;
}'
```

The output is a per-CPU histogram of filter latencies. If `bus_bind_intr` placed the RX vector on CPU 1, the histogram should show all invocations on CPU 1, with latencies in the hundreds of nanoseconds to single-digit microseconds.

### What the Results Show

On a well-placed MSI-X vector:

- Every invocation is on the same CPU (the bound CPU).
- Latencies are consistently short (the hot cache lines stay on one CPU).
- No cross-CPU cache bouncing.

On a legacy INTx shared line:

- Invocations spread across CPUs (the kernel routes randomly).
- Latencies are more variable (cold cache lines on each new CPU).
- Cross-CPU cache traffic appears in performance counters.

The difference can be measured in nanoseconds per invocation. For a driver handling a few hundred interrupts per second, the difference is invisible. For a driver handling a million interrupts per second, the difference is the difference between "works" and "does not work".

### The General Lesson

Chapter 20's machinery is overkill for low-rate drivers. It is essential for high-rate drivers. The patterns the chapter teaches scale from "demo driver doing a hundred interrupts per second" to "production NIC doing ten million". Knowing where on that scale a specific driver lives determines how much of Chapter 20's advice matters in practice.



## A Deeper Look at sysctl Tree Design for Multi-Vector Drivers

Chapter 20's driver exposes its per-vector counters as flat sysctls (`vec0_fire_count`, `vec1_fire_count`, `vec2_fire_count`). For a driver with many vectors, a flat namespace becomes unwieldy. This section shows how to use `SYSCTL_ADD_NODE` to build a per-vector sysctl tree.

### The Flat vs Tree Trade-off

Flat namespace (what Chapter 20 uses):

```text
dev.myfirst.0.vec0_fire_count: 42
dev.myfirst.0.vec1_fire_count: 9876
dev.myfirst.0.vec2_fire_count: 4523
dev.myfirst.0.vec0_stray_count: 0
dev.myfirst.0.vec1_stray_count: 0
dev.myfirst.0.vec2_stray_count: 0
```

Pros: simple, no `SYSCTL_ADD_NODE` calls.
Cons: many siblings at the top level; no grouping.

Tree namespace:

```text
dev.myfirst.0.vec.admin.fire_count: 42
dev.myfirst.0.vec.admin.stray_count: 0
dev.myfirst.0.vec.rx.fire_count: 9876
dev.myfirst.0.vec.rx.stray_count: 0
dev.myfirst.0.vec.tx.fire_count: 4523
dev.myfirst.0.vec.tx.stray_count: 0
```

Pros: groups per-vector state; scales to many vectors; named rather than numbered.
Cons: more code to set up.

### The Tree-Building Code

```c
void
myfirst_msix_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid *parent = sc->sysctl_tree;
	struct sysctl_oid *vec_node;
	struct sysctl_oid *per_vec_node;
	int i;

	/* Create the "vec" parent node. */
	vec_node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(parent),
	    OID_AUTO, "vec", CTLFLAG_RD, NULL,
	    "Per-vector interrupt statistics");

	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		/* Create "vec.<name>" node. */
		per_vec_node = SYSCTL_ADD_NODE(ctx,
		    SYSCTL_CHILDREN(vec_node),
		    OID_AUTO, sc->vectors[i].name,
		    CTLFLAG_RD, NULL,
		    "Per-vector statistics");

		/* Add fire_count under it. */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(per_vec_node),
		    OID_AUTO, "fire_count", CTLFLAG_RD,
		    &sc->vectors[i].fire_count, 0,
		    "Times this vector's filter was called");

		/* Add stray_count. */
		SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(per_vec_node),
		    OID_AUTO, "stray_count", CTLFLAG_RD,
		    &sc->vectors[i].stray_count, 0,
		    "Stray returns from this vector");

		/* Other per-vector fields... */
	}
}
```

The `SYSCTL_ADD_NODE` calls create the intermediate nodes; subsequent `SYSCTL_ADD_U64` calls attach leaf counters under them. The tree structure becomes visible in `sysctl` output automatically.

### Querying the Tree

```sh
# Show all per-vector stats.
sysctl dev.myfirst.0.vec

# Show just the rx vector.
sysctl dev.myfirst.0.vec.rx

# Show only fire counts.
sysctl -n dev.myfirst.0.vec.admin.fire_count dev.myfirst.0.vec.rx.fire_count dev.myfirst.0.vec.tx.fire_count
```

The tree structure makes the sysctl namespace much more readable, especially for drivers with many vectors (NVMe with 32 I/O queues, or a NIC with 16 queue pairs).

### When to Use the Tree

For Chapter 20's three-vector driver, the flat namespace is fine. For a driver with eight or more vectors, the tree becomes valuable. A reader writing a production driver should use the tree.

### Common Mistakes

- **Leaking the parent node.** `SYSCTL_ADD_NODE` registers the node in `sc->sysctl_ctx`; it is freed with the rest of the context. No explicit free needed.
- **Forgetting `NULL` for the handler argument.** `SYSCTL_ADD_NODE` is not a CTLPROC; it is a pure grouping node. The handler argument is `NULL`.
- **Wrong parent passed to child `SYSCTL_ADD_*` calls.** `SYSCTL_CHILDREN(vec_node)` for children of `vec_node`, not `SYSCTL_CHILDREN(parent)`.

This tree-design pattern is the cleanest way to expose multi-vector state. Chapter 20's challenge exercise suggests implementing it as an extension.



## A Deeper Look at Error Paths in MSI-X Setup

Section 3 and Section 5 showed the happy-path setup code. This section walks through what can go wrong and how to diagnose it.

### Failure Mode 1: pci_msix_count Returns 0

Symptom: the MSI-X attempt is skipped because the count is 0.

Cause: the device has no MSI-X capability, or the PCI bus driver has not discovered it.

Fix: Confirm with `pciconf -lvc`. If the device advertises MSI-X but `pci_msix_count` returns 0, the device's PCI configuration is broken or the kernel's probe did not find it; rare and hard to fix from the driver.

### Failure Mode 2: pci_alloc_msix Returns EINVAL

Symptom: allocation fails with `EINVAL`.

Cause: the driver is asking for a count greater than the device's advertised max, or it is asking for 0.

Fix: Clamp the requested count to `pci_msix_count`'s returned value. Always request at least 1.

### Failure Mode 3: pci_alloc_msix Returns Fewer Vectors Than Requested

Symptom: `count` after the call is less than requested.

Cause: the kernel's vector pool was partially depleted; the device's allocation was given whatever remained.

Fix: Decide upfront whether to accept, adapt, or release. Chapter 20's driver releases and falls back to MSI.

### Failure Mode 4: bus_alloc_resource_any Returns NULL for an MSI-X Vector

Symptom: after `pci_alloc_msix` succeeded, the per-vector IRQ allocation fails.

Causes:
- Wrong rid (using 0 instead of i+1).
- Already released previously (double-release).
- Out of IRQ resources at the bus layer.

Fix: Check the rid is i+1. Audit the release code. Log the error.

### Failure Mode 5: bus_setup_intr Returns EINVAL for a Per-Vector Handler

Symptom: `bus_setup_intr` fails.

Causes:
- Filter and ithread both NULL.
- Missing `INTR_TYPE_*` flag.
- Already set up previously (double-setup).

Fix: Ensure the filter argument is non-NULL. Include an `INTR_TYPE_*` flag. Audit the setup code for double-registration.

### Failure Mode 6: bus_bind_intr Returns an Error

Symptom: `bus_bind_intr` returns non-zero.

Causes:
- Platform does not support rebinding.
- CPU out of range.
- Kernel configuration (NO_SMP, NUMA disabled).

Fix: Treat as non-fatal (`device_printf` a warning and continue). The driver still works without binding.

### Failure Mode 7: vmstat -i Shows Vectors but Counters Don't Increment

Symptom: the kernel sees the vectors but the filters never fire.

Causes:
- The device's `INTR_MASK` is zero (chapter 19 problem).
- The device reset its interrupt state.
- Hardware bug or bhyve/QEMU configuration problem.

Fix: Verify the device's INTR_MASK. Use the simulated-interrupt sysctl to confirm the filter works at all.

### Failure Mode 8: Second kldload Falls Back to Lower Tier

Symptom: first load uses MSI-X; unload; second load uses legacy or MSI.

Cause: `pci_release_msi` not called on teardown.

Fix: Audit the teardown path. Make sure `pci_release_msi` runs on every successful allocation path.

### Failure Mode 9: WITNESS Panics on Multi-Vector Setup

Symptom: `WITNESS` reports a lock-order violation or a "lock held during sleep" in the per-vector setup.

Cause: holding `sc->mtx` across a `bus_setup_intr` call. The bus hooks may sleep, and holding a mutex across a sleep is illegal.

Fix: Release `sc->mtx` before calling `bus_setup_intr`. Reacquire afterwards if needed.

### Failure Mode 10: Partial Setup Doesn't Clean Up Properly

Symptom: attach fails; second attach fails with "resource in use".

Cause: the partial-failure goto cascade doesn't undo all the way. Some per-vector state lingers.

Fix: Ensure the cascade unwinds to the vector that failed, not past it. Use the per-vector helper consistently.



## Additional Troubleshooting

A handful of extra failure modes Chapter 20 readers might hit.

### "QEMU guest does not expose MSI-X"

Causes: QEMU version too old, or the guest is booting with legacy virtio.

Fix: Update QEMU to a recent version. In the guest, check:

```sh
pciconf -lvc | grep -B 1 -A 2 'cap 11'
```

If no `cap 11` lines appear, MSI-X is not available. Switch to QEMU's modern virtio-rng-pci with `-device virtio-rng-pci,disable-legacy=on`.

### "intr_simulate_rx increments fire_count but task never runs"

Cause: the task's `TASK_INIT` was not called, or the taskqueue was not started.

Fix: Verify `TASK_INIT(&vec->task, 0, myfirst_rx_task_fn, vec)` in setup. Verify `taskqueue_start_threads(&sc->intr_tq, ...)`.

### "Per-vector counters increment but the stray count goes up proportionally"

Cause: the filter's status check is wrong, or multiple vectors are triggering on the same bit.

Fix: Each filter should check for its specific bit(s). If two filters both try to handle `DATA_AV`, one will win and the other will see stray.

### "cpuset -g -x $irq reports mask 0 on all vectors"

Cause: `bus_bind_intr` has not been called, or it was called with CPU 0 (mask 1).

Fix: If intentionally unbound, "mask 0" might be platform-specific. If binding was attempted, check return value of `bus_bind_intr`.

### "Driver load succeeds but dmesg shows no attach banner"

Cause: the `device_printf` came before the banner flush, or the banner is in a very early boot buffer.

Fix: `dmesg -a` shows the full message buffer. Verify `dmesg -a | grep myfirst`.

### "Detach hangs after multi-vector setup"

Cause: a vector's handler is still running when teardown tries to proceed. `bus_teardown_intr` blocks waiting for it.

Fix: Make sure the device's `INTR_MASK` is cleared *before* `bus_teardown_intr`, so no new handlers can be dispatched. Make sure the filter does not loop forever; short-runtime discipline.

### "pci_alloc_msix succeeds but only some vectors fire"

Cause: the device is not actually signalling on the vectors it should. Could be a driver bug (forgot to enable) or a device quirk.

Fix: Use the simulated-interrupt sysctl to confirm the filter works for each vector. If the simulated path works but real events don't fire the vector, the issue is on the device side.



## Worked Example: Tracing an Event Through Three Tiers

To make the fallback ladder concrete, here is a complete trace of the same event (a simulated DATA_AV interrupt) on each of the three tiers.

### Tier 3: Legacy INTx

On bhyve with virtio-rnd (no MSI-X exposed), the driver falls back to legacy INTx with one handler at rid 0.

1. User runs `sudo sysctl dev.myfirst.0.intr_simulate_admin=1` (or `intr_simulate_rx=1`, etc.).
2. The sysctl handler acquires `sc->mtx`, writes the bit to INTR_STATUS, releases, calls the filter.
3. The single `myfirst_intr_filter` (from Chapter 19) runs. It reads INTR_STATUS, sees the bit, acknowledges, and either enqueues the task (for DATA_AV) or handles inline (for ERROR/COMPLETE).
4. `intr_count` increments.
5. On legacy, there is only one vector, so all three simulated-interrupt sysctls go through the same filter.

Observations:
- `sysctl dev.myfirst.0.intr_mode` returns 0.
- `vmstat -i | grep myfirst` shows one line.
- The per-vector counters do not exist (legacy mode uses the Chapter 19 counters).

### Tier 2: MSI

On a system that supports MSI but not MSI-X, the driver allocates a single MSI vector. MSI requires a power-of-two vector count, so the driver cannot ask for 3 here; it requests 1 and uses the Chapter 19 single-handler pattern.

1. User runs `sudo sysctl dev.myfirst.0.intr_simulate_admin=1` (or `intr_simulate_rx=1`, or `intr_simulate_tx=4`).
2. Because only one vector is set up on the MSI tier, all three per-vector simulation sysctls route through the same Chapter 19 `myfirst_intr_filter`.
3. The filter reads INTR_STATUS, sees the bit, acknowledges, and either handles inline or enqueues the task.

Observations:
- `sysctl dev.myfirst.0.intr_mode` returns 1.
- `vmstat -i | grep myfirst` shows one line (the single MSI handler at rid=1, labelled "msi").
- The per-vector counters on slots 1 and 2 stay at 0 because only slot 0 is in use; the Chapter 19 global counters (`intr_count`, `intr_data_av_count`, etc.) are the ones that move.

### Tier 1: MSI-X

On QEMU with virtio-rng-pci, the driver allocates MSI-X with 3 vectors, each bound to a CPU.

1. User runs `sudo sysctl dev.myfirst.0.intr_simulate_rx=1`.
2. The sysctl calls the rx filter directly (simulated path does not go through the hardware).
3. `myfirst_rx_filter` runs (on whichever CPU the sysctl was invoked on, because simulation is not going through the kernel's interrupt dispatch).
4. Counters increment; task runs.

Observations:
- `sysctl dev.myfirst.0.intr_mode` returns 2.
- `vmstat -i | grep myfirst` shows three lines; each has a different IRQ number.
- `cpuset -g -x <irq>` for each IRQ shows different CPU masks.

A real (non-simulated) MSI-X interrupt would dispatch on the bound CPU; the simulation bypass makes it run on the calling thread's CPU. This is a limitation of the simulation technique but does not affect correctness.

### The Lesson

All three tiers drive the same filter logic and the same task. The only differences are:

- Which rid the IRQ resource uses (0 for legacy, 1+ for MSI/MSI-X).
- Whether `pci_alloc_msi` or `pci_alloc_msix` succeeded.
- How many filter functions are registered (1 for legacy, 3 for MSI/MSI-X).
- Which CPU real interrupts dispatch on.

A well-written driver works identically on all three tiers. Chapter 20's fallback ladder ensures this.



## A Practical Lab: Regression Testing Across All Three Tiers

A lab that exercises the fallback ladder to confirm all three tiers work.

### Setup

You need two test environments:

- **Environment A**: bhyve with virtio-rnd. The driver falls back to legacy INTx.
- **Environment B**: QEMU with virtio-rng-pci. The driver uses MSI-X.

(A third environment with only MSI and no MSI-X is hard to construct reliably on modern platforms. The MSI path is exercised only if the reader has a system where MSI-X fails but MSI works.)

### Procedure

1. On Environment A, load `myfirst.ko`. Verify:

```sh
sysctl dev.myfirst.0.intr_mode   # returns 0
vmstat -i | grep myfirst          # one line
```

2. Exercise the pipeline via the simulated-interrupt sysctls. All three should work, though on legacy they all go through the same filter.

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2
sudo sysctl dev.myfirst.0.intr_simulate_rx=1
sudo sysctl dev.myfirst.0.intr_simulate_tx=4
sysctl dev.myfirst.0.intr_count   # should be 3
```

3. Unload. Verify no leaks.

4. On Environment B, repeat:

```sh
sysctl dev.myfirst.0.intr_mode   # returns 2
vmstat -i | grep myfirst          # three lines
for irq in <IRQs>; do cpuset -g -x $irq; done
```

5. Exercise the per-vector pipeline. Each sysctl should increment its own vector's counter.

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2
sysctl dev.myfirst.0.vec.admin.fire_count  # 1
sysctl dev.myfirst.0.vec.rx.fire_count     # 0
sysctl dev.myfirst.0.vec.tx.fire_count     # 0
```

6. Unload. Verify no leaks.

### Expected Observations

- Both environments attach cleanly.
- The dmesg summary line shows the correct mode for each.
- Per-vector counters tick independently on MSI-X.
- On legacy, a single counter covers all events.
- No leaks after unload in either environment.

### What to Do if a Tier Fails

If the MSI-X tier fails on Environment B:

1. Verify QEMU is new enough. Older versions (pre-5.0) have quirks.
2. Check `pciconf -lvc` in the guest; MSI-X capability should be visible.
3. Check `dmesg` for errors from `pci_alloc_msix`.

If the legacy tier fails on Environment A:

1. Check `pciconf -lvc` for the device's interrupt line configuration.
2. Ensure `virtio_rnd` is not already attached (Chapter 18 caveat).
3. Look for `pci_alloc_resource` failures in `dmesg`.



## Extended Challenge: Building a Production-Quality Driver

An optional exercise for readers who want to practise multi-vector design on a realistic scale.

### The Goal

Take the Chapter 20 driver and extend it to handle N queues dynamically, where N is discovered at attach time based on the allocated MSI-X vector count. Each queue has:

- Its own vector (MSI-X vector 1+queue_id).
- Its own filter function (or a shared one that identifies the queue from the vector arg).
- Its own counters.
- Its own task on its own taskqueue.
- Its own NUMA-local CPU binding.

### Implementation Outline

1. Replace `MYFIRST_MAX_VECTORS` with a runtime-chosen count.
2. Allocate the `vectors[]` array dynamically (using `malloc`).
3. Allocate a separate taskqueue per vector.
4. Use `bus_get_cpus(INTR_CPUS, ...)` to distribute vectors across NUMA-local CPUs.
5. Add sysctls that scale with the vector count.

### Testing

Run the driver on a guest with varying MSI-X vector counts. For each count, verify:
- The fire counters tick for the simulated interrupts.
- The CPU affinity respects NUMA locality.
- Teardown is clean.

### What This Exercises

- Dynamic memory management in a driver.
- The `bus_get_cpus` API.
- Per-queue taskqueues (challenge 3 from earlier).
- Runtime sysctl tree construction (challenge 7 from earlier).

This is a significant exercise and will likely take several hours. The result is a driver recognisably similar to production NIC and NVMe drivers.



## Reference: Priority Values for Interrupt and Task Work

For quick reference, the priority constants a Chapter 20 driver might use (from `/usr/src/sys/sys/priority.h`):

```text
PI_REALTIME  = PRI_MIN_ITHD + 0   (highest; rarely used)
PI_INTR      = PRI_MIN_ITHD + 4   (common hardware interrupt level)
PI_AV        = PI_INTR            (audio/video)
PI_NET       = PI_INTR            (network)
PI_DISK      = PI_INTR            (block storage)
PI_TTY       = PI_INTR            (terminal/serial)
PI_DULL      = PI_INTR            (low-priority hardware)
PI_SOFT      = PRI_MIN_ITHD + 8   (soft interrupts)
```

The common hardware priorities all map to `PI_INTR`; the names are distinctions of intent rather than of scheduling priority. Chapter 20's driver uses `PI_NET` for its taskqueue; any hardware-level priority would work equivalently.



## Reference: Useful DTrace One-Liners for MSI-X Drivers

For readers who want to observe the Chapter 20 driver's behaviour dynamically.

### Count filter invocations per CPU

```sh
sudo dtrace -n '
fbt::myfirst_admin_filter:entry, fbt::myfirst_rx_filter:entry,
fbt::myfirst_tx_filter:entry { @[probefunc, cpu] = count(); }'
```

Shows which filter runs on which CPU.

### Time spent in each filter

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_rx_filter:return /self->ts/ {
    @[probefunc] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Histogram of RX filter CPU time.

### Rate of simulated vs real interrupts

```sh
sudo dtrace -n '
fbt::myfirst_intr_simulate_vector_sysctl:entry { @sims = count(); }
fbt::myfirst_rx_filter:entry { @filters = count(); }'
```

If `filters > sims`, some real interrupts are firing.

### Task latency

```sh
sudo dtrace -n '
fbt::myfirst_rx_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_rx_task_fn:entry /self->ts/ {
    @lat = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Histogram of time from filter to task invocation. Shows the taskqueue's scheduling latency.



## Reference: A Closing Note Before Part 4's End

Chapters 16 through 20 built the full interrupt and hardware story for the `myfirst` driver. Each chapter added one layer:

- Chapter 16: register access.
- Chapter 17: device behaviour simulation.
- Chapter 18: PCI attach.
- Chapter 19: single-vector interrupt handling.
- Chapter 20: multi-vector MSI/MSI-X.

Chapter 21 will add DMA, completing Part 4's hardware layer. At that point, the `myfirst` driver will be structurally a real driver: a PCI device with MSI-X interrupts and DMA-based data transfer. What distinguishes it from a production driver is the specific protocol it speaks (none, really; it is a demo) and the device it targets (a virtio-rnd abstraction).

A reader who has internalised these five chapters can open any FreeBSD driver in `/usr/src/sys/dev/` and recognise the patterns. That recognition is Part 4's deepest payoff.



## Hands-On Labs

The labs are graduated checkpoints. Each lab builds on the previous one and corresponds to one of the chapter's stages. A reader who works through all five has a complete multi-vector driver, a working QEMU test environment for MSI-X, and a regression script that validates all three tiers of the fallback ladder.

Time budgets assume the reader has already read the relevant sections.

### Lab 1: Discover MSI and MSI-X Capabilities

Time: thirty minutes.

Objective: Build an intuition for which devices on your system support MSI and MSI-X.

Steps:

1. Run `sudo pciconf -lvc > /tmp/pci_caps.txt`. The `-c` flag includes capability lists.
2. Search for MSI capabilities: `grep -B 1 "cap 05" /tmp/pci_caps.txt`.
3. Search for MSI-X capabilities: `grep -B 1 "cap 11" /tmp/pci_caps.txt`.
4. For three devices that support MSI-X, note:
   - The device's name (`pci0:B:D:F`).
   - The number of MSI-X messages supported.
   - Whether the driver is currently using MSI-X (check `vmstat -i` for multiple lines of the same device name).
5. Compare the total number of MSI-capable devices to the total number of MSI-X-capable devices. Modern systems typically have more MSI-X devices than MSI-only devices.

Expected observations:

- NICs usually advertise MSI-X with many vectors (4 to 64).
- SATA and NVMe controllers advertise MSI-X (NVMe often with dozens of vectors).
- Some legacy devices (an audio chip, a USB controller) advertise only MSI.
- A few very old devices advertise neither and rely on legacy INTx.

This lab is about vocabulary. No code. The payoff is that Section 2 and 5's allocation calls become concrete.

### Lab 2: Stage 1, MSI Fallback Ladder

Time: two to three hours.

Objective: Extend Chapter 19's driver with the MSI-first fallback ladder. Version target: `1.3-msi-stage1`.

Steps:

1. Starting from Chapter 19 Stage 4, copy the driver source to a new working directory.
2. Add the `intr_mode` field and enum to `myfirst.h`.
3. Modify `myfirst_intr_setup` (in `myfirst_intr.c`) to attempt MSI allocation first, falling back to legacy INTx.
4. Modify `myfirst_intr_teardown` to call `pci_release_msi` when MSI was used.
5. Add the `dev.myfirst.N.intr_mode` sysctl.
6. Update the `Makefile` version string to `1.3-msi-stage1`.
7. Compile (`make clean && make`).
8. Load on a guest. Note which mode the driver reports:

```sh
sudo kldload ./myfirst.ko
sudo dmesg | tail -5
sysctl dev.myfirst.0.intr_mode
```

On QEMU with virtio-rng-pci, the driver should report `MSI, 1 vector` (or similar). On bhyve with virtio-rnd, it should report `legacy INTx`.

9. Unload and verify no leaks.

Common failures:

- Missing `pci_release_msi`: next load fails or falls back to legacy.
- Wrong rid (using 0 for MSI): `bus_alloc_resource_any` returns NULL.
- Not checking the returned count: driver proceeds with fewer vectors than expected.

### Lab 3: Stage 2, Multi-Vector Allocation (MSI)

Time: three to four hours.

Objective: Extend to three MSI vectors with per-vector handlers. Version target: `1.3-msi-stage2`.

Steps:

1. Starting from Lab 2, add the `myfirst_vector` struct and per-vector array to `myfirst.h`.
2. Write three filter functions: `myfirst_admin_filter`, `myfirst_rx_filter`, `myfirst_tx_filter`.
3. Write the `myfirst_intr_setup_vector` and `myfirst_intr_teardown_vector` helpers.
4. Modify `myfirst_intr_setup` to try `pci_alloc_msi` for `MYFIRST_MAX_VECTORS` vectors, setting up each vector independently.
5. Modify `myfirst_intr_teardown` to loop per-vector.
6. Add per-vector counter sysctls (`vec0_fire_count`, `vec1_fire_count`, `vec2_fire_count`).
7. Add per-vector simulated-interrupt sysctls (`intr_simulate_admin`, `intr_simulate_rx`, `intr_simulate_tx`).
8. Bump the version to `1.3-msi-stage2`.
9. Compile, load, verify:

```sh
sysctl dev.myfirst.0.intr_mode   # should be 1 on QEMU
vmstat -i | grep myfirst          # should show 3 lines
```

10. Exercise each vector:

```sh
sudo sysctl dev.myfirst.0.intr_simulate_admin=2  # ERROR
sudo sysctl dev.myfirst.0.intr_simulate_rx=1     # DATA_AV
sudo sysctl dev.myfirst.0.intr_simulate_tx=4     # COMPLETE
sysctl dev.myfirst.0 | grep vec
```

Each vector's counter should increment independently.

11. Unload, verify no leaks.

### Lab 4: Stage 3, MSI-X With CPU Binding

Time: three to four hours.

Objective: Prefer MSI-X over MSI, bind each vector to a CPU. Version target: `1.3-msi-stage3`.

Steps:

1. Starting from Lab 3, change the fallback ladder to attempt MSI-X first (via `pci_msix_count` and `pci_alloc_msix`), MSI as second tier, legacy as last.
2. Add the `myfirst_msix_bind_vectors` helper that calls `bus_bind_intr` for each vector.
3. Call the bind helper after all vectors are registered.
4. Update the dmesg summary line to distinguish MSI-X from MSI.
5. Bump the version to `1.3-msi-stage3`.
6. Compile, load on QEMU with `virtio-rng-pci`. Verify:

```sh
sysctl dev.myfirst.0.intr_mode   # should be 2 on QEMU
sudo dmesg | grep myfirst | grep MSI-X
```

The attach line should read `interrupt mode: MSI-X, 3 vectors`.

7. Check per-vector CPU bindings:

```sh
# For each myfirst IRQ, show its CPU binding.
vmstat -i | grep myfirst
# (Note the IRQ numbers, then:)
for irq in <IRQ1> <IRQ2> <IRQ3>; do
    echo "IRQ $irq:"
    cpuset -g -x $irq
done
```

On a multi-CPU guest, each vector should be bound to a different CPU.

8. Exercise each vector (same as Lab 3).

9. Detach and reattach:

```sh
sudo devctl detach myfirst0
sudo devctl attach pci0:0:4:0
sysctl dev.myfirst.0.intr_mode  # should still be 2
```

10. Unload, verify no leaks.

### Lab 5: Stage 4, Refactor, Regression, Version

Time: three to four hours.

Objective: Move the multi-vector code into `myfirst_msix.c`, write `MSIX.md`, run the regression. Version target: `1.3-msi`.

Steps:

1. Starting from Lab 4, create `myfirst_msix.c` and `myfirst_msix.h`.
2. Move the per-vector filter functions, helpers, setup, teardown, and sysctl registration into `myfirst_msix.c`.
3. Keep the legacy-INTx fallback in `myfirst_intr.c` (Chapter 19's file).
4. In `myfirst_pci.c`, replace the old interrupt setup/teardown calls with calls into `myfirst_msix.c`.
5. Update the `Makefile` to add `myfirst_msix.c` to SRCS. Bump the version to `1.3-msi`.
6. Write `MSIX.md` documenting the multi-vector design.
7. Compile, load, run the full regression script (from the companion examples).
8. Confirm all three tiers work (by testing on bhyve with virtio-rnd for legacy and QEMU with virtio-rng-pci for MSI-X).

Expected outcomes:

- The driver at `1.3-msi` works on both bhyve (legacy fallback) and QEMU (MSI-X).
- `myfirst_intr.c` now only contains the Chapter 19 single-handler fallback path.
- `myfirst_msix.c` contains the Chapter 20 multi-vector logic.
- `MSIX.md` documents the design clearly.



## Challenge Exercises

The challenges build on the labs and extend the driver in directions the chapter did not take.

### Challenge 1: Dynamic Vector-Count Adaptation

Modify the setup to adapt to whatever vector count the kernel actually allocates. If 3 are requested but 2 are allocated, the driver should still work with 2 (fold admin and tx into one combined vector). If 1 is allocated, fold everything into one.

This exercise teaches the "adapt" strategy from the fallback ladder.

### Challenge 2: NUMA-Aware CPU Binding

Replace the round-robin CPU binding with a NUMA-aware binding using `bus_get_cpus(dev, INTR_CPUS, ...)`. Verify with `cpuset -g -x <irq>` that vectors land on CPUs in the same NUMA domain as the device.

On a single-socket system the exercise is academic; on a multi-socket test host it is measurable.

### Challenge 3: Per-Vector Taskqueues

Each vector currently shares one taskqueue. Modify the driver so each vector has its own taskqueue (with its own worker thread). Measure the latency impact with DTrace.

This exercise introduces per-vector workers and shows when they help vs hurt.

### Challenge 4: Per-Vector MSI-X Mask Control

The MSI-X table's vector-control register has a mask bit per vector. Add a sysctl that lets the operator mask an individual vector at runtime. Verify that a masked vector stops receiving interrupts.

Hint: the mask bit is programmed through direct MSI-X table access, which is a deeper topic than Chapter 20 covers. The FreeBSD MSI-X implementation may or may not expose this directly; a reader might need to use `bus_teardown_intr` and later `bus_setup_intr` as a higher-level "soft mask".

### Challenge 5: Implement Interrupt Moderation

For a simulated driver, moderation is easy to prototype: a sysctl that coalesces N simulated interrupts into one task run. Implement the coalescing, measure the latency-vs-throughput trade-off.

### Challenge 6: Vector Reassignment at Runtime

Add a sysctl that lets the operator reassign which vector handles which event class (e.g., swap RX and TX). Demonstrate that after the reassignment, simulated-interrupt-RX triggers the TX filter and vice versa.

### Challenge 7: Per-Queue Sysctl Tree

Restructure the per-vector sysctls into a proper tree: `dev.myfirst.N.vec.admin.fire_count`, `dev.myfirst.N.vec.rx.fire_count`, etc. Use `SYSCTL_ADD_NODE` to create the tree nodes.

### Challenge 8: Dtrace Instrumentation

Write a DTrace script that shows the per-CPU distribution of each vector's filter invocations. Plot the per-CPU breakdown as a histogram. This is the diagnostic that confirms CPU binding is working.



## Troubleshooting and Common Mistakes

### "pci_alloc_msix returns EBUSY or ENXIO"

Possible causes:

1. The device is not connected in a way that supports MSI-X (legacy virtio-rnd on bhyve, for example). Check `pciconf -lvc`.
2. A previous load of the driver did not call `pci_release_msi` at teardown. Reboot or try `kldunload` + `kldload` again.
3. The kernel ran out of interrupt vectors. Rare on modern x86, possible on low-vector platforms.

### "vmstat -i shows only one line on MSI-X guest"

Likely cause: `pci_alloc_msix` succeeded but allocated only 1 vector. Check the returned count vs requested. Either accept (fold work into one) or release and fall back.

### "Filter fires but vec->fire_count stays at zero"

Likely cause: the `sc` argument is confused with `vec`. The handler receives `vec`, not `sc`. Check `bus_setup_intr`'s argument.

### "Driver panics on kldunload after multiple load/unload cycles"

Likely cause: `pci_release_msi` not called on teardown. The device-level MSI state leaks across loads; eventually the kernel's internal bookkeeping is confused.

### "Different vectors all fire on the same CPU"

Likely cause: `bus_bind_intr` failed silently. Check the return value and log non-zero results.

### "MSI-X allocation succeeds but vmstat -i shows no events"

Likely cause: the device's `INTR_MASK` write targeted the wrong register or was skipped. Verify the mask is set (Chapter 17/Chapter 19 diagnostic).

### "Stray interrupts accumulate on the MSI-X admin vector"

Likely cause: the admin filter's status check is wrong; the filter returns `FILTER_STRAY` when it should handle. Check the `status & MYFIRST_INTR_ERROR` check.

### "Shared-IRQ behaviour on legacy fallback differs from MSI-X"

Expected. On legacy INTx the single handler sees every event bit; on MSI-X each vector sees only its own event. Tests that exercise per-vector stray counts differ between the two modes.

### "Stage 2 compiles but Stage 3 fails at `bus_get_cpus` link error"

Cause: `bus_get_cpus` may not be available in older FreeBSD versions or may require specific `#include <sys/bus.h>` placement. Check the include order.

### "QEMU guest does not expose MSI-X despite using virtio-rng-pci"

Likely cause: older QEMU versions use legacy virtio by default. Check `pciconf -lvc` in the guest; if MSI-X is not listed, the guest is using legacy. Update QEMU or use `-device virtio-rng-pci,disable-modern=off,disable-legacy=on`.



## Wrapping Up

Chapter 20 gave the driver the ability to handle multiple interrupt vectors. The starting point was `1.2-intr` with one handler on the legacy INTx line. The ending point is `1.3-msi` with a three-tier fallback ladder (MSI-X, MSI, legacy), three per-vector filter handlers, per-vector counters and tasks, per-vector CPU binding, a clean multi-vector teardown, and a new `myfirst_msix.c` file plus `MSIX.md` document.

The eight sections walked the full progression. Section 1 introduced MSI and MSI-X at the hardware level. Section 2 added MSI as a single-vector alternative to legacy INTx. Section 3 extended to multi-vector MSI. Section 4 examined the concurrency implications of multiple filters on multiple CPUs. Section 5 moved to MSI-X with per-vector CPU binding. Section 6 codified the per-vector event roles. Section 7 consolidated the teardown. Section 8 refactored into the final layout.

What Chapter 20 did not do is DMA. Each vector's handler still only touches registers; the device does not yet have the ability to reach into RAM. Chapter 21 is where that changes. DMA introduces new complications (coherence, scatter-gather, mapping) that interact with interrupts (completion interrupts signal that a DMA transfer is done). The Chapter 20 interrupt machinery is ready to handle completion interrupts; Chapter 21 writes the DMA side.

The file layout has grown: 14 source files (including `cbuf`), 6 documentation files (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`), and a growing regression test suite. The driver is structurally parallel to production FreeBSD drivers at this point.

### A Reflection Before Chapter 21

Chapter 20 was the last chapter in Part 4 that is purely about interrupts. Chapter 21 moves to DMA, which is about moving data. The two are complementary: interrupts signal events; DMA moves the data those events are about. A high-performance driver uses both together: receive descriptors are populated by DMA from the device into RAM, then a completion interrupt signals the driver to process the descriptors.

Chapter 20's per-vector handlers are already the right shape for this. Each receive queue's completion interrupt fires its own vector; each vector's filter acknowledges and defers; the task walks the receive ring (populated by DMA, Chapter 21) and passes packets up. Chapter 21 writes the DMA side; Chapter 20's interrupt side is already in place.

The chapter's teaching also generalises. A reader who has internalised Chapter 20's three-tier fallback ladder, per-vector state design, CPU binding, and clean teardown will find similar patterns in every multi-queue FreeBSD driver. The specific device differs; the structure does not.

### What to Do If You Are Stuck

Three suggestions.

First, read `/usr/src/sys/dev/virtio/pci/virtio_pci.c` carefully, focusing on the `vtpci_alloc_intr_resources` family of functions. The pattern matches Chapter 20 exactly and the code is compact enough to read in one sitting.

Second, run the chapter's regression test on both a bhyve guest (legacy fallback) and a QEMU guest (MSI-X). Seeing the same driver behave correctly on both targets confirms the fallback ladder is right.

Third, skip the challenges on first pass. The labs are calibrated for Chapter 20's pace; the challenges assume the material is solid. Come back to them after Chapter 21 if they feel out of reach now.

Chapter 20's goal was to give the driver a multi-vector interrupt path. If it has, Chapter 21's DMA work becomes a complement rather than an entirely new topic.



## Bridge to Chapter 21

Chapter 21 is titled *DMA and High-Speed Data Transfer*. Its scope is the topic Chapter 20 deliberately did not take: the device's ability to read and write RAM directly, without the driver in the loop for each word. A NIC with a 64-entry receive descriptor ring populates those entries by DMA from the wire; a single interrupt signals "N entries are ready". The driver's handler walks the ring and processes the entries. Without DMA the driver would have to read each byte from a device register, which does not scale.

Chapter 20 prepared the ground in three specific ways.

First, **you have per-vector completion interrupts**. Each queue's receive completions and transmit completions can fire a dedicated vector. Chapter 21's DMA ring work plugs into Chapter 20's per-vector filter and task; the filter sees "completions N through M are ready", the task processes them.

Second, **you have per-CPU handler placement**. A DMA ring's memory is on a specific NUMA node; the handler that processes it should run on a CPU on that node. Chapter 20's `bus_bind_intr` work is the mechanism. Chapter 21 extends this: the DMA memory is allocated with NUMA awareness too, so the ring, the handler, and the processing all end up on the same node.

Third, **you have the teardown discipline**. DMA adds more resources (DMA tags, DMA maps, DMA memory regions), and each needs its own teardown step. The Chapter 19/Chapter 20 per-vector teardown pattern extends naturally to per-queue DMA cleanup.

Specific topics Chapter 21 will cover:

- What DMA is, the difference between memory-mapped I/O and DMA.
- `bus_dma(9)`: tags, maps, and the DMA state machine.
- `bus_dma_tag_create` to describe DMA requirements (alignment, boundaries, address range).
- `bus_dmamap_create` and `bus_dmamap_load` to set up DMA transfers.
- Synchronisation: `bus_dmamap_sync` around DMA.
- Bounce buffers: what they are and when they are used.
- Cache coherence: why CPUs and devices see different memory at different times.
- Scatter-gather lists: physical addresses that are not contiguous.
- Ring buffers: the producer-consumer descriptor ring pattern.

You do not need to read ahead. Chapter 20 is sufficient preparation. Bring your `myfirst` driver at `1.3-msi`, your `LOCKING.md`, your `INTERRUPTS.md`, your `MSIX.md`, your `WITNESS`-enabled kernel, and your regression script. Chapter 21 starts where Chapter 20 ended.

The hardware conversation is deepening. The vocabulary is yours; the structure is yours; the discipline is yours. Chapter 21 adds the next missing piece: the device's ability to move data without asking.



## Reference: Chapter 20 Quick-Reference Card

A compact summary of the vocabulary, APIs, macros, and procedures Chapter 20 introduced.

### Vocabulary

- **MSI (Message Signalled Interrupts)**: PCI 2.2 mechanism. 1 to 32 vectors, contiguous, single address.
- **MSI-X**: PCIe mechanism. Up to 2048 vectors, per-vector address, per-vector mask, table in a BAR.
- **vector**: a single interrupt source identified by an index.
- **rid**: the resource ID used with `bus_alloc_resource_any`. 0 for legacy INTx, 1+ for MSI and MSI-X.
- **intr_mode**: the driver's record of which tier it is using (legacy, MSI, or MSI-X).
- **fallback ladder**: try MSI-X first, then MSI, then legacy INTx.
- **per-vector state**: counters, filter, task, cookie, resource per vector.
- **CPU binding**: routing a vector to a specific CPU via `bus_bind_intr`.
- **LOCAL_CPUS / INTR_CPUS**: CPU-set queries for NUMA-aware placement.

### Essential APIs

- `pci_msi_count(dev)`: query MSI vector count.
- `pci_msix_count(dev)`: query MSI-X vector count.
- `pci_alloc_msi(dev, &count)`: allocate MSI vectors.
- `pci_alloc_msix(dev, &count)`: allocate MSI-X vectors.
- `pci_release_msi(dev)`: release MSI or MSI-X vectors.
- `pci_msix_table_bar(dev)`, `pci_msix_pba_bar(dev)`: identify table/PBA BARs.
- `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)`: allocate per-vector IRQ resource.
- `bus_setup_intr(dev, res, flags, filter, ihand, arg, &cookie)`: register per-vector handler.
- `bus_teardown_intr(dev, res, cookie)`: unregister per-vector handler.
- `bus_describe_intr(dev, res, cookie, "name")`: label per-vector handler.
- `bus_bind_intr(dev, res, cpu)`: bind vector to a specific CPU.
- `bus_get_cpus(dev, op, size, &set)`: query NUMA-local CPUs (op = LOCAL_CPUS or INTR_CPUS).

### Essential Macros

- `PCIY_MSI = 0x05`: MSI capability ID.
- `PCIY_MSIX = 0x11`: MSI-X capability ID.
- `PCIM_MSIXCTRL_TABLE_SIZE = 0x07FF`: mask for vector count.
- `PCI_MSIX_MSGNUM(ctrl)`: macro to extract vector count from control register.
- `MYFIRST_MAX_VECTORS`: driver-defined constant (3 in Chapter 20).

### Common Procedures

**Implement the three-tier fallback ladder:**

1. `pci_msix_count(dev)`; if > 0, try `pci_alloc_msix`.
2. On failure, `pci_msi_count(dev)`; if > 0, try `pci_alloc_msi`.
3. On failure, fall back to legacy INTx with `rid = 0` and `RF_SHAREABLE`.

**Register per-vector handlers (MSI-X):**

1. Loop from `i = 0` to `num_vectors - 1`.
2. For each: `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE)` with `rid = i + 1`.
3. `bus_setup_intr(dev, vec->irq_res, INTR_TYPE_MISC | INTR_MPSAFE, vec->filter, NULL, vec, &vec->intr_cookie)`.
4. `bus_describe_intr(dev, vec->irq_res, vec->intr_cookie, vec->name)`.
5. `bus_bind_intr(dev, vec->irq_res, target_cpu)`.

**Tear down a multi-vector driver:**

1. Clear `INTR_MASK` at the device.
2. For each vector (reverse order): `bus_teardown_intr`, `bus_release_resource`.
3. Drain each per-vector task.
4. Free the taskqueue.
5. `pci_release_msi(dev)` if MSI or MSI-X was used.

### Useful Commands

- `pciconf -lvc`: list devices with capability lists.
- `vmstat -i`: show per-handler interrupt counts.
- `cpuset -g -x <irq>`: query CPU affinity for an IRQ.
- `cpuset -l <cpu> -x <irq>`: set CPU affinity for an IRQ.
- `sysctl dev.myfirst.0.intr_mode`: query driver's interrupt mode.

### Files to Keep Bookmarked

- `/usr/src/sys/dev/pci/pcivar.h`: MSI/MSI-X inline wrappers.
- `/usr/src/sys/dev/pci/pcireg.h`: capability IDs and bit fields.
- `/usr/src/sys/dev/pci/pci.c`: kernel-side implementation of `pci_alloc_msi`/`msix`.
- `/usr/src/sys/dev/virtio/pci/virtio_pci.c`: clean MSI-X fallback-ladder example.
- `/usr/src/sys/dev/nvme/nvme_ctrlr.c`: per-queue MSI-X pattern at scale.



## Reference: Glossary of Chapter 20 Terms

**affinity**: the mapping from an interrupt vector to a specific CPU (or set of CPUs).

**bus_bind_intr(9)**: function to route an interrupt vector to a specific CPU.

**bus_get_cpus(9)**: function to query CPU sets associated with a device (local, interrupt-suitable).

**capability list**: the linked list of PCI device capabilities in configuration space.

**coalescing**: buffering multiple events into one interrupt to reduce rate.

**cookie**: the opaque handle returned by `bus_setup_intr(9)`, used by `bus_teardown_intr(9)`.

**fallback ladder**: the sequence MSI-X → MSI → legacy INTx that drivers implement.

**intr_mode**: driver-side enum recording which interrupt tier is active.

**INTR_CPUS**: cpu_sets enum value; CPUs suitable for handling device interrupts.

**LOCAL_CPUS**: cpu_sets enum value; CPUs in the same NUMA domain as the device.

**MSI**: Message Signalled Interrupts, PCI 2.2.

**MSI-X**: the fuller mechanism, PCIe.

**moderation**: buffering interrupts at the device level to trade latency for throughput.

**NUMA**: Non-Uniform Memory Access; multi-socket system architecture.

**per-vector state**: the softc fields specific to one vector (counters, filter, task, cookie, resource).

**pci_msi_count(9) / pci_msix_count(9)**: capability-count queries.

**pci_alloc_msi(9) / pci_alloc_msix(9)**: vector allocation.

**pci_release_msi(9)**: release of MSI/MSI-X (handles both).

**rid**: resource ID. 0 for legacy INTx, 1+ for MSI/MSI-X vectors.

**stray interrupt**: an interrupt that no filter claims.

**taskqueue**: FreeBSD's deferred-work primitive.

**vector**: a single interrupt source in the MSI or MSI-X mechanism.

**vmstat -i**: diagnostic showing per-handler interrupt counts.



## Reference: The Complete Stage 4 myfirst_msix.c Walkthrough

For readers who want a single place to see the final multi-vector layer annotated, this appendix walks through `myfirst_msix.c` from the companion examples, showing each function and explaining the design choices.

### The Top of the File

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/types.h>
#include <sys/smp.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"
#include "myfirst_msix.h"
```

The include list is longer than `myfirst_intr.c`'s: `<dev/pci/pcireg.h>` and `<dev/pci/pcivar.h>` for the MSI/MSI-X API, `<sys/smp.h>` for `mp_ncpus`, and `<machine/atomic.h>` for the per-vector counter increments. Note that `<dev/pci/pcireg.h>` is pulled in even though the file does not directly use `PCIY_MSI` or similar constants; the accessor inline functions in `pcivar.h` depend on it.

### The Per-Vector Helpers

```c
static int
myfirst_msix_setup_vector(struct myfirst_softc *sc, int idx, int rid)
{
	struct myfirst_vector *vec = &sc->vectors[idx];
	int error;

	vec->irq_rid = rid;
	vec->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &vec->irq_rid, RF_ACTIVE);
	if (vec->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, vec->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    vec->filter, NULL, vec, &vec->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    vec->irq_rid, vec->irq_res);
		vec->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, vec->irq_res, vec->intr_cookie,
	    "%s", vec->name);
	return (0);
}

static void
myfirst_msix_teardown_vector(struct myfirst_softc *sc, int idx)
{
	struct myfirst_vector *vec = &sc->vectors[idx];

	if (vec->intr_cookie != NULL) {
		bus_teardown_intr(sc->dev, vec->irq_res, vec->intr_cookie);
		vec->intr_cookie = NULL;
	}
	if (vec->irq_res != NULL) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    vec->irq_rid, vec->irq_res);
		vec->irq_res = NULL;
	}
}
```

These helpers are the symmetry pair from Section 3. Each takes a vector index and operates on the `vec` at that slot. The setup helper is idempotent in the sense that it leaves the vector in a clean state on failure; the teardown helper is safe to call even if setup did not complete.

### The Per-Vector Filter Functions

The three filters differ only in which bit they check. Their common shape:

```c
int
myfirst_msix_rx_filter(void *arg)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;
	uint32_t status;

	status = ICSR_READ_4(sc, MYFIRST_REG_INTR_STATUS);
	if ((status & MYFIRST_INTR_DATA_AV) == 0) {
		atomic_add_64(&vec->stray_count, 1);
		return (FILTER_STRAY);
	}

	atomic_add_64(&vec->fire_count, 1);
	ICSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, MYFIRST_INTR_DATA_AV);
	atomic_add_64(&sc->intr_data_av_count, 1);
	if (sc->intr_tq != NULL)
		taskqueue_enqueue(sc->intr_tq, &vec->task);
	return (FILTER_HANDLED);
}
```

The admin filter checks `MYFIRST_INTR_ERROR`, the tx filter checks `MYFIRST_INTR_COMPLETE`. Each increments the appropriate global counter and the per-vector counter. Only the rx filter enqueues a task.

### The RX Task

```c
static void
myfirst_msix_rx_task_fn(void *arg, int npending)
{
	struct myfirst_vector *vec = arg;
	struct myfirst_softc *sc = vec->sc;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->pci_attached) {
		sc->intr_last_data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
		sc->intr_task_invocations++;
		cv_broadcast(&sc->data_cv);
	}
	MYFIRST_UNLOCK(sc);
}
```

The task runs in thread context and takes `sc->mtx` safely. It checks `sc->pci_attached` before touching shared state, guarding against a race where the task runs during detach.

### The Main Setup Function

The setup function orchestrates the fallback ladder:

```c
int
myfirst_msix_setup(struct myfirst_softc *sc)
{
	int error, wanted, allocated, i;

	/* Initialise per-vector state common to all tiers. */
	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		sc->vectors[i].id = i;
		sc->vectors[i].sc = sc;
	}
	TASK_INIT(&sc->vectors[MYFIRST_VECTOR_RX].task, 0,
	    myfirst_msix_rx_task_fn,
	    &sc->vectors[MYFIRST_VECTOR_RX]);
	sc->vectors[MYFIRST_VECTOR_RX].has_task = true;
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_msix_admin_filter;
	sc->vectors[MYFIRST_VECTOR_RX].filter = myfirst_msix_rx_filter;
	sc->vectors[MYFIRST_VECTOR_TX].filter = myfirst_msix_tx_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].name = "admin";
	sc->vectors[MYFIRST_VECTOR_RX].name = "rx";
	sc->vectors[MYFIRST_VECTOR_TX].name = "tx";

	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	wanted = MYFIRST_MAX_VECTORS;

	/* Tier 0: MSI-X. */
	if (pci_msix_count(sc->dev) >= wanted) {
		allocated = wanted;
		if (pci_alloc_msix(sc->dev, &allocated) == 0 &&
		    allocated == wanted) {
			for (i = 0; i < wanted; i++) {
				error = myfirst_msix_setup_vector(sc, i,
				    i + 1);
				if (error != 0) {
					for (i -= 1; i >= 0; i--)
						myfirst_msix_teardown_vector(
						    sc, i);
					pci_release_msi(sc->dev);
					goto try_msi;
				}
			}
			sc->intr_mode = MYFIRST_INTR_MSIX;
			sc->num_vectors = wanted;
			myfirst_msix_bind_vectors(sc);
			device_printf(sc->dev,
			    "interrupt mode: MSI-X, %d vectors\n", wanted);
			goto enabled;
		}
		if (allocated > 0)
			pci_release_msi(sc->dev);
	}

try_msi:
	/*
	 * Tier 1: MSI with a single vector. MSI requires a power-of-two
	 * count, so we cannot request MYFIRST_MAX_VECTORS (3) here. We
	 * request 1 vector and fall back to the Chapter 19 single-handler
	 * pattern, matching the approach sys/dev/virtio/pci/virtio_pci.c
	 * takes in vtpci_alloc_msi().
	 */
	allocated = 1;
	if (pci_msi_count(sc->dev) >= 1 &&
	    pci_alloc_msi(sc->dev, &allocated) == 0 && allocated >= 1) {
		sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
		sc->vectors[MYFIRST_VECTOR_ADMIN].name = "msi";
		error = myfirst_msix_setup_vector(sc, MYFIRST_VECTOR_ADMIN, 1);
		if (error == 0) {
			sc->intr_mode = MYFIRST_INTR_MSI;
			sc->num_vectors = 1;
			device_printf(sc->dev,
			    "interrupt mode: MSI, 1 vector "
			    "(single-handler fallback)\n");
			goto enabled;
		}
		pci_release_msi(sc->dev);
	}

try_legacy:
	/* Tier 2: legacy INTx. */
	sc->vectors[MYFIRST_VECTOR_ADMIN].filter = myfirst_intr_filter;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid = 0;
	sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = bus_alloc_resource_any(
	    sc->dev, SYS_RES_IRQ,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res == NULL) {
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (ENXIO);
	}
	error = bus_setup_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_rid,
		    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res);
		sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res = NULL;
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
		return (error);
	}
	bus_describe_intr(sc->dev,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].irq_res,
	    sc->vectors[MYFIRST_VECTOR_ADMIN].intr_cookie, "legacy");
	sc->intr_mode = MYFIRST_INTR_LEGACY;
	sc->num_vectors = 1;
	device_printf(sc->dev,
	    "interrupt mode: legacy INTx (1 handler for all events)\n");

enabled:
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}
```

The function is long because it handles three tiers, each with its own allocation, per-vector setup loop, and partial-failure unwind. A reader tracing the flow sees MSI-X tried first, falling through to MSI on any failure, falling through to legacy on any failure there. The `enabled:` label is reached from any successful tier.

The legacy tier is the Chapter 19 path: one filter (`myfirst_intr_filter` from `myfirst_intr.c`), `rid = 0`, `RF_SHAREABLE`. The per-vector counters are not really used on this tier; the Chapter 19 code does its own counting.

### The Teardown Function

```c
void
myfirst_msix_teardown(struct myfirst_softc *sc)
{
	int i;

	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	for (i = sc->num_vectors - 1; i >= 0; i--)
		myfirst_msix_teardown_vector(sc, i);

	if (sc->intr_tq != NULL) {
		for (i = 0; i < sc->num_vectors; i++) {
			if (sc->vectors[i].has_task)
				taskqueue_drain(sc->intr_tq,
				    &sc->vectors[i].task);
		}
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	if (sc->intr_mode == MYFIRST_INTR_MSI ||
	    sc->intr_mode == MYFIRST_INTR_MSIX)
		pci_release_msi(sc->dev);

	sc->num_vectors = 0;
	sc->intr_mode = MYFIRST_INTR_LEGACY;
}
```

The function follows the strict ordering: disable at device, per-vector teardown in reverse, per-vector task drain, free taskqueue, release MSI. No surprises; the symmetry is the payoff.

### The Bind Function

```c
static void
myfirst_msix_bind_vectors(struct myfirst_softc *sc)
{
	int i, cpu;
	int err;

	if (mp_ncpus < 2)
		return;

	for (i = 0; i < sc->num_vectors; i++) {
		cpu = i % mp_ncpus;
		err = bus_bind_intr(sc->dev, sc->vectors[i].irq_res, cpu);
		if (err != 0)
			device_printf(sc->dev,
			    "bus_bind_intr vec %d: %d\n", i, err);
	}
}
```

Round-robin binding. Only called on MSI-X (the function is not useful on MSI or legacy; the setup ladder skips it for those tiers). On single-CPU systems the function returns early without binding.

### The sysctl Function

```c
void
myfirst_msix_add_sysctls(struct myfirst_softc *sc)
{
	struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
	struct sysctl_oid_list *kids = SYSCTL_CHILDREN(sc->sysctl_tree);
	char name[32];
	int i;

	SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "intr_mode",
	    CTLFLAG_RD, &sc->intr_mode, 0,
	    "0=legacy, 1=MSI, 2=MSI-X");

	for (i = 0; i < MYFIRST_MAX_VECTORS; i++) {
		snprintf(name, sizeof(name), "vec%d_fire_count", i);
		SYSCTL_ADD_U64(ctx, kids, OID_AUTO, name,
		    CTLFLAG_RD, &sc->vectors[i].fire_count, 0,
		    "Times this vector's filter was called");
		snprintf(name, sizeof(name), "vec%d_stray_count", i);
		SYSCTL_ADD_U64(ctx, kids, OID_AUTO, name,
		    CTLFLAG_RD, &sc->vectors[i].stray_count, 0,
		    "Stray returns from this vector");
	}

	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_admin",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_ADMIN], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate admin vector interrupt");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_rx",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_RX], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate rx vector interrupt");
	SYSCTL_ADD_PROC(ctx, kids, OID_AUTO, "intr_simulate_tx",
	    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
	    &sc->vectors[MYFIRST_VECTOR_TX], 0,
	    myfirst_intr_simulate_vector_sysctl, "IU",
	    "Simulate tx vector interrupt");
}
```

The function builds three read-only per-vector counter sysctls and three write-only simulated-interrupt sysctls. The tree-style (Challenge 7) is left as an exercise.

### Lines of Code

The full `myfirst_msix.c` file is about 330 lines. That is a substantial addition to the driver, but it buys all of Chapter 20's capabilities: three-tier fallback, per-vector handlers, per-vector counters, CPU binding, clean teardown.

Compare to the Chapter 19 `myfirst_intr.c` which was about 250 lines. The Chapter 20 file is not much longer in absolute terms; the per-vector logic adds complexity but each piece is small.



## Reference: A Closing Note on Multi-Vector Philosophy

A paragraph to close the chapter with.

A multi-vector driver does not fundamentally differ from a single-vector driver. It has the same filter shape, the same task pattern, the same teardown ordering, the same lock discipline. What changes is the count: N filters instead of one, N teardowns instead of one, N tasks instead of one. The design's quality comes from how cleanly those N pieces coexist.

Chapter 20's lesson is that multi-vector handling is an exercise in symmetry. Each vector looks like every other vector at the structural level; each has its own counter, its own filter, its own description. The code that allocates, the code that handles, the code that tears down: all of them loop over the vectors and do the same thing N times. The simplicity of the loop is what makes the N-vector driver manageable; a driver where each vector is special is a driver that does not scale.

For this reader and for this book's future readers, the Chapter 20 multi-vector pattern is a permanent part of the `myfirst` driver's architecture and a permanent tool in the reader's toolkit. Chapter 21 assumes it: per-queue DMA rings, per-queue completion interrupts, per-queue CPU placement. The vocabulary is the vocabulary every high-performance FreeBSD driver shares; the patterns are the patterns the kernel's own test drivers use; the discipline is the discipline production drivers live by.

The skill Chapter 20 teaches is not "how to allocate MSI-X for virtio-rng-pci". It is "how to design a multi-vector driver, allocate its vectors, place them on CPUs, route events per vector, and tear it all down cleanly". That skill applies across every multi-queue device the reader will ever work on.
