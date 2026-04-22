---
title: "Handling Interrupts"
description: "Chapter 19 turns the Chapter 18 PCI driver into an interrupt-aware driver. It teaches what interrupts are, how FreeBSD models and routes them, how a driver claims an IRQ resource and registers a handler through bus_setup_intr(9), how to split work between a fast filter and a deferred ithread, how to handle shared IRQs safely with FILTER_STRAY and FILTER_HANDLED, how to simulate interrupts for testing without real IRQ events, and how to tear the handler down in detach. The driver grows from 1.1-pci to 1.2-intr, gains a new interrupt-specific file, and leaves Chapter 19 ready for MSI and MSI-X in Chapter 20."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 19
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Handling Interrupts

## Reader Guidance & Outcomes

Chapter 18 ended with a driver that had finally met real PCI hardware. The `myfirst` module at version `1.1-pci` probes a PCI device by vendor and device ID, attaches as a proper newbus child of `pci0`, claims the device's BAR through `bus_alloc_resource_any(9)` with `SYS_RES_MEMORY` and `RF_ACTIVE`, hands the BAR to the Chapter 16 accessor layer so `CSR_READ_4` and `CSR_WRITE_4` read and write real silicon, creates a per-instance cdev, and tears everything down in strict reverse order on detach. The Chapter 17 simulation is still in the tree but does not run on the PCI path; its callouts stay silent so they cannot write into the real device's registers.

What the driver does not yet do is react to the device. Every register access so far has been initiated by the driver: a user-space `read` or `write` hits the cdev, the cdev handler acquires `sc->mtx`, the accessor reads or writes the BAR, and control returns to user space. If the device itself has something to say, such as "my receive queue has a packet" or "a command completed" or "a temperature threshold was crossed", the driver has no way of hearing it. The driver polls; it does not listen.

That is what Chapter 19 fixes. Real devices talk by **interrupting** the CPU. The bus fabric carries a signal from the device to the interrupt controller, the interrupt controller dispatches the signal to a CPU, the CPU takes a short detour from whatever it was doing, and a handler the driver registered runs for a few microseconds. The handler's job is small: figure out what the device wants, acknowledge the interrupt at the device, do the little bit of work that is safe to do here, and hand the rest off to a thread that has the freedom to block, sleep, or take slow locks. The "hand off" is the second half of modern interrupt discipline; the first half is getting into the handler at all.

Chapter 19's scope is precisely the core interrupt path: what interrupts are at the hardware level, how FreeBSD models them in the kernel, how a driver allocates an IRQ resource and registers a handler through `bus_setup_intr(9)`, how the split between a fast filter handler and a deferred ithread handler works, what the `INTR_MPSAFE` flag commits the driver to, how to simulate interrupts for testing when real events are not easy to produce, how to behave correctly on a shared IRQ line that other drivers also listen to, and how to tear every one of these things down in detach without leaking resources or executing stale handlers. The chapter stops short of MSI and MSI-X, which belong to Chapter 20; those mechanisms build on the core handler the reader writes here, and teaching both at once would dilute both.

Chapter 19 holds back on territory that interrupt work naturally touches. MSI and MSI-X, per-vector handlers, interrupt coalescing, and per-queue interrupt routing are Chapter 20. DMA and the interaction between interrupts and DMA descriptor rings are Chapters 20 and 21. Advanced interrupt affinity strategies on NUMA platforms are discussed briefly but the deep treatment belongs to Chapter 20 and beyond. Platform-specific interrupt routing (GICv3 on arm64, APIC on x86, NVIC on embedded targets) is mentioned only for vocabulary; the book's focus is the driver-visible API that hides those differences. Chapter 19 stays inside the ground it can cover well and hands off explicitly where a topic deserves its own chapter.

The filter-plus-ithread model Chapter 19 teaches does not stand alone. Chapter 16 gave the driver a vocabulary of register access. Chapter 17 taught it to think like a device. Chapter 18 introduced it to a real PCI device. Chapter 19 gives it ears. Chapters 20 and 21 will give it legs: direct memory access so the device can reach into RAM without the driver in the loop. Each chapter adds one layer. Each layer depends on the ones before it. Chapter 19 is where the driver stops polling and starts listening, and the disciplines Part 3 built are what keep the listening honest.

### Why Interrupt Handling Earns a Chapter of Its Own

One concern that surfaces here is whether `bus_setup_intr(9)` and the filter-plus-ithread model really justify a full chapter. The Chapter 17 simulation used callouts to produce autonomous state changes; the Chapter 18 driver runs on real PCI but ignores the interrupt line entirely. Could we not just keep polling through callouts and avoid the topic?

Two reasons.

The first is performance. A callout that polls the device ten times a second wastes CPU time when there is nothing to do and misses events that happen between polls. A real device may produce many events per millisecond; a poll interval of 100 milliseconds misses nearly all of them. Interrupts invert the cost: no CPU is spent when nothing is happening, and the handler runs within microseconds of the event. Every serious driver in FreeBSD uses interrupts for the same reason; a driver that polls is a driver with a special excuse.

The second is correctness. Some devices require the driver to respond within a tight time window. A network card's receive FIFO fills in a handful of microseconds; if the driver does not drain it, the card drops packets. A serial port's transmit FIFO empties at the wire's rate; if the driver does not refill it, the transmitter starves. Polling at any interval long enough to be cheap is an interval short enough to miss deadlines. Interrupts are the only mechanism that lets a driver meet real-time device requirements without burning a CPU full time.

The chapter also earns its place by teaching a discipline that applies far beyond PCI. The FreeBSD interrupt model (filter plus ithread, `INTR_MPSAFE`, `bus_setup_intr(9)`, clean teardown in detach) is the same model USB drivers use, the same model SDIO drivers use, the same model virtio drivers use, and the same model arm64 SoC drivers use. A reader who understands Chapter 19's model can read any FreeBSD driver's interrupt handler with comprehension. That generality is what makes the chapter worth reading carefully even for readers who will not work on PCI.

### Where Chapter 18 Left the Driver

A short checkpoint before we go further. Chapter 19 extends the driver produced at the end of Chapter 18 Stage 4, tagged as version `1.1-pci`. If any of the items below feels uncertain, return to Chapter 18 before starting this chapter.

- Your driver compiles cleanly and identifies itself as `1.1-pci` in `kldstat -v`.
- On a bhyve or QEMU guest that exposes a virtio-rnd device (vendor `0x1af4`, device `0x1005`), the driver attaches through `myfirst_pci_probe` and `myfirst_pci_attach`, prints its banner, claims BAR 0 as `SYS_RES_MEMORY` with `RF_ACTIVE`, walks the PCI capability list, and creates `/dev/myfirst0`.
- The softc holds the BAR resource pointer (`sc->bar_res`), the resource ID (`sc->bar_rid`), and the `pci_attached` flag.
- The detach path destroys the cdev, quiesces any active callouts and tasks, detaches the hardware layer, releases the BAR, and deinit the softc.
- The full regression script from Chapter 18 passes: attach, exercise cdev, detach, unload, no leaks.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, and `PCI.md` are current.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel.

That driver is what Chapter 19 extends. The additions are again modest in volume: one new file (`myfirst_intr.c`), one new header (`myfirst_intr.h`), a small set of new softc fields (`irq_res`, `irq_rid`, `intr_cookie`, a counter or two), three new functions in the interrupt file (setup, teardown, the filter handler), one sysctl for simulated interrupts, a version bump to `1.2-intr`, and a short `INTERRUPTS.md` document. The mental-model change is again larger than the line count suggests: the driver finally has two threads of control instead of one, and the discipline that keeps them from stepping on each other is new.

### What You Will Learn

By the end of this chapter you will be able to:

- Explain what an interrupt is at the hardware level, the difference between edge-triggered and level-triggered signalling, and how a CPU's interrupt-handling flow gets from the device to a driver's handler.
- Describe how FreeBSD represents interrupt events: what an interrupt event (`intr_event`) is, what an interrupt thread (`ithread`) is, what a filter handler is, and why the split between filters and ithreads matters.
- Read the output of `vmstat -i` and `devinfo -v` and locate the interrupts your system is handling, their counts, and the drivers bound to each.
- Allocate an IRQ resource through `bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, using `rid = 0` on a legacy PCI line and (in Chapter 20) non-zero RIDs for MSI and MSI-X vectors.
- Register an interrupt handler through `bus_setup_intr(9)`, choosing between a filter handler (`driver_filter_t`), an ithread handler (`driver_intr_t`), or the filter-plus-ithread combination, and picking the right `INTR_TYPE_*` flag for the device's class of work.
- Write a minimal filter handler that reads the device's status register, acknowledges the interrupt at the device, returns `FILTER_HANDLED` or `FILTER_STRAY` appropriately, and cooperates with the kernel's interrupt machinery.
- Know what is safe inside a filter (spin locks only, no `malloc`, no sleeping, no blocking locks) and what the ithread relaxes (sleep mutexes, condition variables, `malloc(M_WAITOK)`), and why the constraints exist.
- Set `INTR_MPSAFE` only when you mean it, and understand what the flag commits the driver to (own synchronisation, no implicit Giant acquisition, the right to run on any CPU concurrently).
- Hand deferred work from a filter handler to a taskqueue task or to the ithread, and preserve the discipline that small urgent work happens in the filter while bulk work happens in thread context.
- Simulate interrupts through a sysctl that invokes the handler directly under the normal locking rules, so the reader can exercise the handler's state machine without needing a real IRQ to fire.
- Handle shared interrupt lines correctly: read the device's INTR_STATUS register first, decide whether this interrupt belongs to our device, return `FILTER_STRAY` if it does not, and avoid stealing another driver's work.
- Tear down the interrupt handler in detach with `bus_teardown_intr(9)` before releasing the IRQ with `bus_release_resource(9)`, and structure the detach path so no interrupt can fire against freed state.
- Recognise what an interrupt storm is, know how FreeBSD's `hw.intr_storm_threshold` machinery detects one, and understand the common device-side causes (failing to clear INTR_STATUS, edge-triggered lines mis-configured as level).
- Bind an interrupt to a specific CPU through `bus_bind_intr(9)` when affinity matters, and describe the interrupt to `devinfo -v` through `bus_describe_intr(9)` so operators can see which handler is on which CPU.
- Split the interrupt-related code into its own file, update the module's `SRCS` line, tag the driver as `1.2-intr`, and produce a short `INTERRUPTS.md` document that describes the handler's behaviour and the deferred-work discipline.

The list is long; each item is narrow. The point of the chapter is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 19 stays focused.

- **MSI and MSI-X.** `pci_alloc_msi(9)`, `pci_alloc_msix(9)`, vector allocation, per-vector handlers, and the MSI-X table layout are Chapter 20. Chapter 19 targets the legacy PCI INTx line allocated with `rid = 0`; the vocabulary transfers, but the per-vector mechanics do not.
- **DMA.** `bus_dma(9)` tags, scatter-gather lists, bounce buffers, cache coherence around DMA descriptors, and the way interrupts signal the completion of descriptor-ring transfers are Chapters 20 and 21. Chapter 19's handler reads a BAR register and decides what to do; it does not touch DMA.
- **Per-queue multi-queue networking.** Modern NICs have separate receive and transmit queues with separate MSI-X vectors and separate interrupt handlers. The `iflib(9)` framework builds on this; `em(4)`, `ix(4)`, and `ixl(4)` use it. Chapter 19's driver has one interrupt; Chapter 20 onwards develops the multi-queue story.
- **Deep interrupt affinity on NUMA hardware.** `bus_bind_intr` is introduced; elaborate strategies for pinning interrupts to CPUs near the device's PCIe root port are left for later chapters on scalability.
- **Driver suspend and resume around interrupts.** `bus_suspend_intr(9)` and `bus_resume_intr(9)` exist; they are mentioned for completeness but are not exercised in Chapter 19's driver.
- **Real-time interrupt-priority manipulation.** FreeBSD's `intr_priority(9)` and the `INTR_TYPE_*` flags influence ithread priority, but the book treats the priority system as a black box outside advanced-topics chapters.
- **Software-only interrupts (SWI).** `swi_add(9)` creates a pure software interrupt that a driver can schedule from arbitrary context. The chapter mentions SWIs when discussing deferred work, but the preferred modern pattern (a taskqueue) covers the same use cases with fewer footguns.

Staying inside those lines keeps Chapter 19 a chapter about core interrupt handling. The vocabulary is what transfers; the specific chapters that follow apply the vocabulary to MSI/MSI-X, DMA, and multi-queue designs.

### Estimated Time Investment

- **Reading only**: four to five hours. The interrupt model is conceptually small but requires careful reading, particularly around filter vs ithread and the safety rules inside a filter.
- **Reading plus typing the worked examples**: ten to twelve hours over two or three sessions. The driver evolves in four stages; each stage is a small but real extension on top of the Chapter 18 codebase.
- **Reading plus all labs and challenges**: sixteen to twenty hours over four or five sessions, including standing up the bhyve lab (if Chapter 18's setup is not already in place), reading `if_em.c`'s interrupt path and `if_mgb.c`'s filter handler, and running the Chapter 19 regression against both the simulated-interrupt path and (where possible) a real-interrupt path.

Sections 3, 4, and 6 are the densest. If the filter-versus-ithread split feels unfamiliar on first pass, that is normal. Stop, re-read Section 3's decision tree, and continue when the shape has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 18 Stage 4 (`1.1-pci`). The starting point assumes the Chapter 16 hardware layer, the Chapter 17 simulation backend, the Chapter 18 PCI attach, the complete `CSR_*` accessor family, the sync header, and every primitive introduced in Part 3.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- `bhyve(8)` or `qemu-system-x86_64` is available, and Chapter 18's lab environment (a FreeBSD guest with a virtio-rnd device at `-s 4:0,virtio-rnd`) is reproducible on demand.
- The `devinfo(8)`, `vmstat(8)`, and `pciconf(8)` tools are in your path. All three live in the base system.

If any item above is shaky, fix it now rather than pushing through Chapter 19 and trying to reason from a moving foundation. Interrupt bugs often manifest as kernel panics or subtle corruption under load; the debug kernel's `WITNESS` in particular catches the common classes of lock mistakes early.

### How to Get the Most Out of This Chapter

Four habits will pay off quickly.

First, keep `/usr/src/sys/sys/bus.h` and `/usr/src/sys/kern/kern_intr.c` bookmarked. The first file defines `driver_filter_t`, `driver_intr_t`, `INTR_TYPE_*`, `INTR_MPSAFE`, and the `FILTER_*` return values you will use in every handler. The second file is the kernel's interrupt-event machinery: the code that receives the low-level IRQ, dispatches to filters, wakes ithreads, and detects interrupt storms. You do not need to read `kern_intr.c` in depth, but skimming the top thousand lines once gives you a picture of what happens between "the device asserts IRQ 19" and "your filter gets called".

Second, run `vmstat -i` on your lab host and your guest, and keep the output open in a terminal as you read. Every concept Section 2 and Section 3 introduce (per-handler counts, per-CPU affinity, interrupt naming conventions) is visible in that output. A reader who has stared at `vmstat -i` for their own machine finds interrupt routing much less abstract.

Third, type the changes by hand and run each stage. Interrupt code is where small mistakes become silent bugs. Forgetting `FILTER_HANDLED` makes your handler technically illegal; forgetting `INTR_MPSAFE` silently acquires Giant around your handler; forgetting to clear INTR_STATUS produces an interrupt storm five milliseconds later. Typing each line, checking the `dmesg` output after each `kldload`, and watching `vmstat -i` between iterations is how you catch these mistakes at the point where they are cheap.

Fourth, read `/usr/src/sys/dev/mgb/if_mgb.c` (look for `mgb_legacy_intr` and `mgb_admin_intr`) after Section 4. `mgb(4)` is the driver for Microchip's LAN743x gigabit Ethernet controllers. Its interrupt path is a clean, readable example of filter-plus-ithread design, and it sits at about the level of complexity Chapter 19 teaches. Seven hundred lines of careful reading pays off across the rest of Part 4.

### Roadmap Through the Chapter

The sections in order are:

1. **What Are Interrupts?** The hardware picture: what an interrupt is, edge versus level triggering, the CPU's dispatch flow, and the minimum a driver must do when one arrives. The conceptual foundation.
2. **Interrupts in FreeBSD.** How the kernel represents interrupt events, what an ithread is, how interrupts are counted and displayed through `vmstat -i` and `devinfo -v`, and what happens from the IRQ line to the driver's handler.
3. **Registering an Interrupt Handler.** The code the driver writes: `bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, `bus_setup_intr(9)`, `INTR_TYPE_*` flags, `INTR_MPSAFE`, `bus_describe_intr(9)`. The first stage of the Chapter 19 driver (`1.2-intr-stage1`).
4. **Writing a Real Interrupt Handler.** The filter handler's shape: read INTR_STATUS, decide ownership, acknowledge the device, return the right `FILTER_*` value. The ithread handler's shape: take sleep locks, defer to a taskqueue, do the slow work. Stage 2 (`1.2-intr-stage2`).
5. **Using Simulated Interrupts for Testing.** A sysctl that invokes the handler synchronously with the real lock discipline, so the reader can exercise the handler without a real IRQ. Stage 3 (`1.2-intr-stage3`).
6. **Handling Shared Interrupts.** Why `RF_SHAREABLE` matters on a legacy PCI line, how a filter handler must decide ownership against other handlers on the same IRQ, and how to avoid starvation. No stage bump; this is a discipline, not a new code artefact.
7. **Cleaning Up Interrupt Resources.** `bus_teardown_intr(9)` first, then `bus_release_resource(9)`. The detach ordering now has two more steps, and the partial-failure cascade gets one more label.
8. **Refactoring and Versioning Your Interrupt-Ready Driver.** The final split into `myfirst_intr.c`, a new `INTERRUPTS.md`, the version bump to `1.2-intr`, and the regression pass. Stage 4.

After the eight sections come hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 19's story and opens Chapter 20's, and a bridge to Chapter 20. The reference-and-cheat-sheet material at the end of the chapter is meant to be re-read as you work through Chapter 20 and 21; Chapter 19's vocabulary is the foundation both of them build on.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3 and 4 stand alone and make good single-sitting reads.



## Section 1: What Are Interrupts?

Before the driver code, the hardware picture. Section 1 teaches what an interrupt is at the level of the CPU and the bus, without any FreeBSD-specific vocabulary. A reader who understands Section 1 can read the rest of the chapter with the kernel's interrupt path as a concrete object rather than a vague abstraction. The payoff is that every later section is easier.

The one-sentence summary, which you can keep with you through the rest of the chapter: an interrupt is a way for a device to interrupt the CPU's current work, run a driver's handler for a short time, and then let the CPU go back to what it was doing. Everything else is mechanism around that sentence.

### The Problem Interrupts Solve

A CPU runs a stream of instructions in order. Each instruction completes, the program counter advances, the next instruction runs, and so on. Left alone, the CPU would execute one program until that program finished, then another, and so on, never paying attention to anything happening outside its own instruction stream.

That is not how computers work. A keyboard pressed for half a second generates four or five separate events; a network packet arrives within a few microseconds of the previous one; a disk finishes a read, the fan controller crosses a temperature threshold, a sensor value updates, a timer expires. Each of these events happens outside the CPU's direct control, at a time the CPU did not choose. The CPU has to notice.

One way to notice is to poll. The CPU can look at the device's status register periodically. If the status register says "I have data", the CPU reads the data. If the status register says "I have nothing", the CPU moves on. Polling works for devices whose events are rare, predictable, and not time-sensitive. It works badly for everything else. A keyboard polled every hundred milliseconds feels sluggish. A network card polled every millisecond still misses most of its packets. And polling consumes CPU time proportional to the poll rate, even when nothing is happening.

The other way to notice is to let the device tell the CPU. That is an interrupt. The device raises a signal on a wire or sends a message over the bus. The CPU interrupts its current work, remembers where it was, runs a small piece of code that asks the device what happened, responds appropriately, and then resumes the work it was doing. The discipline for writing that "small piece of code" is what the rest of Chapter 19 teaches.

### What a Hardware Interrupt Actually Is

Physically, a hardware interrupt starts as a signal on a wire (or, more commonly on modern systems, as a message on a bus). A device asserts the signal when something has happened that the OS needs to know about. Examples include:

- A network card asserts its IRQ line when a packet has arrived and is sitting in its receive FIFO.
- A serial UART asserts its IRQ line when a byte has arrived at the receiver, or when the transmit FIFO has fallen below a threshold.
- A SATA controller asserts its IRQ line when a command queue entry has completed.
- A timer chip asserts its IRQ line when a programmed interval has elapsed.
- A temperature sensor asserts its IRQ line when a programmed threshold is crossed.

The assertion is the device's way of saying "something I need you to know about". The CPU and the OS must be prepared to respond. The path from "signal asserted" to "handler called" passes through the interrupt controller, the CPU's interrupt dispatch mechanism, and the kernel's interrupt-event machinery. Section 2 walks the whole path; this subsection stays at the hardware level.

Several useful facts about the signalling itself.

First, **interrupt lines are usually shared**. A CPU has a small number of interrupt inputs, often sixteen to twenty-four on legacy PCs and more on modern platforms through APICs and GICs. A system typically has more devices than interrupt inputs, so multiple devices share a single line. When an interrupt fires on a shared line, each driver whose device might be the source has to check: is this my interrupt? If no, return a "stray" indication; if yes, handle it. Section 6 covers the shared-interrupt protocol.

Second, **interrupt signalling comes in two flavours**. Edge-triggered signalling means the interrupt is signalled by a transition on the wire (low to high, or high to low). Level-triggered signalling means the interrupt is signalled by holding the wire at a specific level (high or low) for as long as the interrupt is pending. The two flavours have different operational consequences, which the next subsection explores.

Third, **the interrupt is asynchronous with respect to the CPU**. The CPU does not know when the device will raise the signal. The driver's handler must tolerate being called at any point in the driver's own work, and must synchronise with its own non-interrupt code appropriately. Chapter 11's locking discipline is what the driver uses to do that.

Fourth, **the interrupt carries essentially no information on its own**. The wire says "something happened"; it does not say what. The driver discovers what happened by reading the device's status register. A single IRQ line can report many different events (receive data ready, transmit FIFO empty, link state change, error, and so on), and it is the driver's job to decode the status bits and decide what to do.

### Edge-Triggered vs Level-Triggered

The difference is worth understanding because it explains why certain bugs produce interrupt storms, certain bugs produce silently-dropped interrupts, and certain bugs produce stuck systems.

An **edge-triggered** interrupt fires once when the signal transitions. The device pulls the wire low (for an active-low line); the interrupt controller notices the transition; an interrupt is queued for the CPU. If the device continues to hold the wire low, no additional interrupts fire, because the signal is not transitioning, only continuing to be asserted. For a new interrupt to fire, the device must release the wire and then assert it again.

Edge-triggered interrupts are efficient. The interrupt controller only needs to track transitions, not ongoing signals. The downside is fragility: if an interrupt fires while the controller is not watching (because another interrupt is being handled, for example), the transition may be missed. Modern interrupt controllers queue edge-triggered interrupts to avoid most of this, but the risk is real, and some drivers (or some device bugs) produce edge-triggered setups that occasionally drop an event.

A **level-triggered** interrupt fires continuously while the signal is asserted. As long as the device holds the wire at the asserted level, the interrupt controller reports an interrupt. When the device releases the wire, the interrupt controller stops reporting. The CPU sees an interrupt, the driver's handler runs, the handler reads the device's status and clears the pending condition, the device stops asserting the signal, and the interrupt controller stops reporting. If the handler fails to clear the pending condition, the signal stays asserted, the interrupt controller keeps reporting, and the driver's handler gets called again immediately, in a loop that consumes the CPU. This is the classic **interrupt storm**.

Level-triggered interrupts are robust. As long as the device has something to report, the OS will know; there is no window where an event can be missed. The cost is that a buggy driver can produce a storm. FreeBSD has storm detection to mitigate this (the appendix *A Deeper Look at Interrupt Storm Detection* later in the chapter covers it); other operating systems have similar protections. The common rule of thumb: level-triggered is the safer default, and PCI's legacy INTx lines are level-triggered for that reason.

The distinction matters for driver authors in a few specific places:

- A driver that fails to clear the device's INTR_STATUS register before returning from the handler will produce an interrupt storm on a level-triggered line. On an edge-triggered line the same bug produces a lost interrupt instead.
- A driver that reads and writes INTR_STATUS correctly works on both types without special knowledge.
- A driver that manipulates the interrupt controller's trigger mode directly (rare; mostly legacy) must understand the distinction.

For Chapter 19's PCI driver the signalling is level-triggered INTx on the legacy path. On MSI and MSI-X (Chapter 20) the signalling is message-based and does not correspond directly to either edge or level, but the driver pattern is the same: read status, acknowledge the device, return.

### The CPU's Interrupt Handling Flow, Simplified

What happens, step by step, when a device's IRQ line is asserted? A simplified trace on a modern x86 system:

1. The device asserts its IRQ on the bus (or sends an MSI packet, for PCIe with MSI enabled).
2. The system's interrupt controller (APIC on x86, GIC on arm64) receives the signal and determines which CPU should handle it, based on the configured affinity. On multi-CPU systems this is a steerable decision.
3. The chosen CPU's interrupt hardware detects the pending interrupt. Before completing the current instruction, the CPU saves enough state (the program counter, the flags register, and a few other fields) to return to the interrupted work later.
4. The CPU jumps to a vector in its interrupt descriptor table. The entry for this vector is a small piece of kernel code called a **trap stub** that transitions to supervisor mode, saves the interrupted thread's register set, and calls into the kernel's interrupt dispatch code.
5. The kernel's interrupt dispatch code finds the `intr_event` associated with the IRQ (this is the FreeBSD structure Section 2 covers) and calls the driver handlers attached to it.
6. The driver's filter handler runs. It reads the device's status register, decides what kind of event has occurred, writes the device's INTR_STATUS register to acknowledge the event (so the device stops asserting the line, for level-triggered), and returns a value that tells the kernel what to do next.
7. If the filter returned `FILTER_SCHEDULE_THREAD`, the kernel schedules the ithread associated with this interrupt. The ithread is a kernel thread that wakes up, runs the driver's secondary handler, and goes back to sleep.
8. After all handlers have run, the kernel sends an End-of-Interrupt (EOI) signal to the interrupt controller, which re-arms the IRQ line.
9. The CPU returns from the interrupt. The interrupted thread's register set is restored and the thread resumes at the instruction that was about to execute when the interrupt arrived.

Steps 3 through 9 take a few microseconds on modern hardware for a simple handler. The whole flow is invisible to the interrupted thread: code that was not designed to be interrupt-safe (say, a floating-point computation in user space) runs correctly on either side of the interrupt, because the CPU saves and restores its state around the whole sequence.

From the driver author's perspective, steps 1 through 5 are the kernel's concern; steps 6 and 7 are where the driver code runs. The driver's handler must be fast (step 8's EOI waits for it), must not sleep (the interrupted thread holds CPU resources), and must not take locks that could block indirectly on the interrupted thread. Section 2 will make these constraints precise in FreeBSD terms; Section 1 has now set up the mental model.

### What a Driver Must Do When an Interrupt Arrives

The driver's obligations on an interrupt are small in number but not small in detail:

1. **Identify the cause.** Read the device's interrupt status register. If no bit is set (the device has no pending interrupt), this is a shared-IRQ spurious call; return `FILTER_STRAY` and let the kernel try the next handler on the line.
2. **Acknowledge the interrupt at the device.** Write the status bits back (typically by writing a 1 to each bit, since most INTR_STATUS registers are RW1C) so the device deasserts the line and the interrupt controller can re-enable the IRQ. Not every device requires the acknowledgment to be inside the filter, but doing it here is the safe default; the level-triggered storm story depends on timely acknowledgment.
3. **Decide what work to do.** Read enough of the device to decide. Was this a receive event? A transmit completion? An error? A link change? The status bits tell you.
4. **Do the urgent small work.** Update a counter. Copy a byte from a FIFO into a queue. Toggle a control bit. Whatever can be done in microseconds without taking a sleep lock is fair game here.
5. **Defer the bulk work.** If the event triggers a long operation (processing a received packet, decoding a data stream, sending a command to user space), schedule an ithread or a taskqueue task and return. The deferred work runs in thread context, where it can take sleep locks, allocate memory, and take its time.
6. **Return the appropriate FILTER_* value.** `FILTER_HANDLED` means the interrupt is entirely handled; no ithread is needed. `FILTER_SCHEDULE_THREAD` means the ithread should run. `FILTER_STRAY` means the interrupt was not for this driver. These three values are the vocabulary the kernel uses to dispatch further work.

A driver that does these six things correctly on every interrupt has the shape the rest of Chapter 19 teaches. A driver that skips any one of them has a bug.

### Real-World Examples

A short tour of the events Chapter 19's vocabulary will cover.

**Keypresses.** A PS/2 keyboard controller fires an interrupt when a scancode arrives. The driver reads the scancode, passes it to the keyboard subsystem, and acknowledges. The whole handler runs in a few microseconds; a taskqueue is usually unnecessary.

**Network packets.** A NIC fires an interrupt when packets accumulate in the receive queue. The driver's filter reads a status register to confirm receive events, schedules the ithread, and returns. The ithread walks the descriptor ring, constructs `mbuf` packets, and passes them up the network stack. The split between filter and ithread matters here because stack processing is slow enough that running it in the filter would extend the interrupt window too far.

**Sensor readings.** An I2C-attached temperature sensor fires an interrupt when a new measurement is ready. The driver reads the value, updates a sysctl cache, optionally wakes up any pending user-space readers, and acknowledges. Simple and fast.

**Serial port.** A UART fires an interrupt on receive or transmit-FIFO-empty conditions. The driver drains or refills the FIFO, updates a circular buffer, and acknowledges. At high baud rates this can happen tens of thousands of times per second, so the handler must be tight.

**Disk completion.** A SATA or NVMe controller fires an interrupt when a queued command completes. The driver walks the completion queue, matches each completion to a pending I/O request, wakes the waiting thread, and acknowledges. The matching and waking is sometimes split between filter and ithread.

Each of these devices reaches Chapter 19's vocabulary in the same way: the filter reads status, the filter decides what happened, the filter acknowledges, and the filter either handles or defers. The specific register layout differs; the pattern does not.

### A Quick Exercise: Find Interrupt-Driven Devices on Your Lab Host

Before moving to Section 2, a short exercise to make the hardware picture concrete.

On your lab host, run:

```sh
vmstat -i
```

The output is a list of interrupt sources with their counts since boot. Each line looks roughly like this:

```text
interrupt                          total       rate
cpu0:timer                      1234567        123
cpu1:timer                      1234568        123
irq9: acpi0                          42          0
irq19: uhci0+                     12345         12
irq21: ahci0                      98765         99
irq23: em0                       123456        123
```

Pick three lines from your own output. For each, identify:

- The interrupt name (a mix of the IRQ number and the driver-set description).
- The total count (how many times the interrupt has fired since boot).
- The rate (interrupts per second; a high rate means the device is busy).

Run `vmstat -i` a second time ten seconds later. Compare the counts. Which interrupts are actively counting? Which ones are essentially idle?

Now match interrupts to devices with `devinfo -v`:

```sh
devinfo -v | grep -B 2 irq
```

Each match shows a device that claims an IRQ. Cross-check against `vmstat -i`'s output to see which driver is served by each line.

Keep this output open while you read Section 2. The `em0` entry in the example is the Intel Ethernet controller; if you are using an Intel-based system with FreeBSD, `em0` or `igc0` or `ix0` is probably running a version of the same pattern Chapter 19 teaches. A modern NUC running FreeBSD 14.3 shows one or two dozen interrupt sources; a server shows many more. The system you actually have is more interesting to stare at than any diagram.

### A Brief History of Interrupts

Interrupts are one of the oldest ideas in computer architecture. The original PDP-1 supported them in 1961 as a way for I/O devices to signal the CPU without the CPU polling. The IBM 704 had them at around the same time. Early timesharing systems used interrupts for the clock tick that drove scheduling, and for every I/O completion.

Through the 1970s and 1980s, personal computers inherited the pattern. The original IBM PC used the 8259 Programmable Interrupt Controller (PIC), which supported eight IRQ lines; the PC/AT extended this to fifteen usable lines by cascading two PICs. The x86 instruction set added specific interrupt-handling instructions (`CLI`, `STI`, `INT`, `IRET`) that persist today in extended forms.

PCI introduced the concept of a device advertising its interrupt through configuration space (the `INTLINE` and `INTPIN` fields Chapter 18 discussed). PCIe added MSI and MSI-X, which replace the physical IRQ line with a memory-write message. All three coexist in modern systems; Chapter 19's legacy INTx is the oldest of the three and the only one that shares lines.

Operating systems have evolved alongside. Early Unix was monolithic and single-threaded in the kernel; interrupts preempted whatever was running. Modern kernels (FreeBSD included) have fine-grained locking, per-CPU data structures, and ithread-based deferred dispatch. The handler discipline Chapter 19 teaches is the distillation of that evolution: fast in the filter, slow in the task, MP-safe by default, shareable, debuggable.

Knowing the history is not required for writing a driver. But the vocabulary (IRQ, PIC, EOI, INTx) comes from specific points in the history, and a driver author who knows where the words come from finds less of the field mysterious.

### Wrapping Up Section 1

An interrupt is a way for a device to interrupt the CPU's current work, run a small piece of driver code, and let the CPU resume. The mechanism runs through the interrupt controller, the CPU's interrupt dispatch, the kernel's interrupt-event machinery, and finally the driver's handler. Edge-triggered and level-triggered signalling have different operational consequences, most visibly in the form of interrupt storms when a level-triggered line is not properly acknowledged.

A driver's handler has six obligations: identify the cause, acknowledge the device, decide the work, do the urgent part, defer the bulk part, and return the right `FILTER_*` value. Each of these is small in isolation and demanding in aggregate; the rest of Chapter 19 is about doing each one correctly in FreeBSD terms.

Section 2 now walks the FreeBSD kernel's interrupt model: what an `intr_event` is, what an ithread is, how `vmstat -i` and `devinfo -v` expose the kernel's view of interrupts, and what constraints the model places on driver handlers.



## Section 2: Interrupts in FreeBSD

Section 1 established the hardware model. Section 2 introduces the software model. The kernel's interrupt machinery is the layer between the interrupt controller and the driver's handler; understanding it clearly is what turns the hardware model into a driver-writable concept. A reader who finishes Section 2 should be able to answer three questions in plain English: what runs when an interrupt fires, what runs later as deferred work, and what the driver has to promise so both can happen safely.

### The FreeBSD Interrupt Model in One Picture

A driver's handler does not run in isolation. It runs inside a small ecosystem of kernel objects that together make interrupt handling orderly and debuggable. The ecosystem has three pieces worth naming up front.

The first is the **interrupt event**, represented by `struct intr_event` in `/usr/src/sys/sys/interrupt.h` (with the dispatch code in `/usr/src/sys/kern/kern_intr.c`). One `intr_event` exists per IRQ line (or per MSI vector, in Chapter 20's world). It is the central coordinator: it holds a list of handlers (the driver's filter functions and ithread functions), a human-readable name, flags, a loop counter used for storm detection (`ie_count`), a rate-limiter for warning messages (`ie_warntm`), and a CPU binding. When the interrupt controller reports an IRQ to the kernel, the kernel looks up the corresponding `intr_event` and walks its list of handlers. Stray interrupts are counted globally, not per-event; they surface through `vmstat -i`'s separate accounting and through kernel log messages rather than through a field on the event.

The second is the **interrupt handler**, represented by `struct intr_handler`. One `intr_handler` exists per registered handler on an `intr_event`. A single IRQ line can have many handlers (one per driver that shares the line). The handler carries the driver-provided filter function (if any), the driver-provided ithread function (if any), the `INTR_*` flags (most importantly `INTR_MPSAFE`), and a cookie pointer the kernel keeps for the driver's reference.

The third is the **interrupt thread**, usually called an **ithread**, represented by `struct intr_thread`. Unlike the event and the handler, the ithread is a real kernel thread with its own stack, its own scheduling priority, and its own `proc` structure. When a filter returns `FILTER_SCHEDULE_THREAD` (or when a driver registered an ithread-only handler with no filter), the ithread is scheduled to run. The ithread then calls the driver's handler function in thread context, where regular sleep mutexes and sleeping are permitted.

The three together produce the classic two-phase interrupt pattern FreeBSD has used for more than a decade: a fast filter runs in primary interrupt context to do the urgent work, and a thread-context handler runs later to do the slow work. Chapter 19's driver will use both phases.

### IRQ Assignment and Routing

A modern x86 system has more than one interrupt controller. The legacy 8259 PIC has been superseded by the Local APIC (one per CPU, handling per-CPU interrupts like the local timer) and the I/O APIC (a shared unit that receives IRQs from the I/O fabric and routes them to CPUs). On arm64, the equivalent is the Generic Interrupt Controller (GIC), with per-CPU redistributors and a shared distributor. On embedded targets there are a handful of other controllers. FreeBSD abstracts these behind the `intr_pic(9)` interface; a driver author rarely interacts with the interrupt controller directly.

What the driver sees is an IRQ number (on legacy PCI paths, the number a BIOS assigned in configuration space) or a vector index (on MSI and MSI-X paths). The driver asks for an IRQ resource by that number, the kernel allocates a `struct resource *`, and the driver hands the resource to `bus_setup_intr(9)` to attach a handler. The kernel does the work of wiring the handler to the right `intr_event`, configuring the interrupt controller to route the IRQ to a CPU, and arming the line.

From a driver author's perspective, IRQ routing is usually a black box. The kernel handles it; the driver sees a handle and a handler. One exception: on platforms with many CPUs and multiple devices, **affinity** matters. An interrupt that fires on a CPU far from the device produces cache misses and cross-socket traffic; an interrupt that fires on a CPU close to the device is cheaper. `bus_bind_intr(9)` lets the driver ask for a specific CPU; operators use `cpuset -x <irq> -l <cpu>` to override the affinity at runtime, and `cpuset -g -x <irq>` to query it. The appendix *A Deeper Look at CPU Affinity for Interrupts* later in the chapter covers both paths in more detail.

### SYS_RES_IRQ: The Interrupt Resource

Chapter 18 introduced three resource types: `SYS_RES_MEMORY` for memory-mapped BARs, `SYS_RES_IOPORT` for I/O port BARs, and (mentioned in passing) `SYS_RES_IRQ` for interrupts. Section 3 will use the third one for the first time. The vocabulary is the same as for BARs:

```c
int rid = 0;                  /* legacy PCI INTx */
struct resource *irq_res;

irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
    RF_SHAREABLE | RF_ACTIVE);
```

Three things are worth calling out about this allocation.

First, **`rid = 0`** is the convention for a legacy PCI INTx line. The PCI bus driver treats the zeroth IRQ resource as the device's legacy interrupt, set up from the `PCIR_INTLINE` field in configuration space. For MSI and MSI-X (Chapter 20), the rid is 1, 2, 3, and so on, corresponding to allocated vectors.

Second, **`RF_SHAREABLE`** asks the kernel to allow the IRQ line to be shared with other drivers. On legacy PCI this is the common case: one physical line can serve multiple devices. Without `RF_SHAREABLE`, the allocation fails if another driver already holds a handler on the same line. Passing `RF_SHAREABLE` does not mean your driver must handle stray interrupts; it means it must tolerate them. Section 6 is about exactly that tolerance.

Third, **`RF_ACTIVE`** activates the resource in one step, as with BAR allocation. Without it, the driver would need to call `bus_activate_resource(9)` separately. Chapter 19 always uses `RF_ACTIVE`.

On success, the returned `struct resource *` is a handle to the IRQ. It is not the IRQ number; the kernel does not expose that. The driver passes the handle to `bus_setup_intr(9)` and `bus_teardown_intr(9)` and `bus_release_resource(9)`.

### Filter Handlers vs ithread Handlers

This is the conceptual heart of Section 2 and of the chapter. A reader who internalises the filter-versus-ithread distinction reads every FreeBSD driver's interrupt code with comprehension.

A **filter handler** is a C function registered by the driver that runs in primary interrupt context. Primary interrupt context means: the CPU jumped directly from whatever it was doing, minimal state was saved, and the filter is running with the interrupted thread's context still partially in place. Specifically:

- The filter cannot sleep. There is no thread to block; the kernel is in the middle of dispatching an interrupt.
- The filter cannot acquire a sleep mutex (`mtx(9)` by default is a sleep-ish adaptive mutex, which can spin briefly but will eventually sleep). Spin mutexes (`mtx_init` with `MTX_SPIN`) are safe.
- The filter cannot allocate memory with `M_WAITOK`; it can use `M_NOWAIT`, which may fail.
- The filter cannot call into code that uses any of the above.

The filter is supposed to be fast (microseconds), do the urgent work (read status, acknowledge, update a counter), and return. The kernel's return value convention is:

- `FILTER_HANDLED`: the interrupt is mine, it is fully handled, no ithread is needed.
- `FILTER_SCHEDULE_THREAD`: the interrupt is mine, some of the work is done, schedule the ithread for the rest.
- `FILTER_STRAY`: the interrupt is not mine; try the next handler on this line.

A driver can also specify `FILTER_HANDLED | FILTER_SCHEDULE_THREAD` to say both "I handled part of it" and "schedule the thread for more".

An **ithread handler** is a different C function that runs in thread context. The thread is scheduled by the kernel after a filter returns `FILTER_SCHEDULE_THREAD`, or if the handler is registered as ithread-only (with no filter), the ithread is scheduled automatically when the kernel dispatches the interrupt.

In ithread context, the constraints loosen considerably:

- The ithread can sleep briefly on a mutex or condition variable.
- The ithread can use `malloc(M_WAITOK)`.
- The ithread can call into most kernel APIs that use sleepable locks.
- The ithread cannot still sleep arbitrarily long (it is a real-time-ish thread), but it can do normal driver work.

The split lets a driver separate urgent, short work (in the filter) from slower, bulk work (in the ithread). A network driver's filter might read the status register and acknowledge; its ithread walks the receive descriptor ring and passes packets up the stack. A disk driver's filter might record which completions happened and acknowledge; its ithread matches completions to pending requests and wakes waiting threads.

### When to Use Filter Only

A driver uses filter-only when every piece of work it does on an interrupt can run in primary interrupt context. Examples:

- **A minimal test driver** that increments a counter on each interrupt and nothing more.
- **A simple sensor driver** that reads one register, caches the value, and wakes a sysctl reader. (If `selwakeup` or a condition-variable broadcast requires a sleep mutex, this moves to filter-plus-ithread.)
- **A timer driver** whose job is to tick some kernel-internal counter.

The Chapter 19 driver at Stage 1 is filter-only: it reads INTR_STATUS, acknowledges, updates a counter, and returns `FILTER_HANDLED`. That is enough to prove the handler is wired up.

### When to Use Filter Plus ithread

A driver uses filter-plus-ithread when the interrupt requires urgent small work followed by slower bulk work. Examples:

- **A NIC driver.** The filter acknowledges and marks which queues have events. The ithread walks the descriptor rings, builds mbufs, and passes packets up.
- **A disk controller.** The filter reads the completion status and acknowledges. The ithread matches completions to I/O requests and wakes waiters.
- **A USB host controller.** The filter reads the status and acknowledges. The ithread walks the transfer descriptor list and completes any pending URBs.

The Chapter 19 driver at Stage 2 moves to filter-plus-ithread when simulated "work-requested" events are added; the filter records the event, and a deferred worker (via a taskqueue; Chapter 14's primitive) handles the work.

### When to Use ithread Only

A driver uses ithread-only when every piece of work must run in thread context. This is less common; the usual reason is that the driver needs to take a sleep mutex on every interrupt and cannot usefully do anything in primary context.

Registering an ithread-only handler is simple: pass `NULL` for the filter argument of `bus_setup_intr(9)`. The kernel schedules the ithread whenever the interrupt fires.

The Chapter 19 driver does not use ithread-only; the filter is always cheap to run.

### INTR_MPSAFE: What the Flag Promises

`INTR_MPSAFE` is a bit in the flags argument of `bus_setup_intr(9)`. Setting it promises the kernel two things:

1. Your handler does its own synchronisation. The kernel will not acquire the Giant lock around it.
2. Your handler is safe to run concurrently on multiple CPUs (for a handler shared by multiple CPUs, which happens in MSI-X scenarios and in some PIC configurations).

If you do **not** set `INTR_MPSAFE`, the kernel acquires Giant before calling your handler. This is the old BSD default, preserved for backward compatibility with pre-SMP drivers that relied on Giant's implicit protection. Modern drivers always set `INTR_MPSAFE`.

Failing to set `INTR_MPSAFE` has a visible symptom: the `dmesg` banner on `kldload` includes a line like `myfirst0: [GIANT-LOCKED]`. This is the kernel telling you that Giant is being acquired around your handler. On production systems it serialises every interrupt through a single lock, which is disastrous for scalability. The line is a deliberate nag from `bus_setup_intr` to help you notice.

Setting `INTR_MPSAFE` when you actually still rely on Giant is also a bug, but a quieter one. The kernel will not acquire Giant, so any code path that used to be serialised by Giant will no longer be. Race conditions appear where none existed before. The fix is not to remove `INTR_MPSAFE` (that masks the bug); the fix is to add the right locking to the handler and the code it touches.

Chapter 19's driver always sets `INTR_MPSAFE` and relies on the existing `sc->mtx` for synchronisation. The Chapter 11 discipline carries through.

### INTR_TYPE_* Flags

In addition to `INTR_MPSAFE`, `bus_setup_intr` takes a category flag that hints at the interrupt's class:

- `INTR_TYPE_TTY`: tty and serial devices.
- `INTR_TYPE_BIO`: block I/O (disk, CD-ROM).
- `INTR_TYPE_NET`: network.
- `INTR_TYPE_CAM`: SCSI (CAM framework).
- `INTR_TYPE_MISC`: miscellaneous.
- `INTR_TYPE_CLK`: clock and timer interrupts.
- `INTR_TYPE_AV`: audio and video.

The category influences the ithread's scheduling priority. Historically, each category had a distinct priority; in modern FreeBSD, only `INTR_TYPE_CLK` gets an elevated priority, and the rest are approximately equal. The category is still worth setting correctly because it flows through `devinfo -v` and `vmstat -i` output, making the interrupt self-identifying.

For the Chapter 19 driver, `INTR_TYPE_MISC` is appropriate because the demo target does not fit any of the more specific categories. Chapter 20 will use `INTR_TYPE_NET` once the driver starts targeting NICs in labs.

### Shared vs Exclusive Interrupts

On legacy PCI, multiple devices can share a single INTx line. The kernel tracks this with two resource flags:

- `RF_SHAREABLE`: this driver is willing to share the line with other drivers.
- `RF_SHAREABLE` absent: this driver wants the line for itself; allocation fails if another driver already holds it.

A driver that wants shareable interrupts uses `RF_SHAREABLE | RF_ACTIVE` in its `bus_alloc_resource_any` call. A driver that wants exclusive access (perhaps for latency reasons) uses `RF_ACTIVE` alone, but the request may fail on crowded systems.

The kernel never stops the driver from sharing; it stops another driver from joining if this one asks for exclusivity. On modern PCIe with MSI-X, sharing is much less common because each device has its own message-signalled vector.

Chapter 19's driver sets `RF_SHAREABLE` because virtio-rnd in bhyve may or may not share its line with other bhyve-emulated devices, depending on the slot topology. Being shareable is the safe default.

The `INTR_EXCL` flag passed through `bus_setup_intr`'s flags field (not to be confused with the resource-allocation flag) is a related but distinct concept: it asks the bus to give the handler exclusive access at the interrupt-event level. Legacy PCI drivers rarely need it. Some bus drivers use it internally. For Chapter 19's driver, we do not set `INTR_EXCL`.

### What vmstat -i Shows

`vmstat -i` prints the kernel's interrupt counters. Each line corresponds to one `intr_event`. The columns are:

- **interrupt**: a human-readable identifier. For hardware interrupts, the name is derived from the IRQ number and the driver description. `devinfo -v`-style names (such as `em0:rx 0`) appear when MSI-X vectors are in use.
- **total**: the number of times this interrupt has fired since boot.
- **rate**: the interrupt rate in interrupts per second, averaged over a recent window.

A few interpretation notes. A `total` column that grows quickly for an idle device is a red flag (interrupt storm). A `rate` column at zero for a device that should be handling traffic suggests the handler is not wired up correctly. When several devices share a legacy INTx line, `vmstat -i` shows one line per `intr_event` (per IRQ source), and the driver name on that line is the description of the first registered handler; the other drivers sharing the line do not get their own lines. When a device has its own MSI or MSI-X vectors, each vector is its own `intr_event`, and each gets its own line. Per-CPU interrupts such as the local timer appear as distinct per-CPU lines (`cpu0:timer`, `cpu1:timer`) because the kernel creates one event per CPU for them.

The kernel exposes the same counters through `sysctl hw.intrcnt` and `sysctl hw.intrnames`, which are the raw data `vmstat -i` formats. A driver author rarely reads these directly; `vmstat -i` is the friendly view.

### What devinfo -v Shows About Interrupts

`devinfo -v` walks the newbus tree and prints each device with its resources. For a PCI driver with an interrupt, the resources list includes an `irq:` entry next to the `memory:` entry:

```text
myfirst0
    pnpinfo vendor=0x1af4 device=0x1005 ...
    resources:
        memory: 0xc1000000-0xc100001f
        irq: 19
```

The number after `irq:` is the kernel's IRQ identifier. On x86 it is often a pin number from the I/O APIC; on arm64 it is a GIC vector; the exact meaning is platform-specific but the number is stable across reboots of the same system.

Matching the `irq: 19` to `vmstat -i`'s `irq19: ` entry confirms the driver is attached to the expected interrupt line.

For MSI-X interrupts (Chapter 20), each vector has its own `irq:` entry, and `devinfo -v` lists them individually.

### A Simple Interrupt Path Diagram

Putting it all together, here is what happens from device to driver:

```text
  Device        IRQ line          Interrupt         CPU        intr_event         Handler
 --------     -----------       -controller-      --------   --------------      ---------
   |              |                  |                |             |                 |
   | asserts     |                  |                |             |                 |
   | IRQ line    | signal           |                |             |                 |
   |------------>|                  |                |             |                 |
   |             | latch            |                |             |                 |
   |             |----------------->|                |             |                 |
   |             |                  | steer to CPU   |             |                 |
   |             |                  |--------------->|             |                 |
   |             |                  |                | save state  |                 |
   |             |                  |                | jump vector |                 |
   |             |                  |                |             | look up         |
   |             |                  |                |------------>|                 |
   |             |                  |                |             | for each        |
   |             |                  |                |             | handler         |
   |             |                  |                |             |---------------->|
   |             |                  |                |             |                 | filter runs
   |             |                  |                |             |<----------------|
   |             |                  |                |             | FILTER_HANDLED  |
   |             |                  |                |             | or              |
   |             |                  |                |             | FILTER_SCHEDULE |
   |             |                  |                | EOI         |                 |
   |             |                  |<---------------|             |                 |
   |             |                  |                | restore     |                 |
   |             |                  |                | state       |                 |
   |             |                  |                | resume thread                 |
   |             |                  |                |             | ithread wakeup  |
   |             |                  |                |             | (if scheduled)  |
   |             |                  |                |             |                 | ithread runs
   |             |                  |                |             |                 | slower work
```

The diagram elides several details (interrupt coalescing, the swapping of the interrupted thread's stack for the ithread's stack, the ithread's own scheduling), but it captures the shape. A filter runs in interrupt context, an ithread (if scheduled) runs later in thread context, the EOI happens after filters are done, and the interrupted thread resumes once the CPU is free.

### Constraints on What Handlers May Do

A short consolidated list of what a filter handler may and may not do. This is the most-referenced list in Chapter 19; mark it for return visits.

**Filter handlers MAY:**

- Read and write device registers through the accessor layer.
- Acquire spin mutexes (`struct mtx` initialised with `MTX_SPIN`).
- Read softc fields that are protected only by spin locks.
- Call the kernel's atomic operations (`atomic_add_int`, etc.).
- Call `taskqueue_enqueue(9)` to schedule thread-context work.
- Call `wakeup_one(9)` to wake a thread sleeping on a channel, if the context permits (most do).
- Return `FILTER_HANDLED`, `FILTER_SCHEDULE_THREAD`, `FILTER_STRAY`, or combinations.

**Filter handlers MAY NOT:**

- Acquire a sleep mutex (`struct mtx` initialised default, `struct sx`, `struct rwlock`).
- Call any function that may sleep: `malloc(M_WAITOK)`, `tsleep`, `pause`, `cv_wait`, and so on.
- Acquire Giant.
- Call code that may indirectly do any of the above.
- Take a long time (microseconds are fine; milliseconds are a bug).

**Ithread handlers MAY:**

- Everything a filter handler may do, plus:
- Acquire sleep mutexes, sx locks, rwlocks.
- Call `malloc(M_WAITOK)`.
- Call `cv_wait`, `tsleep`, `pause`, and other blocking primitives.
- Take longer (tens or hundreds of microseconds are normal).
- Do bounded work with unpredictable completion time.

**Ithread handlers SHOULD NOT:**

- Sleep for arbitrarily long times. The ithread has a scheduling priority that assumes responsiveness; a handler that sleeps for seconds starves other work on the same ithread.
- Block the ithread waiting for external events that have no bound.

Chapter 19's filter respects the first list rigorously; any violation is a bug the debug kernel will often catch.

### A Note on Per-CPU vs Shared ithreads

For legacy PCI INTx lines, the kernel usually assigns one ithread per `intr_event`, shared among any handlers on that event. For MSI-X (Chapter 20), each vector has its own ithread. The difference matters when multiple handlers need to run concurrently on the same IRQ: on the shared ithread, they serialise; on separate MSI-X vectors, they can run in parallel.

Chapter 19's driver uses legacy PCI. One IRQ, one ithread (if any ithread at all), one queue of deferred work. The serialisation is usually what you want for a single-device driver.

### Wrapping Up Section 2

FreeBSD's interrupt model centres on three objects: the `intr_event` (one per IRQ line or MSI vector), the `intr_handler` (one per registered driver on that event), and the ithread (one per event, shared among handlers). A driver registers a filter function, an ithread function, or both, through `bus_setup_intr(9)`, and promises `INTR_MPSAFE` compliance via the flags argument. The kernel dispatches filter handlers in primary interrupt context and schedules ithreads after the filter returns.

The constraints on filter handlers are strict (no sleeping, no sleep locks, no slow calls); the constraints on ithread handlers are loose by comparison. Shared PCI INTx lines allow many drivers on one IRQ, so filters must identify whether the interrupt belongs to their device and return `FILTER_STRAY` when it does not. `vmstat -i` and `devinfo -v` expose the kernel's view so operators and driver authors can see what is happening.

Section 3 is where the driver finally writes code against this model. It allocates an IRQ resource with `SYS_RES_IRQ`, registers a filter handler through `bus_setup_intr(9)`, sets `INTR_MPSAFE`, and logs a short message on every call. Stage 1 is the first time the driver gets interrupted in anger.



## Section 3: Registering an Interrupt Handler

Sections 1 and 2 established the hardware and kernel models. Section 3 puts the driver to work. The task is narrow: extend Chapter 18's attach path so that after allocating the BAR and walking the capability list, it also allocates an IRQ resource, registers a filter handler, and sets `INTR_MPSAFE`. The detach path grows in the mirror direction: tear down the handler first, then release the IRQ. At the end of Section 3 the driver is at version `1.2-intr-stage1` and fires a small counter-bumping filter handler every time its IRQ line is asserted.

### What Stage 1 Produces

Stage 1's handler is deliberately minimal. The driver needs a filter that is correct in every formal sense (returns the right `FILTER_*` value, respects the "no sleeping" rule, is `INTR_MPSAFE`) but does not yet do any real work. The goal is to prove the handler is wired correctly before introducing the complications of status decoding and deferred work.

The handler's behaviour at Stage 1:

1. Acquire a spin-safe counter lock (in our case a simple atomic operation).
2. Increment a counter in the softc.
3. Return `FILTER_HANDLED`.

That is it. No status register read, no acknowledgment, no deferred work. The counter lets the reader observe whether the handler fires, and if so how often. The `dmesg` output is silent by default; the count is visible through a sysctl the stage exposes.

Section 4 adds the real work (status decoding, acknowledgment, ithread scheduling). Section 5 adds simulated interrupts for testing. Section 6 extends the filter to be shared-IRQ-safe by checking whether the interrupt is actually for our device. But the scaffolding is this stage's contribution, and it needs to be right before anything else lands on top.

### The IRQ Resource Allocation

The first new line of attach, right after the BAR allocation:

```c
sc->irq_rid = 0;   /* legacy PCI INTx */
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
    RF_SHAREABLE | RF_ACTIVE);
if (sc->irq_res == NULL) {
	device_printf(dev, "cannot allocate IRQ\n");
	error = ENXIO;
	goto fail_hw;
}
```

A few things worth noticing.

First, `rid = 0` is the legacy PCI INTx convention. Every PCI device has a single legacy IRQ line advertised through the `PCIR_INTLINE` and `PCIR_INTPIN` fields of configuration space; the PCI bus driver exposes this as resource rid 0. Chapter 20 will use non-zero rids for MSI and MSI-X vectors, but Chapter 19's driver uses the legacy path.

Second, the `rid` variable is updated by `bus_alloc_resource_any` if the kernel chose a different rid than requested. For `rid = 0` the kernel always returns `rid = 0`, so the update is a no-op, but the pattern is consistent with BAR allocation from Chapter 18.

Third, `RF_SHAREABLE | RF_ACTIVE` is the standard flag set. `RF_SHAREABLE` allows the kernel to put our handler on a shared `intr_event` with other drivers. `RF_ACTIVE` activates the resource in one step.

Fourth, the allocation can fail. The most common reason on a real system is that the device's PCI configuration-space interrupt fields are zero (the firmware did not route an interrupt to the device). On bhyve with a virtio-rnd device, the allocation normally succeeds; on some older QEMU configurations with `intx=off`, it can fail. If the allocation fails, the attach path unwinds through the goto cascade.

### Storing the Resource in the Softc

The softc gains three new fields:

```c
struct myfirst_softc {
	/* ... existing fields ... */

	/* Chapter 19 interrupt fields. */
	struct resource	*irq_res;
	int		 irq_rid;
	void		*intr_cookie;     /* for bus_teardown_intr */
	uint64_t	 intr_count;      /* handler invocation count */
};
```

`irq_res` is the handle to the claimed IRQ resource. `irq_rid` is the resource ID (for the matching release call). `intr_cookie` is the opaque cookie that `bus_setup_intr(9)` returns and `bus_teardown_intr(9)` consumes; it identifies the specific handler so the kernel can remove it cleanly later. `intr_count` is a diagnostic counter the Stage 1 handler increments on every call.

The three fields parallel the three BAR fields (`bar_res`, `bar_rid`, `pci_attached`) added in Chapter 18. The parallelism is not accidental: every resource class gets a handle, an ID, and whatever bookkeeping the driver needs.

### The Filter Handler's Signature

The driver's filter is a function with signature:

```c
static int myfirst_intr_filter(void *arg);
```

The argument is whatever pointer the driver passes to `bus_setup_intr`'s `arg` parameter; by convention, a pointer to the driver's softc. The return value is a bitwise OR of `FILTER_STRAY`, `FILTER_HANDLED`, and `FILTER_SCHEDULE_THREAD`, as Section 2 described.

The Stage 1 implementation:

```c
static int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;

	atomic_add_64(&sc->intr_count, 1);
	return (FILTER_HANDLED);
}
```

One line of real work. The counter is incremented atomically because the handler may run concurrently on multiple CPUs (MSI-X scenarios) or in parallel with non-interrupt code that reads the counter through the sysctl. A sleep lock would be wrong in a filter; an atomic operation is the lightweight primitive that is safe here.

The return value is `FILTER_HANDLED` because we have no ithread work to do and no reason to return `FILTER_STRAY` (Section 6 adds the stray check; Stage 1 assumes the IRQ is ours).

### Registering the Handler With bus_setup_intr

After the IRQ allocation, the driver calls `bus_setup_intr(9)`:

```c
error = bus_setup_intr(dev, sc->irq_res,
    INTR_TYPE_MISC | INTR_MPSAFE,
    myfirst_intr_filter, NULL, sc,
    &sc->intr_cookie);
if (error != 0) {
	device_printf(dev, "bus_setup_intr failed (%d)\n", error);
	goto fail_release_irq;
}
```

The seven arguments:

1. **`dev`**: the device handle.
2. **`sc->irq_res`**: the IRQ resource we just allocated.
3. **`INTR_TYPE_MISC | INTR_MPSAFE`**: the flags. `INTR_TYPE_MISC` categorises the interrupt (Section 2). `INTR_MPSAFE` promises the handler does its own synchronisation.
4. **`myfirst_intr_filter`**: our filter handler. Non-NULL.
5. **`NULL`**: the ithread handler. NULL because Stage 1 is filter-only.
6. **`sc`**: the argument passed to both handlers.
7. **`&sc->intr_cookie`**: out-parameter where the kernel stores the cookie for later teardown.

The return value is 0 on success, or an errno on failure. A failure at this point is rare; the most common cause is an interrupt controller or platform-specific restriction.

A successful `bus_setup_intr` combined with the `device_printf` below produces a short banner in `dmesg` when the driver is loaded:

```text
myfirst0: attached filter handler on IRQ resource
```

The IRQ number itself is not in this line; `devinfo -v` and `vmstat -i` show it (the IRQ number depends on the guest's configuration). If you see an additional `myfirst0: [GIANT-LOCKED]` line, your flags argument is missing `INTR_MPSAFE` and the kernel is warning that Giant is being acquired around the handler; fix it.

### Describing the Handler to devinfo

An optional but recommended step. `bus_describe_intr(9)` lets the driver attach a human-readable name to the handler, which `devinfo -v` and the kernel's diagnostics will use:

```c
bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "legacy");
```

After this call, `vmstat -i` shows the handler's line as `irq19: myfirst0:legacy` rather than plain `irq19: myfirst0`. The suffix is the name the driver provided. For Chapter 19's single-interrupt driver the suffix is mostly decorative; for Chapter 20's MSI-X driver with multiple vectors it becomes essential for distinguishing `rx0`, `rx1`, `tx0`, `admin`, and so on.

### The Extended Attach Cascade

Putting the new pieces into the Stage 3 attach from Chapter 18:

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

	/* Step 3: attach the hardware layer against the BAR. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release_bar;

	/* Step 4: allocate the IRQ. */
	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "cannot allocate IRQ\n");
		error = ENXIO;
		goto fail_hw;
	}

	/* Step 5: register the filter handler. */
	error = bus_setup_intr(dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		device_printf(dev, "bus_setup_intr failed (%d)\n", error);
		goto fail_release_irq;
	}
	bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "legacy");
	device_printf(dev, "attached filter handler on IRQ resource\n");

	/* Step 6: create the cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_teardown_intr;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 7: read a diagnostic word from the BAR. */
	MYFIRST_LOCK(sc);
	sc->bar_first_word = CSR_READ_4(sc, 0x00);
	MYFIRST_UNLOCK(sc);
	device_printf(dev, "BAR[0x00] = 0x%08x\n", sc->bar_first_word);

	sc->pci_attached = true;
	return (0);

fail_teardown_intr:
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	sc->intr_cookie = NULL;
fail_release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	sc->irq_res = NULL;
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

The attach sequence now has seven steps instead of five. Two new goto labels (`fail_teardown_intr`, `fail_release_irq`) extend the cascade. The pattern is the same as Chapter 18's: each step undoes the one before it, chaining down to the softc init.

### The Extended Detach

The detach path mirrors the attach, with the interrupt teardown fitting between the cdev teardown and the hardware-layer detach:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Destroy the cdev so no new user-space access starts. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Tear down the interrupt handler before anything it depends on. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Quiesce callouts and tasks (includes Chapter 17 simulation if
	 * attached; includes any deferred taskqueue work). */
	myfirst_quiesce(sc);

	/* Release the Chapter 17 simulation if attached. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer. */
	myfirst_hw_detach(sc);

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

Two changes from Chapter 18: the `bus_teardown_intr` call, and the `bus_release_resource(..., SYS_RES_IRQ, ...)` call. The ordering is important. `bus_teardown_intr` must happen before anything the handler reads or writes is freed; in particular, before `myfirst_hw_detach` (which frees `sc->hw`). After `bus_teardown_intr` returns, the kernel guarantees the handler is not running and will not be called again; the driver can then free anything the handler touched.

Releasing the IRQ resource happens after the teardown and after the hardware layer is detached. The exact position between the hardware detach and the BAR release is a judgment call: the BAR and the IRQ do not depend on each other, so either order works. The Chapter 19 driver releases the IRQ first because that is the reverse of the attach order (attach allocated the IRQ after the BAR; detach releases it before the BAR).

Section 7 has more to say about ordering.

### The sysctl for the Interrupt Counter

A small diagnostic: a sysctl exposing the `intr_count` field so the reader can watch the counter grow:

```c
SYSCTL_ADD_U64(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_count",
    CTLFLAG_RD, &sc->intr_count, 0,
    "Number of times the interrupt filter has run");
```

After load, `sysctl dev.myfirst.0.intr_count` returns the current count. For the virtio-rnd device with no interrupts firing (the device has nothing to signal yet), the count stays at zero. Section 5's simulated interrupts will drive the count up without needing real IRQ events.

The `sysctl` is readable by any user (the `CTLFLAG_RD` makes it world-readable at the sysctl level; file permissions on the sysctl MIB are set elsewhere). Access is through:

```sh
sysctl dev.myfirst.0.intr_count
```

### What Stage 1 Proves

Loading the Stage 1 driver on a bhyve guest with a virtio-rnd device produces:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: attached filter handler on IRQ resource
myfirst0: BAR[0x00] = 0x10010000
```

`vmstat -i | grep myfirst` shows the interrupt event the kernel created:

```text
irq19: myfirst0:legacy              0          0
```

(The IRQ number and the rate depend on the environment.)

The initial count is zero because the virtio-rnd device is not generating interrupts yet (we have not programmed it to). The driver is correctly wired up, the handler is registered, and the filter is ready to fire. Stage 1's job is done.

### What Stage 1 Does Not Do

Several things are deliberately absent from Stage 1 and appear in later stages:

- **Status register reading.** The filter does not read the device's INTR_STATUS; it just increments a counter. Section 4 adds the status read.
- **Acknowledgment.** The filter does not write INTR_STATUS to acknowledge. On a level-triggered line with a device that is actually firing, this is a bug. On our virtio-rnd target in bhyve, the device is not firing, so the absence is invisible. Section 4 adds the acknowledgment and explains why it matters.
- **Ithread handler.** No deferred work yet. Section 4 introduces a taskqueue-based deferred path and wires the filter to schedule it.
- **Simulated interrupts.** There is no way to make the handler fire without a real IRQ from the device. Section 5 adds a sysctl that invokes the filter directly under the driver's normal lock rules.
- **Shared-IRQ discipline.** The filter assumes every interrupt belongs to our device. Section 6 adds the `FILTER_STRAY` check for devices that share a line.

These are the topics of Sections 4, 5, and 6. Stage 1 is deliberately incomplete; each later section adds a specific thing that Stage 1 left out.

### Common Mistakes at This Stage

A short list of traps beginners hit at Stage 1.

**Forgetting `INTR_MPSAFE`.** The handler gets wrapped in Giant. Scalability disappears. `dmesg` prints `[GIANT-LOCKED]`. Fix: add `INTR_MPSAFE` to the flags argument.

**Passing the wrong argument to the filter.** A C function pointer is fussy; passing `&sc` instead of `sc` produces a double pointer that the filter then dereferences incorrectly. The result is usually a kernel panic. Fix: the `arg` in `bus_setup_intr` is `sc`; the filter receives the same value as a `void *`.

**Returning 0 from the filter.** The return value is a bitwise OR of `FILTER_*` values. Zero is "no flags", which is illegal (the kernel requires at least one of `FILTER_STRAY`, `FILTER_HANDLED`, or `FILTER_SCHEDULE_THREAD`). The debug kernel asserts on this. Fix: return `FILTER_HANDLED`.

**Using a sleep lock in the filter.** The filter takes `sc->mtx` (a regular sleep mutex). `WITNESS` complains; the debug kernel panics. Fix: use atomic operations, or move the work to an ithread.

**Tearing down the IRQ before the cookie is consumed.** Calling `bus_release_resource` on the IRQ before `bus_teardown_intr` is a bug: the resource is gone, but the handler is still registered against it. The next interrupt fires and the kernel dereferences freed state. Fix: always `bus_teardown_intr` first.

**Mismatched rid.** The rid passed to `bus_release_resource` must match the rid that `bus_alloc_resource_any` returned (or the rid passed in initially, for `rid = 0`). Mismatch usually surfaces as "Resource not found" or a kernel message. Fix: store the rid in the softc alongside the resource handle.

**Forgetting to drain pending deferred work before teardown.** This applies more in Stage 2, but worth flagging here: if the filter has scheduled a taskqueue item, the item must complete before the softc goes away. A teardown that releases the IRQ but leaves a taskqueue item pending produces a use-after-free when the item runs.

### Checkpoint: Stage 1 Working

Before Section 4, confirm Stage 1 is in place:

- `kldstat -v | grep myfirst` shows the driver at version `1.2-intr-stage1`.
- `dmesg | grep myfirst` shows the attach banner including `attached filter handler on IRQ resource`.
- No `[GIANT-LOCKED]` warning.
- `devinfo -v | grep -A 5 myfirst` shows both the BAR and the IRQ resource.
- `vmstat -i | grep myfirst` shows the handler's row.
- `sysctl dev.myfirst.0.intr_count` returns `0` (or a small number, depending on whether the device happens to interrupt).
- `kldunload myfirst` runs cleanly; no panic, no warning.

If any step fails, return to the relevant subsection. The failures are diagnosed the same way Chapter 18's failures were: check `dmesg` for banners, check `devinfo -v` for resources, check `WITNESS` output for lock-order issues.

### Wrapping Up Section 3

Registering an interrupt handler is three new calls (`bus_alloc_resource_any` with `SYS_RES_IRQ`, `bus_setup_intr`, `bus_describe_intr`), three new softc fields (`irq_res`, `irq_rid`, `intr_cookie`), and one new counter (`intr_count`). The attach cascade grows by two labels; the detach path grows by the `bus_teardown_intr` call. The handler itself is a one-line atomic increment returning `FILTER_HANDLED`.

The point of Stage 1 is not the work the handler does. The point is that the handler is correctly registered, `INTR_MPSAFE` is set, the counter increments when an interrupt fires, and the teardown runs cleanly on unload. Every later stage builds on top of this scaffolding; getting it right now is the investment that pays off through the rest of the chapter.

Section 4 makes the handler do real work: read INTR_STATUS, decide what to do, acknowledge the device, and defer bulk work to a taskqueue. This is the heart of a real interrupt handler, and the content that matters most for the rest of Part 4.



## Section 4: Writing a Real Interrupt Handler

Stage 1 proved the handler wiring is correct. Stage 2 makes the handler do the work a real driver's filter does. The structure of Section 4 is a close walk through the Chapter 17 simulation's hardware model, with the filter now reading and acknowledging the device's `INTR_STATUS` register, making decisions based on which bits are set, handling the urgent small work inline, and deferring the bulk work to a taskqueue. By the end of Section 4 the driver is at version `1.2-intr-stage2` and has a filter-plus-task pipeline that behaves like a small real driver.

### The Register Picture

A quick recap of the Chapter 17 simulation's interrupt register layout (see `HARDWARE.md` for the full details). Offset `0x14` holds `INTR_STATUS`, a 32-bit register with these bits defined:

- `MYFIRST_INTR_DATA_AV` (`0x00000001`): a data-available event has occurred.
- `MYFIRST_INTR_ERROR` (`0x00000002`): an error condition has been detected.
- `MYFIRST_INTR_COMPLETE` (`0x00000004`): a command has completed.

The register is "write-one-to-clear" (RW1C) semantics: writing a 1 to a bit clears that bit; writing a 0 leaves it unchanged. This is the standard PCI interrupt-status convention and is what Chapter 19's handler expects.

Offset `0x10` holds `INTR_MASK`, a parallel register that controls which bits of `INTR_STATUS` actually assert the IRQ line. Setting a bit in `INTR_MASK` enables that interrupt class; clearing it disables it. The driver sets `INTR_MASK` at attach time to enable the interrupts it wants to receive.

Chapter 17's simulation can drive these bits autonomously. Chapter 18's PCI driver runs against a real virtio-rnd BAR, where the offsets mean something different (virtio legacy configuration, not the Chapter 17 register map). Section 4 writes the handler against the Chapter 17 semantics; Section 5 shows how to exercise the handler without real IRQ events; the virtio-rnd device does not implement this register layout, so in the bhyve lab the handler is primarily exercised through the simulated-interrupt path.

This is an honest limitation of the teaching target. A reader adapting the driver to a real device that implements Chapter 17-style registers would see the handler fire on real interrupts directly. For the bhyve virtio-rnd target, Section 5's sysctl-triggered handler is the way to exercise the Stage 2 filter in practice.

### The Filter Handler at Stage 2

The Stage 2 filter reads `INTR_STATUS`, decides what happened, acknowledges the bits it is handling, and either does the urgent work inline or schedules a task for bulk work.

```c
int
myfirst_intr_filter(void *arg)
{
	struct myfirst_softc *sc = arg;
	uint32_t status;
	int rv = 0;

	/*
	 * Read the raw status. The filter runs in primary interrupt
	 * context and cannot take sc->mtx (a sleep mutex), so the access
	 * goes through the specialised accessor that asserts the correct
	 * context. We use a local, spin-safe helper for the BAR access;
	 * Stage 2 uses a small inline instead of the lock-asserting
	 * CSR_READ_4 macro.
	 */
	status = bus_read_4(sc->bar_res, MYFIRST_REG_INTR_STATUS);
	if (status == 0)
		return (FILTER_STRAY);

	atomic_add_64(&sc->intr_count, 1);

	/* Handle the DATA_AV bit: small urgent work only. */
	if (status & MYFIRST_INTR_DATA_AV) {
		atomic_add_64(&sc->intr_data_av_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		taskqueue_enqueue(sc->intr_tq, &sc->intr_data_task);
		rv |= FILTER_HANDLED;
	}

	/* Handle the ERROR bit: log and acknowledge. */
	if (status & MYFIRST_INTR_ERROR) {
		atomic_add_64(&sc->intr_error_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_ERROR);
		rv |= FILTER_HANDLED;
	}

	/* Handle the COMPLETE bit: wake any pending waiters. */
	if (status & MYFIRST_INTR_COMPLETE) {
		atomic_add_64(&sc->intr_complete_count, 1);
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_COMPLETE);
		rv |= FILTER_HANDLED;
	}

	/* If we didn't recognise any bit, this wasn't our interrupt. */
	if (rv == 0)
		return (FILTER_STRAY);

	return (rv);
}
```

Several things worth reading carefully.

**The raw access.** The filter uses `bus_read_4` and `bus_write_4` (the newer resource-based accessors) directly, not the `CSR_READ_4` and `CSR_WRITE_4` macros from Chapter 16. The reason is subtle. The Chapter 16 macros take `sc->mtx` via `MYFIRST_ASSERT`, which is a sleep mutex. A filter must not take a sleep mutex. The right approach is either to use the raw `bus_space` accessors directly (as shown) or to introduce a parallel family of CSR macros that assert no lock requirement. Section 8's refactor introduces `ICSR_READ_4` and `ICSR_WRITE_4` ("I" for interrupt-context) to make the distinction explicit; Stage 2 uses the raw accessors.

**The early stray check.** A status of zero means no bit is set; this is a shared-IRQ call from another driver. Returning `FILTER_STRAY` lets the kernel try the next handler. The check is also a defence against a real hardware race: if the interrupt controller asserts the line but the device has already cleared the status (by the time we read it), we should not claim the interrupt.

**The per-bit handling.** Each bit of interest is checked, counted, and acknowledged. The order does not matter (the bits are independent), but the structure is conventional: one `if` per bit.

**The acknowledgment.** Writing the bit back to `INTR_STATUS` clears it (RW1C). This is what makes the interrupt line deassert. Failing to acknowledge on a level-triggered line produces an interrupt storm.

**The taskqueue enqueue.** The `DATA_AV` bit triggers deferred work. The filter enqueues a task; the taskqueue's worker thread runs the task later in thread context, where it can take sleep locks and do slow work. The enqueue is safe to call from a filter (taskqueues use spin locks internally for this path).

**The final return value.** A bitwise OR of `FILTER_HANDLED` for each bit we recognised, or `FILTER_STRAY` if nothing matched. If we had work for an ithread, we would OR in `FILTER_SCHEDULE_THREAD`; but Stage 2 uses a taskqueue rather than the ithread, so the return value is just `FILTER_HANDLED`.

### Why a Taskqueue, Not an ithread?

FreeBSD lets a driver register an ithread handler through the fifth argument of `bus_setup_intr(9)`. Why does Stage 2 use a taskqueue instead?

Two reasons.

First, the taskqueue is more flexible. An ithread is tied to the specific `intr_event`; it runs the driver's ithread function after the filter. A taskqueue lets the driver schedule a task from any context (filter, ithread, other tasks, user-space ioctl paths) and have it run on a shared worker thread. For the Chapter 19 driver, which exercises the handler through simulated interrupts as well as real ones, the taskqueue is a more uniform deferred-work primitive.

Second, the taskqueue separates priority from the interrupt type. The ithread's priority is derived from `INTR_TYPE_*`; the taskqueue's priority is controlled by `taskqueue_start_threads(9)`. For drivers that want their deferred work at a different priority than the interrupt category implies, the taskqueue gives that control.

Real FreeBSD drivers use both patterns. Simple drivers with fire-and-forget interrupts often use the ithread (less code). Drivers with richer deferred-work patterns use taskqueues. The `iflib(9)` framework uses a kind of hybrid.

Chapter 19 teaches the taskqueue pattern because it composes better with the rest of the book. Chapter 17 already has a taskqueue; Chapter 14 introduced the pattern; the deferred-work discipline is a book-wide theme.

### The Deferred-Work Task

The filter enqueued `sc->intr_data_task` when it saw `DATA_AV`. That task is:

```c
static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);

	/*
	 * The data-available event has fired. Read the device's data
	 * register through the Chapter 16 accessor (which takes sc->mtx
	 * implicitly), update the driver's state, and wake any waiting
	 * readers.
	 */
	uint32_t data = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
	sc->intr_last_data = data;
	sc->intr_task_invocations++;

	/* Wake any thread sleeping on the data-ready condition. */
	cv_broadcast(&sc->data_cv);

	MYFIRST_UNLOCK(sc);
}
```

Several notable properties.

**The task runs in thread context.** It can take `sc->mtx`, use `cv_broadcast`, call `malloc(M_WAITOK)`, and do slow work.

**The task respects the Chapter 11 locking discipline.** The mutex is acquired; the CSR access uses the standard Chapter 16 macro; the condition-variable broadcast uses the Chapter 12 primitive.

**The task's argument is the softc.** Same as the filter. One subtle implication: the task cannot assume the driver has not been detached. If detach fires after the filter has enqueued the task but before the task runs, the task could execute against a freed softc. Section 7 covers the discipline that prevents this (drain before release).

**The `npending` argument** is the number of times the task was enqueued since its last run. For most drivers this is useful as a coalescing hint: if `npending` is 5, the device signalled five data-ready events that all coalesce into one run. Stage 2's task ignores it; larger drivers use it to size batch operations.

### Declaring and Initialising the Task

The softc gains task-related fields:

```c
struct myfirst_softc {
	/* ... existing fields ... */

	/* Chapter 19 interrupt-related fields. */
	struct resource		*irq_res;
	int			 irq_rid;
	void			*intr_cookie;
	uint64_t		 intr_count;
	uint64_t		 intr_data_av_count;
	uint64_t		 intr_error_count;
	uint64_t		 intr_complete_count;
	uint64_t		 intr_task_invocations;
	uint32_t		 intr_last_data;

	struct taskqueue	*intr_tq;
	struct task		 intr_data_task;
};
```

In `myfirst_init_softc` (or the init path):

```c
TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
    taskqueue_thread_enqueue, &sc->intr_tq);
taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
    "myfirst intr taskq");
```

The taskqueue is created with one worker thread at priority `PI_NET` (an interrupt priority; see `/usr/src/sys/sys/priority.h`). The name `"myfirst intr taskq"` appears in `top -H` for diagnosis. The `M_WAITOK` during create is fine because `myfirst_init_softc` runs in attach context, before any interrupts fire.

### Enabling the Interrupts at the Device

A detail often forgotten: the device itself must be told to deliver interrupts. For the Chapter 17 simulation's register layout, this is done by setting bits in the `INTR_MASK` register:

```c
/* After attaching the hardware layer, enable the interrupts we care
 * about. */
MYFIRST_LOCK(sc);
CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
    MYFIRST_INTR_COMPLETE);
MYFIRST_UNLOCK(sc);
```

The `INTR_MASK` register controls which bits of `INTR_STATUS` actually assert the IRQ line. Without it the device may set `INTR_STATUS` bits internally but never raise the line, so the handler never fires. Setting all three bits enables all three interrupt classes.

This is another honest limitation of the teaching target. Offset `0x10` on the virtio-rnd legacy BAR is not an interrupt-mask register at all. In the legacy virtio layout (see `/usr/src/sys/dev/virtio/pci/virtio_pci_legacy_var.h`), the dword starting at `0x10` is shared by three small fields: `queue_notify` at offset `0x10` (16-bit), `device_status` at offset `0x12` (8-bit), and `isr_status` at offset `0x13` (8-bit). A 32-bit write of our `DATA_AV | ERROR | COMPLETE` pattern (`0x00000007`) at that offset writes `0x0007` to `queue_notify` (notifying a virtqueue index the device does not have) and `0x00` to `device_status` (which the virtio specification defines as a **device reset**). Writing zero to `device_status` is how the virtio driver is supposed to reset the device before reinitialisation.

For that reason, the `CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, ...)` call as written is **safe but pointless** on the bhyve virtio-rnd target: it resets the device's virtio state machine (which our driver was not using anyway), and it never enables any real interrupt because the Chapter 17 `INTR_MASK` register does not exist on that device. If you plan to drive a reader through this on bhyve, keep the write in the code for continuity with a real Chapter-17-compatible device, and rely on Section 5's simulated-interrupt sysctl for testing instead of expecting real IRQ events. A reader who adapts the driver to a real device that matches the Chapter 17 register map would see the mask write do its job.

### Disabling the Interrupts at Detach

The symmetric step at detach:

```c
/* Disable all interrupts at the device before tearing down. */
MYFIRST_LOCK(sc);
if (sc->hw != NULL)
	CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0);
MYFIRST_UNLOCK(sc);
```

This write happens before `bus_teardown_intr` so the device stops asserting the line before the handler is removed. The guard against `sc->hw == NULL` protects against partial-attach cases where the hardware layer failed; the disable is skipped if the hardware is not attached.

### A Worked Flow

A concrete trace of what happens when a `DATA_AV` event fires (on a device that actually implements the Chapter 17 semantics):

1. The device sets `INTR_STATUS.DATA_AV`. Because `INTR_MASK.DATA_AV` is set, the device asserts its IRQ line.
2. The interrupt controller routes the IRQ to a CPU.
3. The CPU takes the interrupt and jumps to the kernel's dispatch code.
4. The kernel finds the `intr_event` for our IRQ and calls `myfirst_intr_filter`.
5. The filter reads `INTR_STATUS`, sees `DATA_AV`, increments counters, writes `DATA_AV` back to `INTR_STATUS` (clearing it), enqueues `intr_data_task`, returns `FILTER_HANDLED`.
6. The device deasserts its IRQ line (because `INTR_STATUS.DATA_AV` is now clear).
7. The kernel sends EOI, returns to the interrupted thread.
8. Some milliseconds later, the taskqueue's worker thread wakes up, runs `myfirst_intr_data_task_fn`, reads `DATA_OUT`, updates the softc, broadcasts the condition variable.
9. Any thread that was waiting on the condition variable wakes up and proceeds.

Steps 1 through 7 take a few microseconds. Step 8 can take hundreds of microseconds or more, which is why it is in thread context. The split is what lets the interrupt path stay fast.

For the bhyve virtio-rnd target, steps 1 through 6 do not happen (the device does not match the Chapter 17 register layout). Steps 4 through 9 can still be exercised through the Section 5 simulated-interrupt path.

### Urgent Work Inline vs Deferred

A useful way to decide what goes in the filter versus in the task: the filter handles what must be done **per interrupt**, the task handles what must be done **per event**.

Per-interrupt (filter):
- Read `INTR_STATUS` to identify the event.
- Acknowledge the event at the device (write back to `INTR_STATUS`).
- Update counters.
- Make a single scheduling decision (enqueue the task).

Per-event (task):
- Read data from device registers or DMA buffers.
- Update the driver's internal state machine.
- Wake waiting threads.
- Pass data up to the network stack, the storage stack, or the cdev queue.
- Handle errors that require slow recovery.

The rule of thumb: if the filter takes more than a hundred CPU cycles of real work (not counting the register accesses, which are cheap in themselves), it is probably doing too much.

### FILTER_SCHEDULE_THREAD vs Taskqueue

A reader might ask: when would I use `FILTER_SCHEDULE_THREAD` instead of a taskqueue?

Use `FILTER_SCHEDULE_THREAD` when:
- You want the kernel's per-event ithread (one per `intr_event`) to run the slow work.
- You do not need to schedule the work from anywhere except the filter.
- You want the scheduling priority to follow the interrupt's `INTR_TYPE_*`.

Use a taskqueue when:
- You want to schedule the same work from multiple paths (filter, ioctl, sysctl, sleep-based timeout).
- You want to share the worker thread across multiple devices.
- You want explicit control over priority via `taskqueue_start_threads`.

For the Chapter 19 driver the taskqueue is the cleaner choice because Section 5 will schedule the same task from a simulated-interrupt path. An ithread would not be reachable from there.

### When the Taskqueue Itself Is the Wrong Answer

A caveat. The taskqueue is great for short deferred work. It is not great for long-running operations. If the driver needs to run a state machine for seconds, or block waiting on a USB transfer, or process a large buffer chain, a dedicated worker thread is better. The taskqueue's worker thread is shared across tasks; a single task that blocks for a long time delays every other task behind it.

Chapter 19's task runs for microseconds. The taskqueue is fine. Chapter 20's MSI-X driver with per-queue receive processing may want per-queue worker threads. Chapter 21's DMA-driven bulk transfer may want a dedicated thread. Each chapter picks the right primitive for its workload; Chapter 19 uses the simplest one that fits.

### Common Mistakes at This Stage

A short list.

**Reading INTR_STATUS without acknowledging.** The handler reads, decides, and returns without writing back. On a level-triggered line, the device keeps asserting; the handler fires again immediately; storm. Fix: acknowledge every bit you handled.

**Acknowledging too many bits.** A sloppy handler writes `0xffffffff` to `INTR_STATUS` on every call to "clear all bits". This also clears events the handler has not processed, dropping data or confusing the state machine. Fix: acknowledge only the bits you actually handled.

**Taking sleep locks in the filter.** `MYFIRST_LOCK(sc)` takes `sc->mtx`, which is a sleep mutex. In the filter this is a bug; `WITNESS` panics. Fix: use atomic operations in the filter, and take the sleep mutex only in the task (which runs in thread context).

**Scheduling a task after the softc is torn down.** If the task is scheduled from the filter but the filter runs after detach has partially torn down, the task runs against stale state. Fix: Section 7 covers the ordering. Briefly: `bus_teardown_intr` must happen before the hardware layer is freed, and `taskqueue_drain` must happen before the taskqueue is freed.

**Using `CSR_READ_4`/`CSR_WRITE_4` directly in the filter.** If the Chapter 16 accessor asserts `sc->mtx` held (which it does on debug kernels), the filter panics. Fix: use raw `bus_read_4`/`bus_write_4` or introduce a parallel set of interrupt-safe CSR macros. Section 8 handles this with `ICSR_READ_4`.

**Enqueueing the task without `TASK_INIT`.** A task enqueued before `TASK_INIT` has a corrupt function pointer. The first run of the task jumps to garbage. Fix: initialise the task in the attach path before enabling interrupts.

**Forgetting to enable interrupts at the device.** The handler is registered and `bus_setup_intr` succeeded; `vmstat -i` still shows zero firings. The problem is the device's `INTR_MASK` register is still zero (or whatever post-reset value it has), so the device never asserts the line. Fix: write the `INTR_MASK` during attach.

**Forgetting to disable interrupts at detach.** The handler has been torn down but the device is still asserting the line. The kernel will eventually complain about a stray interrupt, or (worse) another driver that shares the line will see mysterious activity. Fix: clear `INTR_MASK` before `bus_teardown_intr`.

### Stage 2 Output: What Success Looks Like

After loading Stage 2 on a real device that produces interrupts, `dmesg` shows:

```text
myfirst0: <Red Hat Virtio entropy source (myfirst demo target)> ... on pci0
myfirst0: attaching: vendor=0x1af4 device=0x1005 revid=0x00
myfirst0: BAR0 allocated: 0x20 bytes at 0xc1000000
myfirst0: hardware layer attached to BAR: 32 bytes
myfirst0: attached filter handler on IRQ resource
myfirst0: interrupts enabled (mask=0x7)
myfirst0: BAR[0x00] = 0x10010000
```

The `interrupts enabled` line is new. It confirms the driver has written to `INTR_MASK`.

On a real device generating interrupts, `sysctl dev.myfirst.0.intr_count` will tick up. On the bhyve virtio-rnd target, the count stays at zero because the device does not fire our expected interrupts. Section 5's simulated-interrupt path is the way to exercise the handler from there.

### Wrapping Up Section 4

A real interrupt handler reads `INTR_STATUS` to identify the cause, handles each bit of interest, acknowledges the bits it handled by writing back to `INTR_STATUS`, and returns the right combination of `FILTER_*` values. Urgent work (register access, counter updates, acknowledgments) happens in the filter. Slow work (data reads through Chapter 16 accessors that take `sc->mtx`, condition-variable broadcasts, user-space notifications) happens in a taskqueue task that the filter enqueues.

The filter is short (twenty to forty lines of real code for a typical device). The task is short too (ten to thirty lines). The composition is what makes the driver functional: the filter handles interrupts at interrupt rate; the task handles events at thread-rate; the split keeps the interrupt window tight and the deferred work free to block.

Section 5 is the section that lets a reader exercise this machinery on a bhyve target where the real IRQ path does not match the Chapter 17 register semantics. It adds a sysctl that invokes the filter under the driver's normal lock rules, lets the reader trigger simulated interrupts at will, and confirms that the counters, the task, and the condition-variable broadcast all behave as designed.



## Section 5: Using Simulated Interrupts for Testing

Section 4's filter and task are real driver code. They are ready to handle real interrupts on a device that matches the Chapter 17 register layout. The problem Chapter 19's lab target presents is that the device we have (virtio-rnd under bhyve) does not match that layout; writing the Chapter 17 interrupt-mask bits to the virtio-rnd BAR has defined but unrelated effects, and reading Chapter 17 interrupt-status bits from the virtio-rnd BAR returns virtio-specific values that have nothing to do with our simulated semantics. On this target the filter, if it fires at all, will see garbage.

Section 5 solves this by teaching the reader to simulate interrupts. The core idea is simple: expose a sysctl that, when written, invokes the filter handler directly under the driver's normal lock rules, exactly as the kernel would invoke it from a real interrupt. The filter reads the `INTR_STATUS` register (which the reader has also written to through another sysctl, or through the Chapter 17 simulation backend on a simulation-only build), makes the same decisions it would on a real interrupt, and drives the full pipeline end-to-end.

### Why Simulation Deserves a Section

A reader who finished Chapter 17 might reasonably ask: was the whole Chapter 17 simulation not already a way to simulate interrupts? Yes and no.

Chapter 17 simulated an **autonomous device**. Its callouts changed register values on their own schedule, its command callout fired when the driver wrote `CTRL.GO`, its fault-injection framework made the simulated device misbehave. The driver at Chapter 17 was a simulation-only driver; there was no `bus_setup_intr` because there was no real bus.

Chapter 19 is different. The driver now has a real `bus_setup_intr` handler registered on a real IRQ line. Chapter 17's callouts are not involved; on the PCI build the Chapter 17 simulation does not run. What we want is a way to trigger the **filter handler** directly, with the exact lock semantics a real interrupt would produce, so we can validate Section 4's filter and task pipeline without depending on a device that actually produces the right interrupts.

The cleanest way to do this, and the way many FreeBSD drivers have for similar purposes, is a sysctl write that invokes the filter function directly. The filter runs in the caller's context (thread context, where the sysctl write originates), but the filter's code does not care about the outer context as long as the lock discipline inside is right. An atomic increment, a BAR read, a BAR write, a `taskqueue_enqueue`: all of these work from thread context too. The simulated call exercises the same code paths the kernel would exercise on a real interrupt.

There is one subtle distinction. On a real interrupt, the kernel arranges for filters on the same `intr_event` to run serially on one CPU. A sysctl-triggered simulated call does not have that guarantee; another thread could be invoking the filter at the same moment. For the Chapter 19 driver this is fine because the filter's state is protected by atomic operations (not by the kernel's single-CPU-per-IRQ guarantee). For a driver that relies on implicit single-CPU serialisation, simulation through a sysctl would not be a faithful test. The lesson is: `INTR_MPSAFE` drivers that use atomics and spin locks translate cleanly to simulation.

### The Simulated-Interrupt sysctl

The mechanism is a write-only sysctl that invokes the filter:

```c
static int
myfirst_intr_simulate_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	uint32_t mask;
	int error;

	mask = 0;
	error = sysctl_handle_int(oidp, &mask, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/*
	 * "mask" is the INTR_STATUS bits the caller wants to pretend
	 * the device has set. Set them in the real register, then call
	 * the filter directly.
	 */
	MYFIRST_LOCK(sc);
	if (sc->hw == NULL) {
		MYFIRST_UNLOCK(sc);
		return (ENODEV);
	}
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, mask);
	MYFIRST_UNLOCK(sc);

	/* Invoke the filter directly. */
	(void)myfirst_intr_filter(sc);

	return (0);
}
```

And the sysctl declaration in `myfirst_intr_add_sysctls`:

```c
SYSCTL_ADD_PROC(&sc->sysctl_ctx,
    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO, "intr_simulate",
    CTLTYPE_UINT | CTLFLAG_WR | CTLFLAG_MPSAFE,
    sc, 0, myfirst_intr_simulate_sysctl, "IU",
    "Simulate an interrupt by setting INTR_STATUS bits and "
    "invoking the filter");
```

Writing to `dev.myfirst.0.intr_simulate` causes the handler to run with the specified INTR_STATUS bits set.

### Exercising the Simulation

Once the sysctl is in place, the reader can drive the full pipeline from user space:

```sh
# Simulate a DATA_AV event.
sudo sysctl dev.myfirst.0.intr_simulate=1

# Check counters.
sysctl dev.myfirst.0.intr_count
sysctl dev.myfirst.0.intr_data_av_count
sysctl dev.myfirst.0.intr_task_invocations

# Simulate an ERROR event.
sudo sysctl dev.myfirst.0.intr_simulate=2

# Simulate a COMPLETE event.
sudo sysctl dev.myfirst.0.intr_simulate=4

# Simulate all three at once.
sudo sysctl dev.myfirst.0.intr_simulate=7
```

The first call increments `intr_count` (filter fired), `intr_data_av_count` (DATA_AV bit recognised), and eventually `intr_task_invocations` (the taskqueue task ran). The second increments `intr_count` and `intr_error_count`. The third increments `intr_count` and `intr_complete_count`. The fourth hits all three.

A reader can verify the full pipeline:

```sh
# Watch counters in a loop.
while true; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
    sleep 0.5
    sysctl dev.myfirst.0 | grep intr_
done
```

The counters tick forward at the expected rate. The driver behaves as if real interrupts were arriving.

### Why This Is Not a Toy

One might think this simulated path is a teaching artifact. It is not. Many real drivers keep a similar path for diagnostic purposes. The reasons are worth naming:

**Regression testing.** A simulated-interrupt path lets a CI pipeline exercise the handler without needing real hardware. Chapter 17 made the same argument for simulating device behaviour; Section 5 makes it for simulating the interrupt path.

**Fault injection.** A simulated-interrupt sysctl lets a test inject specific `INTR_STATUS` patterns to exercise error-handling code. The driver's response to `INTR_STATUS = ERROR | COMPLETE` (both bits set simultaneously) is hard to trigger through real hardware; a sysctl that sets both bits and calls the handler makes it easy.

**Developer productivity.** When a driver author is debugging the handler logic, having a sysctl that triggers the handler on demand is enormously useful. `dtrace -n 'fbt::myfirst_intr_filter:entry'` combined with `sudo sysctl dev.myfirst.0.intr_simulate=1` gives a single-step view of the handler on demand.

**Bringing up new hardware.** A driver author often has a prototype device that does not yet produce interrupts correctly. A simulated-interrupt path lets the driver's upper layers be tested before the hardware works, which means the driver and the hardware can be brought up in parallel rather than serially.

**Teaching.** For this book's purpose, the simulated path makes the filter and task observable on a lab target that does not naturally produce the expected interrupts. The reader can see the pipeline work even though the hardware does not cooperate.

### Locking in the Simulated Path

A detail worth thinking through. The sysctl writes `INTR_STATUS` while holding `sc->mtx`. The filter handler, when invoked through the kernel's real interrupt path, runs without `sc->mtx` held (the filter uses `bus_read_4` / `bus_write_4` directly, not the lock-asserting CSR macros). When invoked through the sysctl, what is the calling context?

The sysctl handler runs in thread context. The `MYFIRST_LOCK(sc)` acquires the sleep mutex. Between the lock acquisition and the release, the thread holds the mutex. Then the lock is released, and `myfirst_intr_filter(sc)` is called. The filter takes no locks, uses only atomics and `bus_read_4`/`bus_write_4`, enqueues a task, and returns. The whole sequence is safe.

Would it be safe to call the filter with `sc->mtx` held? Yes, actually: the filter does not try to acquire the same mutex, and the filter runs in a context where holding the lock is not itself illegal (thread context). But the filter was designed to be context-agnostic; calling it with a sleep lock held would obscure that contract. The sysctl releases the lock before invoking the filter for clarity.

### Using the Chapter 17 Simulation to Produce Interrupts

A complementary technique worth mentioning. Chapter 17's simulation backend, if attached, produces autonomous state changes on its own schedule. In particular, its `DATA_AV`-producing callout sets `INTR_STATUS.DATA_AV`. On a simulation-only build (`MYFIRST_SIMULATION_ONLY` defined at compile time), the simulation is live, the callout fires, and the Chapter 17 driver can even call the filter from the callout itself.

Chapter 19 does not change Chapter 17's behaviour on simulation-only builds. A reader who wants to see the filter driven by the Chapter 17 simulation can build with `-DMYFIRST_SIMULATION_ONLY`, load the module, and watch the callouts set `INTR_STATUS` bits. The sysctl-triggered path from Section 5 remains available on both builds.

On the PCI build, the Chapter 17 simulation is not attached (Chapter 18's discipline), so the Chapter 17 callouts do not run. The simulated-interrupt path is the only way to drive the filter on the PCI build.

### Extending the sysctl to Schedule at a Rate

A useful extension for load testing: a sysctl that schedules simulated interrupts periodically through a callout. The callout fires every N milliseconds, sets a bit in `INTR_STATUS`, and invokes the filter. A reader can tune the rate and observe the pipeline under load.

```c
static void
myfirst_intr_sim_callout_fn(void *arg)
{
	struct myfirst_softc *sc = arg;

	MYFIRST_LOCK(sc);
	if (sc->intr_sim_period_ms > 0 && sc->hw != NULL) {
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS,
		    MYFIRST_INTR_DATA_AV);
		MYFIRST_UNLOCK(sc);
		(void)myfirst_intr_filter(sc);
		MYFIRST_LOCK(sc);
		callout_reset_sbt(&sc->intr_sim_callout,
		    SBT_1MS * sc->intr_sim_period_ms, 0,
		    myfirst_intr_sim_callout_fn, sc, 0);
	}
	MYFIRST_UNLOCK(sc);
}
```

The callout reschedules itself as long as `intr_sim_period_ms` is non-zero. A sysctl exposes the period:

```sh
# Fire a simulated interrupt every 100 ms.
sudo sysctl hw.myfirst.intr_sim_period_ms=100

# Stop simulating.
sudo sysctl hw.myfirst.intr_sim_period_ms=0
```

Watch the counters grow at the expected rate:

```sh
sleep 10
sysctl dev.myfirst.0.intr_count
```

After ten seconds of 100 ms period, the counter should read about 100. If it reads much less, the filter or the task is the bottleneck (unlikely at this scale; more of a concern for high-rate tests). If it reads much more, something is firing the filter from elsewhere.

### What the Simulation Does Not Capture

Honest limits of the technique.

**Concurrent firings.** The sysctl serialises simulated interrupts at one per write. A real interrupt path can see two interrupts back-to-back on different CPUs, which a sysctl test would not produce. For stress-testing concurrency, a separate test that spawns multiple threads each writing the sysctl is more effective.

**Interrupt-controller behaviour.** The simulation bypasses the interrupt controller entirely. Tests that depend on EOI timing, masking, or storm detection cannot be driven this way.

**CPU affinity.** The simulated filter runs on whichever CPU the sysctl-writing thread is on. A real interrupt fires on whichever CPU the affinity configuration selects. Tests of per-CPU behaviour need real interrupts or another mechanism.

**Contention with the real interrupt path.** If real interrupts are also firing (perhaps because the device actually does generate some), the simulated path can race with the real path. The atomic counters handle it correctly; more complex shared state might not.

These are limits, not deal-breakers. For most Chapter 19 testing, the simulated path is sufficient. For advanced stress testing, additional techniques (rt-threads, multi-CPU invocations, real hardware) apply.

### Observing the Task Running

A diagnostic worth exposing. The task's `intr_task_invocations` counter ticks every time the task runs. A reader can compare it to `intr_data_av_count` to check whether the taskqueue is keeping up:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=1    # fire DATA_AV
sleep 0.1
sysctl dev.myfirst.0.intr_data_av_count       # should be 1
sysctl dev.myfirst.0.intr_task_invocations    # should also be 1
```

If the task counter lags the DATA_AV counter, the taskqueue worker is backlogged. At this scale it should not be; at higher rates (thousands per second) it might be.

A more sensitive probe: add a `cv_signal` path that a user-space program waits on. The sysctl fires the simulated interrupt; the filter enqueues the task; the task updates `sc->intr_last_data` and broadcasts; the user-space thread that was waiting on the condition variable (via the cdev's `read`) wakes up. Round-trip latency from sysctl write to wake-up is the driver's interrupt-to-user-space latency, roughly, a useful number to know.

### Integration With Chapter 17's Fault Framework

A thought worth noting. Chapter 17's fault-injection framework (the `FAULT_MASK` and `FAULT_PROB` registers) applies to commands, not to interrupts. Chapter 19 can extend the framework by adding a "fault on next interrupt" option: a sysctl that makes the next filter call skip the acknowledgment, causing a storm on a level-triggered line.

This is an optional extension. The challenge exercises mention it; the chapter's main body does not require it.

### Wrapping Up Section 5

Simulating interrupts is a simple but effective technique. A sysctl writes `INTR_STATUS`, invokes the filter directly, and the filter drives the full pipeline: counter updates, acknowledgment, task enqueue, task execution. The technique lets a driver be exercised end-to-end on a lab target that does not naturally produce the expected interrupts, and it is cheap to keep in production drivers for regression testing and diagnostic access.

Section 6 is the last conceptual piece of core interrupt handling. It covers shared interrupts: what happens when multiple drivers listen on the same IRQ line, how a filter handler must identify whether the interrupt belongs to its device, and what `FILTER_STRAY` means in practice.



## Section 6: Handling Shared Interrupts

Section 4's Stage 2 filter already has the right return-value shape for shared IRQs: it returns `FILTER_STRAY` when `INTR_STATUS` is zero. Section 6 explores why that check is the entire discipline, what goes wrong when a handler gets it wrong, and when the `RF_SHAREABLE` flag is worth setting.

### Why Share an IRQ at All?

Two reasons.

First, **hardware constraints**. The classic PC architecture had 16 hardware IRQ lines; the I/O APIC expanded this to 24 on many chipsets. A system with 30 devices necessarily has some of them sharing lines. On modern x86 systems the shortage is less acute (hundreds of vectors), but on legacy PCI and on many arm64 SoCs sharing is normal.

Second, **driver portability**. A driver that handles shared interrupts correctly also handles exclusive interrupts correctly (the shared path is a superset). A driver that assumes exclusive interrupts breaks when the hardware changes or when another driver arrives on the same line. Writing for the shared case costs essentially nothing and future-proofs the driver.

On PCIe with MSI or MSI-X enabled (Chapter 20), each device has its own vectors and sharing is rarely needed. But even there, a driver that correctly handles a stray interrupt (by returning `FILTER_STRAY`) is a better driver than one that does not. The discipline transfers.

### The Flow on a Shared IRQ

When a shared IRQ fires, the kernel walks the list of filter handlers attached to the `intr_event` in registration order. Each filter runs, checks whether the interrupt belongs to its device, and returns accordingly:

- If the filter claims the interrupt (returns `FILTER_HANDLED` or `FILTER_SCHEDULE_THREAD`), the kernel continues with the next filter if any and aggregates the results. On modern kernels a filter that returns `FILTER_HANDLED` does not stop later filters from running; the kernel always walks the whole list.
- If the filter returns `FILTER_STRAY`, the kernel tries the next filter.

After all filters have run, if any filter claimed the interrupt, the kernel acknowledges at the interrupt controller and returns. If all filters returned `FILTER_STRAY`, the kernel increments a stray-interrupt counter; if the stray count exceeds a threshold, the kernel disables the IRQ (the drastic last resort).

A filter that returns `FILTER_STRAY` when the interrupt actually was for its device is a bug: the line stays asserted (level-triggered), the storm machinery kicks in, and the device does not get serviced. A filter that returns `FILTER_HANDLED` when the interrupt was not for its device is also a bug: another driver's interrupt is marked as serviced, its handler never runs, its data sits in its FIFO, and the user's network or disk stops working.

The discipline is to decide ownership precisely, based on the device's state, and return the right value.

### The INTR_STATUS Test

The standard way to decide ownership is to read a device register that tells you whether the interrupt is pending. On a device with a per-device INTR_STATUS register, the question is "is any bit in INTR_STATUS set?" If yes, the interrupt is mine. If no, it is not.

The Chapter 17 register layout makes this easy:

```c
status = bus_read_4(sc->bar_res, MYFIRST_REG_INTR_STATUS);
if (status == 0)
	return (FILTER_STRAY);
```

This is exactly what the Stage 2 filter already does. The pattern is robust: if the status register reads as zero, there is no pending event from this device, so the interrupt is not ours.

A subtle detail: the read of `INTR_STATUS` must happen before any state changes that could mask or reset the bits. Reading `INTR_STATUS` with the device in a mid-state is fine (the register reflects the device's current view); writing to other registers first and then reading `INTR_STATUS` could miss bits that the writes inadvertently cleared.

### What "Is It Mine?" Looks Like on Real Hardware

The INTR_STATUS test is textbook because the Chapter 17 register layout is textbook. Real devices come in flavours.

**Devices with a clean INTR_STATUS.** Most modern devices have a register that, when read as zero, definitively says "not mine". The Chapter 19 driver's filter shape applies directly.

**Devices with bits that are always set.** Some devices have pending-interrupt bits that stay set across interrupts (waiting for the driver to reset them). The filter must mask these out or check against a per-interrupt-class mask. The Chapter 17 register layout avoids this complication; real drivers occasionally face it.

**Devices with no INTR_STATUS at all.** A few older devices require the driver to read a separate register sequence (or to infer from state registers) whether the interrupt is pending. These drivers are more complex; the filter may need to acquire a spin lock and read several registers. The FreeBSD source has examples in a few embedded drivers.

**Devices with a global INTR_STATUS and per-source registers.** A common pattern on NICs: a top-level register reports which queue has a pending event, and per-queue registers contain the event details. The filter reads the top-level register to decide ownership; the ithread or task reads the per-queue registers to process events.

Chapter 19's driver uses the first flavour. The discipline for the other flavours is the same: read a register, decide.

### Returning FILTER_STRAY Correctly

The rule is simple: if the filter does not recognise any bit as belonging to a class it handles, return `FILTER_STRAY`.

```c
if (rv == 0)
	return (FILTER_STRAY);
```

The variable `rv` accumulates `FILTER_HANDLED` from each recognised bit. If no bit was recognised, `rv` is zero, and the filter has nothing to return but `FILTER_STRAY`.

A subtle corollary: a filter that recognises some bits but not others returns `FILTER_HANDLED` for the bits it recognised, and does not return `FILTER_STRAY` for the bits it did not. Setting a bit in `INTR_MASK` that the driver will not handle is a driver bug; the kernel cannot help.

An interesting edge case: a bit is set in `INTR_STATUS` but the driver does not recognise it (perhaps a new device revision added a bit the driver's code predates). The driver has two options:

1. Ignore the bit. Do not acknowledge it. Let it stay set. On a level-triggered line this produces a storm because the bit asserts the line forever. Bad.

2. Acknowledge the bit without doing any work. Write it back to `INTR_STATUS`. The device stops asserting for that bit, no storm, but the event is lost. On an essential event this is a functional bug; on a diagnostic event it may be acceptable.

The recommended pattern is option 2 with a log message: acknowledge unrecognised bits, log them at a reduced rate (to avoid log flooding if the bit is asserted continuously), and move on. This makes the driver robust against new hardware revisions at the cost of potentially losing information about unknown events.

```c
uint32_t unknown = status & ~(MYFIRST_INTR_DATA_AV |
    MYFIRST_INTR_ERROR | MYFIRST_INTR_COMPLETE);
if (unknown != 0) {
	atomic_add_64(&sc->intr_unknown_count, 1);
	bus_write_4(sc->bar_res, MYFIRST_REG_INTR_STATUS, unknown);
	rv |= FILTER_HANDLED;
}
```

This snippet is not in Stage 2's filter; it is a useful extension for Stage 3 or beyond.

### What Happens If Multiple Drivers Share an IRQ

A concrete scenario. Suppose the virtio-rnd device in a bhyve guest shares IRQ 19 with the AHCI controller. Both drivers have registered handlers. An interrupt arrives on IRQ 19.

The kernel walks the handler list in registration order. Suppose AHCI registered first, so its filter runs first:

1. AHCI filter: reads its INTR_STATUS, sees bits set (AHCI has pending I/O), acknowledges, returns `FILTER_HANDLED`.
2. `myfirst` filter: reads its INTR_STATUS, reads zero, returns `FILTER_STRAY`.

The kernel sees "at least one FILTER_HANDLED" and does not mark the interrupt as stray.

Now the reverse case. The virtio-rnd device has an event:

1. AHCI filter: reads its INTR_STATUS, sees zero, returns `FILTER_STRAY`.
2. `myfirst` filter: reads its INTR_STATUS, sees `DATA_AV`, acknowledges, returns `FILTER_HANDLED`.

The kernel sees one `FILTER_HANDLED` and is happy.

The key property is that each filter checks only its own device. No filter assumes the interrupt is its own; each decides from its device's state.

### What Happens If a Driver Gets It Wrong

A broken AHCI filter that returns `FILTER_HANDLED` whenever it fires (without checking status) would claim our `myfirst` interrupt. The `myfirst` filter would never run, `DATA_AV` would never be acknowledged, and the line would storm.

The fix is not on the `myfirst` side; it is on the AHCI side. In reality, all major FreeBSD drivers get the check right because the code has been audited and tested for years. The lesson is that the shared-IRQ protocol requires cooperation: every driver on the line must check its own state correctly.

The protection against a single broken driver is `hw.intr_storm_threshold`. When the kernel detects a run of interrupts all marked as stray (or all returning `FILTER_HANDLED` without any device actually having work), it will eventually mask the line. The machinery is detection, not prevention.

### Coexisting With Non-Sharing Drivers

A driver that allocates its IRQ with `RF_SHAREABLE` can coexist with drivers that allocate without sharing, provided the kernel can satisfy both requests. If our `myfirst` driver allocates first with `RF_SHAREABLE`, then AHCI tries to allocate exclusively, AHCI's allocation will fail (the line is already held by a driver that might not be exclusive). If AHCI allocates first without sharing, our `myfirst` allocation (with `RF_SHAREABLE`) will fail.

In practice, modern drivers almost always use `RF_SHAREABLE`. Legacy drivers occasionally omit it; if a reader's driver cannot be loaded because of an interrupt-allocation conflict, the fix is often to add `RF_SHAREABLE` to the allocation.

Exclusive allocation is appropriate for:

- Drivers with strict latency requirements that cannot tolerate other handlers on the line.
- Drivers that use `INTR_EXCL` for a specific kernel reason.
- Some legacy drivers that were written before shared-IRQ support matured.

For Chapter 19's driver, `RF_SHAREABLE` is the default and never wrong.

### The bhyve Virtio IRQ Topology

A practical detail about the Chapter 19 lab environment. The bhyve emulator maps each emulated PCI device to an IRQ line based on the slot's INTx pin. Multiple devices on the same slot's different functions share a line; different slots usually have different lines. The virtio-rnd device at slot 4 function 0 has its own pin.

In practice, on a bhyve guest with just a handful of emulated devices, each device often has its own IRQ line (no sharing). The `myfirst` driver with `RF_SHAREABLE` allocated on a non-shared line behaves identically to a non-shareable allocation; the flag is harmless.

To deliberately test shared-IRQ behaviour in bhyve, a reader can stack multiple virtio devices onto the same slot (different functions), forcing them to share a line. This is advanced and not necessary for basic Chapter 19 labs.

### The Starvation Concern

A shared-IRQ line has a potential starvation problem: a single driver that takes too long in its filter can delay every other driver on the line. Every filter sees its device's state as "unchanging" for the duration of the slow filter, and events can accumulate undetected.

The discipline is the same one Section 4 covered: filters must be fast. Tens or hundreds of microseconds of real work is usually the maximum a well-behaved filter does; anything slower moves to the task. A filter that does long work starves not only its own driver's upper layers but also every other driver on the line.

On MSI-X (Chapter 20), each vector has its own `intr_event`, so the starvation concern disappears for the specific driver pairs that use MSI-X. But the discipline still applies: a filter that takes a millisecond is hurting latency for every subsequent interrupt.

### False Positives and Defensive Handling

A useful property of the status-register check is that it is naturally tolerant of false positives from the kernel side. Occasionally, interrupt controllers report a spurious interrupt when no device is actually asserting (noise on the line, a race between edge-trigger and masking, a platform-specific quirk). The kernel dispatches, the filter reads INTR_STATUS, it is zero, the filter returns `FILTER_STRAY`, the kernel moves on.

This is a no-op for the driver. The count of stray interrupts goes up; nothing else changes.

Some drivers add a rate-limited log message to make spurious interrupts visible. A reasonable default is to log only if the rate exceeds a threshold:

```c
static struct timeval last_stray_log;
static int stray_rate_limit = 5;  /* messages per second */
if (rv == 0) {
	if (ppsratecheck(&last_stray_log, &stray_rate_limit, 1))
		device_printf(sc->dev, "spurious interrupt\n");
	return (FILTER_STRAY);
}
```

The `ppsratecheck(9)` utility limits the message rate. Without it, a line that is storming would flood `dmesg` with identical messages.

Chapter 19's driver does not include the rate-limited log in its Stage 2 filter; it is added in a challenge exercise.

### When the Filter Should Handle and the Task Should Not Run

A thought experiment. Imagine the filter recognises `ERROR` but not `DATA_AV`. The filter handles `ERROR` (acknowledges, increments counter) and returns `FILTER_HANDLED`. No task is enqueued. The device is satisfied; the line deasserts.

But `INTR_STATUS.DATA_AV` might still be set, because the filter did not acknowledge it (the filter did not recognise the bit as belonging to a class the driver handles). On a level-triggered line, the device keeps asserting for `DATA_AV`, a new interrupt fires, and the loop repeats.

This is a version of the "unknown bit storm" problem. The fix is to acknowledge every bit the driver is willing to see, even if the driver does not do anything with some of them. Setting `INTR_MASK` to only the bits the driver handles is the preventive measure; acknowledging unrecognised bits in the filter is the defensive measure.

### Wrapping Up Section 6

Shared interrupts are the common case on legacy PCI and still the right assumption to write for on modern hardware. A filter on a shared line must check whether the interrupt belongs to its device (usually by reading the device's INTR_STATUS register), handle the bits it recognises, acknowledge those bits, and return `FILTER_STRAY` if it recognised nothing. The discipline is small in code and large in reliability: a driver that gets it right coexists with every other well-behaved driver on its line, and `RF_SHAREABLE` in the allocation is the only additional line of code it needs.

Section 7 is the teardown section. It is short: `bus_teardown_intr` before `bus_release_resource`, drain the taskqueue before anything the task touches is freed, clear `INTR_MASK` so the device stops asserting, and verify the counters make sense. But the ordering is strict, and Chapter 19's detach path extends by exactly these steps.



## Section 7: Cleaning Up Interrupt Resources

The attach path gained three new operations (allocate IRQ, register handler, enable interrupts at the device); the detach path must undo each of them in strict reverse order. Section 7 is short because the pattern is now familiar; but the order matters in specific ways the previous sections did not touch on, and a mistake here produces kernel panics that the debug kernel is very good at catching and very good at making confusing to diagnose.

### The Required Order

From most-specific-to-most-general, the detach sequence at Chapter 19 Stage 2 is:

1. **Refuse if busy.** `myfirst_is_busy(sc)` returns true if the cdev is open or a command is in flight.
2. **Mark as no longer attached** so user-space paths refuse to start.
3. **Destroy the cdev** so no new user-space access begins.
4. **Disable interrupts at the device.** Clear `INTR_MASK` so the device stops asserting.
5. **Tear down the interrupt handler.** `bus_teardown_intr(9)` on `irq_res` with the saved cookie. After this returns, the kernel guarantees the filter will not run again.
6. **Drain the taskqueue.** `taskqueue_drain(9)` waits for any pending task to complete and prevents new ones from starting.
7. **Destroy the taskqueue.** `taskqueue_free(9)` shuts down the worker threads.
8. **Quiesce Chapter 17 simulation callouts** if `sc->sim` is non-NULL.
9. **Detach the Chapter 17 simulation** if attached.
10. **Detach the hardware layer** so `sc->hw` is freed.
11. **Release the IRQ resource** with `bus_release_resource(9)`.
12. **Release the BAR** with `bus_release_resource(9)`.
13. **Deinit the softc.**

Thirteen steps. Each does one thing. The hazards are in the ordering.

### Why Disable at the Device Before bus_teardown_intr

Clearing `INTR_MASK` before tearing down the handler is defensive. If we tore down the handler first, a pending interrupt at the device might fire and have no handler; the kernel would mark it as stray and eventually disable the line. Clearing `INTR_MASK` first stops the device from asserting, then the teardown removes the handler, and no interrupts can fire in between.

For MSI-X (Chapter 20), the logic is slightly different because each vector is independent. But the principle transfers: stop the source before removing the handler.

On real hardware this window is microseconds; a stray interrupt during it is rare. On bhyve, where the event rate is low, it essentially never happens. But careful drivers close the window anyway because careful drivers are the ones you want to read in production.

### Why bus_teardown_intr Before Releasing the Resource

`bus_teardown_intr` removes the driver's handler from the `intr_event`. After it returns, the kernel guarantees the filter will not run again. But the IRQ resource (the `struct resource *`) is still valid; the kernel has not freed it. `bus_release_resource` is what frees it.

If we released the resource first, the kernel's internal bookkeeping around the `intr_event` would see a handler registered against a resource that no longer exists. Depending on timing, this produces either an immediate failure during `bus_release_resource` (the kernel detects the handler is still attached) or a delayed problem later when the line tries to fire.

The safe ordering is always `bus_teardown_intr` first. The man page for `bus_setup_intr(9)` makes this explicit.

### Why Drain the Taskqueue Before Freeing the Softc

The filter may have enqueued a task that has not yet run. The task's function pointer is stored in the `struct task`, and the argument pointer is the softc. If we freed the softc before the task ran, the task would dereference a freed pointer and panic.

`taskqueue_drain(9)` on a specific task waits for that task to complete and prevents future enqueues of that task from running. Calling `taskqueue_drain` on `&sc->intr_data_task` is the exact right thing: it waits for the data-available task to finish.

After `taskqueue_drain` returns, no task run is in progress. The softc can be freed safely.

A common mistake: draining a single task with `taskqueue_drain(tq, &task)` is different from draining the whole taskqueue with `taskqueue_drain_all(tq)`. For a driver with multiple tasks on the same taskqueue, each task needs its own drain, or `taskqueue_drain_all` handles them as a group.

For Chapter 19's driver, there is one task, so a single `taskqueue_drain` suffices.

### Why bus_teardown_intr Before taskqueue_drain

The filter may still enqueue a task between the time `INTR_MASK` was cleared and the time `bus_teardown_intr` returns. If we drained the taskqueue before tearing down the handler, a still-running filter could enqueue a task after the drain, and the drain's guarantee would be violated.

The correct order is: clear `INTR_MASK` (stops new interrupts), tear down the handler (stops the filter from running again), drain the taskqueue (stops any previously enqueued task from running). Each step narrows the set of code paths that can touch the state.

### The Cleanup Code

Putting the order into the Chapter 19 Stage 2 detach:

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	/* Destroy the cdev so no new user-space access starts. */
	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	/* Disable interrupts at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);

	/* Tear down the interrupt handler. */
	if (sc->intr_cookie != NULL) {
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
		sc->intr_cookie = NULL;
	}

	/* Drain and destroy the interrupt taskqueue. */
	if (sc->intr_tq != NULL) {
		taskqueue_drain(sc->intr_tq, &sc->intr_data_task);
		taskqueue_free(sc->intr_tq);
		sc->intr_tq = NULL;
	}

	/* Quiesce Chapter 17 callouts (if sim attached). */
	myfirst_quiesce(sc);

	/* Detach Chapter 17 simulation if attached. */
	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	/* Detach the hardware layer. */
	myfirst_hw_detach(sc);

	/* Release the IRQ resource. */
	if (sc->irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release the BAR. */
	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

Thirteen distinct actions, each simple. The code is longer than earlier stages only because each new capability adds its own teardown step.

### Handling Partial-Attach Failure

The attach path's goto cascade in Section 3 had labels for each allocation step. With the interrupt handler registered at Stage 2, the cascade grows one more:

```c
fail_teardown_intr:
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL && sc->bar_res != NULL)
		bus_write_4(sc->bar_res, MYFIRST_REG_INTR_MASK, 0);
	MYFIRST_UNLOCK(sc);
	bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	sc->intr_cookie = NULL;
fail_release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	sc->irq_res = NULL;
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
```

Each cascade label undoes the step that succeeded before it. A failed `bus_setup_intr` jumps to `fail_release_irq` (skips teardown because the handler was not registered). A failed `make_dev` (cdev creation) jumps to `fail_teardown_intr` (tears down the handler before releasing the IRQ).

The taskqueue is initialised in `myfirst_init_softc` and destroyed in `myfirst_deinit_softc`, so the cascade does not need to handle it explicitly; whichever label reaches `fail_softc` gets the taskqueue cleaned up via deinit.

### Verifying the Teardown

After `kldunload myfirst` the kernel should be in a clean state. Specific checks:

- `kldstat -v | grep myfirst` returns nothing (module unloaded).
- `devinfo -v | grep myfirst` returns nothing (device detached).
- `vmstat -i | grep myfirst` returns nothing (interrupt event cleaned up).
- `vmstat -m | grep myfirst` returns nothing or shows zero `InUse` (malloc type drained).
- `dmesg | tail` shows the detach banner and no warnings or panics.

Failing any of these is a bug. The most common failure is `vmstat -i` showing a stale entry; this usually means `bus_teardown_intr` was not called. The second most common is `vmstat -m` showing live allocations; this usually means a task was enqueued and not drained, or the simulation was attached and not detached.

### Handling the "Handler Fired During Detach" Case

A subtle case worth thinking through. Suppose a real interrupt fires on a shared IRQ line between the cdev destruction and the `INTR_MASK` write. Another driver's device is asserting, our filter runs (because the line is shared), our filter reads `INTR_STATUS` (which is zero on our device), and returns `FILTER_STRAY`. No state is touched, no task is enqueued.

Suppose instead that the interrupt is from our device. Our `INTR_STATUS` has a bit set. The filter recognises it, acknowledges, enqueues a task, returns. The task enqueue happens against a taskqueue that has not yet been drained. The task runs later, acquires `sc->mtx`, reads `DATA_OUT` through the hardware layer (which is still attached because we haven't called `myfirst_hw_detach` yet). All safe.

Suppose the interrupt arrives after `INTR_MASK = 0` but before `bus_teardown_intr`. The device has stopped asserting for the bits we cleared, but an already-in-flight interrupt (queued in the interrupt controller) can still run the filter. The filter reads `INTR_STATUS`, sees zero (because the mask write was ahead of the device's internal state), returns `FILTER_STRAY`. The interrupt is counted as stray; the kernel ignores it.

Suppose the interrupt arrives after `bus_teardown_intr`. The handler is gone. The kernel's stray-interrupt accounting notices. After enough strays, the kernel disables the line. This is the scenario the `INTR_MASK = 0` step is designed to prevent; if the mask is cleared first, no strays can accumulate.

The code paths are all defensive. The debug kernel's assertions catch the common mistakes. A driver that follows the Chapter 19 order reliably tears down cleanly.

### What Goes Wrong When Teardown Is Skipped

A few scenarios that produce concrete symptoms.

**Handler not torn down, resource released.** `kldunload` calls `bus_release_resource` on the IRQ without `bus_teardown_intr`. The kernel detects an active handler on a resource being released and panics with a message like "releasing allocated IRQ with active handler". The debug kernel is reliable here.

**Handler torn down, taskqueue not drained.** The task is enqueued in the filter, the filter's last call happens just before teardown, the task has not yet run. The driver frees `sc` (via softc deinit) and unloads. The task's worker thread wakes up, runs the task function, dereferences the freed softc, panics with a null-pointer or use-after-free fault. The debug kernel's `WITNESS` or `MEMGUARD` may catch it; if not, the crash is at the task function's first memory access.

**Taskqueue drained, not freed.** `taskqueue_drain` succeeds, but `taskqueue_free` is skipped. The taskqueue's worker thread keeps running (idle). A `vmstat -m` shows the allocation. Not a functional bug, but a leak that accumulates across load-unload cycles.

**Simulation callouts not quiesced.** If the Chapter 17 simulation is attached (on a simulation-only build), its callouts are running. Without quiescing, they fire after detach has freed the register block, and access garbage. The `WITNESS` or `MEMGUARD` catch varies by hit; sometimes a plain null dereference is the symptom.

**INTR_MASK not cleared.** Real interrupts fire after detach starts. The filter (briefly, until teardown) handles them; after teardown, they are strays that the kernel eventually disables the line for. The line's disabled state is visible in `vmstat -i` (growing stray count) and in `dmesg` (kernel warnings).

Each of these is recoverable by fixing the teardown order. The Chapter 19 code is set up correctly; the hazards are for a reader who modifies the order.

### Sanity Testing the Teardown

A simple sanity test the reader can run after writing the detach code:

```sh
# Load.
sudo kldload ./myfirst.ko

# Fire a few simulated interrupts, make sure tasks run.
for i in 1 2 3 4 5; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
done
sleep 1
sysctl dev.myfirst.0.intr_task_invocations  # should be 5

# Unload.
sudo kldunload myfirst

# Check nothing leaked.
vmstat -m | grep myfirst  # should be empty
devinfo -v | grep myfirst   # should be empty
vmstat -i | grep myfirst    # should be empty
```

Running this sequence in a loop (twenty iterations in a shell loop) is a reasonable regression test: any leak accumulates, any crash manifests, any failure pattern becomes visible.

### Wrapping Up Section 7

Cleaning up interrupt resources is six small operations in the detach path: disable `INTR_MASK`, tear down the handler, drain and free the taskqueue, detach the hardware layer, release the IRQ, release the BAR. Each operation undoes exactly one attach-path operation. The order is the inverse of attach. The taskqueue drain is an important new concern specific to filter-plus-task drivers; a driver that skips it has a use-after-free bug waiting for the next load-unload cycle.

Section 8 is the housekeeping section: split the interrupt code into its own file, bump the version to `1.2-intr`, write `INTERRUPTS.md`, and run the regression pass. The driver is functionally complete after Section 7; Section 8 makes it maintainable.



## Section 8: Refactoring and Versioning Your Interrupt-Ready Driver

The interrupt handler is working. Section 8 is the housekeeping section. It splits the interrupt code into its own file, updates the module metadata, adds a new `INTERRUPTS.md` document, introduces a small set of interrupt-context CSR macros so the filter can access registers without the lock-asserting macros, bumps the version to `1.2-intr`, and runs the regression pass.

A reader who has made it this far may again be tempted to skip this section. It is the same temptation Chapter 18's Section 8 warned against, and the same refusal: a driver whose interrupt code is mixed into the PCI file, whose filter uses raw `bus_read_4` ad hoc, whose taskqueue setup is split across three files, becomes painful to extend. Chapter 20 adds MSI and MSI-X; Chapter 21 adds DMA. Both of them build on Chapter 19's interrupt code. A clean structure now saves effort across both.

### The Final File Layout

At the end of Chapter 19, the driver consists of these files:

```text
myfirst.c         - Main driver: softc, cdev, module events, data path.
myfirst.h         - Shared declarations: softc, lock macros, prototypes.
myfirst_hw.c      - Ch16 hardware access layer: CSR_* accessors,
                     access log, sysctl handlers.
myfirst_hw_pci.c  - Ch18 hardware layer extension: myfirst_hw_attach_pci.
myfirst_hw.h      - Register map and accessor declarations.
myfirst_sim.c     - Ch17 simulation backend.
myfirst_sim.h     - Ch17 simulation interface.
myfirst_pci.c     - Ch18 PCI attach: probe, attach, detach,
                     DRIVER_MODULE, MODULE_DEPEND, ID table.
myfirst_pci.h     - Ch18 PCI declarations.
myfirst_intr.c    - Ch19 interrupt handler: filter, task, setup, teardown.
myfirst_intr.h    - Ch19 interrupt interface.
myfirst_sync.h    - Part 3 synchronisation primitives.
cbuf.c / cbuf.h   - Ch10 circular buffer.
Makefile          - kmod build.
HARDWARE.md       - Ch16/17 register map.
LOCKING.md        - Ch15 onward lock discipline.
SIMULATION.md     - Ch17 simulation.
PCI.md            - Ch18 PCI support.
INTERRUPTS.md     - Ch19 interrupt handling.
```

`myfirst_intr.c` and `myfirst_intr.h` are new. `INTERRUPTS.md` is new. Every other file either existed before or was extended slightly (the softc gained fields; the PCI attach calls into `myfirst_intr.c`).

The rule of thumb remains: each file has one responsibility. `myfirst_intr.c` owns the interrupt handler, the deferred task, and the simulated-interrupt sysctl. `myfirst_pci.c` owns the PCI attach but delegates interrupt setup and teardown to functions exported by `myfirst_intr.c`.

### The Final Makefile

```makefile
# Makefile for the Chapter 19 myfirst driver.

KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.2-intr\"

# CFLAGS+= -DMYFIRST_SIMULATION_ONLY
# CFLAGS+= -DMYFIRST_PCI_ONLY

.include <bsd.kmod.mk>
```

One additional source file in the SRCS list; the version string bumped; the rest unchanged.

### The Version String

`1.1-pci` to `1.2-intr`. The bump reflects that the driver has acquired a significant new capability (interrupt handling) without changing any user-visible interface (the cdev still does what it did). A minor-version bump is appropriate.

Later chapters continue: `1.3-msi` after Chapter 20's MSI and MSI-X work; `1.4-dma` after Chapters 20 and 21 add DMA. Each minor version reflects one significant capability addition.

### The myfirst_intr.h Header

The header exports the interrupt layer's public interface to the rest of the driver:

```c
#ifndef _MYFIRST_INTR_H_
#define _MYFIRST_INTR_H_

#include <sys/types.h>
#include <sys/taskqueue.h>

struct myfirst_softc;

/* Interrupt setup and teardown, called from the PCI attach path. */
int  myfirst_intr_setup(struct myfirst_softc *sc);
void myfirst_intr_teardown(struct myfirst_softc *sc);

/* Register sysctl nodes specific to the interrupt layer. */
void myfirst_intr_add_sysctls(struct myfirst_softc *sc);

/* Interrupt-context accessor macros. These do not acquire sc->mtx
 * and therefore are safe in the filter. They are NOT a replacement
 * for CSR_READ_4 / CSR_WRITE_4 in other contexts. */
#define ICSR_READ_4(sc, off) \
	bus_read_4((sc)->bar_res, (off))
#define ICSR_WRITE_4(sc, off, val) \
	bus_write_4((sc)->bar_res, (off), (val))

#endif /* _MYFIRST_INTR_H_ */
```

The public API is three functions (`myfirst_intr_setup`, `myfirst_intr_teardown`, `myfirst_intr_add_sysctls`) and two accessor macros (`ICSR_READ_4`, `ICSR_WRITE_4`). The "I" prefix is for "interrupt-context"; these macros do not take `sc->mtx`, so they are safe in the filter.

### The myfirst_intr.c File

The full file is in the companion examples tree; here is the core structure:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_intr.h"

/* Deferred task for data-available events. */
static void myfirst_intr_data_task_fn(void *arg, int npending);

/* The filter handler. Exported so the simulated-interrupt sysctl can
 * call it directly. */
int myfirst_intr_filter(void *arg);

int
myfirst_intr_setup(struct myfirst_softc *sc)
{
	int error;

	TASK_INIT(&sc->intr_data_task, 0, myfirst_intr_data_task_fn, sc);
	sc->intr_tq = taskqueue_create("myfirst_intr", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->intr_tq);
	taskqueue_start_threads(&sc->intr_tq, 1, PI_NET,
	    "myfirst intr taskq");

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->irq_res == NULL)
		return (ENXIO);

	error = bus_setup_intr(sc->dev, sc->irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE,
	    myfirst_intr_filter, NULL, sc,
	    &sc->intr_cookie);
	if (error != 0) {
		bus_release_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid,
		    sc->irq_res);
		sc->irq_res = NULL;
		return (error);
	}

	bus_describe_intr(sc->dev, sc->irq_res, sc->intr_cookie, "legacy");

	/* Enable the interrupts we care about at the device. */
	MYFIRST_LOCK(sc);
	if (sc->hw != NULL)
		CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK,
		    MYFIRST_INTR_DATA_AV | MYFIRST_INTR_ERROR |
		    MYFIRST_INTR_COMPLETE);
	MYFIRST_UNLOCK(sc);

	return (0);
}

void
myfirst_intr_teardown(struct myfirst_softc *sc)
{
	/* Disable interrupts at the device. */
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
}

int
myfirst_intr_filter(void *arg)
{
	/* ... as in Section 4 ... */
}

static void
myfirst_intr_data_task_fn(void *arg, int npending)
{
	/* ... as in Section 4 ... */
}

void
myfirst_intr_add_sysctls(struct myfirst_softc *sc)
{
	/* ... counters and intr_simulate sysctl ... */
}
```

The file is about 250 lines at Stage 4. `myfirst_pci.c` shrinks correspondingly: the interrupt allocation and setup move out.

### The Refactored PCI Attach

After moving interrupt code into `myfirst_intr.c`, `myfirst_pci_attach` becomes:

```c
static int
myfirst_pci_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	error = myfirst_init_softc(sc);
	if (error != 0)
		return (error);

	/* Step 1: allocate BAR 0. */
	sc->bar_rid = PCIR_BAR(0);
	sc->bar_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->bar_rid, RF_ACTIVE);
	if (sc->bar_res == NULL) {
		device_printf(dev, "cannot allocate BAR0\n");
		error = ENXIO;
		goto fail_softc;
	}

	/* Step 2: attach the hardware layer. */
	error = myfirst_hw_attach_pci(sc, sc->bar_res,
	    rman_get_size(sc->bar_res));
	if (error != 0)
		goto fail_release_bar;

	/* Step 3: set up interrupts. */
	error = myfirst_intr_setup(sc);
	if (error != 0) {
		device_printf(dev, "interrupt setup failed (%d)\n", error);
		goto fail_hw;
	}

	/* Step 4: create cdev. */
	sc->cdev = make_dev(&myfirst_cdevsw, sc->unit, UID_ROOT,
	    GID_WHEEL, 0600, "myfirst%d", sc->unit);
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail_intr;
	}
	sc->cdev->si_drv1 = sc;

	/* Step 5: register sysctls. */
	myfirst_intr_add_sysctls(sc);

	sc->pci_attached = true;
	return (0);

fail_intr:
	myfirst_intr_teardown(sc);
fail_hw:
	myfirst_hw_detach(sc);
fail_release_bar:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid, sc->bar_res);
	sc->bar_res = NULL;
fail_softc:
	myfirst_deinit_softc(sc);
	return (error);
}
```

The PCI attach is shorter; the interrupt details are hidden behind `myfirst_intr_setup`. The goto cascade is four labels instead of six (the interrupt-specific labels moved into `myfirst_intr.c`).

### The Refactored Detach

```c
static int
myfirst_pci_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	if (myfirst_is_busy(sc))
		return (EBUSY);

	sc->pci_attached = false;

	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}

	myfirst_intr_teardown(sc);

	if (sc->sim != NULL)
		myfirst_sim_detach(sc);

	myfirst_hw_detach(sc);

	if (sc->bar_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->bar_rid,
		    sc->bar_res);
		sc->bar_res = NULL;
	}

	myfirst_deinit_softc(sc);

	device_printf(dev, "detached\n");
	return (0);
}
```

The interrupt-specific teardown is one call to `myfirst_intr_teardown`, which encapsulates the mask-clear, teardown, drain, and resource-release steps.

### The INTERRUPTS.md Document

The new document lives next to the driver source. Its role is to describe the driver's interrupt handling to a future reader without requiring them to read `myfirst_intr.c`:

```markdown
# Interrupt Handling in the myfirst Driver

## Allocation and Setup

The driver allocates a single legacy PCI IRQ through
`bus_alloc_resource_any(9)` with `SYS_RES_IRQ`, `rid = 0`,
`RF_SHAREABLE | RF_ACTIVE`. The filter handler is registered through
`bus_setup_intr(9)` with `INTR_TYPE_MISC | INTR_MPSAFE`. A taskqueue
named "myfirst_intr" is created with one worker thread at `PI_NET`
priority.

On successful setup, `INTR_MASK` is written with
`DATA_AV | ERROR | COMPLETE` so the device will assert the line for
those three event classes.

## Filter Handler

`myfirst_intr_filter(sc)` reads `INTR_STATUS`. If zero, it returns
`FILTER_STRAY` (shared-IRQ defence). Otherwise it inspects each of
the three recognised bits, increments a per-bit counter atomically,
writes the bit back to `INTR_STATUS` to acknowledge the device, and
(for `DATA_AV`) enqueues `intr_data_task` on the taskqueue.

The filter returns `FILTER_HANDLED` if any bit was recognised, or
`FILTER_STRAY` otherwise.

## Deferred Task

`myfirst_intr_data_task_fn(sc, npending)` runs in thread context on
the taskqueue's worker thread. It acquires `sc->mtx`, reads
`DATA_OUT`, stores the value in `sc->intr_last_data`, broadcasts
`sc->data_cv` to wake pending readers, and releases the lock.

## Simulated Interrupt sysctl

`dev.myfirst.N.intr_simulate` is write-only; writing a bitmask to it
sets the corresponding bits in `INTR_STATUS` and invokes
`myfirst_intr_filter` directly. This exercises the full pipeline
without needing real IRQ events.

## Teardown

`myfirst_intr_teardown(sc)` runs during detach. It clears
`INTR_MASK`, calls `bus_teardown_intr`, drains and destroys the
taskqueue, and releases the IRQ resource. The order is strict:
mask-clear before teardown (so strays do not accumulate), teardown
before drain (so no new task enqueues happen), drain before free
(so no task runs against freed state).

## Interrupt-Context Accessor Macros

Since the filter runs in primary interrupt context, it cannot take
`sc->mtx`. Two macros in `myfirst_intr.h` hide the raw
`bus_read_4`/`bus_write_4` calls without asserting any lock: `ICSR_READ_4`
and `ICSR_WRITE_4`. Use them only in contexts where a sleep lock
would be illegal.

## Known Limitations

- Only the legacy PCI INTx line is handled. MSI and MSI-X are
  Chapter 20.
- The filter coalesces per-bit counters via atomic ops; the task
  runs at a single priority. Per-queue or per-priority designs are
  later-chapter topics.
- Interrupt storm detection is managed by the kernel
  (`hw.intr_storm_threshold`); the driver does not implement its
  own storm mitigation.
- Chapter 17 simulation callouts are not active on the PCI build;
  the simulated-interrupt sysctl is the way to drive the pipeline
  on a bhyve lab target.
```

Five minutes to read; a clear picture of the interrupt layer's shape.

### The Regression Pass

The Chapter 19 regression is a superset of Chapter 18's:

1. Compile cleanly. `make` succeeds; no warnings.
2. Load. `kldload ./myfirst.ko` succeeds; `dmesg` shows the attach sequence.
3. Attach to a real PCI device. `devinfo -v` shows the BAR and the IRQ.
4. No `[GIANT-LOCKED]` warning.
5. `vmstat -i | grep myfirst` shows the `intr_event`.
6. `sysctl dev.myfirst.0.intr_count` starts at zero.
7. Simulated interrupt. `sudo sysctl dev.myfirst.0.intr_simulate=1`; counters increment; task runs.
8. Rate test. Set `intr_sim_period_ms` to 100; check counters after 10 seconds.
9. Detach. `devctl detach myfirst0`; `dmesg` shows clean detach.
10. Reattach. `devctl attach pci0:0:4:0`; full attach cycle runs.
11. Unload. `kldunload myfirst`; `vmstat -m | grep myfirst` shows zero live allocations; `vmstat -i | grep myfirst` returns nothing.

Running the full regression takes a minute or two per iteration. A CI job that runs it twenty times in a loop is the kind of guard that catches regressions introduced by Chapter 20's and Chapter 21's extensions.

### What the Refactor Accomplished

The Chapter 19 code is one file smaller than it would have been without the refactor; one new document exists; the version number has moved forward by one. The driver is recognisably FreeBSD, structurally parallel to a production driver in `/usr/src/sys/dev/`, and ready to accept Chapter 20's MSI-X machinery and Chapter 21's DMA machinery without another reorganisation.

### Wrapping Up Section 8

The refactor follows the same shape Chapters 16 through 18 established. A new file owns the new responsibility. A new header exports the public interface. A new document explains the behaviour. The version bumps; the regression passes; the driver stays maintainable. Nothing dramatic; a little housekeeping; a clean codebase to build on.

The instructional body of Chapter 19 is complete. Labs, challenges, troubleshooting, a wrap-up, and the bridge to Chapter 20 follow.



## Reading a Real Driver Together: mgb(4)'s Interrupt Path

Before the labs, a short walk through a real driver that uses the same filter-plus-task pattern Chapter 19 teaches. `/usr/src/sys/dev/mgb/if_mgb.c` is the FreeBSD driver for Microchip's LAN743x gigabit Ethernet controllers. It is readable, it is production-quality, and its interrupt handling is about the level of complexity Chapter 19's vocabulary covers.

This section walks through the interrupt-relevant parts of `mgb_legacy_intr` and the setup code, and flags where each piece corresponds to a Chapter 19 concept.

### The Legacy Filter

The filter handler for `mgb(4)`'s legacy-IRQ path:

```c
int
mgb_legacy_intr(void *xsc)
{
	struct mgb_softc *sc;
	if_softc_ctx_t scctx;
	uint32_t intr_sts, intr_en;
	int qidx;

	sc = xsc;
	scctx = iflib_get_softc_ctx(sc->ctx);

	intr_sts = CSR_READ_REG(sc, MGB_INTR_STS);
	intr_en = CSR_READ_REG(sc, MGB_INTR_ENBL_SET);
	intr_sts &= intr_en;

	/* TODO: shouldn't continue if suspended */
	if ((intr_sts & MGB_INTR_STS_ANY) == 0)
		return (FILTER_STRAY);

	if ((intr_sts &  MGB_INTR_STS_TEST) != 0) {
		sc->isr_test_flag = true;
		CSR_WRITE_REG(sc, MGB_INTR_STS, MGB_INTR_STS_TEST);
		return (FILTER_HANDLED);
	}
	if ((intr_sts & MGB_INTR_STS_RX_ANY) != 0) {
		for (qidx = 0; qidx < scctx->isc_nrxqsets; qidx++) {
			if ((intr_sts & MGB_INTR_STS_RX(qidx))){
				iflib_rx_intr_deferred(sc->ctx, qidx);
			}
		}
		return (FILTER_HANDLED);
	}
	if ((intr_sts & MGB_INTR_STS_TX_ANY) != 0) {
		for (qidx = 0; qidx < scctx->isc_ntxqsets; qidx++) {
			if ((intr_sts & MGB_INTR_STS_RX(qidx))) {
				CSR_WRITE_REG(sc, MGB_INTR_ENBL_CLR,
				    MGB_INTR_STS_TX(qidx));
				CSR_WRITE_REG(sc, MGB_INTR_STS,
				    MGB_INTR_STS_TX(qidx));
				iflib_tx_intr_deferred(sc->ctx, qidx);
			}
		}
		return (FILTER_HANDLED);
	}

	return (FILTER_SCHEDULE_THREAD);
}
```

Walk through it. The filter reads two registers (`INTR_STS` and `INTR_ENBL_SET`), ANDs them to get the subset of pending interrupts that are enabled, and checks whether any bit is set. If none, it returns `FILTER_STRAY`, the Chapter 19 discipline for shared IRQs.

For each class of interrupt (test, receive, transmit), the filter acknowledges the relevant bits in `INTR_STS` (by writing them back) and schedules deferred processing. `iflib_rx_intr_deferred` is the iflib framework's way of scheduling receive-queue work; conceptually it is the same as Chapter 19's `taskqueue_enqueue`.

A line worth noting: the test-interrupt handler writes `INTR_STS` but also sets a flag (`sc->isr_test_flag = true`). This is the driver's way of signalling user-space code (via a sysctl or ioctl) that a test interrupt fired. Chapter 19's equivalent is the `intr_count` counter.

The last return is `FILTER_SCHEDULE_THREAD`. This fires if none of the specific bit classes matched but `MGB_INTR_STS_ANY` did. The ithread handles the residual case. Chapter 19's driver does not have this particular fall-through because it does not have a registered ithread; `mgb(4)` does.

### What the mgb Filter Teaches

Three lessons transfer directly to Chapter 19's filter:

1. **Read-AND-mask.** `intr_sts & intr_en` ensures the filter only reports interrupts that were actually enabled. A device may internally report events the driver has masked out; the AND filters them.
2. **Acknowledge per-bit.** Each bit class is acknowledged individually (by writing the specific bits back). The filter does not write `0xffffffff`; it writes only the bits it handled.
3. **Defer per-queue work.** Each receive queue and transmit queue has its own deferred path. Chapter 19's simpler driver has one task; `mgb(4)`'s multi-queue driver has many.

### Interrupt Setup in mgb

Searching for `bus_setup_intr` in `if_mgb.c` shows several call sites, one for the legacy IRQ path and one for each MSI-X vector:

```c
if (bus_setup_intr(sc->dev, sc->irq[0], INTR_TYPE_NET | INTR_MPSAFE,
    mgb_legacy_intr, NULL, sc, &sc->irq_tag[0]) != 0) {
	/* ... */
}
```

The pattern is exactly Chapter 19's: filter handler, no ithread, `INTR_MPSAFE`, softc as argument, cookie returned. The only difference is `INTR_TYPE_NET` instead of `INTR_TYPE_MISC` (the driver targets networking).

### Interrupt Teardown in mgb

The teardown pattern is distributed across `iflib(9)`'s helpers, which handle the drain and release for the driver. A bespoke driver outside the iflib framework does the teardown explicitly; Chapter 19's driver does it explicitly.

### What the Walkthrough Teaches

`mgb(4)`'s interrupt path is not a toy. It is a production-quality implementation of the same pattern Chapter 19's driver follows. A reader who can read `mgb_legacy_intr` with comprehension has internalised the Chapter 19 vocabulary. The file is freely available; reading the surrounding code (the attach path, the ithread, the iflib integration) deepens the understanding further.

Worth reading after `mgb(4)`: `/usr/src/sys/dev/e1000/em_txrx.c` for MSI-X multi-vector patterns (Chapter 20 material), `/usr/src/sys/dev/usb/controller/xhci_pci.c` for a USB host controller's interrupt path (Chapter 21+), and `/usr/src/sys/dev/ahci/ahci_pci.c` for a storage controller's interrupt path.



## A Deeper Look at Interrupt Context

Section 2 listed what may and may not happen in a filter. This section goes one level deeper and explains why each rule exists. A reader who understands the why can reason about unfamiliar constraints (new locks, new kernel APIs) and predict whether they are filter-safe.

### The Stack Situation

When the CPU takes an interrupt, it saves the interrupted thread's registers and switches to a kernel interrupt stack. The interrupt stack is small (a few KB, platform-dependent), is per-CPU, and is shared among all interrupts on that CPU. The filter runs on this stack.

Two implications:

First, the filter has limited stack space. A filter that allocates a large array on the stack (hundreds of bytes or more) can overflow the interrupt stack. The symptom is usually a panic, sometimes a silent corruption of whatever sits next to the stack in memory. The rule is: filters have small stack budgets. Big arrays belong to the task.

Second, the stack is shared among interrupts. A filter that sleeps (hypothetically, though it cannot) would leave the stack occupied; other interrupts on the same CPU could not reuse it. Even if sleep were allowed, it would not be free. The small-stack constraint is one reason the filter must be short.

### Why No Sleep Locks

A sleep mutex (default `mtx(9)`) may block: if another thread holds the mutex, `mtx_lock` puts the calling thread to sleep on the mutex. In the filter's context:

- There is no calling "thread" in the usual sense. The interrupted thread was suspended mid-instruction; the filter is a kernel-side excursion on the CPU.
- Sleeping from the interrupt would stall the CPU: the interrupt stack is occupied, no other interrupts can run on this CPU, and the scheduler cannot easily schedule a different thread without a working kernel state.

The kernel could, in principle, handle this (some kernels do). FreeBSD's design is to prohibit it. The prohibition is enforced by `WITNESS` on debug kernels: any attempt to acquire a sleepable lock in interrupt context produces an immediate panic.

Spin mutexes (`MTX_SPIN` mutexes) are safe because they do not sleep; they just spin. A filter acquiring a spin mutex is fine.

### Why No Sleeping malloc

`malloc(M_WAITOK)` calls the VM page allocator and may sleep if the system is low on memory. Same problem as with locks: the caller cannot be suspended. `malloc(M_NOWAIT)` is the alternative; it may fail, but it never sleeps.

In the filter, the only safe options are `M_NOWAIT`, `UMA` zones (which have their own bounded allocators), or pre-allocated buffers. Chapter 19's driver does not allocate in the filter at all; all the memory the filter needs is in the softc, pre-allocated during attach.

### Why No Condition Variables

`cv_wait` and `cv_timedwait` sleep. The filter cannot sleep. `cv_signal` and `cv_broadcast` do not sleep, but they acquire a sleep mutex internally in most usages; a filter using them must be careful. The Chapter 19 task handles the `cv_broadcast`; the filter only enqueues the task.

### Why the Filter Cannot Re-enter Itself

The kernel's interrupt dispatch disables further interrupts on the CPU while the filter is running (on most platforms; some architectures use priority levels instead). This means the filter cannot recursively fire itself, even if its device asserts the line during execution. Any such assertion is queued and fires after the filter returns.

A consequence: the filter does not need internal re-entry protection. A single `sc->intr_count++` is safe from the filter's perspective against simultaneous filter calls on the same CPU. It may still race with other code (the task, user-space reads), which is why the atomic operation is used, but the filter does not race with itself.

### Why Atomic Operations Are Safe

Atomic operations in FreeBSD are implemented as CPU instructions that are, by definition, atomic. They do not take a lock; they do not sleep; they do not block. They are safe in every context, including the filter.

Chapter 19 uses `atomic_add_64` extensively: for the interrupt counter, for each per-bit counter, and for task-invocation counts. The operations are cheap (a few cycles) and predictable (no scheduling involved).

### Why the ithread Gets More Rope

The ithread runs in thread context. It has:

- Its own stack (normal kernel thread stack, much larger than the interrupt stack).
- Its own scheduling priority (elevated, but still a normal thread).
- The ability to sleep if the scheduler decides to.

Its constraints are the usual thread-context rules: hold sleepable locks in order (to prevent deadlocks), avoid holding a lock while calling unbounded code, use `M_WAITOK` if the allocation might fail otherwise, and so on. The Chapter 13 and 15 disciplines apply.

The ithread's elevated priority means it should not block for arbitrary lengths of time. Blocking for microseconds (a short mutex contention) is fine; blocking for seconds starves every other ithread on the system.

### Why taskqueue Threads Have Still More Rope

A taskqueue's worker thread is a regular kernel thread, usually at normal priority (or at the priority the driver specified in `taskqueue_start_threads`). It can sleep, it can block on any sleepable lock, it can allocate arbitrarily. It is the most flexible of the three contexts.

The trade-off is that taskqueue work is less timely than ithread work. The taskqueue's worker may not run immediately; the scheduler decides. For latency-critical work, the ithread is better; for bulk work, the taskqueue is simpler.

Chapter 19's driver uses the taskqueue because the bulk work it does (reading DATA_OUT, updating the softc, broadcasting a condition variable) is not latency-critical. Chapter 20's and Chapter 21's drivers may choose ithreads or taskqueues differently depending on their workload.

### How Interrupt Context Interacts With Locking

The locking discipline introduced in Part 3 still applies, with one addition: know which locks you can take in each context.

**Filter context.** Only spin locks, atomics, lock-free algorithms. No sleepable mutexes, sx locks, rwlocks, or `mtx` initialised without `MTX_SPIN`.

**ithread context.** All lock types. Obeys the project's lock ordering as defined in `LOCKING.md`.

**Taskqueue worker context.** All lock types. Obeys the project's lock ordering. May sleep arbitrarily if needed (though the driver author should not).

**Thread context in general (cdev open/read/write/ioctl, sysctl handlers).** All lock types. Obeys the project's lock ordering.

For Chapter 19's driver, the filter takes no locks (uses atomics), the task takes `sc->mtx` (a sleep mutex) via `MYFIRST_LOCK`, and the sysctl handlers take `sc->mtx` too. The discipline is preserved.

### The "Giant" Footnote

Older BSDs used a single global lock called Giant to serialise the whole kernel. When FreeBSD introduced SMPng (fine-grained locking) in the late 1990s and early 2000s, most of the kernel was converted, but some legacy paths still hold Giant. Drivers that do not set `INTR_MPSAFE` are automatically wrapped with Giant acquisition around their handlers; `WITNESS` may complain about lock-ordering issues involving Giant.

Chapter 19's driver sets `INTR_MPSAFE` and does not touch Giant. Modern FreeBSD driver conventions deprecate Giant in driver code. The footnote is here because a reader who searches for "Giant" in `kern_intr.c` will find references; they are backward-compatibility artefacts.



## A Deeper Look at CPU Affinity for Interrupts

A short appendix on interrupt affinity, which Section 2 introduced briefly. The deep treatment belongs to Chapter 20 (when multiple MSI-X vectors are available) and to later scalability chapters; Chapter 19's coverage is a starting point.

### What Affinity Means

Interrupt affinity is the set of CPUs on which an interrupt is allowed to fire. For a single-CPU system, affinity is trivial (one CPU). For multi-CPU systems, affinity becomes interesting: routing an interrupt to a specific CPU (rather than letting the interrupt controller pick) can improve cache locality, reduce cross-socket traffic, and align interrupt handling with thread placement.

On x86, the I/O APIC has a programmable destination field per IRQ; the kernel uses this to route IRQs. On arm64, the GIC has similar facilities. FreeBSD's `bus_bind_intr(9)` is the portable API that configures the affinity for a specific IRQ resource.

### Default Behaviour

Without explicit binding, FreeBSD spreads interrupts across CPUs using a round-robin or platform-specific algorithm. For a single-interrupt driver like Chapter 19's, this usually means the interrupt fires on whichever CPU the kernel decided at boot. The current affinity is visible through `cpuset -g -x <irq>`; the per-CPU breakdown of firings for a given IRQ is not part of `vmstat -i`'s default output (which aggregates all firings for the `intr_event` into one count) but can be reconstructed from kernel tooling when the platform supports it.

For many drivers the default is fine. The interrupt rate is low enough that affinity does not matter, or the work is brief enough that cross-CPU costs are negligible. Chapter 19's driver is in this category.

### When Affinity Matters

Three scenarios where a driver author wants explicit affinity:

1. **High interrupt rate.** A NIC handling ten gigabits of traffic fires tens of thousands of interrupts per second. The overhead of moving interrupt work between CPUs becomes a real cost. Binding each receive queue's MSI-X vector to a specific CPU keeps its cache lines warm.
2. **NUMA locality.** On multi-socket systems, the device's PCIe root complex is physically attached to one socket. Interrupts from that device are cheaper to handle on CPUs in the same NUMA node as the root complex. Placement matters for both latency and throughput.
3. **Real-time constraints.** A system that needs low-latency response on specific CPUs (for real-time applications) may pin housekeeping interrupts away from those CPUs. `bus_bind_intr` lets the driver participate in this partitioning.

### The bus_bind_intr API

The function signature:

```c
int bus_bind_intr(device_t dev, struct resource *r, int cpu);
```

`cpu` is an integer CPU ID in the range 0 to `mp_ncpus - 1`. On success, the interrupt is routed to that CPU. On failure, the function returns an errno (most commonly `EINVAL` if the platform does not support rebinding or the CPU is invalid).

The call goes after `bus_setup_intr`:

```c
error = bus_setup_intr(dev, irq_res, flags, filter, ihand, arg,
    &cookie);
if (error == 0)
	bus_bind_intr(dev, irq_res, preferred_cpu);
```

Chapter 19's driver does not bind its interrupt. A challenge exercise adds a sysctl that lets the operator set the preferred CPU.

### The Kernel's CPU-Set Abstraction

A more sophisticated API: `bus_get_cpus(9)` lets the driver query which CPUs are considered "local" for a device, useful for multi-queue drivers that want to spread interrupts across a NUMA-local subset of CPUs. The `LOCAL_CPUS` and `INTR_CPUS` cpusets from `/usr/src/sys/sys/bus.h` expose this information.

Chapter 20's MSI-X work will use `bus_get_cpus(9)` to place per-queue interrupts on different CPUs in the device's local NUMA node. Chapter 19's single-interrupt driver does not need the complexity.

### Observing Affinity

The `cpuset -g -x <irq>` command shows the current CPU mask for an IRQ. For the `myfirst` driver on a multi-CPU system, obtain the IRQ number from `devinfo -v | grep -A 5 myfirst0`, bind the interrupt to (say) CPU 1 with `cpuset -l 1 -x <irq>`, and confirm with `cpuset -g -x <irq>`.

The details are platform-specific. On x86 the I/O APIC (or MSI routing) implements the request; on arm64 the GIC redistributor does. Some architectures refuse rebinding and return an error; a cooperative driver's `bus_bind_intr` call treats that as a non-fatal hint.



## A Deeper Look at Interrupt Storm Detection

The FreeBSD kernel has built-in protection against a specific failure mode: a level-triggered IRQ that fires continuously because the driver fails to acknowledge. The protection is called interrupt storm detection, is implemented in `/usr/src/sys/kern/kern_intr.c`, and is controlled by a single sysctl.

### The hw.intr_storm_threshold Sysctl

```c
static int intr_storm_threshold = 0;
SYSCTL_INT(_hw, OID_AUTO, intr_storm_threshold, CTLFLAG_RWTUN,
    &intr_storm_threshold, 0,
    "Number of consecutive interrupts before storm protection is enabled");
```

The default is zero (storm detection disabled). Setting the sysctl to a positive value enables detection: if an `intr_event` delivers more than N interrupts in a row without any other interrupt happening on the same CPU, the kernel assumes a storm and throttles the event.

Throttling means the kernel pauses (via `pause("istorm", 1)`) before running the handlers again. The pause is a single clock tick, which on most systems is a millisecond or so. The effect is to cap the rate at which a storming source can consume CPU.

### When to Enable Detection

Default off is the production setting. Enabling storm detection means the kernel pauses interrupts when it thinks a storm is happening; if the detection is wrong (a high-rate legitimate interrupt, say a 10-gigabit NIC), the pause is a performance bug.

For driver development, enabling storm detection is useful: a forgotten acknowledgment in the filter produces an interrupt storm, which the kernel detects and throttles (and logs to `dmesg`). Without detection, the storm consumes a CPU forever; with detection, the storm is visible and throttled.

A reasonable development-time setting is `hw.intr_storm_threshold=1000`. A thousand consecutive interrupts on the same event without interleaving is unusual for legitimate traffic and reliably flags a storm.

### What a Storm Looks Like

In `dmesg`:

```text
interrupt storm detected on "irq19: myfirst0:legacy"; throttling interrupt source
```

Repeated at rate-limited intervals (once per second by default, governed by the `ppsratecheck` inside the kernel's storm code). The interrupt source is named; the driver can be identified from the name.

The kernel does not disable the line permanently; it paces the handler. After the storm ends (perhaps because the driver was unloaded, or the device stopped asserting), the handler resumes at full rate.

### Driver-Side Storm Mitigation

A driver can implement its own storm mitigation. The classic technique is:

1. Count interrupts in a rolling window.
2. If the rate exceeds a threshold, mask the device's interrupts (via `INTR_MASK`) and schedule a task to re-enable them later.
3. In the task, inspect the device, clear whatever is causing the storm, and re-enable.

This is more invasive than the kernel's default. Most drivers do not implement it. The Chapter 19 driver does not; the kernel's threshold is sufficient for the scenarios the chapter exercises.

### The Relationship to Shared IRQs

On a shared IRQ line, one driver's storm can interfere with another driver's legitimate interrupts. The kernel's storm detection is per-event, not per-handler, so if one driver's handler is slow or incorrect, the whole event throttles. This is a strong argument for writing correct filters: the storm impact is not confined to the buggy driver.



## A Mental Model for Filter vs ithread Selection

Beginners often struggle with the decision between filter-only, filter-plus-ithread, filter-plus-taskqueue, and ithread-only. This section provides a decision framework that helps in most situations, grounded in questions a driver author can answer about their specific device.

### Four Questions

Ask these about the interrupt's work:

1. **Can every piece of the work be done in primary context?** If yes (all state access through spin locks or atomics; all acknowledgments through BAR writes; no sleeping), filter-only is the cleanest choice.
2. **Does any piece of the work require a sleep lock or a condition-variable broadcast?** If yes, the bulk work must go in thread context. The choice is between ithread and taskqueue.
3. **Is the deferred work scheduled from anywhere besides the interrupt?** If yes (sysctl handlers, ioctl, timer callouts, other tasks), a taskqueue is better. The same work can be scheduled from any context.
4. **Is the deferred work sensitive to the interrupt's priority class?** If yes (you want `INTR_TYPE_NET` ithread priority for network work), register an ithread handler. The ithread inherits the interrupt's priority; a taskqueue runs at whatever priority its worker thread got at creation.

### Applying the Framework

**Filter-only fits:**
- A counter-incrementing demo driver.
- A driver that only reads a device register and passes the value through an atomic.
- A very simple sensor whose data is produced rarely and read directly.

**Filter-plus-ithread fits:**
- A simple driver where the deferred work only matters on interrupt.
- A driver that benefits from the interrupt's priority class.
- A driver that wants the kernel-managed ithread without the taskqueue's extra machinery.

**Filter-plus-taskqueue fits:**
- A driver where the same deferred work can be triggered by multiple sources (interrupt, sysctl, ioctl).
- A driver that needs to coalesce interrupts (the taskqueue's `npending` count tells you how many enqueues happened since the last run).
- A driver that wants a specific worker-thread count or priority independent of the interrupt category.
- Chapter 19's target case: the `myfirst` driver schedules the same task from the filter and from the simulated-interrupt sysctl.

**ithread-only fits:**
- A driver where there is no urgent work and every action needs a sleep lock.
- A driver where the filter would be trivial (just "schedule the thread"); registering no filter and letting the kernel schedule the ithread saves a function call.

### Worked Example: A Hypothetical Storage Driver

Suppose you are writing a driver for a small storage controller. The device has one IRQ line. When an I/O completes, it sets `INTR_STATUS.COMPLETION` and lists the completed command IDs in a completion queue register.

The decisions:

- **Can every piece be done in primary context?** No. Waking up the thread that issued the I/O requires a condition-variable broadcast, which requires the thread's lock. The filter cannot take that lock.
- **Which deferred mechanism?** The completion-handling work is scheduled only by the interrupt, so filter-plus-ithread is clean. The priority class is `INTR_TYPE_BIO`, which the ithread inherits.
- **Final design.** Filter reads `INTR_STATUS`, extracts the completed command IDs into a per-interrupt-context queue, acknowledges, returns `FILTER_SCHEDULE_THREAD`. ithread walks the per-context queue, matches command IDs to pending requests, wakes each request's thread.

### Worked Example: A Hypothetical Network Driver

A NIC with four MSI-X vectors (two receive queues, two transmit queues). Each vector has its own filter.

The decisions:

- **Filter work?** Per-queue: acknowledge, note that the queue has events.
- **Deferred work?** Per-queue: walk the descriptor ring, build mbufs, pass to the stack.
- **Multiple sources?** Just the interrupt for normal operation; polling mode (for high-load offload) is a second source. Taskqueue is better: both the filter and the poll-mode timer can enqueue.
- **Priority?** `INTR_TYPE_NET`, which the taskqueue worker's `PI_NET` priority matches.
- **Final design.** Per-vector filter returns `FILTER_HANDLED` after enqueueing the per-queue task. Taskqueue per receive queue, one worker each. Taskqueues configured at priority `PI_NET`.

### Worked Example: The Chapter 19 Driver

One IRQ line, simple event types, taskqueue-based deferred work.

- **Filter work:** Read `INTR_STATUS`, per-bit counters, acknowledge, enqueue task for DATA_AV.
- **Deferred work:** Read `DATA_OUT`, update softc, broadcast `data_cv`.
- **Multiple sources?** Filter and simulated-interrupt sysctl both need the task. Taskqueue is right.
- **Priority?** `PI_NET` is a sensible default even though the driver isn't a NIC; the simulation framework expects responsiveness.

### When to Revisit the Decision

The decision is not permanent. A driver that starts as filter-only may grow a task when it gains a new capability; a driver that starts as taskqueue may move to ithread when the extra flexibility of taskqueue is not needed. The refactor is usually small (half an hour of code movement).

The framework helps you avoid an obviously-wrong initial choice. The details are a judgment call that the driver author makes based on the specific device.



## Lock Ordering and the Interrupt Path

Part 3's lock discipline introduced the idea that the driver has a fixed lock order: `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx`. Chapter 19 does not add new locks but does add new contexts where existing locks are touched. This subsection examines whether the Chapter 19 additions respect the existing order.

### The Filter Takes No Locks

The filter reads `INTR_STATUS`, updates atomic counters, and enqueues a task. No sleep lock is acquired. The filter's access to `INTR_STATUS` uses `ICSR_READ_4` and `ICSR_WRITE_4`, which do not assert any lock. Therefore the filter does not participate in the lock order; it is lock-free.

This is the simplest possible choice. A more sophisticated filter might use a spin lock (to protect a small shared data structure); Chapter 19's filter is simpler than that.

### The Task Acquires sc->mtx

The task's function `myfirst_intr_data_task_fn` acquires `sc->mtx` (via `MYFIRST_LOCK`), does its work, and releases. It does not acquire any other lock. Therefore the task respects the existing lock order by not introducing any new lock acquisition pattern.

### The Simulated-Interrupt sysctl Acquires and Releases sc->mtx

The sysctl handler acquires `sc->mtx` to set `INTR_STATUS`, releases the lock, and then invokes the filter. This is not a lock-order violation because the filter takes no locks; no new edge is added to the lock graph.

### The Attach and Detach Paths

The attach path acquires `sc->mtx` briefly to set `INTR_MASK` and to perform the initial diagnostic read. It does not hold the lock across `bus_setup_intr` (which could, in principle, call into other parts of the kernel that take their own locks; `bus_setup_intr` is documented as lockable, meaning the caller can hold nothing of its own). The detach path similarly brief-holds `sc->mtx` around the `INTR_MASK` clear, then releases before calling `bus_teardown_intr`.

### A Subtle Ordering Concern: bus_teardown_intr Can Block

A detail worth calling out. `bus_teardown_intr` waits for any in-progress filter or ithread invocation to complete before returning. If the driver holds a lock that the filter needs (say, a spin lock that the filter acquires briefly), `bus_teardown_intr` may block forever because the filter cannot run to completion.

Chapter 19's filter takes no spin locks, so this concern is academic. But a driver that uses spin locks in the filter must be careful: never hold the filter's spin lock while calling `bus_teardown_intr`.

### WITNESS and the Interrupt Path

The debug kernel's `WITNESS` tracks lock ordering across every context, including the filter. A filter that takes a spin lock creates an ordering edge in `WITNESS`'s graph. If any thread-context code takes the same spin lock while holding a different spin lock, `WITNESS` flags a potential deadlock.

For the Chapter 19 driver, no edges are added. `WITNESS` is silent.

### What to Document in LOCKING.md

A good driver's `LOCKING.md` documents the lock order clearly. Chapter 19's additions are minor:

- The filter takes no locks (atomic operations only).
- The task takes `sc->mtx` (leaf of the existing order).
- The simulated-interrupt sysctl takes `sc->mtx` briefly to set state, releases, then invokes the filter (outside any lock).

A short paragraph in `LOCKING.md` notes these facts. The order itself does not change.



## Observability: What Chapter 19 Exposes to Operators

A chapter about interrupts is also, indirectly, about observability. The user of a driver (a system operator or a driver author debugging an issue) wants to see what the driver is doing. Chapter 19 exposes a modest amount of observability through counters and the simulated-interrupt sysctl; this subsection consolidates what is visible and how.

### The Counter Suite

After Stage 4, the driver exposes these read-only sysctls:

- `dev.myfirst.N.intr_count`: total filter invocations.
- `dev.myfirst.N.intr_data_av_count`: DATA_AV events.
- `dev.myfirst.N.intr_error_count`: ERROR events.
- `dev.myfirst.N.intr_complete_count`: COMPLETE events.
- `dev.myfirst.N.intr_task_invocations`: task runs.
- `dev.myfirst.N.intr_last_data`: most recent DATA_OUT read by the task.

The counters give a concise view of the interrupt activity. Watching them over time (through `watch sysctl dev.myfirst.0` or a shell loop) shows the driver's activity live.

### The Writable Sysctls

- `dev.myfirst.N.intr_simulate`: write a bitmask to simulate an interrupt.

(Chapter 19's driver only exposes this one writable sysctl for interrupts. Challenge exercises add `intr_sim_period_ms` for rate-based simulation and `intr_cpu` for affinity.)

### The Kernel-Level View

`vmstat -i` and `devinfo -v` already show the kernel's view:

- `vmstat -i` shows the `intr_event`'s total count and rate.
- `devinfo -v` shows the device's IRQ resource.

These are not specific to `myfirst`; they are available for every driver. Learning to read them is part of general FreeBSD operator skill.

### Correlating the Views

An operator trying to diagnose an issue might cross-check counters:

```sh
# The kernel's count of interrupts delivered to our handler.
vmstat -i | grep myfirst

# The driver's count of times the filter was invoked.
sysctl dev.myfirst.0.intr_count
```

If these numbers match, the kernel's path and the driver's filter are in agreement. If the kernel count exceeds the driver count, some interrupts are being handled but not recognised (perhaps by another handler on a shared line). If the driver count exceeds the kernel count, something is wrong (the driver is counting invocations the kernel did not deliver; the simulated-interrupt sysctl is the most likely culprit if it was fired recently).

A disparity of one or two across a load-unload cycle is normal (timing around the unload). A consistent growing disparity indicates a bug.

### DTrace

The kernel's `fbt` provider lets you trace the entry and exit of any kernel function, including `myfirst_intr_filter`:

```sh
sudo dtrace -n 'fbt::myfirst_intr_filter:entry { @[probefunc] = count(); }'
```

This prints the count of filter invocations seen by DTrace. Cross-check against `intr_count`.

More interestingly, a DTrace script can aggregate per-call timing:

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->t = timestamp; }
fbt::myfirst_intr_filter:return /self->t/ {
    @["filter_ns"] = quantize(timestamp - self->t);
    self->t = 0;
}'
```

The output is a histogram of filter execution times in nanoseconds. A healthy filter spends between a few hundred nanoseconds and a few microseconds; anything higher is a bug or an extremely slow device.

### ktrace and kgdb

For deep debugging, `ktrace` can trace system-call activity; `kgdb` can inspect a panic's kernel core dump. Chapter 19 does not use these directly, but a reader whose driver panics in the interrupt path would need them.



 Each lab builds on the previous one and corresponds to one of the chapter's stages. A reader who works through all five has a complete interrupt-aware driver, a simulated-interrupt pipeline, and a regression script that validates the whole thing.

Time budgets assume the reader has already read the relevant sections.

### Lab 1: Explore Interrupt Sources on Your System

Time: thirty minutes.

Objective: Build an intuition for what interrupts your system is handling and at what rate.

Steps:

1. Run `vmstat -i > /tmp/intr_before.txt`.
2. Do something that exercises the system for thirty seconds: run `dd if=/dev/urandom of=/dev/null bs=1m count=1000`, or open a browser page (on a system with a graphical session), or scp a file from another host.
3. Run `vmstat -i > /tmp/intr_after.txt`.
4. Compute the difference with `diff`:

```sh
paste /tmp/intr_before.txt /tmp/intr_after.txt
```

5. For each source that changed, note:
   - The interrupt name.
   - The count before and after.
   - The inferred rate during the thirty seconds.
6. Pick one source and identify its driver with `devinfo -v` or `pciconf -lv`.

Expected observations:

- The timer interrupts (`cpu0:timer` and so on) are high and steady, one per CPU.
- Network interrupts (`em0`, `igc0`, etc.) are high during `dd` or `scp` activity, near-zero otherwise.
- Storage interrupts (`ahci0`, `nvme0`, etc.) are high during disk activity, low otherwise.
- Some interrupts never change; those are devices that are quiet during your test.

This lab is about reading reality. No code. The payoff is that every later lab's `vmstat -i` output is familiar territory.

### Lab 2: Stage 1, Register and Fire the Handler

Time: two to three hours.

Objective: Add the interrupt allocation, filter registration, and cleanup to Chapter 18's driver. Version target: `1.2-intr-stage1`.

Steps:

1. Starting from Chapter 18 Stage 4, copy the driver source to a new working directory.
2. Edit `myfirst.h` and add the four softc fields (`irq_res`, `irq_rid`, `intr_cookie`, `intr_count`).
3. In `myfirst_pci.c`, add the minimal filter handler (`atomic_add_64`; return `FILTER_HANDLED`).
4. Extend the attach path with the IRQ allocation, `bus_setup_intr`, and `bus_describe_intr` calls. Add the corresponding goto labels.
5. Extend the detach path with `bus_teardown_intr` and `bus_release_resource` for the IRQ.
6. Add a read-only sysctl `dev.myfirst.N.intr_count`.
7. Bump the version string to `1.2-intr-stage1`.
8. Compile: `make clean && make`.
9. Load on a bhyve guest. Check:
   - No `[GIANT-LOCKED]` warning in `dmesg`.
   - `devinfo -v | grep -A 5 myfirst0` shows both `memory:` and `irq:`.
   - `vmstat -i | grep myfirst` shows the handler.
   - `sysctl dev.myfirst.0.intr_count` returns a sensible value (zero if the device is quiet).
10. Unload. Check `vmstat -m | grep myfirst` shows zero live allocations.

Common failures:

- Missing `INTR_MPSAFE`: check for `[GIANT-LOCKED]` in `dmesg`.
- Wrong `rid` value: `bus_alloc_resource_any` returns NULL. Confirm `sc->irq_rid = 0`.
- Sleep lock in the filter: `WITNESS` panics.
- Missing teardown: `kldunload` panics or the debug kernel complains about active handlers.

### Lab 3: Stage 2, Real Filter and Deferred Task

Time: three to four hours.

Objective: Extend the filter to read INTR_STATUS, acknowledge, and enqueue a deferred task. Version target: `1.2-intr-stage2`.

Steps:

1. Starting from Lab 2, add the per-bit counters (`intr_data_av_count`, `intr_error_count`, `intr_complete_count`, `intr_task_invocations`, `intr_last_data`) to the softc.
2. Add the taskqueue (`intr_tq`) and task (`intr_data_task`) fields.
3. In `myfirst_init_softc`, initialise the task and create the taskqueue.
4. In `myfirst_deinit_softc`, drain the task, free the taskqueue.
5. Rewrite the filter to read `INTR_STATUS`, check each bit, acknowledge, enqueue the task for `DATA_AV`, and return the right `FILTER_*` value.
6. Write the task function (`myfirst_intr_data_task_fn`) that reads `DATA_OUT`, updates the softc, and broadcasts the condition variable.
7. In the attach path, after the filter registration, enable `INTR_MASK` at the device.
8. In the detach path, disable `INTR_MASK` before `bus_teardown_intr`.
9. Add read-only sysctls for the new counters.
10. Bump the version to `1.2-intr-stage2`.
11. Compile, load, verify basic wiring (same as Lab 2).

For observation, wait a few seconds after load: if the device is producing real interrupts that match our bit layout, counters tick up. On the bhyve virtio-rnd target, no real interrupts of the right kind arrive; verify counters by proceeding to Lab 4.

### Lab 4: Stage 3, Simulated Interrupts via sysctl

Time: two to three hours.

Objective: Add the `intr_simulate` sysctl, use it to drive the pipeline. Version target: `1.2-intr-stage3`.

Steps:

1. Starting from Lab 3, add the `intr_simulate` sysctl handler (the one in Section 5).
2. Register it in `myfirst_init_softc` or the sysctl setup.
3. Compile, load.
4. Simulate a single `DATA_AV` event:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=1
sleep 0.1
sysctl dev.myfirst.0.intr_count
sysctl dev.myfirst.0.intr_data_av_count
sysctl dev.myfirst.0.intr_task_invocations
```

All three counters should show 1.

5. Simulate ten `DATA_AV` events in a loop:

```sh
for i in 1 2 3 4 5 6 7 8 9 10; do
    sudo sysctl dev.myfirst.0.intr_simulate=1
done
sleep 0.5
sysctl dev.myfirst.0.intr_task_invocations
```

The task count should be close to 10 (may be less if the taskqueue coalesced multiple enqueues into a single run; each run records only one invocation but `npending` would be larger).

6. Simulate all three bits together:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=7
```

All three per-bit counters increment.

7. Check the `intr_error_count` and `intr_complete_count` increment correctly:

```sh
sudo sysctl dev.myfirst.0.intr_simulate=2  # ERROR
sudo sysctl dev.myfirst.0.intr_simulate=4  # COMPLETE
sysctl dev.myfirst.0 | grep intr_
```

8. Implement the optional rate-based callout (`intr_sim_period_ms`), verify rate:

```sh
sudo sysctl hw.myfirst.intr_sim_period_ms=100
sleep 10
sysctl dev.myfirst.0.intr_count  # around 100
sudo sysctl hw.myfirst.intr_sim_period_ms=0
```

### Lab 5: Stage 4, Refactor, Regression, Version

Time: three to four hours.

Objective: Move interrupt code into `myfirst_intr.c`/`.h`, introduce `ICSR_*` macros, write `INTERRUPTS.md`, run the regression. Version target: `1.2-intr`.

Steps:

1. Starting from Lab 4, create `myfirst_intr.c` and `myfirst_intr.h`.
2. Move the filter, the task, the setup, the teardown, and the sysctl registration into `myfirst_intr.c`.
3. Add `ICSR_READ_4` and `ICSR_WRITE_4` macros to `myfirst_intr.h`.
4. Update the filter to use `ICSR_READ_4`/`ICSR_WRITE_4` instead of raw `bus_read_4`/`bus_write_4`.
5. In `myfirst_pci.c`, replace the inline interrupt code with calls to `myfirst_intr_setup` and `myfirst_intr_teardown`.
6. Update the `Makefile` to add `myfirst_intr.c` to SRCS. Bump the version to `1.2-intr`.
7. Write `INTERRUPTS.md` documenting the interrupt handler's design.
8. Compile.
9. Run the full regression script (ten cycles of attach/detach/unload with counter checks; see the companion example).
10. Confirm: no warnings, no leaks, counters match expectations.

Expected outcomes:

- The driver at `1.2-intr` has the same behaviour as Stage 3 but a cleaner file structure.
- `myfirst_pci.c` is shorter by 50-80 lines.
- `myfirst_intr.c` is roughly 200-300 lines.
- The regression script passes ten times in a row.



## Challenge Exercises

The challenges are optional. Each builds on one of the labs and extends the driver in a direction the chapter did not take. They consolidate the chapter's material and are good preparation for Chapter 20.

### Challenge 1: Add a Filter-Plus-ithread Handler

Rewrite Stage 2's filter so that it returns `FILTER_SCHEDULE_THREAD` instead of enqueueing a taskqueue task. Register an ithread handler through the fifth argument of `bus_setup_intr(9)` that does the work the task did. Compare the two approaches.

This exercise is the way to internalise the difference between ithread-based deferred work and taskqueue-based deferred work. After completing it, the reader should be able to say when each is appropriate.

### Challenge 2: Implement Driver-Side Storm Mitigation

Add a counter that tracks the number of interrupts handled in the current millisecond. If the count exceeds a threshold (say, 10000), mask the device's interrupts and schedule a task to re-enable them 10 ms later.

This exercise demonstrates that driver-side mitigation is possible, and it shows why the kernel's default (do-nothing) is usually fine.

### Challenge 3: Bind the Interrupt to a Specific CPU

Add a sysctl `dev.myfirst.N.intr_cpu` that accepts a CPU ID. When written, call `bus_bind_intr(9)` to route the interrupt to that CPU. Verify with `cpuset -g` or the per-CPU counts in `vmstat -i`.

This exercise introduces the CPU-affinity API and shows how the choice is visible in system-level tools.

### Challenge 4: Extend the Simulated Interrupts With Per-Type Rates

Modify the `intr_sim_period_ms` callout to accept a bitmask of which event classes to simulate, not just `DATA_AV`. A reader should be able to simulate alternating `ERROR` and `COMPLETE` events at different rates.

The exercise exercises the reader's understanding of the Stage 2 filter's per-bit handling.

### Challenge 5: Add a Rate-Limited Stray Log

Implement the `ppsratecheck(9)`-based stray-interrupt log mentioned in Section 6. Verify that the log appears at the expected rate when the driver receives strays (you can induce strays by disabling `INTR_MASK` while the device is generating events, or by manually calling the filter with a zero status).

### Challenge 6: Implement MSI Allocation (Preview of Chapter 20)

Add code to the attach path that attempts `pci_alloc_msi(9)` first and falls back to legacy IRX if MSI is not available. The filter remains the same. This is a preview of Chapter 20; doing it now gets the reader comfortable with the MSI allocation API.

Note that on the bhyve virtio-rnd target, MSI is typically not available (bhyve's legacy virtio transport uses INTx). QEMU's `virtio-rng-pci` exposes MSI-X; you may want to switch labs to QEMU for this challenge.

### Challenge 7: Write a Latency Test

Use the simulated-interrupt path to measure the driver's interrupt-to-user-space latency. A user-space program opens `/dev/myfirst0`, issues a `read(2)` that sleeps waiting on the condition variable; a second program writes the `intr_simulate` sysctl, starting a wall-clock timer; the first program's `read` returns, stopping the timer. Plot the distribution over many iterations.

This exercise exposes the reader to performance measurement on the driver's deferred path. Typical latencies are tens of microseconds on a well-tuned system.

### Challenge 8: Share the IRQ Deliberately

If you have a bhyve guest configured with multiple devices on the same slot's functions, deliberately force them to share an IRQ. Load both drivers (the base-system driver for the other device; our `myfirst` for virtio-rnd). Verify with `vmstat -i` that they share the line. Observe the behaviour when either fires.

This exercise is the clearest demonstration of shared-IRQ correctness. A driver that gets Section 6's discipline wrong will misbehave here.



## Troubleshooting and Common Mistakes

A consolidated list of interrupt-specific failure modes, symptoms, and fixes. Kept as a reference you can return to.

### "Driver loads but no interrupts are counted"

Symptom: `kldload` succeeds, `dmesg` shows the attach banner, but `sysctl dev.myfirst.0.intr_count` stays at zero indefinitely.

Likely causes:

1. The device is not producing interrupts. On the bhyve virtio-rnd target this is normal because the device does not generate Chapter 17-style events. Use the simulated-interrupt sysctl to drive the pipeline.
2. `INTR_MASK` was not set. The handler is registered, but the device is not asserting the line because the mask is zero. Check the attach path for the `CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, ...)` call.
3. The device is masked by other means. Check the command register for `PCIM_CMD_INTxDIS` (interrupt disable bit); if set, clear it.
4. The wrong IRQ was allocated. `rid = 0` should produce the device's legacy INTx. Check `devinfo -v | grep -A 5 myfirst0` shows an `irq:` entry.

### "GIANT-LOCKED warning in dmesg"

Symptom: `dmesg` after `kldload` shows `myfirst0: [GIANT-LOCKED]`.

Cause: `INTR_MPSAFE` was not passed to `bus_setup_intr`'s flags argument.

Fix: Add `INTR_MPSAFE` to the flags. Verify the filter uses spin-safe operations only (atomics, spin mutexes). Verify the lock discipline in the softc allows MP-safe operation.

### "Kernel panic in the filter"

Symptom: A kernel panic whose backtrace shows `myfirst_intr_filter`.

Likely causes:

1. `sc` is NULL or stale. Check the argument to `bus_setup_intr`; it should be `sc`, not `&sc`.
2. A sleep lock is being acquired. `WITNESS` panics on this. The fix is to remove the sleep lock or move the work to a taskqueue.
3. The filter is called against a freed softc. This usually means detach did not tear down the handler before freeing state. Check the detach order.
4. `sc->bar_res` is NULL. A race between attach partial-failure unwinding and the filter running. Guard the filter's first access with a check.

### "Task runs but accesses freed state"

Symptom: Kernel panic in the task function, backtrace shows `myfirst_intr_data_task_fn`.

Cause: The task was enqueued during or just before detach, and detach freed the softc before the task ran.

Fix: Add `taskqueue_drain` to the detach path, before freeing anything the task touches. See Section 7.

### "INTR_STATUS bits keep firing; storm detected"

Symptom: `dmesg` shows `interrupt storm detected`.

Cause: The filter is not acknowledging `INTR_STATUS` correctly. Possibilities:

1. Filter does not write `INTR_STATUS` at all. Add the write.
2. Filter writes the wrong value. Write the specific bits you handled, not `0` and not `0xffffffff`.
3. Filter handles only some bits; unrecognised bits stay set and keep asserting. Either recognise all bits, or acknowledge unrecognised ones explicitly, or unset them in `INTR_MASK`.

### "Simulated interrupt does not run the task"

Symptom: `sudo sysctl dev.myfirst.0.intr_simulate=1` increments `intr_count` but not `intr_task_invocations`.

Likely causes:

1. The simulated bit does not match what the filter looks for. The Stage 2 filter enqueues the task for `DATA_AV` (bit 0x1). Writing `2` or `4` sets ERROR or COMPLETE; those do not enqueue. Write `1` or `7`.
2. The task function is not registered. Check `TASK_INIT` is called in `myfirst_init_softc`.
3. The taskqueue is not created. Check `taskqueue_create` and `taskqueue_start_threads` in `myfirst_init_softc`.

### "kldunload fails with Device busy"

Symptom: `kldunload myfirst` fails with `Device busy`.

Causes: Same as Chapter 18. A user-space process has the cdev open; an in-flight command is not complete; the driver's busy check has a bug. Add `fstat /dev/myfirst0` to see who has it open.

### "vmstat -m shows live allocations after unload"

Symptom: `vmstat -m | grep myfirst` returns non-zero `InUse` after `kldunload`.

Likely causes:

1. The taskqueue was not drained. Check `taskqueue_drain` in the detach path.
2. The simulation backend was attached (simulation-only build) and not detached. Check for `myfirst_sim_detach` in the detach path.
3. A leak in `myfirst_init_softc` / `myfirst_deinit_softc`. Check that every allocation has a matching free.

### "Handler fires on the wrong CPU"

Symptom: `cpuset -g` shows the interrupt fired on CPU X; the reader wanted it on CPU Y.

Cause: `bus_bind_intr` was not called, or was called with the wrong CPU argument.

Fix: Add a sysctl that lets the operator set the desired CPU and call `bus_bind_intr`. See Challenge 3.

### "INTR_MASK write has unintended side effects"

Symptom: On the bhyve virtio-rnd target, writing to offset 0x10 (the Chapter 17 `INTR_MASK` offset) changes the device's state in unexpected ways.

Cause: The Chapter 17 register layout does not match virtio-rnd's. The offset 0x10 on virtio-rnd is `queue_notify`, not `INTR_MASK`.

Fix: This is a target mismatch, not a driver bug. The chapter acknowledges the issue. For correctness on a real device with the Chapter 17 layout, the write is right. For the bhyve teaching target, the write is harmless (it notifies an idle virtqueue) but not meaningful.

### "Stray interrupt messages in dmesg"

Symptom: `dmesg` periodically shows messages about stray interrupts on the IRQ line.

Likely causes:

1. The handler is not masking `INTR_MASK` at the device properly during detach (level-triggered stray).
2. The device is producing interrupts the driver has not enabled. Check `INTR_MASK` setting.
3. Another driver sharing the line is returning the wrong `FILTER_*` value. This is that driver's bug, not ours.

### "Handler is called concurrently on multiple CPUs"

Symptom: Atomic counters increment non-monotonically, suggesting concurrent filter invocations.

Cause: On MSI-X (Chapter 20) the same handler can run concurrently on different CPUs. This is by design. For legacy IRQs this is rare but possible in some configurations.

Fix: Ensure all filter state access is atomic or spin-lock-protected. The Chapter 19 driver uses `atomic_add_64` throughout; no changes needed.

### "bus_setup_intr returns EINVAL"

Symptom: The return value of `bus_setup_intr` is `EINVAL` and the driver fails to load.

Likely causes:

1. Both `filter` and `ihand` arguments are `NULL`. At least one must be non-NULL; the kernel has nothing to call otherwise.
2. The `INTR_TYPE_*` flag was omitted from the flags argument. Exactly one category must be set.
3. The IRQ resource was not allocated with `RF_ACTIVE`. An unactivated resource cannot have a handler attached.
4. The flags argument contains mutually-exclusive bits (rare; a driver author would have to invent this).

Fix: read the `bus_setup_intr(9)` manual page; the common case is that the filter or ithread argument is missing or the category flag is missing.

### "bus_setup_intr returns EEXIST"

Symptom: `bus_setup_intr` returns `EEXIST` on a subsequent load.

Cause: The IRQ line already has an exclusive handler attached. Either this driver was loaded before and did not tear down properly, or another driver has claimed the line exclusively.

Fix: First, try unloading any previous instance (`kldunload myfirst`). If the problem persists, check `devinfo -v` for any driver currently using the IRQ.

### "Debug kernel panics on taskqueue_drain"

Symptom: `taskqueue_drain` panics in the debug kernel.

Likely causes:

1. The taskqueue was never created. `sc->intr_tq` is NULL. Check `myfirst_init_softc`.
2. The taskqueue was already freed. Check for double-free in the teardown path.
3. `TASK_INIT` was never called. The task function pointer is garbage.

Fix: Ensure `TASK_INIT` runs before `taskqueue_enqueue` ever runs; ensure `taskqueue_free` runs at most once.

### "Filter is called but INTR_STATUS reads as 0xffffffff"

Symptom: The filter runs, reads `INTR_STATUS`, sees `0xffffffff`.

Likely causes:

1. The device is unresponsive (perhaps the bhyve guest died or the device was hot-unplugged).
2. The BAR mapping is wrong. Check the attach path.
3. A PCI error has put the device into a limp state.

Fix: If the device is alive, the read returns real status bits. If `0xffffffff`, something else is wrong. The filter should still return `FILTER_STRAY` (because 0xffffffff is unlikely to be a legitimate status value; cross-check with the device's datasheet for valid bit combinations).

### "Interrupts are counted but the device makes no progress"

Symptom: `intr_count` ticks up, but the device's operation (data transfer, task completion, etc.) does not advance.

Likely causes:

1. The filter acknowledges every bit but the task does not run. Check `intr_task_invocations`; if zero, the `taskqueue_enqueue` path is broken.
2. The task runs but does not wake waiters. Check `cv_broadcast` in the task.
3. The device is signalling an unusual condition. Check `INTR_STATUS` contents (read via the sysctl path, or in DDB).

Fix: Add logging in the task (via `device_printf`); check whether the task's logic matches the device's actual behaviour.

### "kldunload hangs"

Symptom: `kldunload myfirst` does not return. No panic, no output.

Likely causes:

1. `bus_teardown_intr` is blocked waiting for an in-progress handler (filter or ithread). The handler is stuck.
2. `taskqueue_drain` is blocked waiting for a task that is stuck.
3. The detach function is waiting on a condition variable that is never broadcast.

Fix: If the system is responsive otherwise, switch to DDB (press the NMI key or type `sysctl debug.kdb.enter=1`) and `ps` to find the stuck threads. The backtrace usually pinpoints the stuck function.

### "Unaligned memory access in the filter"

Symptom: A kernel panic on an architecture sensitive to alignment (arm64, MIPS, SPARC), with a backtrace pointing to the filter.

Cause: The filter is reading or writing a register at an unaligned offset. PCI BAR reads and writes require natural alignment (4-byte for 32-bit reads, 2-byte for 16-bit reads).

Fix: Use `bus_read_4` / `bus_write_4` at 4-byte-aligned offsets. Chapter 17's register map is 4-byte aligned throughout.

### "device_printf from the filter slows the system"

Symptom: Adding `device_printf` calls to the filter makes the system noticeably laggy at high interrupt rates.

Cause: `device_printf` acquires a lock and does a formatted print. At ten thousand interrupts per second, the overhead is measurable.

Fix: Remove debug prints from the filter before production testing. Use counters and DTrace for observability instead.

### "Driver passes all tests but misbehaves under load"

Symptom: The single-threaded tests pass, but load testing with many concurrent processes triggers occasional errors or state corruption.

Likely causes:

1. A race condition between the filter and the task. The filter sets a flag the task reads; the task updates state the filter reads. Without proper synchronisation, one can miss the other's update.
2. A race between the task and another thread-context path (cdev handler, sysctl). The task takes `sc->mtx`; the other path should too.
3. An atomic variable used in a compound operation without a lock. `atomic_add_64` on its own is atomic; `atomic_load_64` followed by a computation followed by `atomic_store_64` is not atomic as a sequence.

Fix: Review the locking discipline. `WITNESS` does not catch pure atomic-variable races; careful code review does. Run under heavy load with `INVARIANTS` enabled and watch for assertion failures.

### "vmstat -i shows many strays on a line I don't own"

Symptom: A driver on a shared line sees the line's stray counter growing steadily.

Likely causes:

1. Another driver on the line returns `FILTER_STRAY` incorrectly (the interrupt is for it but it claims otherwise).
2. A device on the line is signalling events the drivers do not acknowledge, producing phantom strays.
3. Hardware noise or a misconfigured trigger mode.

Fix: The fix is usually in whichever driver is returning `FILTER_STRAY` wrongly. Your own driver's behaviour is correct as long as its status-register check is right.



## Advanced Observability: Integrating With DTrace

FreeBSD's DTrace can observe the interrupt path at many levels. This subsection shows some useful DTrace one-liners and scripts a driver author can use during development.

### Counting Filter Invocations

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { @invocations = count(); }'
```

Shows the total number of times the filter has been called since DTrace started. Compare with `sysctl dev.myfirst.0.intr_count`; they should agree.

### Measuring Filter Latency

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry { self->ts = vtimestamp; }
fbt::myfirst_intr_filter:return /self->ts/ {
    @["filter_ns"] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

`vtimestamp` measures CPU-time (not wall-clock), so the histogram is truly the filter's CPU time. A healthy filter is in the hundreds of nanoseconds to single-digit microseconds range.

### Observing the Task Queue

```sh
sudo dtrace -n '
fbt::myfirst_intr_data_task_fn:entry {
    @["task_runs"] = count();
    self->ts = vtimestamp;
}
fbt::myfirst_intr_data_task_fn:return /self->ts/ {
    @["task_ns"] = quantize(vtimestamp - self->ts);
    self->ts = 0;
}'
```

Shows the task's invocation count and per-invocation execution time. The task is typically an order of magnitude slower than the filter (because it takes a sleep lock and does more work).

### Correlating the Filter and Task

```sh
sudo dtrace -n '
fbt::myfirst_intr_filter:entry /!self->in_filter/ {
    self->in_filter = 1;
    self->filter_start = vtimestamp;
    @["filter_enters"] = count();
}
fbt::myfirst_intr_filter:return /self->in_filter/ {
    self->in_filter = 0;
}
fbt::myfirst_intr_data_task_fn:entry {
    @["task_starts"] = count();
}'
```

If `filter_enters` is 100 and `task_starts` is 80, some filter invocations did not schedule a task (because the event was ERROR or COMPLETE, not DATA_AV).

### Tracing Taskqueue Scheduling Decisions

The taskqueue infrastructure has DTrace probes too; one can observe how the task is enqueued and when the worker thread runs:

```sh
sudo dtrace -n '
fbt::taskqueue_enqueue:entry /arg0 == $${tq_addr}/ {
    @["enqueues"] = count();
}'
```

where `$${tq_addr}` is the numeric address of `sc->intr_tq`, obtainable through `kldstat` / `kgdb` combinations. This level of detail is usually overkill for Chapter 19's driver.

### DTrace and the Simulated-Interrupt Path

Simulated interrupts are distinguishable from real interrupts because the simulated path goes through the sysctl handler:

```sh
sudo dtrace -n '
fbt::myfirst_intr_simulate_sysctl:entry { @["simulate"] = count(); }
fbt::myfirst_intr_filter:entry { @["filter"] = count(); }'
```

The difference between the two counts is the number of real interrupts (filter calls not preceded by a sysctl call).



## Detailed Walkthrough: Stage 2 End to End

To make the Chapter 19 driver concrete, here is a complete walkthrough of what happens when a `DATA_AV` event is simulated through the sysctl, traced step by step.

### The Sequence

1. User runs `sudo sysctl dev.myfirst.0.intr_simulate=1`.
2. The kernel's sysctl machinery routes the write to `myfirst_intr_simulate_sysctl`.
3. The handler parses the value (1), acquires `sc->mtx` via `MYFIRST_LOCK`.
4. The handler writes `1` to `INTR_STATUS` in the BAR.
5. The handler releases `sc->mtx`.
6. The handler calls `myfirst_intr_filter(sc)` directly.
7. The filter reads `INTR_STATUS` through `ICSR_READ_4`. Value is `1` (DATA_AV).
8. The filter increments `intr_count` atomically.
9. The filter sees DATA_AV bit set, increments `intr_data_av_count`.
10. The filter acknowledges by writing `1` back to `INTR_STATUS` via `ICSR_WRITE_4`.
11. The filter enqueues `intr_data_task` on `intr_tq` via `taskqueue_enqueue`.
12. The filter returns `FILTER_HANDLED`.
13. The sysctl handler returns 0 to the kernel's sysctl layer.
14. The user's `sysctl` command returns success.

Meanwhile, in the taskqueue:

15. The taskqueue's worker thread (woken by `taskqueue_enqueue`) schedules.
16. The worker thread calls `myfirst_intr_data_task_fn(sc, 1)`.
17. The task acquires `sc->mtx`.
18. The task reads `DATA_OUT` through `CSR_READ_4`.
19. The task stores the value in `sc->intr_last_data`.
20. The task increments `intr_task_invocations`.
21. The task broadcasts `sc->data_cv` (no waiters in this example).
22. The task releases `sc->mtx`.
23. The worker thread goes back to waiting for more work.

Steps 1-14 take microseconds; steps 15-23 take tens to hundreds of microseconds depending on scheduling.

### What the Counters Show

After one simulated interrupt:

```text
dev.myfirst.0.intr_count: 1
dev.myfirst.0.intr_data_av_count: 1
dev.myfirst.0.intr_error_count: 0
dev.myfirst.0.intr_complete_count: 0
dev.myfirst.0.intr_task_invocations: 1
```

If `intr_task_invocations` is still 0, the task has not run yet (usually because the sysctl returned before the worker thread was scheduled). A short `sleep 0.01` is enough.

### What dmesg Shows

By default, nothing. The Stage 4 driver is not verbose. A reader who wants to see the filter fire can add `device_printf` calls for debugging, but production-quality drivers typically do not print on every interrupt.

### What vmstat -i Shows

`vmstat -i | grep myfirst` shows the `intr_event`'s total count. This counts only real interrupts delivered by the kernel to our filter. Simulated interrupts invoked through the sysctl do not pass through the kernel's interrupt dispatcher, so they do not appear in the `vmstat -i` count.

This is a useful distinction: the sysctl-delivered simulation is a complementary mechanism, not a replacement. Real interrupts still count; simulated ones do not.

### Tracing With Print Statements

For quick debugging, adding `device_printf` calls to the filter and task gives a live picture:

```c
/* In the filter, temporarily for debugging: */
device_printf(sc->dev, "filter: status=0x%x\n", status);

/* In the task: */
device_printf(sc->dev, "task: data=0x%x npending=%d\n",
    data, npending);
```

This produces `dmesg` output like:

```text
myfirst0: filter: status=0x1
myfirst0: task: data=0xdeadbeef npending=1
```

Remove these prints before production; the chatter at high interrupt rates is expensive.



## Patterns From Real FreeBSD Drivers

A compact tour of interrupt patterns that appear repeatedly in `/usr/src/sys/dev/`. Each pattern is a concrete snippet from a real driver (rewritten slightly for readability) with a note on why it matters. Reading these after Chapter 19 consolidates the vocabulary.

### Pattern: Fast-Filter-With-Slow-Task

From `/usr/src/sys/dev/mgb/if_mgb.c`:

```c
int
mgb_legacy_intr(void *xsc)
{
	struct mgb_softc *sc = xsc;
	uint32_t intr_sts = CSR_READ_REG(sc, MGB_INTR_STS);
	uint32_t intr_en = CSR_READ_REG(sc, MGB_INTR_ENBL_SET);

	intr_sts &= intr_en;
	if ((intr_sts & MGB_INTR_STS_ANY) == 0)
		return (FILTER_STRAY);

	/* Acknowledge and defer per-queue work. */
	if ((intr_sts & MGB_INTR_STS_RX_ANY) != 0) {
		for (int qidx = 0; qidx < scctx->isc_nrxqsets; qidx++) {
			if (intr_sts & MGB_INTR_STS_RX(qidx))
				iflib_rx_intr_deferred(sc->ctx, qidx);
		}
		return (FILTER_HANDLED);
	}
	return (FILTER_SCHEDULE_THREAD);
}
```

Why it matters: the filter is short, the deferred work is per-queue, the shared-IRQ discipline is maintained. Chapter 19's filter follows the same shape.

### Pattern: ithread-Only Handler

From `/usr/src/sys/dev/ath/if_ath_pci.c`:

```c
bus_setup_intr(dev, psc->sc_irq,
    INTR_TYPE_NET | INTR_MPSAFE,
    NULL, ath_intr, sc, &psc->sc_ih);
```

The filter argument is `NULL`; `ath_intr` is the ithread handler. The kernel schedules `ath_intr` on every interrupt without a filter in the middle.

Why it matters: sometimes all the work needs thread context. Registering NULL for the filter is simpler than writing a trivial filter that just returns `FILTER_SCHEDULE_THREAD`.

### Pattern: INTR_EXCL for Exclusive Access

Some drivers need exclusive access to an interrupt line:

```c
bus_setup_intr(dev, irq,
    INTR_TYPE_BIO | INTR_MPSAFE | INTR_EXCL,
    NULL, driver_intr, sc, &cookie);
```

Why it matters: in rare cases, a driver needs the line to itself (the handler's assumption of being the only listener is baked in). `INTR_EXCL` asks the kernel to refuse other drivers on the same event.

### Pattern: Short Debug Log

Some drivers have an optional verbose mode that logs every filter call:

```c
if (sc->sc_debug > 0)
	device_printf(sc->sc_dev, "interrupt: status=0x%x\n", status);
```

Why it matters: a driver under development benefits from logging; a production driver wants the log suppressed. A sysctl (`dev.driver.N.debug`) toggles the mode.

### Pattern: Bind to a Specific CPU

Drivers that know their topology bind the interrupt to a local CPU:

```c
/* After bus_setup_intr: */
error = bus_bind_intr(dev, irq, local_cpu);
if (error != 0)
	device_printf(dev, "bus_bind_intr: %d\n", error);
/* Non-fatal: some platforms do not support binding. */
```

Why it matters: NUMA-local handlers are faster. A driver that bothers to bind produces a better scalability story on multi-socket systems.

### Pattern: Describe the Handler for Diagnostics

Every driver should call `bus_describe_intr`:

```c
bus_describe_intr(dev, irq, cookie, "rx-%d", queue_id);
```

Why it matters: `vmstat -i` and `devinfo -v` use the description to distinguish handlers on shared events. A driver with N queues and N MSI-X vectors has N `bus_describe_intr` calls.

### Pattern: Quiesce Before Detach

```c
mtx_lock(&sc->mtx);
sc->shutting_down = true;
mtx_unlock(&sc->mtx);

/* Let the interrupt handler drain. */
bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
```

Why it matters: the `shutting_down` flag gives the handler a quick-exit path (the handler checks the flag before doing its normal work). The `bus_teardown_intr` is the definitive drain, but the flag makes the drain faster.

Chapter 19's driver uses `sc->pci_attached` for a similar purpose.



## Reference: Common Mistakes Cheat Sheet

A compact list of interrupt-specific mistakes and their one-line fixes. Useful as a checklist when reviewing your own driver.

1. **No INTR_MPSAFE.** Fix: `flags = INTR_TYPE_MISC | INTR_MPSAFE`.
2. **Sleep lock in filter.** Fix: use atomic operations or a spin mutex.
3. **Missing acknowledgment.** Fix: `bus_write_4(res, INTR_STATUS, bits_handled);`.
4. **Acknowledging too many bits.** Fix: write back only the bits you handled.
5. **Missing `FILTER_STRAY` return.** Fix: if status is zero or unrecognised, return `FILTER_STRAY`.
6. **Missing `FILTER_HANDLED` return.** Fix: `rv |= FILTER_HANDLED;` for each recognised bit.
7. **Task using stale softc.** Fix: add `taskqueue_drain` to detach.
8. **Missing `bus_teardown_intr`.** Fix: before `bus_release_resource(SYS_RES_IRQ, ...)`.
9. **Missing `INTR_MASK = 0` at detach.** Fix: clear the mask before teardown.
10. **Missing `taskqueue_drain`.** Fix: drain before freeing softc state.
11. **Wrong filter return value.** Fix: must be `FILTER_HANDLED`, `FILTER_STRAY`, `FILTER_SCHEDULE_THREAD`, or a bitwise OR.
12. **Enqueueing a task before `TASK_INIT`.** Fix: initialise the task in attach.
13. **Not setting `INTR_MASK` at attach.** Fix: write the bits you want enabled.
14. **Wrong rid for legacy IRQ.** Fix: use `rid = 0`.
15. **Wrong resource type.** Fix: use `SYS_RES_IRQ` for interrupts.
16. **Missing `RF_SHAREABLE` on shared line.** Fix: include the flag in allocation.
17. **Holding sc->mtx across `bus_setup_intr`.** Fix: release the lock before the call.
18. **Holding a spin lock across `bus_teardown_intr`.** Fix: never hold a filter's spin lock when tearing down.
19. **Taskqueue destroyed while task is still enqueued.** Fix: `taskqueue_drain` before `taskqueue_free`.
20. **Missing `bus_describe_intr` call.** Fix: add it after `bus_setup_intr` for diagnostic clarity.



## Reference: ithread and Taskqueue Priorities

The Chapter 19 code uses `PI_NET` for the taskqueue. FreeBSD defines several priority constants in `/usr/src/sys/sys/priority.h`. A simplified view:

```text
PI_REALTIME  = PRI_MIN_ITHD + 0   (highest ithread priority)
PI_INTR      = PRI_MIN_ITHD + 4   (the common "hardware interrupt" level)
PI_AV        = PI_INTR            (audio/video)
PI_NET       = PI_INTR            (network)
PI_DISK      = PI_INTR            (block storage)
PI_TTY       = PI_INTR            (terminal/serial)
PI_DULL      = PI_INTR            (low-priority hardware ithreads)
PI_SOFT      = PRI_MIN_ITHD + 8   (soft interrupts)
PI_SOFTCLOCK = PI_SOFT            (soft clock)
PI_SWI(c)    = PI_SOFT            (per-category SWI)
```

A reader looking at this list will notice that most of the "hardware interrupt" aliases (`PI_AV`, `PI_NET`, `PI_DISK`, `PI_TTY`, `PI_DULL`) resolve to the same numeric value (`PI_INTR`). The comment at the top of that block in `priority.h` makes the reason explicit: "Most hardware interrupt threads run at the same priority, but can decay to lower priorities if they run for full time slices". The category names exist because each one reads naturally at the call site, not because the numeric priorities differ.

Only `PI_REALTIME` (slightly above `PI_INTR`) and `PI_SOFT` (below `PI_INTR`) are actually distinct from the common hardware-interrupt level.

The ithread's priority comes from the `INTR_TYPE_*` flag; the taskqueue's priority is set explicitly. Passing `PI_NET` to `taskqueue_start_threads` puts the worker at the same nominal level as a network ithread, which is the right choice for work that cooperates with network-rate interrupt handling. A storage driver would pass `PI_DISK`; a low-priority background driver would pass `PI_DULL`. Because the constants all map to the same numeric value, the names are pragmatically interchangeable for correctness. They still matter for readability and for any future kernel where the distinction becomes real.



## Reference: A Short Tour of /usr/src/sys/kern/kern_intr.c

A reader curious about what happens behind `bus_setup_intr(9)` and `bus_teardown_intr(9)` can open `/usr/src/sys/kern/kern_intr.c`. The file is about 1800 lines and has distinct sections:

- **intr_event management** (`intr_event_create`, `intr_event_destroy`): top-level creation and cleanup of the `intr_event` structure.
- **Handler management** (`intr_event_add_handler`, `intr_event_remove_handler`): the underlying operations that `bus_setup_intr` and `bus_teardown_intr` call.
- **Dispatch** (`intr_event_handle`, `intr_event_schedule_thread`): the code that actually runs when an interrupt fires.
- **Storm detection** (`intr_event_handle`): the `intr_storm_threshold` logic.
- **ithread creation and scheduling** (`ithread_create`, `ithread_loop`, `ithread_update`): the per-event ithread machinery.
- **SWI (software interrupt) management** (`swi_add`, `swi_sched`, `swi_remove`): soft interrupts.

A reader does not need to understand the whole file to write a driver. Browsing the top-level function list and reading the comments on `intr_event_handle` (the dispatch function) is a worthwhile half-hour.

### Key Functions in kern_intr.c

| Function | Purpose |
|----------|---------|
| `intr_event_create` | Allocate a new `intr_event`. |
| `intr_event_destroy` | Free an `intr_event`. |
| `intr_event_add_handler` | Attach a filter/ithread handler. |
| `intr_event_remove_handler` | Detach a handler. |
| `intr_event_handle` | Dispatch: called on each interrupt. |
| `intr_event_schedule_thread` | Wake the ithread. |
| `ithread_loop` | The body of an ithread. |
| `swi_add` | Register a software interrupt. |
| `swi_sched` | Schedule a software interrupt. |

The BUS_* functions exposed to drivers (`bus_setup_intr`, `bus_teardown_intr`, `bus_bind_intr`, `bus_describe_intr`) call into these kernel-internal functions after platform-specific bus-driver hooks.







## Wrapping Up

Chapter 19 gave the driver ears. At the start, `myfirst` at version `1.1-pci` was attached to a real PCI device but did not listen to it: every action the driver took was initiated by user space, and the device's own asynchronous events (if any) went unnoticed. At the end, `myfirst` at version `1.2-intr` has a filter handler wired to the device's IRQ line, a deferred-task pipeline that handles the bulk work in thread context, a simulated-interrupt path for testing on lab targets, a shared-IRQ discipline that coexists with other drivers on the same line, a clean teardown that releases every resource in the correct order, and a new `myfirst_intr.c` file plus `INTERRUPTS.md` document.

The transition walked through eight sections. Section 1 introduced interrupts at the hardware level, covering edge-triggered and level-triggered signalling, the CPU's dispatch flow, and the six obligations a driver's handler has. Section 2 introduced FreeBSD's kernel model: `intr_event`, `intr_handler`, ithread, the filter-plus-ithread split, `INTR_MPSAFE`, and the constraints on filter context. Section 3 wrote the minimal filter and the attach/detach wiring. Section 4 extended the filter with status decoding, per-bit acknowledgment, and taskqueue-based deferred work. Section 5 added the simulated-interrupt sysctl that lets the reader exercise the pipeline without real IRQ events. Section 6 codified the shared-IRQ discipline: check ownership, return `FILTER_STRAY` correctly, handle unrecognised bits defensively. Section 7 consolidated the teardown: mask at the device, tear down the handler, drain the taskqueue, release resources. Section 8 refactored the whole thing into a maintainable layout.

What Chapter 19 did not do is MSI, MSI-X, or DMA. The driver's interrupt path is a single legacy IRQ; the data path does not use DMA; the deferred work is a single taskqueue task. Chapter 20 introduces MSI and MSI-X (multiple vectors, per-vector filters, richer interrupt routing). Chapters 20 and 21 introduce DMA and the interaction between interrupts and DMA descriptor rings.

What Chapter 19 accomplished is the split between two threads of control. The driver's filter is short, runs in primary interrupt context, and handles the urgent per-interrupt work. The driver's deferred task is longer, runs in thread context, and handles the bulk per-event work. The discipline that keeps them cooperating (atomics for filter state, sleep locks for task state, strict ordering for teardown) is the discipline every later chapter's interrupt code assumes.

The file layout has grown: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw_pci.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_pci.c`, `myfirst_pci.h`, `myfirst_intr.c`, `myfirst_intr.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`, `myfirst.h`. The documentation has grown: `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`. The test suite has grown: the simulated-interrupt pipeline, the Stage 4 regression script, a handful of challenge exercises to keep the reader practising.

### A Reflection Before Chapter 20

A pause before the next chapter. Chapter 19 taught the filter-plus-task pattern, the `INTR_MPSAFE` promise, the constraints of interrupt context, and the shared-IRQ discipline. The patterns you practised here (read status, acknowledge, defer work, return the right `FILTER_*`, tear down cleanly) are patterns every FreeBSD interrupt handler uses. Chapter 20 will layer MSI-X on top; Chapter 21 will layer DMA on top. Neither chapter replaces the Chapter 19 patterns; both build on them.

A second observation worth making. The composition of Chapter 17's simulation, Chapter 18's real PCI attach, and Chapter 19's interrupt handling is now a complete driver in the architectural sense. A reader who understands the three layers can open any FreeBSD PCI driver and recognise the parts: the register map, the PCI attach, the interrupt filter. The specifics differ; the structure is constant. That recognition is what makes the book's investment pay off across the whole FreeBSD source tree.

Third observation: the payoff of Chapter 16's accessor layer continues. The `CSR_*` macros did not change in Chapter 19; the `ICSR_*` macros were added for filter-context use, but they call the same underlying `bus_read_4` and `bus_write_4`. The abstraction has now paid off three times: against the Chapter 17 simulation backend, against the Chapter 18 real PCI BAR, and against the Chapter 19 filter context. A reader who builds similar accessor layers in their own drivers will find the same dividend.

### What to Do If You Are Stuck

Three suggestions.

First, focus on the simulated-interrupt path. If `sudo sysctl dev.myfirst.0.intr_simulate=1` makes counters tick and the task run, the pipeline is working. Every other piece of the chapter is optional in the sense that it decorates the pipeline, but if the pipeline fails, the whole chapter is not working and Section 5 is the right place to diagnose.

Second, open `/usr/src/sys/dev/mgb/if_mgb.c` and re-read the `mgb_legacy_intr` function slowly. It is about sixty lines of filter code. Every line maps to a Chapter 19 concept. Reading it once after completing the chapter should feel like familiar territory.

Third, skip the challenges on first pass. The labs are calibrated for Chapter 19's pace; the challenges assume the chapter's material is solid. Come back to them after Chapter 20 if they feel out of reach now.

Chapter 19's goal was to give the driver a way to listen to its device. If it has, Chapter 20's MSI-X machinery becomes a specialisation rather than an entirely new topic, and Chapter 21's DMA becomes a matter of wiring descriptor completions to the interrupt path you already have.



## Bridge to Chapter 20

Chapter 20 is titled *Advanced Interrupt Handling*. Its scope is the specialisation that Chapter 19 deliberately did not take: MSI (Message Signaled Interrupts) and MSI-X, the modern PCIe interrupt mechanisms that replace the legacy INTx line with per-device (or per-queue) vectors delivered as memory writes.

Chapter 19 prepared the ground in four specific ways.

First, **you have a functioning filter handler**. The Chapter 19 filter reads status, handles bits, acknowledges, and defers. Chapter 20's filter looks similar, but is replicated per vector: each MSI-X vector has its own filter, and each handles a specific subset of the device's events.

Second, **you understand the attach/detach cascade**. Chapter 19 grew the cascade by two labels (`fail_release_irq`, `fail_teardown_intr`). Chapter 20 grows it further: one pair of labels per vector. The pattern does not change; the count does.

Third, **you have an interrupt teardown discipline**. Chapter 20 reuses Chapter 19's order: clear interrupts at the device, `bus_teardown_intr` for each vector, `bus_release_resource` for each IRQ resource. The per-vector nature adds a small loop; the order is the same.

Fourth, **you have a lab environment that exposes MSI-X**. On QEMU with `virtio-rng-pci`, MSI-X is available; on bhyve with `virtio-rnd`, only legacy INTx is exposed. Chapter 20's labs may require switching to QEMU or to a more richly-emulated bhyve device to exercise the MSI-X path.

Specific topics Chapter 20 will cover:

- Why MSI and MSI-X are an improvement over legacy INTx.
- How MSI differs from MSI-X (single vector vs table of vectors).
- `pci_alloc_msi(9)`, `pci_alloc_msix(9)`: allocating vectors.
- `pci_msi_count(9)`, `pci_msix_count(9)`: querying capability.
- `pci_release_msi(9)`: the teardown counterpart.
- Multi-vector interrupt handlers: per-queue filters.
- The MSI-X table layout and how to reach specific entries.
- CPU affinity across vectors for NUMA awareness.
- Interrupt coalescing: reducing the rate of interrupts when the device supports it.
- Interaction between MSI-X and iflib (the modern network driver framework).
- Migrating the `myfirst` driver from Chapter 19's legacy path to an MSI-X path, with a fallback to legacy for devices that do not support MSI-X.

You do not need to read ahead. Chapter 19 is sufficient preparation. Bring your `myfirst` driver at `1.2-intr`, your `LOCKING.md`, your `INTERRUPTS.md`, your `WITNESS`-enabled kernel, and your regression script. Chapter 20 starts where Chapter 19 ended.

Chapter 21 is one chapter further along; it is worth a brief forward pointer. DMA will introduce yet another interaction with interrupts: completion interrupts that signal "descriptor ring entry N is done". The filter-plus-task discipline Chapter 19 taught carries through; the task's work now involves walking a descriptor ring rather than reading a single register.

The vocabulary is yours; the structure is yours; the discipline is yours. Chapter 20 adds precision to all three.



## Reference: Chapter 19 Quick-Reference Card

A compact summary of the vocabulary, APIs, macros, and procedures Chapter 19 introduced.

### Vocabulary

- **Interrupt**: an asynchronous hardware-signalled event.
- **IRQ (Interrupt Request)**: the identifier of an interrupt line.
- **Edge-triggered**: signalled by a transition; one interrupt per transition.
- **Level-triggered**: signalled by a level held; an interrupt fires while the level is held.
- **intr_event**: FreeBSD's kernel structure for one interrupt source.
- **ithread**: FreeBSD's kernel thread that runs deferred interrupt handlers.
- **filter handler**: a function that runs in primary interrupt context.
- **ithread handler**: a function that runs in thread context after the filter.
- **FILTER_HANDLED**: the filter handled the interrupt; no ithread needed.
- **FILTER_SCHEDULE_THREAD**: the filter partially handled; run the ithread.
- **FILTER_STRAY**: the interrupt was not for this driver.
- **INTR_MPSAFE**: a flag promising the handler does its own synchronisation.
- **INTR_TYPE_*** (TTY, BIO, NET, CAM, MISC, CLK, AV): handler-category hints.
- **INTR_EXCL**: exclusive interrupt.

### Essential APIs

- `bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, flags)`: claim an IRQ.
- `bus_release_resource(dev, SYS_RES_IRQ, rid, res)`: release the IRQ.
- `bus_setup_intr(dev, res, flags, filter, ihand, arg, &cookie)`: register a handler.
- `bus_teardown_intr(dev, res, cookie)`: unregister the handler.
- `bus_describe_intr(dev, res, cookie, "name")`: name the handler for tools.
- `bus_bind_intr(dev, res, cpu)`: route the interrupt to a CPU.
- `pci_msi_count(dev)`, `pci_msix_count(dev)` (Chapter 20).
- `pci_alloc_msi(dev, &count)`, `pci_alloc_msix(dev, &count)` (Chapter 20).
- `pci_release_msi(dev)` (Chapter 20).
- `taskqueue_create("name", M_WAITOK, taskqueue_thread_enqueue, &tq)`: create a taskqueue.
- `taskqueue_start_threads(&tq, n, PI_pri, "thread name")`: start workers.
- `taskqueue_enqueue(tq, &task)`: enqueue a task.
- `taskqueue_drain(tq, &task)`: wait for a task to finish, prevent new enqueues.
- `taskqueue_free(tq)`: free the taskqueue.
- `TASK_INIT(&task, pri, fn, arg)`: initialise a task.

### Essential Macros

- `FILTER_HANDLED`, `FILTER_STRAY`, `FILTER_SCHEDULE_THREAD`.
- `INTR_TYPE_TTY`, `INTR_TYPE_BIO`, `INTR_TYPE_NET`, `INTR_TYPE_CAM`, `INTR_TYPE_MISC`, `INTR_TYPE_CLK`, `INTR_TYPE_AV`.
- `INTR_MPSAFE`, `INTR_EXCL`.
- `RF_SHAREABLE`, `RF_ACTIVE`.
- `SYS_RES_IRQ`.

### Common Procedures

**Allocate a legacy PCI interrupt and register a filter handler:**

1. `sc->irq_rid = 0;`
2. `sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid, RF_SHAREABLE | RF_ACTIVE);`
3. `bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE, filter, NULL, sc, &sc->intr_cookie);`
4. `bus_describe_intr(dev, sc->irq_res, sc->intr_cookie, "name");`

**Tear down an interrupt handler:**

1. Disable interrupts at the device (clear `INTR_MASK`).
2. `bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);`
3. `taskqueue_drain(sc->intr_tq, &sc->intr_data_task);`
4. `taskqueue_free(sc->intr_tq);`
5. `bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);`

**Write a filter handler:**

1. Read `INTR_STATUS`; if zero, return `FILTER_STRAY`.
2. For each recognised bit, increment a counter, acknowledge by writing back, and optionally enqueue a task.
3. Return `FILTER_HANDLED` (or `FILTER_SCHEDULE_THREAD`), or `FILTER_STRAY` if nothing was recognised.

### Useful Commands

- `vmstat -i`: list interrupt sources with counts.
- `devinfo -v`: list devices with their IRQ resources.
- `sysctl hw.intrcnt` and `sysctl hw.intrnames`: raw counters.
- `sysctl hw.intr_storm_threshold`: enable kernel storm detection.
- `cpuset -g`: query interrupt CPU affinity (platform-specific).
- `sudo sysctl dev.myfirst.0.intr_simulate=1`: fire a simulated interrupt.

### Files to Keep Bookmarked

- `/usr/src/sys/sys/bus.h`: `driver_filter_t`, `driver_intr_t`, `FILTER_*`, `INTR_*`.
- `/usr/src/sys/kern/kern_intr.c`: the kernel's interrupt-event machinery.
- `/usr/src/sys/sys/taskqueue.h`: taskqueue API.
- `/usr/src/sys/dev/mgb/if_mgb.c`: a readable filter-plus-task example.
- `/usr/src/sys/dev/ath/if_ath_pci.c`: a minimal ithread-only interrupt setup.



## Reference: A Comparison Table Across Part 4

A compact summary of where each chapter of Part 4 fits, what it adds, and what it assumes. Useful for readers jumping in or back through the part.

| Topic | Ch 16 | Ch 17 | Ch 18 | Ch 19 | Ch 20 (preview) | Ch 21 (preview) |
|-------|-------|-------|-------|-------|------------------|------------------|
| BAR access | Simulated with malloc | Extended with sim layer | Real PCI BAR | Same | Same | Same |
| Chapter 17 simulation | N/A | Introduced | Inactive on PCI | Inactive on PCI | Inactive on PCI | Inactive on PCI |
| PCI attach | N/A | N/A | Introduced | Same + IRQ | MSI-X option | DMA init added |
| Interrupt handling | N/A | N/A | N/A | Introduced | MSI-X per-vector | Completion-driven |
| DMA | N/A | N/A | N/A | N/A | Preview | Introduced |
| Version | 0.9-mmio | 1.0-simulated | 1.1-pci | 1.2-intr | 1.3-msi | 1.4-dma |
| New file | `myfirst_hw.c` | `myfirst_sim.c` | `myfirst_pci.c` | `myfirst_intr.c` | `myfirst_msix.c` | `myfirst_dma.c` |
| Key discipline | Accessor abstraction | Fake device | Newbus attach | Filter/task split | Per-vector handlers | DMA maps |

The table makes the book's cumulative structure visible at a glance. A reader who understands the row for a given topic can predict how Chapter 19's work fits into the bigger picture.



## Reference: FreeBSD Manual Pages for Chapter 19

A list of the manual pages most useful for Chapter 19's material. Open each with `man 9 <name>` (for kernel APIs) or `man 4 <name>` (for subsystem overviews) on a FreeBSD system.

### Kernel API Manual Pages

- **`bus_setup_intr(9)`**: registering an interrupt handler.
- **`bus_teardown_intr(9)`**: tearing down a handler.
- **`bus_bind_intr(9)`**: binding to a CPU.
- **`bus_describe_intr(9)`**: labelling a handler.
- **`bus_alloc_resource(9)`**: resource allocation (generic).
- **`bus_release_resource(9)`**: resource release.
- **`atomic(9)`**: atomic operations including `atomic_add_64`.
- **`taskqueue(9)`**: taskqueue primitives.
- **`ppsratecheck(9)`**: rate-limited logging helper.
- **`swi_add(9)`**: software interrupts (mentioned as alternative).
- **`intr_event(9)`**: interrupt event machinery (if present; some APIs are internal).

### Device Subsystem Manual Pages

- **`pci(4)`**: PCI subsystem.
- **`vmstat(8)`**: `vmstat -i` for observing interrupts.
- **`devinfo(8)`**: device tree and resources.
- **`devctl(8)`**: runtime device control.
- **`sysctl(8)`**: reading and writing sysctls.
- **`dtrace(1)`**: dynamic tracing.

Most of these have been referenced in the chapter body. This consolidated list is for readers who want a single place to find them.



## Reference: Driver Memorable Phrases

A few aphorisms that summarise Chapter 19's discipline. Useful for reading and for code review.

- **"Read, acknowledge, defer, return."** The four things a filter does.
- **"FILTER_STRAY if you didn't recognise anything."** The shared-IRQ protocol.
- **"Mask before teardown; teardown before release."** The detach ordering.
- **"Filter context is spin-lock-only."** The no-sleep-locks rule.
- **"Every enqueue needs a drain before free."** The taskqueue lifecycle.
- **"One filter, one device, one state."** The isolation that keeps per-device code sane.
- **"If WITNESS panics, believe it."** The debug kernel catches subtle mistakes.
- **"PROD first, interrupt second."** Program the device (`INTR_MASK`) before enabling the handler.
- **"Small in the filter; large in the task."** The work-size discipline.
- **"Storm detection is a safety net, not a design tool."** Do not rely on the kernel's throttling.

None of these are complete specifications. Each is a compact reminder that unpacks into the chapter's detailed treatment.



## Reference: Glossary of Chapter 19 Terms

**ack (acknowledge)**: the operation of writing back to INTR_STATUS to clear the pending bit and deassert the IRQ line.

**driver_filter_t**: the C typedef for a filter-handler function: `int f(void *)`.

**driver_intr_t**: the C typedef for an ithread handler function: `void f(void *)`.

**edge-triggered**: an interrupt signalling mode where the interrupt is signalled by a level transition.

**FILTER_HANDLED**: return value from a filter meaning "this interrupt is handled; no ithread needed".

**FILTER_SCHEDULE_THREAD**: return value meaning "schedule the ithread to run".

**FILTER_STRAY**: return value meaning "this interrupt is not for this driver".

**filter handler**: a C function running in primary interrupt context.

**Giant**: the legacy single global kernel lock; modern drivers avoid it by setting INTR_MPSAFE.

**IE (interrupt event)**: short for `intr_event`.

**INTR_MPSAFE**: a flag promising the handler does its own synchronisation and is safe without Giant.

**INTR_STATUS**: the device register that tracks pending interrupt causes (RW1C).

**INTR_MASK**: the device register that enables specific interrupt classes.

**intr_event**: kernel structure representing one interrupt source.

**ithread**: kernel interrupt thread; runs deferred handlers in thread context.

**level-triggered**: an interrupt signalling mode where the interrupt fires while the level is held.

**MSI**: Message Signaled Interrupts; a PCIe mechanism (Chapter 20).

**MSI-X**: the richer variant of MSI with a table of vectors (Chapter 20).

**primary interrupt context**: the context of a filter handler; no sleeping, no sleep locks.

**PCIR_INTLINE / PCIR_INTPIN**: PCI configuration-space fields specifying the legacy IRQ line and pin.

**RF_ACTIVE**: resource allocation flag; activate the resource in one step.

**RF_SHAREABLE**: resource allocation flag; allow sharing the resource with other drivers.

**stray interrupt**: an interrupt for which no filter returned a claim; counted separately by the kernel.

**storm**: a situation in which a level-triggered interrupt fires continuously because the driver does not acknowledge.

**SYS_RES_IRQ**: resource type for interrupts.

**taskqueue**: a kernel primitive for running deferred work in thread context.

**trap stub**: the small piece of kernel code that runs when the CPU takes an interrupt vector.

**EOI (End of Interrupt)**: the signal sent to the interrupt controller to re-arm the IRQ line.



## Reference: A Closing Note on Interrupt-Handling Philosophy

A paragraph to close the chapter with, worth returning to after the labs.

An interrupt handler's job is not to do the device's work. The device's work (processing a packet, finishing an I/O, reading a sensor) is done by the rest of the driver, in thread context, under the driver's full set of locks. The handler's job is narrower: to notice that the device has something to say, to acknowledge the device so the conversation can continue, to schedule the real work that will happen later, and to return quickly enough that the CPU is free for the interrupted thread or for the next interrupt.

A reader who has written the Chapter 19 driver has written one interrupt handler. It is small. The rest of the driver is what makes it useful. Chapter 20 will specialise the handler to per-vector work on MSI-X. Chapter 21 will specialise the task to walking a DMA descriptor ring. Each of those is an extension, not a replacement. The Chapter 19 handler is the skeleton that both build on.

The skill Chapter 19 teaches is not "how to handle interrupts for the virtio-rnd device". It is "how to split work between primary context and thread context, how to respect the filter's constraints, how to tear down cleanly, and how to cooperate with other drivers on a shared line". Each of those is a transferable skill. Every driver in the FreeBSD tree exercises some of them; most drivers exercise all of them.

For this reader and for this book's future readers, the Chapter 19 filter and task are a permanent part of the `myfirst` driver's architecture. Every later chapter assumes them. Every later chapter extends them. The driver's overall complexity will grow, but the interrupt path will remain what Chapter 19 made it: a narrow, fast, correctly-ordered piece of code that gets out of the way so the rest of the driver can do its job.



---

_End of Chapter 19._
