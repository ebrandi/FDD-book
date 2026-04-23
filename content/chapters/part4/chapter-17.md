---
title: "Simulating Hardware"
description: "Chapter 17 takes the static register block introduced in Chapter 16 and makes it behave like a real device: a callout drives autonomous status changes, a write-triggered protocol schedules delayed events, read-to-clear semantics mirror real hardware, and a fault-injection path teaches error handling without risking real silicon. The driver grows from 0.9-mmio to 1.0-simulated, gains a new simulation file, and arrives at Part 4 ready to meet real PCI devices in Chapter 18."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 17
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 195
---

# Simulating Hardware

## Reader Guidance & Outcomes

Chapter 16 ended with a driver that owned a register block. The `myfirst` module at version `0.9-mmio` carries a 64-byte region of kernel memory shaped to look like a device: ten named 32-bit registers, a small set of bit masks, a device ID that reads `0x4D594649`, a firmware revision that encodes `1.0`, and a `STATUS` register whose `READY` bit is asserted at attach and cleared at detach. Every access goes through `bus_space_read_4` and `bus_space_write_4`, wrapped by the familiar `CSR_READ_4`, `CSR_WRITE_4`, and `CSR_UPDATE_4` macros. The driver's mutex (`sc->mtx`) protects each register touch. An access log remembers the last sixty-four accesses, a ticker task flips `SCRATCH_A` on demand, and a small `HARDWARE.md` documents the whole surface.

That driver can already do a great deal. It can read registers. It can write registers. It can observe its own accesses, catch out-of-bounds offsets on debug kernels, and enforce the lock discipline that every later chapter will depend on. What it cannot do yet is behave like a real device. Its `STATUS` bits never change on their own. Its `DATA_IN` register does not start a multi-cycle operation. Its `INTR_STATUS` does not accumulate events. Writing `CTRL.GO` does not schedule anything. Nothing in the simulation has a heartbeat of its own; every change to the register block is a direct consequence of the last user-space write.

Real devices are not like that. A temperature sensor refreshes its value every few milliseconds without anyone asking. A serial controller raises an interrupt when a byte lands in the receive FIFO, long after the driver's last write. A network card's descriptor ring fills with packets while the driver sleeps. The interesting parts of a device driver come from reacting to change, not from producing it. Teaching a reader to write those reactive paths requires a simulated device that produces change, and Chapter 17 is where the simulation learns to do so.

### What Chapter 17 Is About

Chapter 17's scope is narrow but deep. It takes the static register block from Chapter 16 and gives it four new properties:

- Autonomous behaviour. A callout fires on a cadence the driver controls and updates `STATUS` bits, `INTR_STATUS` bits, or data registers as if a real device's internal state machine were running in silicon. The reader sees values change between two adjacent sysctl reads, even though no user-space write occurred between them.

- Command-triggered protocol. Writing a designated bit in `CTRL` schedules a delayed state change. After a configurable delay, `STATUS.DATA_AV` asserts and, optionally, `INTR_STATUS.DATA_AV` latches. This is the pattern every command-response device follows: a write starts something, a later status change signals that something finished.

- Realistic timing. Delays on the order of microseconds use `DELAY(9)` where appropriate and `pause_sbt(9)` where a sleep is safe. Delays on the order of milliseconds use `callout_reset_sbt(9)` so no thread is blocked while the simulated device works. Each choice has a place; each choice has a cost; Chapter 17 teaches both.

- Fault injection. A sysctl lets the reader ask the simulation to fail. Failures take several shapes: the next read returns a fixed error value, the next write never asserts `DATA_AV`, a random fraction of operations set `STATUS.ERROR`, a `STATUS.BUSY` bit stays on indefinitely. Each mode exercises a different error-handling path in the driver, and the driver learns to notice, recover, or report.

The chapter builds these properties on top of the Chapter 16 driver without replacing it. The same register map, the same accessors, the same `sc->mtx`, the same access log. What grows is the simulation backend. The `myfirst_hw.c` file from Chapter 16 gains a sibling file, `myfirst_sim.c`, that holds the simulation's callouts, the fault-injection hooks, and the helpers that make the register block breathe. By the end of the chapter the driver is at version `1.0-simulated`, and the hardware-surface it shows the driver is rich enough to exercise almost every lesson of Part 3.

### Why Autonomous Behaviour Deserves a Chapter of Its Own

A natural question at this stage is whether callout-driven behaviour and fault injection really deserve a full chapter. Chapter 16 already placed a register block in front of the driver, and Chapter 18 will replace that block with real PCI hardware. Is simulation not just a stepping stone we should step off as soon as possible?

Three answers, each worth taking seriously.

First, the simulation in Chapter 16 was static on purpose. It existed to let the reader practise register access without also managing device behaviour. Chapter 17 is where device behaviour enters the picture, and the right place to introduce it is not in the middle of the real-PCI chapter, where the reader is already juggling BAR allocation, vendor and device IDs, and the newbus glue. A chapter dedicated to simulation gives the reader room to reason about how a device behaves, how delays shape protocol, and how faults propagate, without the distractions real hardware brings.

Second, simulation is not a one-time scaffold we discard after Chapter 18. It is a permanent part of driver development. Real drivers get tested against simulated devices long after they first run on real hardware, because simulation is the only way to produce deterministic failures, reproducible timing, and fault conditions on demand. Every serious FreeBSD subsystem has some form of simulation or test harness, and every working driver author learns how to build one. Chapter 17 teaches the technique on a small scale where the reader can see every moving part.

Third, the simulation in Chapter 17 is a teaching device in its own right. By writing a fake device, the reader is forced to think about what a real device does: when it asserts a bit, when it clears one, when it fails, when it latches, when it forgets. A reader who has written the simulation understands the protocol in a way that a reader who has only used the simulation does not. The chapter is as much about the discipline of thinking like hardware as it is about the code that implements it.

### Where Chapter 16 Left the Driver

A brief recap of where you should stand. Chapter 17 extends the driver produced at the end of Chapter 16 Stage 4, tagged as version `0.9-mmio`. If any of the items below feels uncertain, return to Chapter 16 before starting this chapter.

- Your driver compiles cleanly and identifies itself as `0.9-mmio` in `kldstat -v`.
- The softc contains a pointer `sc->hw` to a `struct myfirst_hw` that holds `regs_buf`, `regs_size`, `regs_tag`, `regs_handle`, and the Chapter 16 access log.
- Every register access in the driver goes through `CSR_READ_4(sc, off)`, `CSR_WRITE_4(sc, off, val)`, or `CSR_UPDATE_4(sc, off, clear, set)`.
- The accessors assert `sc->mtx` is held via `MYFIRST_ASSERT` on debug kernels.
- `sysctl dev.myfirst.0` lists the `reg_*` sysctls for each register, the `reg_ctrl_set` writeable sysctl, the `access_log_enabled` toggle, and the `access_log` dumper.
- `HARDWARE.md` documents the register map and the `CSR_*` API.
- `LOCKING.md` documents the detach ordering, including the new `myfirst_hw_detach` step.

That driver is what Chapter 17 extends. The additions are again moderate in line count: a new `myfirst_sim.c` file, four new callouts, two new sysctl groups, a small fault-injection state structure, and a handful of protocol helpers. The mental-model change is larger than the line count suggests.

### What You Will Learn

Once you have finished this chapter, you will be able to:

- Explain what "simulating hardware" means in kernel space, why simulation is a permanent part of driver development rather than a temporary crutch, and what makes a good simulation versus a misleading one.
- Design a register map for a simulated device, with separate sections for control, status, data, interrupts, and metadata, and justify each choice against the protocol the device is meant to implement.
- Choose register widths, offsets, bit-field layouts, and access semantics (read-only, write-only, read-to-clear, write-one-to-clear, read/write) that mirror patterns used in real FreeBSD drivers.
- Implement a simulated hardware backend in kernel memory that behaves autonomously, with a callout that updates registers on a cadence and a task that reacts to writes the driver issues.
- Expose the fake device to the driver using the same `bus_space(9)` abstraction that real drivers use, so the driver code has no knowledge of whether it is talking to silicon or to a struct in kernel memory.
- Test device behaviour from the driver side by writing commands, polling for status, reading data, and checking invariants under a variety of loads.
- Add timing and delay to the simulation using `DELAY(9)`, `pause_sbt(9)`, and `callout_reset_sbt(9)`, and explain why each tool is appropriate for the time scale where it is used.
- Simulate errors and fault conditions safely, with a sysctl-driven fault-injection path that can reproduce timeouts, data errors, busy-forever states, and random failures, and use those injected faults to exercise the driver's error paths.
- Refactor the simulation so it lives in its own file (`myfirst_sim.c`), document its interface in a `SIMULATION.md`, version the driver as `1.0-simulated`, and run a full regression pass.
- Recognise the limits of simulation and the scenarios where only real hardware (or hypervisor-backed virtual hardware) will produce trustworthy results.

The list is long; each item is narrow. The point of the chapter is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 17 stays focused.

- **Real PCI devices.** The `pci(4)` subsystem, vendor and device ID matching, BAR allocation through `bus_alloc_resource_any`, `pci_enable_busmaster`, and the glue that ties a driver to a real bus belong to Chapter 18. Chapter 17 stays in simulation and uses the Chapter 16 shortcut for the tag and handle.
- **Interrupts.** The simulation produces state changes that mimic what an interrupt handler would see. It does not yet register an actual interrupt handler via `bus_setup_intr(9)`, and it does not split work between a filter handler and an interrupt thread. Chapter 19 covers that. Chapter 17 polls through callouts and user-space syscalls; the callouts stand in for an interrupt source.
- **DMA.** Descriptor rings, scatter-gather lists, `bus_dma(9)` tags, bounce buffers, and cache flushing around DMA are Chapters 20 and 21. Chapter 17's data register stays a single 32-bit slot.
- **Full-system simulators.** QEMU, bhyve, virtio devices, and the kinds of simulation hypervisors offer are mentioned in passing as a bridge to Chapter 18. Chapter 17 stays in-kernel; the simulation runs inside the same kernel as the driver.
- **Protocol verification and formal methods.** Real hardware verification uses formal tools to prove a driver's protocol handling is correct. Those tools exist outside the scope of this book. Chapter 17 uses `INVARIANTS`, `WITNESS`, stress tests, and deliberate fault injection to build confidence.
- **Hypervisor-backed device emulation.** Chapter 17's simulation runs in the kernel's own address space. Virtio devices in a VM, emulated NICs in QEMU, and bhyve's device emulation all run in a hypervisor; the driver sees them through real PCI. That path is Chapter 18's concern.

Staying inside those lines keeps Chapter 17 a chapter about making a fake device behave like a real one. The vocabulary is what transfers; the specific subsystems are what Chapters 18 through 22 apply the vocabulary to.

### Estimated Time Investment

- **Reading only**: three to four hours. The simulation concepts are small but accumulate; each section introduces a new behaviour pattern and the composition is what makes the chapter rich.
- **Reading plus typing the worked examples**: eight to ten hours over two sessions. The driver evolves in five stages; each stage is a small refactor that merges into the existing `myfirst_hw.c` or into the new `myfirst_sim.c`.
- **Reading plus all labs and challenges**: thirteen to sixteen hours over three or four sessions, including stress testing under the debug kernel, working through the fault-injection scenarios, and reading a couple of real callout-driven drivers.

Sections 3, 6, and 7 are the densest. If the interplay of a callout, a mutex, and a state change feels opaque on first pass, that is expected: three primitives from Part 3 are composing in a new way. Stop, re-read Section 6's timing table, and continue when the composition has settled.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 16 Stage 4 (`0.9-mmio`). The starting point assumes every Chapter 16 register accessor, the split between `myfirst.c` and `myfirst_hw.c`, the `CSR_*` macros, and the access log.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly.
- You are comfortable with `callout_init_mtx`, `callout_reset_sbt`, and `callout_drain` from Chapter 13, and with `taskqueue` from Chapter 14.
- You understand `arc4random(9)` at a reading level. If it is new, skim the manual page before Section 7.

If any item above is shaky, fix it now rather than pushing through Chapter 17 and trying to reason from a moving foundation. Simulation code produces timing-sensitive bugs, and a debug kernel plus solid Part 3 foundations catch most of them on first contact.

### How to Get the Most Out of This Chapter

Three habits will pay off quickly.

First, keep a second terminal open on the machine where you are testing. Simulation makes registers change between two sysctl reads, and the only way to see that is to read the same sysctl twice in a row. A shell that loops `sysctl dev.myfirst.0.reg_status` every 200 milliseconds is how the chapter's examples come alive. If you cannot run two terminals side by side, a `watch -n 0.2 sysctl ...` loop in a tmux pane works just as well.

Second, read the access log after every experiment. The log captures every register access with a timestamp, an offset, a value, and a context tag. For a static simulation the log is boring: user-space reads, user-space writes, nothing else. For the Chapter 17 simulation the log is dense: callouts firing, delayed events triggering, faults being injected, timeouts expiring. Every interesting behaviour leaves a trail, and the trail is where the chapter's intuition comes from.

Third, break the simulation on purpose. Set the fault-injection rate to 100%. Set the callout interval to zero. Make the simulated device always busy. See how the driver reacts. The chapter's error-handling lessons are a lot clearer when you have seen the failure modes play out. The simulation is safe: you can inject as many faults as you like without risking real hardware, and the worst case is a kernel panic that a debug kernel catches cleanly.

### Roadmap Through the Chapter

The sections in order are:

1. **Why Simulate Hardware?** The case for simulation as a permanent part of driver development, the difference between a good simulation and a misleading one, and the goals Chapter 17's simulation is aiming at.
2. **Designing the Register Map.** How to choose registers, widths, layouts, and access semantics for a simulated device, with a worked example of the additions Chapter 17 makes on top of the Chapter 16 map.
3. **Implementing the Simulated Hardware Backend.** The first callout that updates registers autonomously, a `myfirst_sim_tick` helper that carries the simulation state machine, and the first stage of the Chapter 17 driver (`1.0-sim-stage1`).
4. **Exposing the Fake Device to the Driver.** The tag, the handle, the accessor path, and how the Chapter 16 abstraction carries forward without modification. A short section; the point is that the Chapter 16 work already did most of it.
5. **Testing Device Behavior from the Driver.** Writing a command, polling for status, reading data, and handling the race between the driver's observation and the simulation's own state updates. Stage 2 of the Chapter 17 driver.
6. **Adding Timing and Delay.** Microsecond-scale `DELAY(9)`, millisecond-scale `pause_sbt(9)`, and second-scale `callout_reset_sbt(9)`; when each is safe; when each is not; what latency the driver can expect from each. Stage 3.
7. **Simulating Errors and Fault Conditions.** A fault-injection framework built on top of the simulation, with modes that cover timeouts, data errors, stuck-busy, and random failures. Teaching the driver to notice, recover, or report. Stage 4.
8. **Refactoring and Versioning Your Simulated Hardware Driver.** The final split into `myfirst_sim.c`, a new `SIMULATION.md`, the version bump to `1.0-simulated`, and the regression pass. Stage 5.

After the eight sections come hands-on labs, challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 17's story and opens Chapter 18's, and a bridge to Chapter 18. The reference material at the end of the chapter is meant to be kept nearby while you work through the labs.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3, 6, and 7 stand alone reasonably well and make good single-sitting reads.



## Section 1: Why Simulate Hardware?

Chapter 16 already made the case for simulation as an on-ramp. A reader who does not own the exact device the book references can still practise register access against a simulated block. That argument is valid and still holds in Chapter 17, but it is not the only reason simulation earns a chapter of its own. Simulation is part of how serious driver development is done, and understanding why is what this section is about.

### The Working Driver Author's Toolkit

Pick any real FreeBSD driver and walk backward from the commit history. Most drivers that see active development share a common pattern: the first commits build the driver against real hardware. Later commits, often many later commits, add tests, harnesses, scaffolding, and instrumentation that let the driver be exercised without the original hardware sitting on the developer's desk. Some of this instrumentation runs in user space. Some of it is a hypervisor-backed fake device that speaks the same protocol as the real one. Some of it is a set of kernel-side stubs that pretend to be the hardware well enough to run the driver's state machines.

Why do working driver authors invest in this? Because real hardware is not reproducible. A real device's behaviour depends on its firmware revision, its temperature, its link partner, the signal integrity of its physical connection, and the phase of the moon. A driver that passes tests against one instance of a device may fail against a slightly different instance. A driver that passes tests today may fail tomorrow because the device's firmware autoupdate went through overnight. A driver whose failure is timing-dependent may pass a hundred runs and fail the hundred-and-first. Real hardware is a moving target, and a moving target is a bad foundation for regression testing.

Simulation fixes the target. A simulated device behaves the same way every time the same inputs arrive. A simulated device's fault conditions are under the author's control. A simulated device can be run in a tight loop without wearing out. A simulated device does not require lab hardware, cabling, a power supply, or a colleague whose help you need. Every property that makes simulation convenient for learning makes it valuable for production testing.

The conclusion is that Chapter 17's simulation is not disposable scaffolding. The patterns you learn here are the same patterns FreeBSD committers use to test drivers that nobody has access to any more because the silicon is twenty years old. The same techniques catch regressions before they reach a customer's machine. The same discipline, applied to the real PCI device in Chapter 18, lets the driver be exercised on a workstation with no network at all by telling a virtio-net backend to drop packets, corrupt frames, or duplicate descriptors at a controlled rate.

### Simulation Goals: Behaviour, Timing, Side Effects

When we say "simulate a device", we usually mean one of three things, and it is worth being explicit about which one we intend.

The first kind of simulation is **behavioural**. The simulation exposes a register interface, and writes and reads produce the same effects a real device would produce. The state machine is correct: writing `CTRL.GO` moves the device from idle to busy to done, setting `STATUS.DATA_AV` at the right point. The data path is correct: data written to `DATA_IN` shows up in `DATA_OUT` after some processing. The simulation does not have to match the real device's timing; it just has to follow the same rules.

The second kind of simulation is **timing-aware**. The simulation enforces the delays the real device would enforce. A write that the real device needs 500 microseconds to process takes 500 microseconds in the simulation, more or less, subject to the CPU's own timing resolution. A status bit that the real device asserts 2 milliseconds after a command asserts 2 milliseconds after the command in the simulation. A driver that passes timing-aware simulation is likely to work on the real hardware; a driver that only passes behavioural simulation may fail when the real device's latency produces a race the driver has not considered.

The third kind of simulation is **side-effect-aware**. The simulation mirrors the subtle parts of the protocol: reads that clear state, writes that have no effect, registers that return all-ones when the device is in a certain mode, bits that are write-one-to-clear versus bits that are read-to-clear. A driver that passes this kind of simulation is confident in the low-level protocol details, not just the high-level state flow.

The three kinds stack. A realistic simulation is all three: behaviour, timing, and side effects. Chapter 17 builds a simulation of all three kinds, though the timing precision is limited by what the kernel's scheduling machinery can deliver. The goal is not to match silicon microsecond-for-microsecond; the goal is to make the simulation realistic enough that the driver's important protocol paths get exercised and the race conditions those paths contain get a chance to manifest.

### What a Good Simulation Teaches and What It Does Not

A good simulation teaches protocol. It exercises every branch in the driver's state machine, every error path, every status check, every delay tolerance. A driver written against a good simulation is robust against the scenarios the simulation covers.

A good simulation does not teach silicon. The CPU's memory hierarchy, the device's internal pipelines, the signal integrity of the PCB traces, the interaction with the rest of the system's traffic, the thermal behaviour of the chip under load: none of these are in the simulation. A driver that passes simulation can still fail on real hardware if the failure is rooted in physics.

This distinction matters. Chapter 17's simulation is a teaching tool for protocol, and the driver it produces is a vehicle for learning, not a production network driver. Chapters 20 through 22 introduce DMA, interrupts, and real device performance concerns, all of which extend the simulation in directions that need real hardware to validate fully. Chapter 17 is step one of a longer progression: get the protocol right, then get the real-hardware details right.

There is a second subtlety worth naming. A simulation can teach the *wrong* protocol if the simulation is inconsistent with the real device. A reader who learns to expect the simulation's behaviour, then encounters real hardware whose behaviour diverges, will be confused. Chapter 17's simulation is deliberately designed to match the patterns common in real FreeBSD drivers: `STATUS.READY` clears while the device is busy and sets when it is ready, `STATUS.DATA_AV` latches until the driver reads the data, `INTR_STATUS` is read-to-clear, `CTRL.RESET` is write-one-to-self-clear. These patterns are not universal; different devices choose different conventions. But they are common, and a driver that handles these patterns well has learned transferable skill.

### No Hardware Dependency: The Practical Benefit

For this book, the most immediate benefit of simulation is that the reader does not need specific hardware to follow along. A reader working through Chapter 16 on a laptop with no external devices, on a virtual machine with no PCI passthrough, on an ARM single-board computer with only the onboard peripherals, can still build the driver and watch it behave. Chapter 17 preserves that property. The simulation lives entirely in kernel memory; the driver needs nothing beyond a running FreeBSD 14.3 system.

This matters for a teaching book. A reader who has to stop, order hardware, install it, and wait for delivery before they can continue is a reader who may not continue. A reader who can build and run the driver in the same session they read the chapter is a reader who is learning. Every Chapter 17 lab and challenge is designed to run on any x86 FreeBSD system without external hardware.

A secondary benefit is that simulation lets the book run ahead of what a reader's hardware can do. The driver in Chapter 17 exercises error paths that a real device would almost never trigger under normal operation. A reader who owns a production-quality network card would have trouble forcing the card into the kinds of failure modes Chapter 7's fault injection teaches. A simulated device can be told to fail on demand, as often as needed, under any condition the reader wants to exercise. The learning surface is broader.

### Full Control: The Pedagogical Benefit

Simulation gives the reader more control than real hardware ever does. The reader can freeze time in the simulation, step forward by a known number of ticks, and observe every intermediate state. The reader can disable the simulation's autonomous behaviour, make a single manipulation, and re-enable it. The reader can inject a specific fault, observe how the driver reacts, and then repair the fault by toggling a sysctl. None of this is possible against real silicon.

This level of control is pedagogically valuable. A chapter that explains "when the device is busy, the driver should wait for `STATUS.READY`" can demonstrate the scenario concretely. Chapter 17 does just that: a sysctl tells the simulation to enter a fake-busy state, and the reader can type commands while the driver patiently waits for the busy bit to clear. On real hardware, producing this scenario reliably would require a specific workload that the device would reject, or an artificial test fixture, or a debugger. In simulation, it is a one-line sysctl change.

Control also means the reader can experiment without consequences. Setting a register to an illegal value, issuing a command the device does not expect, asking the driver to access a register outside the mapped range: each of these is safe in simulation. The worst case is a KASSERT firing and a panic that a debug kernel catches cleanly. The reader can try ten bad ideas in an hour, learn from each, and come out better for it. Experimentation with real hardware carries real risks, and readers (reasonably) exercise more caution there.

### Safe Experiments: The Risk-Reduction Benefit

The word "safe" deserves expansion. A kernel panic from a simulated device is an embarrassment; a kernel panic from a real device can be worse. A driver that writes a wrong bit to a real device's `CTRL` register can brick the device. A driver that corrupts a real device's DMA configuration can write garbage over arbitrary RAM. A driver that mishandles a real interrupt can leave the system in a state where legitimate interrupts are missed indefinitely. Each of these failure modes has appeared in production FreeBSD over the years, and each has required a kernel update to fix.

Simulation eliminates most of this risk. The simulated device is a struct in kernel memory; a wrong bit goes into memory that is not connected to anything else. The simulated DMA does not exist yet (it arrives in Chapter 20). The simulated interrupts are callouts that the driver can stop at any time. A beginner writing their first driver can make every mistake the driver development lifecycle contains without causing a physical problem.

This is worth internalising. Chapter 17's simulation gives the reader permission to experiment aggressively, which is what a beginner needs to do to build confidence. A reader who has never broken a driver is a reader who has not yet learned how drivers break. Chapter 17 is the chapter where the reader learns both how to break a driver on purpose and how to recognise it when it breaks on its own.

### What We Will Simulate in Chapter 17

A short list of the specific behaviours the chapter will introduce. Each is added in the section that discusses it; the list here is a roadmap.

- **Autonomous `STATUS` updates.** A callout fires every 100 milliseconds and updates fields in `STATUS` as if a background hardware monitor were sampling the device's internal state. The driver can observe the updates; the test suite can assert them.
- **Command-triggered delayed events.** Writing `CTRL.GO` schedules a callout that, after a configurable delay (default 500 milliseconds), sets `STATUS.DATA_AV` and copies `DATA_IN` to `DATA_OUT`. This is the textbook "asynchronous device" pattern.
- **Read-to-clear `INTR_STATUS`.** Reading `INTR_STATUS` returns the current pending-interrupt bits and then atomically clears them. Writing `INTR_STATUS` has no effect (real devices vary; some allow write-one-to-clear, which is a common variant we will discuss).
- **Simulated sensor data.** A `SENSOR` register holds a value that the simulation updates on a cadence, mimicking a temperature or pressure reading. The driver can poll it; a user-space tool can graph it.
- **Timeout tolerance.** The driver's command path waits up to a configurable number of milliseconds for `STATUS.DATA_AV` to assert. If the simulation is configured to delay longer than the timeout, the driver reports a timeout and recovers.
- **Random fault injection.** A sysctl controls the probability that any operation will inject a fault. The fault types include: `STATUS.ERROR` set after a command, the next command timing out entirely, a read returning `0xFFFFFFFF`, the device reporting fake-busy indefinitely. Each fault exercises a different driver path.

Every one of these behaviours is implemented by Section 8. The chapter builds them one at a time so the reader can internalise each before moving on.

### The Simulation's Place in the Book

Chapter 17 sits between Chapter 16 (static register access) and Chapter 18 (real PCI hardware). It is the bridge between the abstract vocabulary and the concrete device. A reader who completes Chapter 17 has a driver that has exercised nearly every protocol pattern the book will discuss, against a simulated device that behaves like a real one. That reader walks into Chapter 18 with a driver that is already close to production-grade in structure, needing only the real-PCI glue to run on actual silicon.

The simulation is also a reference. When Chapter 18 replaces the simulated register block with a real PCI BAR, the reader can compare the simulation's behaviour against the real device's behaviour and notice the differences. The simulation is the yardstick. When Chapter 19 adds interrupts, the simulation's callout-driven state changes become the model for what an interrupt handler reacts to. When Chapter 20 adds DMA, the simulation's data registers become the model for what the DMA engine reads and writes. The simulation is the teaching vocabulary that every later chapter extends.

### Wrapping Up Section 1

Simulation is a permanent part of driver development, not a temporary scaffold. It gives driver authors reproducible tests, controlled failure injection, and the ability to exercise error paths that real hardware rarely produces. Chapter 17 builds a simulation of three kinds (behavioural, timing-aware, side-effect-aware) on top of the Chapter 16 driver. The simulation is deliberately designed to match common patterns in real FreeBSD drivers so the reader's intuition transfers to real hardware later.

The chapter's specific goals include autonomous `STATUS` updates, command-triggered delayed events, read-to-clear semantics, simulated sensor data, timeout tolerance, and random fault injection. Each is introduced in the section that needs it; together they produce a driver rich enough to exercise almost every Part 3 lesson.

Section 2 takes the next step. Before we simulate device behaviour, we need a register map that the behaviour operates on. Chapter 16 gave us a static map; Chapter 17 extends it for dynamic behaviour.



## Section 2: Designing the Register Map

Chapter 16 gave the simulated device a register map. Chapter 17's work extends that map, but the extensions are not automatic. Adding a register to a real device requires a team of hardware engineers, a silicon respin, and a new datasheet. Adding a register to a simulated device is easy in mechanical terms, but the design work (what should the register do? who writes it? who reads it? what are its side effects?) is the same, and the discipline of doing it well is the same.

This section walks through the design, not just the result. By the end of Section 2 you will have a register map that supports every behaviour Section 1 listed, documented well enough that the implementation in Sections 3 through 7 is almost mechanical. The exercise is transferable: every time you read a real datasheet, you will recognise the decisions the hardware designers made, and you will have a vocabulary for evaluating whether the decisions were wise.

### What Is a Register Map?

A register map is the complete catalogue of a device's register interface. It lists every register, every offset, every width, every field, every bit, every access type, every reset value, every side effect. A real device's register map may run to hundreds of pages; a simple device's register map may fit on a single page. The form is similar in both cases: a table, or a set of tables, with headings that answer the questions a driver author needs to ask.

A register map is not prose. It is a reference document. The driver author consults it when writing code, and the test author consults it when writing tests. A register map that is vague, incomplete, or ambiguous produces drivers that are wrong in ways that are hard to diagnose. A register map that is precise produces drivers that match what the device expects.

For a simulated device, the register map plays one extra role. It is also the specification against which the simulation's behaviour is tested. If the simulation disagrees with the register map, the simulation is wrong and must be fixed. If the driver disagrees with the register map, the driver is wrong and must be fixed. The map is the contract. Changes to the map require changes to both the simulation and the driver. This is exactly how real device development works: the datasheet is the contract, and the hardware team, the firmware team, and the driver team all work to it.

### The Chapter 16 Map as a Starting Point

Recall the Chapter 16 register map:

| Offset | Width  | Name            | Access    | Chapter 16 Behaviour                           |
|--------|--------|-----------------|-----------|------------------------------------------------|
| 0x00   | 32 bit | `CTRL`          | R/W       | Plain read-write memory.                       |
| 0x04   | 32 bit | `STATUS`        | R/W       | Plain read-write memory.                       |
| 0x08   | 32 bit | `DATA_IN`       | R/W       | Plain read-write memory.                       |
| 0x0c   | 32 bit | `DATA_OUT`      | R/W       | Plain read-write memory.                       |
| 0x10   | 32 bit | `INTR_MASK`     | R/W       | Plain read-write memory.                       |
| 0x14   | 32 bit | `INTR_STATUS`   | R/W       | Plain read-write memory.                       |
| 0x18   | 32 bit | `DEVICE_ID`     | R-only    | Fixed `0x4D594649`.                            |
| 0x1c   | 32 bit | `FIRMWARE_REV`  | R-only    | Fixed `0x00010000`.                            |
| 0x20   | 32 bit | `SCRATCH_A`     | R/W       | Plain read-write memory.                       |
| 0x24   | 32 bit | `SCRATCH_B`     | R/W       | Plain read-write memory.                       |

Ten registers, 40 bytes of defined space, 64 bytes allocated to leave room for growth. Every access is direct: reads return whatever the last write stored, writes go straight into memory. No side effects on reads. No side effects on writes. No hardware state machine behind the registers.

Chapter 17 changes two things about this map. First, the existing registers grow behavioural semantics: `STATUS.DATA_AV` will be set by the simulation, not just by the driver's writes; `INTR_STATUS` becomes read-to-clear. Second, the map adds a small number of new registers that the Chapter 17 simulation needs: a `SENSOR` register, a `DELAY_MS` register, a `FAULT_MASK` register, and a few more. The 64-byte allocation from Chapter 16 already has room; no allocator change is required.

### Register Access Semantics

Before we write any register, we should name the access semantics we care about. A register's access semantics are the rules for what happens when the driver reads or writes it. The main categories in common use are:

**Read-only (RO).** The register returns a value the device or the simulation produced. Writes are ignored or produce an error. `DEVICE_ID` and `FIRMWARE_REV` are examples.

**Read/Write (RW).** The register stores whatever the driver writes. Reads return the last-written value. `CTRL`, `SCRATCH_A`, and `SCRATCH_B` are examples.

**Write-only (WO).** The register accepts writes, but reads return a fixed value (often zero, sometimes garbage, occasionally the last-read value from a different source). `DATA_IN` is often write-only in real devices.

**Read-to-clear (RC).** Reading the register returns its current value and then clears it. Writes are typically ignored. `INTR_STATUS` is the classic example.

**Write-one-to-clear (W1C).** Writing a 1 to a bit clears that bit; writing a 0 has no effect. Reads return the current value. `INTR_STATUS` on some devices uses W1C instead of RC.

**Write-one-to-set (W1S).** Writing a 1 sets the bit; writing a 0 has no effect. Less common than W1C, but used in some hardware for control registers where the driver should not have to do a read-modify-write.

**Read/write with side effect (RWSE).** The register is readable and writable, but the write (or the read) triggers something beyond a plain memory update. `CTRL` often falls in this category: writing `CTRL.RESET` triggers a device reset, which is a side effect.

**Sticky (latched).** A bit, once set by the hardware, stays set until the driver explicitly clears it. Error bits in `STATUS` are usually sticky. `STATUS.ERROR` will be sticky in the Chapter 17 map.

**Reserved.** The bit has no defined meaning. Writes should either preserve the existing value or write zero; reads may return any value. A driver that ignores reserved bits is robust against future hardware revisions; a driver that depends on reserved-bit behaviour is fragile.

Each access type has implications for the driver's code. A driver reading an RC register should do so exactly when the protocol demands, because every extra read consumes an event. A driver reading a WO register is reading garbage, which is a bug waiting to appear if the value is ever acted on. A driver doing a read-modify-write on a sticky-bit register must be careful not to clear a bit it did not intend to touch. A driver writing a W1C register uses `CSR_WRITE_4(sc, REG, mask)` and not `CSR_UPDATE_4(sc, REG, mask, 0)`; the two look similar but produce different behaviour on the hardware.

Chapter 17's simulation uses RO, RW, RC, sticky, and RWSE semantics. It does not introduce W1C or W1S (though the challenges invite the reader to add them). The simulation's goal is to cover the common cases thoroughly; the less common cases become natural extensions once the common ones are understood.

### Chapter 17 Additions

The Chapter 17 register map adds the following registers:

| Offset | Width  | Name            | Access    | Chapter 17 Behaviour                                        |
|--------|--------|-----------------|-----------|-------------------------------------------------------------|
| 0x28   | 32 bit | `SENSOR`        | RO        | Simulated sensor value. Updated by a callout every 100 ms. |
| 0x2c   | 32 bit | `SENSOR_CONFIG` | RW        | Sensor update interval and amplitude settings.              |
| 0x30   | 32 bit | `DELAY_MS`      | RW        | Number of milliseconds the device takes per command.        |
| 0x34   | 32 bit | `FAULT_MASK`    | RW        | Which simulated faults are enabled. Bitfield.               |
| 0x38   | 32 bit | `FAULT_PROB`    | RW        | Probability of random fault per operation (0..10000).       |
| 0x3c   | 32 bit | `OP_COUNTER`    | RO        | Count of commands the simulated device has processed.       |

Six new registers, using offsets `0x28` through `0x3c`, fitting entirely inside the 64-byte region Chapter 16 allocated. No softc change, no allocator change, no `bus_space_map` change.

Each register has a specific job:

- **`SENSOR`** is the simulated temperature, pressure, or voltage. The exact interpretation does not matter for the simulation; what matters is that it changes on its own, teaching the reader how to handle registers whose value is produced autonomously. The value oscillates around a baseline using a simple formula the callout will implement.

- **`SENSOR_CONFIG`** lets the driver (or the user through a sysctl) control the sensor's behaviour. The low 16 bits are the update interval in milliseconds; the high 16 bits are the amplitude of the oscillation. A value of `0x0064_0040` means "100 ms interval, amplitude 64". Changing the register changes the simulation's next update.

- **`DELAY_MS`** is how long a command takes in the simulation. Writing `CTRL.GO` schedules `STATUS.DATA_AV` to assert `DELAY_MS` milliseconds later. The default is 500. Setting it to 0 makes commands complete on the next callout tick; setting it to a large value lets the reader exercise the driver's timeout path.

- **`FAULT_MASK`** is a bitfield that selects which fault modes are active. Bit 0 enables the "operation times out" fault; bit 1 enables the "read returns all-ones" fault; bit 2 enables the "error bit set after every command" fault; bit 3 enables the "device always busy" fault. Multiple bits can be set simultaneously.

- **`FAULT_PROB`** controls the probability that any single operation will inject a fault, in tenths of a basis point. A value of 10000 is 100% (fault every time); 5000 is 50%; 0 disables random faulting entirely. This gives the reader fine-grained control over how aggressively faults are injected.

- **`OP_COUNTER`** counts every command the simulation has processed. It is read-only; writes are ignored. The driver can use it to verify that a command actually reached the simulation, and the user can read it from user space to check activity.

### Header Additions for the New Registers

The header for the additions lives in `myfirst_hw.h`, extending the Chapter 16 definitions:

```c
/* Chapter 17 additions to the simulated device's register map. */

#define MYFIRST_REG_SENSOR        0x28
#define MYFIRST_REG_SENSOR_CONFIG 0x2c
#define MYFIRST_REG_DELAY_MS      0x30
#define MYFIRST_REG_FAULT_MASK    0x34
#define MYFIRST_REG_FAULT_PROB    0x38
#define MYFIRST_REG_OP_COUNTER    0x3c

/* SENSOR_CONFIG register fields. */
#define MYFIRST_SCFG_INTERVAL_MASK  0x0000ffffu  /* interval in ms */
#define MYFIRST_SCFG_INTERVAL_SHIFT 0
#define MYFIRST_SCFG_AMPLITUDE_MASK 0xffff0000u  /* oscillation range */
#define MYFIRST_SCFG_AMPLITUDE_SHIFT 16

/* FAULT_MASK register bits. */
#define MYFIRST_FAULT_TIMEOUT    0x00000001u  /* next op times out        */
#define MYFIRST_FAULT_READ_1S    0x00000002u  /* reads return 0xFFFFFFFF  */
#define MYFIRST_FAULT_ERROR      0x00000004u  /* STATUS.ERROR after op    */
#define MYFIRST_FAULT_STUCK_BUSY 0x00000008u  /* STATUS.BUSY latched on   */

/* CTRL register: GO bit (new for Chapter 17). */
#define MYFIRST_CTRL_GO          0x00000200u  /* bit 9: start command     */

/* STATUS register: BUSY and DATA_AV are now dynamic (set by simulation). */
/* (The mask constants were already defined in Chapter 16.)               */
```

The new CTRL bit `MYFIRST_CTRL_GO` does not overlap any existing bit. `MYFIRST_CTRL_ENABLE` is bit 0, `RESET` is bit 1, `MODE_MASK` covers bits 4 through 7, `LOOPBACK` is bit 8. The new `GO` bit at bit 9 fits cleanly in the gap.

The comments in the header are deliberately terse. In a real driver, comments at this level would point at the datasheet section (for example, `/* See datasheet section 4.2.1 */`). For the simulation, the datasheet is this chapter; a reader looking for the definitive behaviour of `MYFIRST_FAULT_ERROR` should consult Section 7 of Chapter 17.

### Writing a Register Map Table for Your Own Device

The exercise of writing a register map is one of the most valuable things you can do as a beginning driver author. Even if you never write the simulation or the driver, the act of sitting down and thinking "what would this device's registers be?" forces you to think about the device as an entity with a protocol rather than as a magic box.

A useful template:

1. Decide what the device does at a high level. One sentence.
2. Identify the commands the device must accept. Each command may become a bit in a `CTRL` register, a value in a `COMMAND` register, or its own register.
3. Identify the states the device reports. Each state may become a bit in a `STATUS` register, a value in a `STATE` register, or its own read-only register.
4. Identify the data the device produces or consumes. Input data goes in a `DATA_IN` register or a FIFO. Output data comes from a `DATA_OUT` register or a FIFO.
5. Identify the interrupts the device can raise. Each interrupt source becomes a bit in `INTR_STATUS` (or an equivalent).
6. Identify any metadata the driver needs at startup. Device ID, firmware revision, feature flags, capability bits.
7. Leave space for configuration registers the driver may want to tune. In Chapter 17, `DELAY_MS`, `FAULT_MASK`, and `FAULT_PROB` are configuration registers; a real device might have timeout thresholds, error recovery options, power management settings.
8. Decide the width of each register. 32 bits is the common default on modern devices. Narrower widths exist for legacy devices or for devices where the register count is very large.
9. Decide the offset of each register. Keep related registers contiguous when possible. Align offsets to the register's width (32-bit registers at offsets divisible by 4).
10. Decide the access semantics (RO, RW, RC, W1C, ...). Be explicit; ambiguity here produces bugs.
11. Decide the reset value of each register. What should the register hold at power-on? At reset? At module attach?
12. Document the side effects of every non-trivial register.

This list is long, but each step is small. A real device team might take weeks to produce a register map this detailed. A simulated device's map can be produced in an afternoon. The Chapter 17 map is the result of this exercise applied to a teaching device.

### Example Layout and Design Choices

To make the discipline concrete, walk through the design decisions behind the Chapter 17 additions.

**Decision 1: Group related registers.** The new registers `SENSOR` (0x28) and `SENSOR_CONFIG` (0x2c) are adjacent. A driver reading the sensor code can access both with a single region read if needed, and a reader scanning the register dump sees them together. The same logic puts `FAULT_MASK` (0x34) and `FAULT_PROB` (0x38) adjacent.

**Decision 2: Put configuration after diagnostic.** `DEVICE_ID` (0x18) and `FIRMWARE_REV` (0x1c) are read-only diagnostic registers; they come first in the high-offset section. `SENSOR` and its config come next. `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB` are configuration; they come after. `OP_COUNTER` is another diagnostic; it goes at the end.

**Decision 3: Use the existing CTRL register for the new GO bit.** Rather than adding a dedicated `COMMAND` register, the `GO` bit in `CTRL` is how the driver initiates a command. This matches the Chapter 16 pattern (the `ENABLE` and `RESET` bits are both in `CTRL`) and saves a register.

**Decision 4: Make `OP_COUNTER` read-only.** A writable counter would invite bugs where the driver accidentally reset the counter mid-operation. A read-only counter is always monotonic (modulo 32-bit wrap) and always reflects the simulation's view.

**Decision 5: Encode two fields in `SENSOR_CONFIG`.** Rather than two separate registers for interval and amplitude, a single register with two 16-bit fields saves one slot and matches a common hardware pattern. A real device might have an 8-bit command ID in the high byte and a 24-bit argument in the lower three bytes of a register; the principle is the same.

**Decision 6: Use `FAULT_PROB` in tenths of a basis point (range 0 to 10000).** Using 10000 as full-scale rather than 100 gives two extra decimal places of precision. Fault probabilities of 0.5%, 0.75%, 1.25% become expressible as integers without fractional arithmetic.

Each decision is small; the composition is what makes the map usable. A register map designed carelessly produces a driver full of awkward patches; a map designed carefully produces a driver that writes itself.

### Naming Conventions for Constants

A note on the naming conventions Chapter 17 uses for bit masks, which mirror what real FreeBSD drivers do.

The prefix `MYFIRST_` marks everything as belonging to the driver. Inside that, `REG_` indicates a register offset, `CTRL_` indicates a bit or field in `CTRL`, `STATUS_` indicates a bit in `STATUS`, and so on. The pattern is `<driver>_<register>_<field>`. For example, `MYFIRST_STATUS_READY` is the `READY` bit in the `STATUS` register. The name is long, but it is unambiguous, and modern editors complete it quickly.

Masks that cover multiple bits end with `_MASK`; shifts to align the mask end with `_SHIFT`. `MYFIRST_CTRL_MODE_MASK` is the mask for the 4-bit `MODE` field; `MYFIRST_CTRL_MODE_SHIFT` is the number of bits to shift right to bring the field to bit zero. Extracting the field reads as `(ctrl & MYFIRST_CTRL_MODE_MASK) >> MYFIRST_CTRL_MODE_SHIFT`; setting it reads as `(mode << MYFIRST_CTRL_MODE_SHIFT) & MYFIRST_CTRL_MODE_MASK`.

Fixed values end with `_VALUE`. `MYFIRST_DEVICE_ID_VALUE` is the constant value of the `DEVICE_ID` register. This convention keeps values distinguishable from masks at a glance.

Real drivers sometimes deviate from this convention, often for historical reasons or because their register names are long. The `if_em` driver uses shorter prefixes (`EM_` rather than `E1000_` in some places) to fit within reasonable line lengths. The `if_ale` driver uses `CSR_READ_4(sc, ALE_REG_NAME)` with `ALE_` as the prefix. The principle is the same across drivers: every constant is visibly part of one driver's namespace, and the pattern inside the namespace is consistent.

For Chapter 17, consistency matters more than brevity. The reader is still learning the patterns; a long, explicit name is more educational than a short, ambiguous one.

### Validating the Map Before Writing Code

Before implementing the simulation, it is worth validating the map against a few sanity checks.

**Check 1: Offsets fit inside the allocated region.** The Chapter 16 region is 64 bytes. The highest defined offset in the Chapter 17 map is `0x3c`, with width 4, so the last byte used is at offset `0x3f`. `0x40` is one past the allocated region. The map fits exactly; there is no room for a further register without enlarging the allocation. That is a design constraint worth knowing.

**Check 2: All offsets are 4-byte aligned.** Every register is 32 bits wide, and every offset is a multiple of 4. `bus_space_read_4` and `bus_space_write_4` require 4-byte alignment on most platforms; an unaligned access would fault on some architectures and silently misbehave on others. The map follows the rule.

**Check 3: No two registers overlap.** Each 32-bit register occupies 4 consecutive bytes. Check that adjacent offsets differ by at least 4. They do: 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c.

**Check 4: No reserved bits are used for multiple purposes.** A bit defined for one field must not appear in another field. `MYFIRST_CTRL_ENABLE` is bit 0, `RESET` is bit 1, `MODE` covers bits 4 through 7, `LOOPBACK` is bit 8, `GO` is bit 9. No overlap.

**Check 5: Access semantics are consistent with the simulation's intent.** `DEVICE_ID` and `FIRMWARE_REV` are RO, which means the simulation will refuse to change them after attach. `INTR_STATUS` will be RC, meaning the simulation clears it when the driver reads it. `OP_COUNTER` is RO, meaning writes do nothing. Everything else is RW (possibly with side effects). This is consistent with what Section 1 described.

**Check 6: Reset values are sensible.** At attach, `DEVICE_ID` is `0x4D594649`, `FIRMWARE_REV` is `0x00010000`, `STATUS` has `READY` set, everything else is zero. `DELAY_MS` defaults to 500 (500 ms per command). `SENSOR_CONFIG` defaults to 0x0064_0040 (100 ms interval, amplitude 64). `FAULT_MASK` defaults to 0 (no faults enabled). `FAULT_PROB` defaults to 0 (no random faults). The defaults describe a simulation that behaves like a reliable device until told otherwise.

These checks are the kind of thing a disciplined driver author runs through before writing any code. They take a few minutes and catch several common bugs before they appear.

### Documenting the Map

Chapter 16 introduced `HARDWARE.md`. Chapter 17 extends it. The section headings in `HARDWARE.md` at the end of Chapter 17 will be:

1. Version and scope
2. Register summary table (every register, every offset, every width, every access type, every default value)
3. CTRL register fields (table of every bit in CTRL)
4. STATUS register fields (table of every bit in STATUS)
5. INTR_MASK and INTR_STATUS fields (table of every interrupt source)
6. SENSOR_CONFIG fields (interval and amplitude)
7. FAULT_MASK fields (table of every fault type)
8. Register-by-register reference (one paragraph per register, describing behaviour, side effects, and usage)
9. Common sequences (write-then-read-to-verify, command-and-wait, fault-injection-for-test)
10. Observability (every sysctl that reads or writes a register)
11. Simulation notes (which registers are dynamic, which are static, what the callouts do)

This document is long, perhaps 100 lines. It is the single source of truth for what the driver expects from the simulation. A reader who forgets a register's behaviour opens `HARDWARE.md` and finds the answer. A contributor adding a new register updates `HARDWARE.md` first, then changes the code.

### Common Mistakes in Register Map Design

Beginners designing their first register map often make the same few mistakes. A brief catalogue so you can avoid them.

**Mistake 1: Using magic numbers in the code instead of named constants.** A driver that writes `CSR_WRITE_4(sc, 0x00, 0x1)` instead of `CSR_WRITE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_ENABLE)` is hard to read and impossible to refactor. The rule is: every offset is a constant; every bit is a constant; every field is a macro.

**Mistake 2: Overlapping bit definitions.** Two names, one bit. A reader doing a bitwise OR of `FLAG_A` and `FLAG_B` gets a value that corresponds to a third thing entirely, because `FLAG_A` and `FLAG_B` turn out to be the same bit. The defence is a comment next to each bit name listing its bit number, plus a sanity check at compile time (`_Static_assert((MYFIRST_CTRL_ENABLE & MYFIRST_CTRL_RESET) == 0, "bits overlap")` if you want to be defensive).

**Mistake 3: Unaligned accesses.** Defining a 32-bit register at offset `0x06` looks harmless but fails on any architecture where unaligned accesses are disallowed. Always align offsets to the register's width.

**Mistake 4: Forgetting reserved bits.** A 32-bit register where only bits 0 through 3 are defined may have unspecified values in bits 4 through 31. A driver that writes `CSR_WRITE_4(sc, REG, 0xF)` is fine for now, but a future revision that defines bit 5 may break because the driver is effectively writing zero to it every time. The defence is read-modify-write on registers with reserved bits: `CSR_UPDATE_4(sc, REG, mask_of_fields_we_touch, new_field_values)`.

**Mistake 5: Side effects on read that the driver does not expect.** An RC register that the driver reads for debugging purposes clears state the driver was about to act on. The defence is to document every side effect explicitly in the header and in `HARDWARE.md`, so a reader can see the danger before writing the bug.

**Mistake 6: Assuming writes always succeed.** A write to a RO register may be silently ignored, or may raise a fault, or may do something surprising on certain hardware. The defence is to use the correct access semantics and to stress-test the driver's use of each register.

**Mistake 7: Inconsistent widths.** A 32-bit register next to an 8-bit register where the access width is not obvious from the name. The defence is to make the width part of the accessor call (`CSR_READ_4` vs `CSR_READ_1`) and to name the register's type somewhere visible.

**Mistake 8: Overly clever bit layouts.** A 16-bit field that spans two non-adjacent byte lanes, requiring bit-gathering in the driver. Real devices do this sometimes (often because of historical constraints), but a simulated device should stick to contiguous fields. The simulation is a teaching device; clarity wins over cleverness.

**Mistake 9: Missing reset values.** A register whose reset value is unspecified forces the driver to guess what the register holds after attach. The defence is to initialise every register to a documented value in the attach path.

**Mistake 10: Stale documentation.** The code changes, the documentation does not. Two months later the driver works but the documentation lies. The defence is the discipline of updating `HARDWARE.md` in the same commit that changes the code.

Each mistake is small in isolation and expensive in aggregate. A driver written without these mistakes is a driver that reads well, tests well, and ages well.

### Wrapping Up Section 2

A register map is the specification of a device's programmer-visible interface. Chapter 17's map extends Chapter 16's with six new registers: `SENSOR`, `SENSOR_CONFIG`, `DELAY_MS`, `FAULT_MASK`, `FAULT_PROB`, and `OP_COUNTER`. Each register has a purpose, a width, an access type, a reset value, and a documented behaviour. The design choices behind each register are worth understanding, because they mirror the decisions real device designers make.

The map is documented in the header (`myfirst_hw.h`) and in `HARDWARE.md`. The header is consumed by the compiler; the markdown is consumed by humans. Both are kept in sync as the code evolves.

Section 3 takes the map and implements the simulation. The first callout fires; the first autonomous register update happens; the static Chapter 16 block acquires a heartbeat.



## Section 3: Implementing the Simulated Hardware Backend

Section 2 produced a register map. Section 3 makes the map behave. The simulation's first job is to update registers autonomously, without the driver asking it to. That is how real hardware behaves: a sensor refreshes its value on a clock, an interrupt controller latches events as they arrive, a network card's receive counter ticks up as frames arrive. Chapter 17's first simulation step is to produce a similar heartbeat.

The tool is one the reader already knows: a `callout`. Chapter 13 introduced `callout_init_mtx` and `callout_reset` for internal driver timers. Chapter 17 uses the same primitive for a different purpose: driving simulated device state. The callout fires every 100 milliseconds, acquires the driver's mutex, updates some registers, releases the mutex, and schedules its next firing. From the driver's point of view the registers just change; from the simulation's point of view a timer is running.

### The First Simulation File

Chapter 16's file layout was `myfirst.c` for the driver lifecycle, `myfirst_hw.c` for the hardware-access layer, and `myfirst_hw.h` for the shared definitions. Chapter 17 adds a new file, `myfirst_sim.c`, for the simulation backend. The split keeps the simulation code out of the hardware-access file, so a later chapter can replace `myfirst_sim.c` with real PCI-facing code without touching the accessors.

Create `myfirst_sim.c` alongside the Chapter 16 files. The first version is small:

```c
/*-
 * myfirst_sim.c -- Chapter 17 simulated hardware backend.
 *
 * Adds a callout that drives autonomous register changes, a
 * command-scheduling callout for command-triggered delays, and
 * the fault-injection state that Section 7 will populate.
 *
 * This file assumes Chapter 16's register access layer
 * (myfirst_hw.h, myfirst_hw.c) is present and functional.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/random.h>
#include <machine/bus.h>

#include "myfirst.h"
#include "myfirst_hw.h"
#include "myfirst_sim.h"
```

The includes follow FreeBSD conventions: `sys/param.h` first, then `sys/systm.h`, then the specific subsystem headers. `sys/random.h` will be needed for `arc4random` later; pulling it in now avoids a second edit. The driver's private headers come last.

The file compiles against the existing `myfirst.h` and `myfirst_hw.h`, plus a new `myfirst_sim.h` that will contain the simulation's API. A stub of that header:

```c
/* myfirst_sim.h -- Chapter 17 simulation API. */
#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

struct myfirst_softc;

/* Attach/detach the simulation. Called from myfirst_attach/detach. */
int  myfirst_sim_attach(struct myfirst_softc *sc);
void myfirst_sim_detach(struct myfirst_softc *sc);

/* Enable/disable the simulation's autonomous updates. */
void myfirst_sim_enable(struct myfirst_softc *sc);
void myfirst_sim_disable(struct myfirst_softc *sc);

/* Command scheduling: called when the driver writes CTRL.GO. */
void myfirst_sim_start_command(struct myfirst_softc *sc);

/* Sysctl registration for simulation controls. */
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
```

Five functions, one header file, one source file. The API is small. The simulation's internal state lives in a `struct myfirst_sim`, pointed at from the softc via `sc->sim`, in the same pattern as `sc->hw`.

### The Simulation State Structure

Inside `myfirst_sim.h`, define the state structure:

```c
struct myfirst_sim {
        /* Autonomous update callout. Fires every sensor_interval_ms. */
        struct callout       sensor_callout;

        /* Command completion callout. Fires DELAY_MS after CTRL.GO. */
        struct callout       command_callout;

        /* Last scheduled command's data. Saved so command_cb can
         * latch DATA_OUT when it fires. */
        uint32_t             pending_data;

        /* Whether a command is currently in flight. */
        bool                 command_pending;

        /* Baseline sensor value; the callout oscillates around this. */
        uint32_t             sensor_baseline;

        /* Counter used by the sensor oscillation algorithm. */
        uint32_t             sensor_tick;

        /* Operation counter. Written to OP_COUNTER on every completion. */
        uint32_t             op_counter;

        /* Whether the simulation is running. Stops cleanly at detach. */
        bool                 running;
};
```

Seven fields. Two are callouts (sensor updates and command completion). One is boolean state (command in flight). One is the saved pending data. Three are internal counters. One is the running flag.

The softc gains a pointer to the simulation structure:

```c
struct myfirst_softc {
        /* ... all existing fields, including hw ... */
        struct myfirst_sim   *sim;
};
```

The allocation and initialisation happen in `myfirst_sim_attach`, called from `myfirst_attach` after `myfirst_hw_attach` has already set up `sc->hw`. Putting the sim attach after the hw attach is deliberate: the simulation reads and writes registers through the Chapter 16 accessors, so the accessors must be ready before the simulation starts.

### The Sensor Callout: The First Autonomous Update

The smallest useful simulation update is the sensor callout. It fires every 100 milliseconds, updates the `SENSOR` register, and re-arms itself. The code:

```c
static void
myfirst_sim_sensor_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t config, interval_ms, amplitude, phase, value;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        /* Read the current SENSOR_CONFIG. */
        config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
        interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
            MYFIRST_SCFG_INTERVAL_SHIFT;
        amplitude = (config & MYFIRST_SCFG_AMPLITUDE_MASK) >>
            MYFIRST_SCFG_AMPLITUDE_SHIFT;

        /* Compute a simple oscillation. phase cycles 0..7 and back. */
        sim->sensor_tick++;
        phase = sim->sensor_tick & 0x7;
        value = sim->sensor_baseline +
                ((phase < 4) ? phase : (7 - phase)) *
                (amplitude / 4);

        /* Publish. */
        CSR_WRITE_4(sc, MYFIRST_REG_SENSOR, value);

        /* Re-arm at the current interval. */
        if (interval_ms == 0)
                interval_ms = 100;
        callout_reset_sbt(&sim->sensor_callout,
            interval_ms * SBT_1MS, 0,
            myfirst_sim_sensor_cb, sc, 0);
}
```

Step by step.

First, the callout asserts the lock. A callout that was scheduled with `callout_init_mtx(&co, &mtx, 0)` automatically acquires `mtx` before calling the callback, so the assertion is redundant for a well-disciplined driver, but it is cheap and it documents the invariant. Debug kernels catch any violation.

Second, the callout checks the `running` flag. If the simulation is being torn down (detach path), the callout exits immediately without re-arming. The detach path then drains the callout with `callout_drain`, which waits for any in-flight callback to finish.

Third, the callback reads `SENSOR_CONFIG` to learn the interval and amplitude. Reading the config each time means the user can change it through a sysctl and the next callback will use the new value, without any signalling mechanism beyond the register write.

Fourth, the callback computes an oscillating value. The `phase < 4 ? phase : (7 - phase)` trick produces a triangle wave that goes 0, 1, 2, 3, 4, 3, 2, 1, 0, 1, ... over successive calls. Multiplied by `amplitude / 4`, it oscillates between `baseline` and `baseline + amplitude`. The formula is simple by design; the point is to produce visible change, not to model a real sensor.

Fifth, the callback writes the new value to `SENSOR`. The write is what the driver or a user-space observer will see.

Sixth, the callback re-arms itself. `callout_reset_sbt` takes a signed binary time, so `interval_ms * SBT_1MS` converts milliseconds into the right units. The `pr` argument (precision) is zero, meaning the kernel can defer the callback by up to 100% of the interval to batch with other timers; a lower value would force stricter timing. The `flags` argument is zero, meaning no special flags like `C_DIRECT_EXEC` or `C_HARDCLOCK`; the callback runs on a normal callout thread with the registered mutex held.

If `interval_ms` was zero, the code defaults to 100 ms. A zero interval would immediately re-arm with zero delay, which would spin the kernel in a tight loop; the guard is defensive.

### Initialising the Sensor Callout

In `myfirst_sim_attach`:

```c
int
myfirst_sim_attach(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim;

        sim = malloc(sizeof(*sim), M_MYFIRST, M_WAITOK | M_ZERO);

        /* Initialise the callouts with the main mutex. */
        callout_init_mtx(&sim->sensor_callout, &sc->mtx, 0);
        callout_init_mtx(&sim->command_callout, &sc->mtx, 0);

        /* Pick a baseline sensor value; 0x1000 is arbitrary but visible. */
        sim->sensor_baseline = 0x1000;

        /* Default config: 100 ms interval, amplitude 64. */
        MYFIRST_LOCK(sc);
        CSR_WRITE_4(sc, MYFIRST_REG_SENSOR_CONFIG,
            (100 << MYFIRST_SCFG_INTERVAL_SHIFT) |
            (64 << MYFIRST_SCFG_AMPLITUDE_SHIFT));
        CSR_WRITE_4(sc, MYFIRST_REG_DELAY_MS, 500);
        CSR_WRITE_4(sc, MYFIRST_REG_FAULT_MASK, 0);
        CSR_WRITE_4(sc, MYFIRST_REG_FAULT_PROB, 0);
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, 0);
        MYFIRST_UNLOCK(sc);

        sc->sim = sim;
        return (0);
}
```

The function allocates the sim structure, initialises its callouts with the driver's mutex (so the callbacks run with `sc->mtx` held automatically), picks a baseline sensor value, writes the default configuration to the simulation registers under the lock, and stores the pointer in the softc.

The callouts are not started yet. The simulation is ready but idle. A separate function, `myfirst_sim_enable`, starts the callouts; a user can toggle the simulation on and off through a sysctl. This separation is useful for debugging and for tests that need a known-quiet starting state.

### Enabling and Disabling the Simulation

```c
void
myfirst_sim_enable(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t config, interval_ms;

        MYFIRST_LOCK_ASSERT(sc);

        if (sim->running)
                return;

        sim->running = true;

        config = CSR_READ_4(sc, MYFIRST_REG_SENSOR_CONFIG);
        interval_ms = (config & MYFIRST_SCFG_INTERVAL_MASK) >>
            MYFIRST_SCFG_INTERVAL_SHIFT;
        if (interval_ms == 0)
                interval_ms = 100;

        callout_reset_sbt(&sim->sensor_callout,
            interval_ms * SBT_1MS, 0,
            myfirst_sim_sensor_cb, sc, 0);
}

void
myfirst_sim_disable(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        sim->running = false;

        callout_stop(&sim->sensor_callout);
        callout_stop(&sim->command_callout);
}
```

`myfirst_sim_enable` sets `running` to true and schedules the first sensor callout. `myfirst_sim_disable` clears the flag and stops both callouts. `callout_stop` does not wait for any in-flight callback to finish; it just cancels any pending reschedule. The in-flight callback will notice `running == false` on its next invocation and exit.

Note that neither `callout_stop` nor `callout_reset_sbt` releases the mutex. Both are safe to call with the mutex held. This is why `callout_init_mtx` was used: the mutex is threaded through the callout subsystem so the callbacks always run with it held, and the control calls never fight the lock.

Also note that `myfirst_sim_disable` does not drain the callouts. Draining must happen in detach, not in disable. The reason is that `callout_drain` sleeps, and sleeping with a mutex held is illegal. Drain happens in the detach path, after the mutex has been released.

### Wiring the Simulation into Attach and Detach

In `myfirst_attach` (the main `myfirst.c` file), add the simulation attach call after the hardware attach:

```c
int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc;
        int error;

        sc = device_get_softc(dev);

        /* ... existing Chapter 11-15 init ... */

        /* Chapter 16: hardware layer. */
        error = myfirst_hw_attach(sc);
        if (error != 0)
                goto fail_hw;

        /* Chapter 17: simulation backend. */
        error = myfirst_sim_attach(sc);
        if (error != 0)
                goto fail_sim;

        /* ... existing sysctl and cdev setup ... */

        myfirst_hw_add_sysctls(sc);
        myfirst_sim_add_sysctls(sc);

        /* Enable the simulation by default. */
        MYFIRST_LOCK(sc);
        myfirst_sim_enable(sc);
        MYFIRST_UNLOCK(sc);

        return (0);

fail_sim:
        myfirst_hw_detach(sc);
fail_hw:
        /* ... existing unwind ... */
        return (error);
}
```

The simulation is attached after the hardware layer and before the sysctl registration. The simulation is enabled at the end of attach, once everything else is ready. If any step fails, the unwind path runs in reverse order: disable what was enabled, detach what was attached, free what was allocated.

In `myfirst_detach`, the simulation detach happens early, before the hardware detach:

```c
int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* ... existing cdev destroy, wait for outstanding fds ... */

        /* Chapter 17: stop the simulation, drain its callouts. */
        MYFIRST_LOCK(sc);
        myfirst_sim_disable(sc);
        MYFIRST_UNLOCK(sc);
        myfirst_sim_detach(sc);

        /* Chapter 16: detach the hardware layer. */
        myfirst_hw_detach(sc);

        /* ... existing synchronization primitive teardown ... */

        return (0);
}
```

Note the ordering. Disable happens with the lock held; detach happens after the lock is released. Detach is where the draining happens:

```c
void
myfirst_sim_detach(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim;

        if (sc->sim == NULL)
                return;

        sim = sc->sim;
        sc->sim = NULL;

        callout_drain(&sim->sensor_callout);
        callout_drain(&sim->command_callout);

        free(sim, M_MYFIRST);
}
```

`callout_drain` waits for any in-flight callback to finish. If the callback is running on another CPU when `callout_drain` is called, `callout_drain` blocks until the callback completes and returns. After `callout_drain` returns, no callback will ever run for that callout again, so freeing the simulation state is safe.

The pattern is the same one Chapter 13 taught for callouts in general. The only subtlety is that the mutex must be released before calling `callout_drain`; if the callback is waiting to acquire the mutex when we call drain, and we are holding the mutex, we deadlock. The unlock-before-drain ordering is essential.

### A First Test: The Sensor Updates Visibly

Build and load the driver. Then:

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4096
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4128
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4144
# sysctl dev.myfirst.0.reg_sensor
dev.myfirst.0.reg_sensor: 4160
```

The value is changing between reads. The baseline is `0x1000 = 4096`, and the oscillation goes up by `amplitude / 4 = 16` per tick, so the values are 4096, 4112, 4128, 4144, 4160, 4144, 4128, 4112, 4096, ... Four values up, four values down, back to the baseline.

If you read fast enough, you may see the same value twice (the callout fires every 100 ms; a sysctl that takes less time to dispatch than 100 ms may see the same state). Slow down and read about once per second, and you will see the full oscillation.

Try changing the config:

```text
# sysctl dev.myfirst.0.reg_sensor_config=0x01000040
```

This sets interval to `0x0100 = 256` ms and amplitude to `0x0040 = 64`. The sensor now updates every 256 ms. You can see the change in the access log:

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sleep 2
# sysctl dev.myfirst.0.access_log_enabled=0
# sysctl dev.myfirst.0.access_log
```

The log should show roughly 8 writes to `SENSOR` over the 2-second window (2000 ms / 256 ms ~= 7.8). Before the config change, the rate would have been 20 writes in 2 seconds.

This is what the simulation is producing. The driver does nothing during this time; the simulation runs on its own, driven by the sensor callout.

### A Second Callout: The Command Completion

The sensor callout is periodic. The command callout is one-shot: it fires once, when a command completes, and does nothing until the next command is issued.

```c
static void
myfirst_sim_command_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t status;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running || !sim->command_pending)
                return;

        /* Complete the command: copy pending data to DATA_OUT,
         * set STATUS.DATA_AV, clear STATUS.BUSY, increment OP_COUNTER. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);

        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~MYFIRST_STATUS_BUSY;
        status |= MYFIRST_STATUS_DATA_AV;
        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

        sim->op_counter++;
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

        sim->command_pending = false;
}
```

The callback does four things. It copies the pending data to `DATA_OUT` (the simulation's "result"). It updates `STATUS`: `BUSY` clears, `DATA_AV` sets. It increments `OP_COUNTER`. And it clears the `command_pending` flag.

The command is initiated by `myfirst_sim_start_command`, called from the driver when it writes `CTRL.GO`:

```c
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t data_in, delay_ms;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        if (sim->command_pending) {
                /* Command already in flight. Real devices might reject
                 * this, set an error bit, or queue the command. For the
                 * simulation, the simplest behaviour is to treat it as
                 * a no-op. The driver should not do this. */
                device_printf(sc->dev,
                    "sim: overlapping command; ignored\n");
                return;
        }

        /* Snapshot DATA_IN now; the callout fires later. */
        data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
        sim->pending_data = data_in;

        /* Set STATUS.BUSY, clear STATUS.DATA_AV. */
        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                status |= MYFIRST_STATUS_BUSY;
                status &= ~MYFIRST_STATUS_DATA_AV;
                CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
        }

        /* Read the configured delay. */
        delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
        if (delay_ms == 0) {
                /* Zero delay: complete on the next tick. */
                delay_ms = 1;
        }

        sim->command_pending = true;

        callout_reset_sbt(&sim->command_callout,
            delay_ms * SBT_1MS, 0,
            myfirst_sim_command_cb, sc, 0);
}
```

The function snapshots `DATA_IN` into the simulation state, sets `STATUS.BUSY`, clears `STATUS.DATA_AV`, reads the configured `DELAY_MS`, and schedules the command callout. The command callout fires `delay_ms` milliseconds later and finishes the command.

A subtle point: why snapshot `DATA_IN` instead of reading it in the command callout? Because the driver might write `DATA_IN` again before the callout fires. The simulation should use the value that was in `DATA_IN` when `CTRL.GO` was written, not whatever happens to be there when the callout runs. Snapshotting at start time is the correct behaviour for most real devices too: the device reads the command register when the command is issued and does not re-read it during execution.

### Intercepting `CTRL.GO`

The `myfirst_sim_start_command` function is called when the driver writes `CTRL.GO`. The write intercept lives in the existing `myfirst_ctrl_update` helper (introduced in Chapter 16):

```c
void
myfirst_ctrl_update(struct myfirst_softc *sc, uint32_t old, uint32_t new)
{
        /* Chapter 16 behaviour: log ENABLE transitions. */
        if ((old & MYFIRST_CTRL_ENABLE) != (new & MYFIRST_CTRL_ENABLE)) {
                device_printf(sc->dev, "CTRL.ENABLE now %s\n",
                    (new & MYFIRST_CTRL_ENABLE) ? "on" : "off");
        }

        /* Chapter 17: if GO was set, start a command in the simulation. */
        if (!(old & MYFIRST_CTRL_GO) && (new & MYFIRST_CTRL_GO)) {
                myfirst_sim_start_command(sc);
                /* Clear the GO bit; it is a one-shot trigger. */
                CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_GO, 0);
        }
}
```

The extension watches for the `GO` bit transitioning from 0 to 1. When it does, it calls `myfirst_sim_start_command` and immediately clears the `GO` bit, because `GO` is a one-shot trigger: it asserts, the command starts, the bit goes back to zero. This is a common pattern for "start" bits in real hardware; the hardware clears the bit automatically once the command has been accepted.

The self-clearing behaviour means the driver does not have to remember to clear the bit itself. It writes `CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO)` to start a command, and the simulation handles the rest.

### A Second Test: The Command Path

With the command-path code in place, the simulation now supports a command cycle. Try it:

```text
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1

# sysctl dev.myfirst.0.reg_ctrl_set_datain=0x12345678   # (new sysctl for DATA_IN)
# sysctl dev.myfirst.0.reg_ctrl_set=0x200                # bit 9 = CTRL.GO

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3    # BUSY | READY

# sleep 1
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 9    # READY | DATA_AV

# sysctl dev.myfirst.0.reg_data_out
dev.myfirst.0.reg_data_out: 305419896   # 0x12345678

# sysctl dev.myfirst.0.reg_op_counter
dev.myfirst.0.reg_op_counter: 1
```

Reading the status right after `CTRL.GO` shows `BUSY|READY`. Waiting for the 500 ms delay and reading again shows `READY|DATA_AV`. Reading `DATA_OUT` returns the value that was in `DATA_IN` when `CTRL.GO` was written. `OP_COUNTER` has been incremented by one.

A new sysctl `reg_ctrl_set_datain` was implied in the example; it is a trivial addition that mirrors `reg_ctrl_set` but writes `DATA_IN` instead of `CTRL`. The pattern is identical to the Chapter 16 `reg_ctrl_set`: an `SYSCTL_ADD_PROC` with a writeable handler. Add one sysctl per register the user may want to poke directly.

### Protecting Against the Overlapping Command

The check for `command_pending` in `myfirst_sim_start_command` prevents overlapping commands. If the driver writes `CTRL.GO` while a command is already in flight, the simulation ignores the second command and logs a warning. This is not realistic for every real device (some queue commands, some reject them with an error), but it is the simplest correct behaviour for a teaching simulation.

The driver, for its part, should not issue a command while one is in flight. The driver can poll `STATUS.BUSY` to see whether the device is ready to accept a new command, and wait until `BUSY` clears before writing `CTRL.GO`. Section 5 teaches this pattern in the driver.

### Sysctls for the Simulation Controls

The simulation registers are already exposed through the Chapter 16 register sysctls (`reg_sensor`, `reg_sensor_config`, `reg_delay_ms`, `reg_fault_mask`, `reg_fault_prob`, `reg_op_counter`). Chapter 17 adds a few top-level sysctls that are not register-mapped:

```c
void
myfirst_sim_add_sysctls(struct myfirst_softc *sc)
{
        struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
        struct sysctl_oid *tree = sc->sysctl_tree;

        SYSCTL_ADD_BOOL(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_running", CTLFLAG_RD,
            &sc->sim->running, 0,
            "Whether the simulation callouts are active");

        SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_sensor_baseline", CTLFLAG_RW,
            &sc->sim->sensor_baseline, 0,
            "Baseline value around which SENSOR oscillates");

        SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "sim_op_counter_mirror", CTLFLAG_RD,
            &sc->sim->op_counter, 0,
            "Mirror of the OP_COUNTER value (for observability)");

        /* Add writeable sysctls for the register-mapped fields. */
        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "reg_delay_ms_set",
            CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
            sc, MYFIRST_REG_DELAY_MS, myfirst_sysctl_reg_write,
            "IU", "Command delay in milliseconds (writeable)");

        SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "reg_sensor_config_set",
            CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
            sc, MYFIRST_REG_SENSOR_CONFIG, myfirst_sysctl_reg_write,
            "IU", "Sensor config: interval|amplitude (writeable)");
}
```

The top-level `sim_running`, `sim_sensor_baseline`, and `sim_op_counter_mirror` expose internal state that is not register-mapped. The rest add writeable sysctls for the Chapter 17 registers, using the existing `myfirst_sysctl_reg_write` handler from Chapter 16.

The addition keeps the user's view consistent: every interesting piece of state is visible through `sysctl dev.myfirst.0`, and the writeable sysctls follow the `_set` suffix pattern the driver already uses.

### What Stage 1 Accomplished

At the end of Section 3, the driver has:

- A new file `myfirst_sim.c` with the simulation backend.
- A new header `myfirst_sim.h` with the simulation API.
- A new simulation state structure pointed at by `sc->sim`.
- Two callouts: one for periodic sensor updates, one for one-shot command completion.
- An extension to `myfirst_ctrl_update` that intercepts `CTRL.GO`.
- Writeable sysctls for the new registers that should be configurable.

The version tag becomes `1.0-sim-stage1`. The driver still does everything Chapter 16 taught; it now also has two callouts that produce autonomous register behaviour.

Build, load, and test:

```text
# cd examples/part-04/ch17-simulating-hardware/stage1-backend
# make clean && make
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.sim_running
# sysctl dev.myfirst.0.reg_sensor
# sleep 1
# sysctl dev.myfirst.0.reg_sensor
# sysctl dev.myfirst.0.reg_op_counter
# kldunload myfirst
```

You should see `sim_running` is 1, the sensor value changes, and `op_counter` stays at zero until you issue a command. If `kldunload` returns cleanly, the detach path is correct.

### What Stage 1 Is Not

Stage 1 has the simulation running, but the driver's syscall paths do not yet know about the new behaviour. A `write(2)` to `/dev/myfirst0` still writes into the ring buffer, not into the simulated device. The bytes that go into the ring buffer never appear in `DATA_IN`, never cause a command, never appear in `DATA_OUT`. The simulation is ready; the driver is not yet using it for real.

Section 4 takes the next step: it connects the simulation to the driver's data path, so a user-space write causes a simulated command, and the command's result is what the user-space read sees. Until then, the simulation is a side show.

### Wrapping Up Section 3

A simulated device's behaviour comes from a small set of primitives. A callout for periodic updates, a callout for one-shot timed events, and a shared state structure that both callouts consult. Chapter 17's simulation uses three of these: a sensor callout that runs always, a command callout that runs per-command, and an internal `command_pending` flag that prevents overlapping commands.

The simulation lives in its own file, `myfirst_sim.c`, which depends on the Chapter 16 `myfirst_hw.h` for register definitions. The split keeps the simulation code isolated; a later chapter that replaces the simulation with real PCI-facing code can delete `myfirst_sim.c` and replace it with `myfirst_pci.c` without touching the driver.

Section 4 begins the real integration: the driver's data path starts sending and receiving through the simulation, and the simulation starts looking like a proper device from the driver's side.



## Section 4: Exposing the Fake Device to the Driver

Section 3 built a simulation backend that runs on callouts and writes to registers through the Chapter 16 accessors. Section 4 asks a different question: how does the driver see the simulation? From the driver's side, what is the access path? What does the code look like, and how does it differ from code that would talk to real hardware?

The short answer is that the access path does not change. The whole point of the Chapter 16 `bus_space(9)` abstraction is that the driver does not know, and does not care, whether the register block is real or simulated. Section 4 is deliberately a shorter section because the heavy lifting was done in Chapter 16. What remains is a careful look at how the abstraction survives the addition of dynamic behaviour, and at the few small hooks the driver needs to let the simulation drive its own behaviour.

### The Tag and the Handle, Still

Chapter 16 set up the simulation's `bus_space_tag_t` and `bus_space_handle_t` in `myfirst_hw_attach`:

```c
#if defined(__amd64__) || defined(__i386__)
        hw->regs_tag = X86_BUS_SPACE_MEM;
#else
#error "Chapter 16 simulation supports x86 only"
#endif
        hw->regs_handle = (bus_space_handle_t)(uintptr_t)hw->regs_buf;
```

That code does not change for Chapter 17. The simulation writes registers through `CSR_WRITE_4`, which expands to `myfirst_reg_write`, which expands to `bus_space_write_4(hw->regs_tag, hw->regs_handle, offset, value)`. The same tag; the same handle; the same accessor. The callouts that Section 3 introduced use the exact same API as the syscall path.

This is not an accident. The reason `bus_space` exists is precisely to hide the difference between real and simulated register access. A driver that uses `bus_space` correctly can be pointed at a real PCI BAR or at a `malloc(9)` allocation without any changes to the access code. The simulation exercises this property.

A useful exercise at this point: open `/usr/src/sys/dev/ale/if_ale.c` in one window and `myfirst_hw.c` in another. Compare the register access code. The ALE driver uses `CSR_READ_4(sc, ALE_REG_XYZ)`; your driver uses `CSR_READ_4(sc, MYFIRST_REG_XYZ)`. The expansion is identical. The only difference is the tag and handle: ALE gets them from `rman_get_bustag(sc->ale_res[0])` and `rman_get_bushandle(sc->ale_res[0])`; your driver gets them from the Chapter 16 simulation setup. Replacing the simulation with real hardware, which is what Chapter 18 will do, is a one-function change in `myfirst_hw_attach`.

### Why the Abstraction Matters More Now

In Chapter 16, the argument for using `bus_space` was forward-looking: "when you eventually switch to real hardware, the driver code will not have to change". In Chapter 17, the argument becomes concrete: the register block now changes on its own, and the driver has no way to know whether a change came from a simulation callout or from a real device's internal state machine. The abstraction is doing real work.

Consider a small example. A driver function that polls `STATUS.DATA_AV`:

```c
static int
myfirst_wait_for_data(struct myfirst_softc *sc, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (status & MYFIRST_STATUS_DATA_AV)
                        return (0);

                /* Release the lock, sleep briefly, reacquire. */
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfwait", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

This function reads `STATUS`, checks a bit, and either returns or sleeps for a millisecond and retries. It does not know whether the bit is being set by the Chapter 17 simulation's command callout, by a real device's internal state machine, or by something else. It just polls.

The `pause_sbt` call is worth examining. The function releases the driver lock before sleeping and reacquires it afterwards. Sleeping with a lock held would block every other context that needs the lock, including the simulation callouts; dropping the lock before sleeping lets the simulation run. `pause_sbt` takes a sleep identifier ("mfwait"), a signed binary time (1 ms here), a precision, and flags. The precision of zero means the kernel can coalesce this sleep with other sleeps, reducing timer interrupts.

Section 6 revisits the choice between `DELAY(9)`, `pause_sbt(9)`, and `callout_reset_sbt(9)` in more depth. The short version: polling a register a thousand times per second while sleeping between each poll is not the most efficient pattern, but it is correct, simple, and readable. Chapter 17's driver uses it because the protocol is small and the polling overhead does not matter. Real high-performance drivers use interrupts (Chapter 19) instead.

### The Command-Path Integration

The driver's current data path (`myfirst_write`, from Chapter 10) writes bytes into the ring buffer. Chapter 17 wants to route those bytes through the simulation: user writes a byte, the driver writes it into `DATA_IN`, sets `CTRL.GO`, waits for `STATUS.DATA_AV`, reads `DATA_OUT`, writes the result into the ring buffer. This is the full command cycle.

For Stage 2 of the Chapter 17 driver, we add the command-cycle code path. The write function becomes:

```c
static int
myfirst_write_cmd(struct myfirst_softc *sc, uint8_t byte)
{
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        /* Wait for the device to be ready for a new command. */
        error = myfirst_wait_for_ready(sc, 100);
        if (error != 0)
                return (error);

        /* Write the byte to DATA_IN. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)byte);

        /* Issue the command. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        /* Wait for the command to complete (STATUS.DATA_AV). */
        error = myfirst_wait_for_data(sc, 2000);
        if (error != 0)
                return (error);

        /* Check for errors. */
        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (status & MYFIRST_STATUS_ERROR) {
                        /* Clear the error latch. */
                        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                            MYFIRST_STATUS_ERROR, 0);
                        return (EIO);
                }
        }

        /* Read the result. DATA_OUT will hold the byte the simulation
         * echoed (or, if loopback is active, the same byte we wrote). */
        (void)CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);

        /* Clear DATA_AV so the next command can set it. */
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);

        return (0);
}
```

Six steps. Wait for ready, write `DATA_IN`, set `CTRL.GO`, wait for `DATA_AV`, check for errors, read `DATA_OUT`. Clear `DATA_AV` to be polite to the next iteration.

The `myfirst_wait_for_ready` helper polls `STATUS.BUSY`:

```c
static int
myfirst_wait_for_ready(struct myfirst_softc *sc, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (!(status & MYFIRST_STATUS_BUSY))
                        return (0);
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfrdy", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Same structure as `myfirst_wait_for_data`; only the bit being polled differs. Both helpers are reasonable candidates for a shared implementation:

```c
static int
myfirst_wait_for_bit(struct myfirst_softc *sc, uint32_t mask,
    uint32_t target, int timeout_ms)
{
        int i;

        MYFIRST_LOCK_ASSERT(sc);

        for (i = 0; i < timeout_ms; i++) {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if ((status & mask) == target)
                        return (0);
                MYFIRST_UNLOCK(sc);
                pause_sbt("mfwait", SBT_1MS, 0, 0);
                MYFIRST_LOCK(sc);
        }

        return (ETIMEDOUT);
}
```

Then `wait_for_ready` calls `wait_for_bit(sc, MYFIRST_STATUS_BUSY, 0, timeout_ms)` and `wait_for_data` calls `wait_for_bit(sc, MYFIRST_STATUS_DATA_AV, MYFIRST_STATUS_DATA_AV, timeout_ms)`. The abstraction is worth extracting once you have two users; Section 6 revisits this pattern.

### Where the Command Integrates with the Write Syscall

The existing Chapter 10 `myfirst_write` pushes bytes into the ring buffer. Chapter 17 does not replace that path; it augments it. For Stage 2, we add a per-byte command-cycle hook that sends each byte through the simulation in addition to pushing it into the ring.

```c
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t buf[64];
        size_t n;
        int error;

        /* ... existing validation ... */

        while (uio->uio_resid > 0) {
                n = MIN(uio->uio_resid, sizeof(buf));
                error = uiomove(buf, n, uio);
                if (error != 0)
                        return (error);

                MYFIRST_LOCK(sc);
                for (size_t i = 0; i < n; i++) {
                        /* Send each byte through the simulated device. */
                        error = myfirst_write_cmd(sc, buf[i]);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }

                        /* Existing Chapter 10 path: push into ring. */
                        error = cbuf_put(sc->cbuf, buf[i]);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }
                }
                MYFIRST_UNLOCK(sc);
        }

        return (0);
}
```

The loop is one-byte-at-a-time for clarity. Each byte goes through the command cycle, then into the ring. A real driver would usually pipeline this (write several bytes to a FIFO, issue a single command, read several bytes back), but Chapter 17's teaching point is about single commands; pipelining is a refinement for later.

Note the locking. The outer lock covers the entire batch (up to 64 bytes). Each `myfirst_write_cmd` call releases and reacquires the lock internally when it sleeps. Other contexts that need the lock (notably the simulation callouts) get a chance to run between commands.

### Stage 2 Test

Build, load, and test:

```text
# kldload ./myfirst.ko
# echo -n "hi" > /dev/myfirst0
# sysctl dev.myfirst.0.reg_op_counter
dev.myfirst.0.reg_op_counter: 2

# dd if=/dev/myfirst0 bs=1 count=2 2>/dev/null
hi
```

Two bytes written; `OP_COUNTER` incremented by two. The reads return the same two bytes because the simulation echoes `DATA_IN` to `DATA_OUT`. The test validates the end-to-end flow: user space writes, driver commands, simulation processes, driver reads, user space receives.

The whole cycle takes about 1 second (500 ms per byte, two bytes). This is slow, but it is realistic: a real device with similar latency (for example, a SPI-attached sensor that takes 500 ms to produce a reading) would feel exactly like this. Speeding it up means reducing `DELAY_MS` (try `sysctl dev.myfirst.0.reg_delay_ms_set=10` for 10 ms per command).

### What Does Not Change

A few properties of the Chapter 16 driver that do not change in Stage 2 are worth enumerating.

The access log continues to work. Every register read and write the driver performs shows up in the log. The simulation's own register writes also show up (because they go through the same accessors). The log is dense, which is good: you can see the simulation's heartbeat alongside the driver's activity.

The sysctl interface continues to work. Every register is readable; every writeable register is writeable. The Chapter 16 conveniences (read-every-register, write-specific-registers) are unchanged.

The locking discipline continues to work. Every register access happens under `sc->mtx`; the simulation callouts were designed to run with `sc->mtx` held (via `callout_init_mtx`), so there are no lock-order inversions to worry about.

The detach path continues to work. The simulation detach was added to the existing detach ordering, and the draining of the simulation callouts happens before the hardware region is freed.

### What Still Needs Attention

A few loose ends that Sections 5 and 6 will tighten.

The timeouts in `myfirst_wait_for_ready` and `myfirst_wait_for_data` are hardcoded (100 ms and 2000 ms respectively). A configurable timeout (perhaps exposed through a sysctl) would be better; Section 5 adds one.

The `pause_sbt` sleep of 1 ms is a reasonable default but not optimal for every situation. On a fast simulation (e.g., `DELAY_MS=0`), polling every 1 ms is wasteful; on a slow simulation (e.g., `DELAY_MS=5000`), polling every 1 ms is overkill. Section 6 discusses better strategies.

The command-cycle write path does not interact with the simulation's fault injection. If the simulation is configured to fail a command (Section 7's work), the driver needs to recognise and recover from the failure. Section 5 adds the basic error-recovery; Section 7 adds the fault-injection infrastructure.

The driver's read path is not yet integrated. Reads currently pull from the ring buffer, which contains whatever was pushed there by write commands. A more realistic pattern would have reads also cause commands (a "please give me another byte" command), but that is a Section 5 topic.

### Wrapping Up Section 4

Exposing the simulated device to the driver is not a new mechanism in Chapter 17. The Chapter 16 `bus_space(9)` abstraction already handles this; the simulation's writes and the driver's reads go through the same tag and handle, and the driver neither knows nor cares that the register block is simulated. What Stage 2 adds is the command-cycle code path: the driver writes `DATA_IN`, sets `CTRL.GO`, waits for `DATA_AV`, reads `DATA_OUT`. Two helper functions (`myfirst_wait_for_ready` and `myfirst_wait_for_data`) make the polling explicit; one umbrella function (`myfirst_wait_for_bit`) factors the shared structure. The driver now exercises the simulation in a realistic command-and-response pattern.

Section 5 deepens the testing story. It adds error-recovery, configurable timeouts, a clearer separation between the read and the write paths, and the first load tests that hit the simulation with enough traffic to expose race conditions.



## Section 5: Testing Device Behavior from the Driver

The command-cycle code from Section 4 works for a single byte. It times out correctly when the simulation is slow; it returns a data value when the simulation responds; it reads `STATUS` at the appropriate points. What it has not yet experienced is volume, diversity, or concurrency. A driver that passes a one-byte test may still fail under load, under concurrent writers, or when the simulation is configured with unusual parameters.

Section 5 is about stress-testing the driver's interaction with the simulated device. It adds configurable timeouts, a more robust error-recovery path, a read-path integration that lets the simulation drive reads as well as writes, and a set of load tests that exercise the driver across its full behavioural surface. By the end of Section 5, the driver is at Stage 2 of Chapter 17 and has survived a meaningful stress run.

### Configurable Timeouts

Hardcoded timeouts are brittle. 100 ms is enough for the default `DELAY_MS` of 500, but what if the user wants to test a slow configuration? Bumping `DELAY_MS` to 2000 would mean every command times out with the 100 ms limit, which is a false failure mode. Make the timeouts configurable.

Add two fields to the softc (or to a suitable sub-struct):

```c
struct myfirst_softc {
        /* ... existing fields ... */
        int      cmd_timeout_ms;        /* max wait for command completion  */
        int      rdy_timeout_ms;        /* max wait for device ready         */
};
```

Initialise them in attach:

```c
sc->cmd_timeout_ms = 2000;              /* 2 s default                      */
sc->rdy_timeout_ms = 100;               /* 100 ms default                   */
```

Expose them through sysctl:

```c
SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "cmd_timeout_ms", CTLFLAG_RW, &sc->cmd_timeout_ms, 0,
    "Command completion timeout in milliseconds");

SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
    "rdy_timeout_ms", CTLFLAG_RW, &sc->rdy_timeout_ms, 0,
    "Device-ready polling timeout in milliseconds");
```

And change `myfirst_write_cmd` to use them:

```c
error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
/* ... */
error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
```

The test suite can now adjust the timeouts to match the test's expected latency. A fast-simulation test sets them low; a slow-simulation test sets them high. A pathological test sets them very low and expects timeout errors.

### Read-Path Integration

The Chapter 10 `myfirst_read` pulls from the ring buffer. The ring buffer contains whatever previous writes pushed in. Chapter 17 can extend this pattern: when the ring is empty and the reader is willing to wait, issue a "generate a byte" command to the simulated device and push the result into the ring.

This is not how every driver works. A real device has data-production semantics that are device-specific; a network card produces bytes as packets arrive, not on demand. For a simulated sensor-like device, a "sample" command that produces one reading per invocation is a natural pattern. Our driver adopts it.

Add a `myfirst_sample_cmd` function that issues a command and pushes the result into the ring:

```c
static int
myfirst_sample_cmd(struct myfirst_softc *sc)
{
        uint32_t data_out;
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
        if (error != 0)
                return (error);

        /* DATA_IN does not matter for a sample; write a marker. */
        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, 0xCAFE);

        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
        if (error != 0)
                return (error);

        data_out = CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);

        /* Push one byte of the sample into the ring. */
        error = cbuf_put(sc->cbuf, (uint8_t)(data_out & 0xFF));
        return (error);
}
```

The read path can now call `myfirst_sample_cmd` when the ring is empty. Wire it into `myfirst_read`:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t byte;
        int error;

        MYFIRST_LOCK(sc);
        while (uio->uio_resid > 0) {
                while (cbuf_empty(sc->cbuf)) {
                        if (ioflag & O_NONBLOCK) {
                                MYFIRST_UNLOCK(sc);
                                return (EWOULDBLOCK);
                        }

                        /* Chapter 17: trigger a sample when ring is empty. */
                        error = myfirst_sample_cmd(sc);
                        if (error != 0) {
                                MYFIRST_UNLOCK(sc);
                                return (error);
                        }
                }
                error = cbuf_get(sc->cbuf, &byte);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
                MYFIRST_UNLOCK(sc);
                error = uiomove(&byte, 1, uio);
                MYFIRST_LOCK(sc);
                if (error != 0) {
                        MYFIRST_UNLOCK(sc);
                        return (error);
                }
        }
        MYFIRST_UNLOCK(sc);
        return (0);
}
```

Now a read from `/dev/myfirst0` triggers a simulated command if the ring is empty, effectively pulling one byte per command from the simulation. Combined with the write path from Section 4 (push-style), the driver now exercises both directions.

### A First Load Test

With both paths integrated, the driver is ready for load testing. A simple test script:

```sh
#!/bin/sh
# cmd_load.sh: exercise the command path under load.

set -e

# Fast simulation for load testing.
sysctl dev.myfirst.0.reg_delay_ms_set=10

# Spawn 4 writers and 4 readers in parallel.
for i in 1 2 3 4; do
        (for j in $(seq 1 100); do echo -n "X" > /dev/myfirst0; done) &
done
for i in 1 2 3 4; do
        (dd if=/dev/myfirst0 of=/dev/null bs=1 count=100 2>/dev/null) &
done
wait

# Show the resulting counter.
sysctl dev.myfirst.0.reg_op_counter

# Reset to default.
sysctl dev.myfirst.0.reg_delay_ms_set=500
```

With `DELAY_MS=10` and 4 writers each sending 100 bytes plus 4 readers each receiving 100 bytes, the test should complete in about 10 seconds (800 commands at 10 ms each, serialised through the simulation). `OP_COUNTER` should reach at least 800.

Two things are worth noticing about this test.

First, the commands are serialised. The simulation rejects overlapping commands (Section 3's `command_pending` check). The driver waits for `STATUS.BUSY` to clear before issuing the next command. With multiple user-space processes contending for the driver, the lock on `sc->mtx` serialises them. The effective rate is one command per `DELAY_MS` milliseconds, regardless of how many writers are running.

Second, the driver did not change to accommodate concurrency. The Chapter 11 mutex, the Chapter 14 tasks, the Chapter 16 accessors, and the Section 4 command-cycle code all compose naturally. The discipline Part 3 built up pays off here: no additional synchronisation is needed to add simulated hardware to a driver that already coordinates itself properly.

### Error-Recovery Refinement

The command-cycle code in Section 4 clears `STATUS.ERROR` and returns `EIO`. Section 5 refines this to distinguish several error cases.

```c
static int
myfirst_write_cmd(struct myfirst_softc *sc, uint8_t byte)
{
        uint32_t status;
        int error;

        MYFIRST_LOCK_ASSERT(sc);

        error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
        if (error != 0) {
                sc->stats.cmd_rdy_timeouts++;
                return (error);
        }

        CSR_WRITE_4(sc, MYFIRST_REG_DATA_IN, (uint32_t)byte);
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_GO);

        error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
        if (error != 0) {
                sc->stats.cmd_data_timeouts++;
                /* Clear any partial state in the simulation. */
                myfirst_recover_from_stuck(sc);
                return (error);
        }

        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        if (status & MYFIRST_STATUS_ERROR) {
                CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                    MYFIRST_STATUS_ERROR, 0);
                CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
                    MYFIRST_STATUS_DATA_AV, 0);
                sc->stats.cmd_errors++;
                return (EIO);
        }

        (void)CSR_READ_4(sc, MYFIRST_REG_DATA_OUT);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS, MYFIRST_STATUS_DATA_AV, 0);
        sc->stats.cmd_successes++;
        return (0);
}
```

Three changes from Section 4.

First, per-error-type counters. `sc->stats` is a structure of `uint64_t` counters that the driver updates on each outcome. `cmd_successes`, `cmd_rdy_timeouts`, `cmd_data_timeouts`, and `cmd_errors` each get their own counter. A sysctl exposes them. Under load, the counters become the primary diagnostic: "the driver issued 800 commands, 5 timed out, 0 had errors, 795 succeeded" is a much more useful summary than "the driver ran".

Second, `myfirst_recover_from_stuck`. When a command times out, the simulation may still be in the middle of processing. Clearing stale state matters:

```c
static void
myfirst_recover_from_stuck(struct myfirst_softc *sc)
{
        uint32_t status;

        MYFIRST_LOCK_ASSERT(sc);

        /* Clear any pending command flag in the simulation. The next
         * CTRL.GO will be accepted regardless of any stale state. */
        if (sc->sim != NULL) {
                sc->sim->command_pending = false;
                callout_stop(&sc->sim->command_callout);
        }

        /* Force-clear BUSY and DATA_AV. */
        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~(MYFIRST_STATUS_BUSY | MYFIRST_STATUS_DATA_AV);
        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
}
```

Note that `myfirst_recover_from_stuck` reaches into `sc->sim->command_pending` directly. This is a legitimate simulation-only path: on real hardware, there would be no such variable; instead, the driver would issue a reset command to the device. The function is a simulation-specific recovery path, and a comment in the code should say so. When Chapter 18 replaces the simulation with real hardware, this function changes entirely.

Third, status clearing on error. The error path clears both `ERROR` and `DATA_AV`. Leaving either one set would cause the next command's `wait_for_data` to return immediately (if `DATA_AV` is still set) or observe an unexpected error (if `ERROR` is still set). The defensive cleanup ensures the simulation is in a clean state for the next command.

### Stats Infrastructure

The `sc->stats` structure is worth defining properly. Place it in `myfirst.h`:

```c
struct myfirst_stats {
        uint64_t        cmd_successes;
        uint64_t        cmd_rdy_timeouts;
        uint64_t        cmd_data_timeouts;
        uint64_t        cmd_errors;
        uint64_t        cmd_rejected;
        uint64_t        cmd_recoveries;
        uint64_t        samples_taken;
        uint64_t        fault_injected;
};
```

Add it to the softc:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_stats stats;
};
```

Expose each counter through sysctl:

```c
static void
myfirst_add_stats_sysctls(struct myfirst_softc *sc)
{
        struct sysctl_ctx_list *ctx = &sc->sysctl_ctx;
        struct sysctl_oid *tree = sc->sysctl_tree;

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_successes", CTLFLAG_RD,
            &sc->stats.cmd_successes, 0,
            "Successfully completed commands");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_rdy_timeouts", CTLFLAG_RD,
            &sc->stats.cmd_rdy_timeouts, 0,
            "Commands that timed out waiting for READY");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_data_timeouts", CTLFLAG_RD,
            &sc->stats.cmd_data_timeouts, 0,
            "Commands that timed out waiting for DATA_AV");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_errors", CTLFLAG_RD,
            &sc->stats.cmd_errors, 0,
            "Commands that completed with STATUS.ERROR set");

        SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
            "cmd_recoveries", CTLFLAG_RD,
            &sc->stats.cmd_recoveries, 0,
            "Recovery operations triggered");

        /* ... add the remaining counters ... */
}
```

Under load, `sysctl dev.myfirst.0 | grep -E 'cmd_|samples_|fault_'` gives a one-screen summary of the driver's command-path behaviour. After a test run, diffing before and after is how you answer "did the driver behave correctly?".

### Observing the Driver Under Load

A useful debugging pattern: watch the stats while a load test is running. In one terminal:

```text
# sysctl dev.myfirst.0 | grep cmd_ > before.txt
```

In another terminal, start the load test. Wait for it to finish. Then:

```text
# sysctl dev.myfirst.0 | grep cmd_ > after.txt
# diff before.txt after.txt
```

The diff shows exactly how many of each event occurred during the test window. Ideal runs show many successes and zero timeouts or errors. Runs with faults injected (Section 7) show nonzero counts for the expected error types; unexpected counts indicate a bug.

For continuous observation, use `watch`:

```text
# watch -n 1 'sysctl dev.myfirst.0 | grep cmd_'
```

Every second, the current counters print. As the load test runs, the successes counter climbs, while the timeouts and errors counters (ideally) stay flat.

### Hands-On Behaviour Test

A concrete test for Section 5. Set up:

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.reg_delay_ms_set=50    # fast commands
# sysctl dev.myfirst.0.access_log_enabled=1
```

Run:

```text
# echo -n "Hello, World!" > /dev/myfirst0
# dd if=/dev/myfirst0 bs=1 count=13 of=/dev/null 2>/dev/null
# sysctl dev.myfirst.0 | grep -E 'cmd_|op_counter|samples_'
```

Expected output:

```text
dev.myfirst.0.cmd_successes: 26
dev.myfirst.0.cmd_rdy_timeouts: 0
dev.myfirst.0.cmd_data_timeouts: 0
dev.myfirst.0.cmd_errors: 0
dev.myfirst.0.samples_taken: 13
dev.myfirst.0.reg_op_counter: 26
```

Thirteen writes (one per byte of "Hello, World!"), thirteen reads (one per byte back out). Each write and each read is a simulation command, for a total of 26. All successful. All within the latency budget.

Now inspect the access log:

```text
# sysctl dev.myfirst.0.access_log | head -20
```

The log shows the command cycle in detail. For each write command:

1. Read STATUS (polling for BUSY to clear)
2. Write DATA_IN (the byte)
3. Read/Write CTRL (setting GO)
4. Read STATUS (polling for DATA_AV to set)
5. Read DATA_OUT (the result)
6. Read/Write STATUS (clearing DATA_AV)

For each sample command, the same sequence plus a `cbuf_put`. The log is dense but readable, and it is where you would look if the driver misbehaved.

### What Stage 2 Accomplished

Stage 2 integrates the simulation with the driver's data path. Both the write and the read paths now exercise simulated commands. Configurable timeouts let tests target specific behaviour. Per-error-type stats counters expose the driver's interaction with the simulation. A recovery path cleans up stuck state when a command times out. Load tests exercise the driver at volume and confirm the composition of Part 3 synchronisation with Chapter 17 simulation.

The version tag becomes `1.0-sim-stage2`. The driver is now interacting with a dynamic register block in a way that approaches real-hardware behaviour.

### Wrapping Up Section 5

Testing the driver against the simulation is not a separate activity; it is a natural extension of the driver's command path. Add configurable timeouts, integrate simulated commands into both read and write, track per-outcome statistics, and run enough load to expose the common failure modes.

The hands-on test validates the round-trip: user-space input goes through the driver, through the simulation, and back to user space intact. The counters confirm no errors. The access log shows every register touch in order.

Section 6 deepens the timing discussion. The 1 ms `pause_sbt` in `myfirst_wait_for_bit` is a reasonable default, but not the only choice. `DELAY(9)` is better for very short waits; `callout_reset_sbt` is better for fire-and-forget scheduling. Understanding when each is appropriate is the topic of Section 6.



## Section 6: Adding Timing and Delay

Timing is where real hardware gets interesting. A device that processes a command in 5 microseconds needs a very different driver pattern than one that processes a command in 500 milliseconds. A driver that uses the wrong timing primitive for the wrong time scale burns CPU on short waits or accumulates latency on long ones, and in the worst case it deadlocks under contention.

FreeBSD offers three primary timing primitives for driver code: `DELAY(9)`, `pause_sbt(9)`, and `callout_reset_sbt(9)`. Each is appropriate at a specific time scale, each has a specific cost, and each has a specific correctness requirement. Section 6 teaches them in order and shows how to pick the right one for each situation in the Chapter 17 simulation.

### The Three Timing Primitives

The primitives differ along three axes: whether they busy-wait or sleep, whether they are cancelable, and what they cost.

**`DELAY(n)`** busy-waits for `n` microseconds. It does not sleep, does not yield the CPU, and does not release any locks. It is safe to call from any context, including from a spin mutex, an interrupt filter, or a callout. Its cost is the CPU time it consumes: 100 microseconds of `DELAY` is 100 microseconds the CPU cannot do anything else. Its precision is usually quite good (single-digit microseconds), limited by the CPU's time-stamp counter resolution. It is the right choice for very short waits where sleeping would be impractical.

**`pause_sbt(wmesg, sbt, pr, flags)`** puts the calling thread to sleep for `sbt` signed-binary-time units. While the thread is asleep, the CPU is free for other work, and any locks the thread does not hold are free for other contexts. If the thread has a lock held, `pause_sbt` does not release it; it is the caller's job to drop locks that should not be held during the sleep. The precision is on the order of the kernel's timer tick (1 ms on a typical system). It is the right choice for short-to-medium waits where the thread has nothing else to do. `pause_sbt` is not cancelable through any mechanism shorter than the sleep itself; once a thread has called it, it will sleep for at least the requested time (barring exceptional events).

**`callout_reset_sbt(c, sbt, pr, fn, arg, flags)`** schedules a callback to run `sbt` signed-binary-time units in the future. The calling thread does not wait; it returns immediately. The callback runs on a callout thread when the time expires. The callout can be cancelled with `callout_stop` or `callout_drain`. The precision is similar to `pause_sbt`. It is the right choice for fire-and-forget scheduling, for timeouts that may be cancelled early, and for periodic work.

The three primitives compose. A driver might use `DELAY(10)` to wait 10 microseconds for a register bit to settle, `pause_sbt(..., SBT_1MS * 50, ...)` to sleep 50 ms waiting for a command to complete, and `callout_reset_sbt(..., SBT_1S * 5, ...)` to schedule a watchdog 5 seconds in the future. Each choice reflects the time scale and the context.

### A Time-Scale Decision Table

A quick reference for choosing among the three primitives:

| Wait Duration  | Context                     | Preferred Primitive           |
|----------------|-----------------------------|-------------------------------|
| < 10 us        | Any                         | `DELAY(9)`                    |
| 10-100 us      | Non-interrupt, non-spin     | `DELAY(9)` or `pause_sbt(9)`  |
| 100 us - 10 ms | Non-interrupt, non-spin     | `pause_sbt(9)`                |
| > 10 ms        | Non-interrupt, non-spin     | `pause_sbt(9)` or callout     |
| Fire-and-forget| Any (callout state machine) | `callout_reset_sbt(9)`        |
| Interrupt/spin | Any duration                | `DELAY(9)` only               |

The table is a starting point, not a rigid rule. The real question is: can the caller afford to sleep? If yes, pause or callout. If no (spin lock held, interrupt handler, filter handler, critical section), DELAY.

### Where the Chapter 17 Driver Already Uses Each

Section 4's polling loop uses `pause_sbt` with 1 ms delays. That is appropriate for the time scale: the simulation's default `DELAY_MS` is 500, so we expect the driver to sleep many times before the command completes. Sleeping lets other threads use the CPU while we wait. `DELAY(1000)` would also work but would waste CPU; the driver currently sits at about 500 polls per command with no useful CPU work between them, and `DELAY` would spin all 500 times.

Section 3's simulation callouts use `callout_reset_sbt` with variable intervals. That is appropriate because the simulation needs to do work in the future without any thread actively waiting. `pause_sbt` would require a dedicated kernel thread for each callout, which would be wasteful. `DELAY` would require the thread to stay in kernel mode for the full interval, which is unacceptable.

There is no `DELAY` in the driver yet. Section 6 introduces it where appropriate.

### A Good Use of `DELAY`

Consider the recovery path from Section 5. After a timeout, the driver calls `myfirst_recover_from_stuck`, which clears some state and returns. The code works, but there is a subtle race: the simulation's command callout might fire between the timeout detection and the recovery call, changing `STATUS` in unexpected ways. A small fix: use `DELAY(10)` to wait 10 microseconds for any in-flight callout to drain before clearing the state.

Except this is wrong. `DELAY(10)` does nothing useful here. The callout cannot run while we hold `sc->mtx` (because it was registered with `callout_init_mtx`), so there is no race. Adding a `DELAY` here would be cargo-culting: adding a delay because something might go wrong, without a clear model of what the delay is preventing.

A better example. Some real hardware requires a small delay between two register writes to let the device's internal logic settle. A datasheet might say: "After writing `CTRL.RESET`, wait at least 5 microseconds before writing any other register." The Chapter 17 simulation's `CTRL.RESET` bit is not actually implemented, but we can imagine it. If it were, the reset code would use `DELAY(5)`:

```c
static void
myfirst_reset_device(struct myfirst_softc *sc)
{
        MYFIRST_LOCK_ASSERT(sc);

        /* Assert reset. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, 0, MYFIRST_CTRL_RESET);

        /* Datasheet: wait 5 us for internal state to clear. */
        DELAY(5);

        /* Deassert reset. */
        CSR_UPDATE_4(sc, MYFIRST_REG_CTRL, MYFIRST_CTRL_RESET, 0);
}
```

Five microseconds is too short for `pause_sbt` to be efficient (the kernel's timer resolution is typically 1 ms, so `pause_sbt` for 5 us would actually sleep for the next timer tick, wasting most of a millisecond). `DELAY(5)` is the right primitive.

### A Good Use of `pause_sbt` (With the Lock Dropped)

The polling loop from Section 4:

```c
for (i = 0; i < timeout_ms; i++) {
        uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        if ((status & mask) == target)
                return (0);
        MYFIRST_UNLOCK(sc);
        pause_sbt("mfwait", SBT_1MS, 0, 0);
        MYFIRST_LOCK(sc);
}
```

The lock is dropped before `pause_sbt` and reacquired after. This pattern is essential and deserves careful attention.

If the lock were held across `pause_sbt`, the simulation callout (which needs the lock) would be blocked for the duration of the sleep. Since the callout is the thing the driver is waiting on, this would deadlock. Dropping the lock lets the callout run, lets the simulation update `STATUS`, and lets the next iteration of the loop observe the new value.

The correctness requirement is that nothing the driver cares about can change in an unrecoverable way while the lock is dropped. In our case, the driver's loop variables are local (`i`, `status`, `mask`, `target`); they do not live in memory that other contexts touch. The state that other contexts touch (the register block) is exactly what we want them to update. So dropping the lock is safe.

A variation on this pattern uses a sleepable condition variable instead of polling:

```c
MYFIRST_LOCK_ASSERT(sc);

while ((CSR_READ_4(sc, MYFIRST_REG_STATUS) & mask) != target) {
        int error = cv_timedwait_sbt(&sc->data_cv, &sc->mtx,
            SBT_1MS * timeout_ms, 0, 0);
        if (error == EWOULDBLOCK)
                return (ETIMEDOUT);
}
return (0);
```

The condition variable waits until either the target bit changes (signalled by some other context) or the timeout expires. `cv_timedwait_sbt` drops the mutex automatically, sleeps, and reacquires the mutex before returning. No explicit `UNLOCK`/`LOCK` wrapping is needed.

For this to work, the simulation callout that changes `STATUS` must `cv_signal` the condition variable after the change. The code in `myfirst_sim_command_cb` becomes:

```c
/* After setting DATA_AV: */
cv_broadcast(&sc->data_cv);
```

The driver's wait loop and the simulation's update cooperate through the condition variable: the driver sleeps until something interesting happens, and the simulation wakes it when something does. This is a much more efficient pattern than polling with `pause_sbt`. The driver sleeps for the whole wait; one wakeup is all that is needed; no thrashing.

Chapter 17 uses `pause_sbt` in the main polling loop for teaching simplicity (you can see exactly when the sleep happens), but the condition-variable pattern is mentioned here because it is closer to what production drivers do, especially for long waits.

### A Good Use of `callout_reset_sbt`

The simulation callouts from Section 3 are the canonical example. The sensor callout fires every 100 ms, updates the sensor register, and re-arms. The command callout fires once after `DELAY_MS` milliseconds to complete a command. Both use `callout_reset_sbt` with:

- `sbt` = the interval in SBT units
- `pr` = 0 (no precision hint; kernel may coalesce)
- `flags` = 0 (no special behaviour)

For a more precise schedule, `pr` can be set to bound the coalescing:

```c
callout_reset_sbt(&sim->sensor_callout,
    interval_ms * SBT_1MS,
    SBT_1MS,                   /* precision: within 1 ms */
    myfirst_sim_sensor_cb, sc, 0);
```

With `pr = SBT_1MS`, the callout fires no more than 1 ms later than requested. For our simulation, precision is not critical; the default of 0 is fine.

For a callout that must run on a specific CPU, the `C_DIRECT_EXEC` flag arranges for the callback to run directly from the hardclock interrupt rather than being dispatched to a callout thread. This is higher performance but more restrictive: the callback cannot sleep or take sleep locks. The Chapter 17 simulation does not need this; it uses plain mutex-based callouts.

For a callout aligned with the hardclock boundary, the `C_HARDCLOCK` flag is appropriate. Our simulation does not need alignment either; the default is fine.

### A Realistic Example: Simulating a Slow Device

Put the primitives together. Suppose the simulation is configured with `DELAY_MS=2000` (a slow device, two seconds per command). The driver issues a write command. What does each primitive do?

1. The driver writes `DATA_IN`, sets `CTRL.GO`, and calls `myfirst_wait_for_data(sc, 3000)` (3 s timeout).
2. `myfirst_wait_for_data` reads `STATUS`, finds `DATA_AV` not set, drops the lock, calls `pause_sbt("mfwait", SBT_1MS, 0, 0)`, and sleeps for ~1 ms.
3. Meanwhile, the simulation's `myfirst_sim_command_cb` is scheduled to fire in ~2000 ms. It is not running yet.
4. The driver's loop wakes, reacquires the lock, reads `STATUS` again, sees `DATA_AV` still not set, drops the lock, sleeps again.
5. Steps 2-4 repeat about 2000 times over the two seconds.
6. The simulation's callout fires. It reads `STATUS`, sets `DATA_AV`, writes the command-completion values.
7. On the next iteration of the driver's loop, the driver sees `DATA_AV` set and returns.

Total CPU time burnt by the driver waiting: minimal. Each `pause_sbt` is a sleep, not a spin. The CPU is free for other work during each of the 2000 sleeps.

Now suppose the driver had used `DELAY(1000)` instead of `pause_sbt(..., SBT_1MS, ...)`. Same time scale per iteration; completely different CPU behaviour. Each `DELAY(1000)` burns 1000 microseconds of CPU time. 2000 iterations = 2 seconds of pure CPU. The driver works, but it hogs one core entirely for the duration of the command. If other processes need CPU, they wait.

The difference illustrates why `pause_sbt` is the right choice for millisecond-scale waits: sleep is free; spin costs CPU.

Now suppose the driver had used `callout_reset_sbt` instead of polling. That is harder: `callout_reset_sbt` does not make the current thread wait. The thread would return to its caller immediately. The caller would need to know to sleep until the callout fires. This is exactly what condition variables do. The code becomes:

```c
MYFIRST_LOCK_ASSERT(sc);

while (!(CSR_READ_4(sc, MYFIRST_REG_STATUS) & MYFIRST_STATUS_DATA_AV)) {
        int error = cv_timedwait_sbt(&sc->data_cv, &sc->mtx,
            SBT_1MS * timeout_ms, 0, 0);
        if (error == EWOULDBLOCK)
                return (ETIMEDOUT);
}
```

And the simulation callout calls `cv_broadcast(&sc->data_cv)` after setting `DATA_AV`. The driver thread sleeps once (not 2000 times); the CPU overhead is minimal; the response time is bounded by the simulation's own timing, not by the driver's polling rate.

For Chapter 17, the polling pattern is taught first because it is easier to read: the `for` loop is explicit, the `pause_sbt` is explicit, the `CSR_READ_4` is explicit. The condition-variable pattern is more efficient but requires understanding all three of Chapter 12's primitives. We use the polling pattern as a stepping stone and note the condition-variable pattern as the production preference.

### Timing for the Simulation Itself

The simulation's own timing choices are worth examining.

The sensor callout fires on an interval that the `SENSOR_CONFIG` register specifies. The default is 100 ms. The user can change it. The kernel honours the requested interval within a few milliseconds of precision. This is fine for a teaching sensor; a real device might require sub-microsecond precision, which would require `C_DIRECT_EXEC` callbacks or even non-standard timing sources.

The command callout fires `DELAY_MS` milliseconds after the command is issued. Again, millisecond precision is fine. The driver's timeouts are calibrated against this: the default command timeout is 2000 ms, well above the default `DELAY_MS` of 500 ms.

The driver's polling interval is 1 ms. For a command that takes 500 ms, that means ~500 polls, which is tolerable. For a command that takes 5 ms, that means ~5 polls, which is fast enough. For a command that takes 5 seconds, that means 5000 polls, which is wasteful. The polling interval could be adaptive (longer polls for longer commands), but the implementation complexity is not worth it for a teaching driver.

### Realistic Constraints: Ready After 500 ms

To make the timing story concrete, configure the simulation for a 500 ms per-command delay and run a stress test:

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=500
# time echo -n "test" > /dev/myfirst0
```

Expected output:

```text
real    0m2.012s
user    0m0.001s
sys     0m0.005s
```

Four bytes, 500 ms each, 2 seconds total. Almost all of the time is kernel sleep time; CPU usage is negligible. The driver is efficient even though the command is slow.

Now stress with concurrent writers:

```text
# for i in 1 2 3 4; do
    (echo -n "ABCD" > /dev/myfirst0) &
  done
# time wait
```

Four processes, 4 bytes each, 500 ms per byte. Total commands: 16. At one command per 500 ms (serialised through the driver lock), total time: 8 seconds. Expected:

```text
real    0m8.092s
```

The driver lock serialises the commands, and the simulation's own `command_pending` check prevents overlapping. The effective throughput is one command per `DELAY_MS` milliseconds, regardless of the number of writers.

### Handling Timeouts Gracefully

The driver's behaviour when a command times out deserves its own walkthrough. The command path:

1. Driver writes `DATA_IN`, sets `CTRL.GO`.
2. Driver waits up to `cmd_timeout_ms` for `STATUS.DATA_AV`.
3. If the wait times out:
   a. Increment `cmd_data_timeouts` counter.
   b. Call `myfirst_recover_from_stuck` to clear stale simulation state.
   c. Return `ETIMEDOUT` to the caller.
4. If the wait succeeds, continue with the normal path.

The recovery is important. A simulation that is actually slow (not stuck) will eventually set `DATA_AV`, but the driver has already given up. If the driver did not clear the simulation's pending state, a subsequent command might see the lingering `DATA_AV` from the old command and think the new command completed immediately. The recovery function stops the pending callout, clears the `command_pending` flag, and clears the relevant `STATUS` bits.

On real hardware, the equivalent recovery is a device reset. The driver writes `CTRL.RESET`, waits briefly, and restarts. Some devices have a separate reset mechanism accessible through PCI config space. The pattern is the same: after a timeout, get the device back to a known state before doing anything else.

### A Debugging Technique: Artificial Slowdown

A useful debugging technique for timing-sensitive code: artificially slow the simulation down to stretch race windows.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=3000
# sysctl dev.myfirst.0.cmd_timeout_ms=1000
```

Now every command takes 3 seconds but the driver only waits 1 second. Every command times out. The driver's recovery path runs on every command. Bugs in the recovery path surface immediately:

- If the recovery path crashes, the kernel panics on the first command.
- If the recovery path leaves stale state, the second command fails in some unusual way.
- If the recovery path leaks memory, `vmstat -m | grep myfirst` climbs over time.

This is a legitimate debugging technique. Real drivers are hard to stress this way because real hardware is hard to make slow on demand. The simulation, by contrast, is a single register away from being very slow. Use this capability to exercise code paths that normal operation rarely reaches.

### Common Timing Mistakes

A short catalogue of timing mistakes to avoid.

**Mistake 1: Using `DELAY` for milliseconds.** `DELAY(1000000)` busy-waits for a full second, consuming a CPU core for that time. Use `pause_sbt(..., SBT_1S, ...)` instead.

**Mistake 2: Holding a lock across `pause_sbt`.** Blocks every context that needs the lock. Drop the lock, sleep, reacquire.

**Mistake 3: Spinning without yielding on a condition that depends on another context.** If your code is waiting for a callout to set a bit, the callout cannot run while you spin. Yield with `pause_sbt` (dropping the lock) or use a condition variable.

**Mistake 4: Using `callout_reset_sbt` for a fire-and-forget action that should complete immediately.** If the action is quick, just do it. Callouts have setup cost and scheduling overhead; they are worth it when the delay is meaningful.

**Mistake 5: Setting a timeout shorter than the expected operation time.** Every command times out. The driver does no useful work. Make timeouts generous but finite.

**Mistake 6: Setting a timeout much longer than the expected operation time.** A legitimate slow operation hangs the user-space call for the full timeout. Users lose patience. Make timeouts reasonable for the specific operation.

**Mistake 7: Not draining callouts before freeing memory the callouts touch.** The callout fires after the memory is freed, and the callback dereferences freed memory. Drain before free.

**Mistake 8: Re-arming a callout from within its callback without checking for cancellation.** The detach path asks the callout to stop; the callback re-arms itself anyway. Infinite loop; detach never completes. Check the `running` flag (as Section 3's `myfirst_sim_sensor_cb` does) before re-arming.

Each mistake is common in beginning driver code. The Chapter 17 stages avoid all of them by following the patterns Chapter 13 taught for callouts, Chapter 12 taught for condition variables, and Section 6 is now making explicit.

### Wrapping Up Section 6

Three timing primitives, three time scales, three cost profiles. `DELAY(9)` for very short waits (< 100 us). `pause_sbt(9)` for medium waits (100 us to several seconds) when the thread has nothing else to do. `callout_reset_sbt(9)` for fire-and-forget scheduling and for periodic work. Choosing the right one is a judgment call informed by the time scale, the context, and what the thread would otherwise be doing.

The Chapter 17 driver uses `pause_sbt` for its polling loop and `callout_reset_sbt` for the simulation's own timers. A reset path uses `DELAY(5)` where appropriate. The driver's timing story is coherent: it never wastes CPU, never holds locks during sleeps, and never blocks other contexts unnecessarily.

Section 7 adds the last major piece of the Chapter 17 simulation: a fault-injection framework. With timing now reliable, the driver can be exercised under a variety of failure conditions and the error-recovery paths validated.



## Section 7: Simulating Errors and Fault Conditions

A driver that passes every happy-path test and never exercises its error paths is a driver that will eventually fail in production. Error paths are the code that runs when something unexpected happens, and "unexpected" is the most honest word for what real hardware does on a bad day. A network card whose PHY lost its link signal. A disk controller whose command queue overflowed. A sensor whose calibration went out of range. A device whose firmware deadlocked after a specific command sequence.

Real hardware produces these conditions rarely, and usually at the worst possible moment. A driver author who waits for real hardware to produce them is a driver author whose customers will find the bugs. A driver author who uses simulation to produce them on demand can fix the bugs before they ship.

Section 7 builds a fault-injection framework into the Chapter 17 simulation. The framework lets the reader ask the simulation to misbehave in specific, controlled ways: a timeout, a data corruption, a stuck-busy state, a random failure. Each mode exercises a different driver code path. By the end of Section 7, the driver has seen every error it is designed to handle, and the developer has confidence that the error paths work.

### The Fault-Injection Philosophy

Before writing code, a short philosophical pause. What should a fault-injection framework look like, and what should it not look like?

A good fault-injection framework:

- Targets specific failure modes, not random chaos. A fault that says "the next read returns 0xFFFFFFFF" is useful; a fault that says "something bad might happen" is not.
- Is controllable at fine granularity. Turn faults on, off, or partial. Inject one, inject many, inject probabilistically.
- Is observable. When a fault fires, the framework logs it (or provides a counter). The tester can see what happened and correlate with driver behaviour.
- Is orthogonal to other testing. A load test should not care whether faults are enabled; a fault test should not require load to be disabled.
- Is deterministic when configured to be. A fault configured at probability 100% fires every time. A fault configured at probability 50% fires half the time, and the pseudo-random decision is reproducible with a seed.

A bad fault-injection framework is either useless (never triggers the paths you need) or dangerous (produces failures the driver cannot recover from even in principle). The goal is useful unreliability, not destructive chaos.

The Chapter 17 framework achieves these goals with a small set of flags in `FAULT_MASK`, a probability in `FAULT_PROB`, and a counter that tracks how many faults have fired. The simulation consults these whenever an operation begins and either proceeds normally or injects a fault according to the configuration.

### The Fault Modes

The four fault modes Chapter 17 implements:

**`MYFIRST_FAULT_TIMEOUT`** (bit 0). The next command never completes. The command callout is not scheduled; `STATUS.DATA_AV` never asserts. The driver's `myfirst_wait_for_data` times out, triggering the recovery path.

**`MYFIRST_FAULT_READ_1S`** (bit 1). The next `DATA_OUT` read returns `0xFFFFFFFF` instead of the real value. This simulates a bus read that fails in a way many real hardware buses do (a read of a disconnected or power-gated device commonly returns all-ones).

**`MYFIRST_FAULT_ERROR`** (bit 2). The next command completes, but with `STATUS.ERROR` set. The driver is expected to detect the error, clear the latch, and report an error to the caller.

**`MYFIRST_FAULT_STUCK_BUSY`** (bit 3). `STATUS.BUSY` is latched on and never clears. The driver's `myfirst_wait_for_ready` times out before any command can be issued. This simulates a device that got wedged and needs a reset.

Each fault exercises a different path. The timeout fault exercises `cmd_data_timeouts` and the recovery. The read-1s fault tests whether the driver notices corrupted data. The error fault exercises `cmd_errors` and the error-clearing logic. The stuck-busy fault exercises `cmd_rdy_timeouts`.

Multiple faults can be active simultaneously: set multiple bits in `FAULT_MASK`. Each fault is applied independently at the point in the simulation where it is relevant.

### The Fault-Probability Field

`FAULT_MASK` selects which faults are candidates; `FAULT_PROB` controls how often a candidate fault actually fires. Probabilities are expressed as integers from 0 to 10000, where 10000 means 100% (fault every time) and 0 means never. This gives four decimal places of precision without fractional arithmetic.

The test for whether a fault fires:

```c
static bool
myfirst_sim_should_fault(struct myfirst_softc *sc)
{
        uint32_t prob, r;

        MYFIRST_LOCK_ASSERT(sc);

        prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
        if (prob == 0)
                return (false);
        if (prob >= 10000)
                return (true);

        r = arc4random_uniform(10000);
        return (r < prob);
}
```

`arc4random_uniform(10000)` returns a pseudo-random integer in `[0, 10000)`. If it is less than `prob`, the fault fires. With `prob = 5000` (50%), the fault fires about half the time. With `prob = 100` (1%), about one in a hundred operations. With `prob = 10000`, every operation.

The function is called at the start of each operation. If it returns true, the operation applies a fault from `FAULT_MASK` (the simulation's logic decides which fault or faults to apply, typically the first matching one).

### Implementing the Timeout Fault

The timeout fault is the simplest. In `myfirst_sim_start_command`, check the fault mask before scheduling the command callout:

```c
void
myfirst_sim_start_command(struct myfirst_softc *sc)
{
        struct myfirst_sim *sim = sc->sim;
        uint32_t data_in, delay_ms, fault_mask;
        bool fault;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        if (sim->command_pending) {
                sc->stats.cmd_rejected++;
                device_printf(sc->dev,
                    "sim: overlapping command; ignored\n");
                return;
        }

        data_in = CSR_READ_4(sc, MYFIRST_REG_DATA_IN);
        sim->pending_data = data_in;

        {
                uint32_t status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                status |= MYFIRST_STATUS_BUSY;
                status &= ~MYFIRST_STATUS_DATA_AV;
                CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
        }

        delay_ms = CSR_READ_4(sc, MYFIRST_REG_DELAY_MS);
        if (delay_ms == 0)
                delay_ms = 1;

        sim->command_pending = true;

        /* Fault-injection check. */
        fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        fault = (fault_mask != 0) && myfirst_sim_should_fault(sc);

        if (fault && (fault_mask & MYFIRST_FAULT_TIMEOUT)) {
                /* Do not schedule the completion callout. The command
                 * will never complete; the driver will time out.
                 * (Simulation does not clear command_pending; the
                 * driver's recovery path is responsible for that.) */
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting TIMEOUT fault\n");
                return;
        }

        /* Save the fault state for the callout to honour. */
        sim->pending_fault = fault ? fault_mask : 0;

        callout_reset_sbt(&sim->command_callout,
            delay_ms * SBT_1MS, 0,
            myfirst_sim_command_cb, sc, 0);
}
```

Four changes from the Section 3 version.

First, a statistics counter for `cmd_rejected` replaces the bare printf.

Second, the function reads `FAULT_MASK` and calls `myfirst_sim_should_fault` to decide whether this operation injects a fault.

Third, if the timeout fault is selected, the function returns without scheduling the completion callout. `command_pending` is set to true, so overlapping commands are still rejected, but no callout ever fires to complete the command.

Fourth, the simulation stores the fault state in `sim->pending_fault` so the callout (when it fires, for non-timeout faults) can honour the fault.

`pending_fault` is added to the simulation state structure:

```c
struct myfirst_sim {
        /* ... existing fields ... */
        uint32_t   pending_fault;   /* FAULT_MASK bits to apply at completion */
};
```

### Implementing the Error Fault

The error fault fires at command completion. In `myfirst_sim_command_cb`:

```c
static void
myfirst_sim_command_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t status, fault;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running || !sim->command_pending)
                return;

        fault = sim->pending_fault;

        /* Always clear BUSY. */
        status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
        status &= ~MYFIRST_STATUS_BUSY;

        if (fault & MYFIRST_FAULT_ERROR) {
                /* Set ERROR, do not set DATA_AV. The driver should
                 * detect the error on the next STATUS read. */
                status |= MYFIRST_STATUS_ERROR;
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting ERROR fault\n");
        } else if (fault & MYFIRST_FAULT_READ_1S) {
                /* Normal completion, but DATA_OUT is corrupted. */
                CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, 0xFFFFFFFF);
                status |= MYFIRST_STATUS_DATA_AV;
                sc->stats.fault_injected++;
                device_printf(sc->dev,
                    "sim: injecting READ_1S fault\n");
        } else {
                /* Normal completion. */
                CSR_WRITE_4(sc, MYFIRST_REG_DATA_OUT, sim->pending_data);
                status |= MYFIRST_STATUS_DATA_AV;
        }

        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);

        sim->op_counter++;
        CSR_WRITE_4(sc, MYFIRST_REG_OP_COUNTER, sim->op_counter);

        sim->command_pending = false;
        sim->pending_fault = 0;
}
```

Three branches. The error fault sets `STATUS.ERROR` instead of `STATUS.DATA_AV`, leaving `DATA_OUT` untouched. The read-1s fault writes `0xFFFFFFFF` to `DATA_OUT` and sets `DATA_AV` normally. The normal branch writes the real data and sets `DATA_AV`.

The simulation increments `op_counter` and `fault_injected` appropriately. The driver will see the effects through the register values; the counters let the test validate that the faults fired.

### Implementing the Stuck-Busy Fault

The stuck-busy fault is orthogonal to the command cycle: it keeps `STATUS.BUSY` set independently of any command. Add a callout that monitors `FAULT_MASK` and keeps `STATUS.BUSY` latched when the bit is set:

```c
static void
myfirst_sim_busy_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;
        uint32_t fault_mask, status;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        fault_mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        if (fault_mask & MYFIRST_FAULT_STUCK_BUSY) {
                status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
                if (!(status & MYFIRST_STATUS_BUSY)) {
                        status |= MYFIRST_STATUS_BUSY;
                        CSR_WRITE_4(sc, MYFIRST_REG_STATUS, status);
                }
        }

        /* Re-arm every 50 ms. */
        callout_reset_sbt(&sim->busy_callout,
            50 * SBT_1MS, 0,
            myfirst_sim_busy_cb, sc, 0);
}
```

Initialise and start the callout alongside the sensor callout in `myfirst_sim_attach` and `myfirst_sim_enable`:

```c
callout_init_mtx(&sim->busy_callout, &sc->mtx, 0);
/* ... in enable: */
callout_reset_sbt(&sim->busy_callout, 50 * SBT_1MS, 0,
    myfirst_sim_busy_cb, sc, 0);
/* ... in disable: */
callout_stop(&sim->busy_callout);
/* ... in detach: */
callout_drain(&sim->busy_callout);
```

Now, when `FAULT_STUCK_BUSY` is set, the busy callout continually re-asserts `STATUS.BUSY` every 50 ms. Any command the driver tries to issue will find `BUSY` set, wait for it to clear, and time out on `wait_for_ready`. Clearing the `FAULT_STUCK_BUSY` bit stops the re-assertion, and `BUSY` returns to its natural state (whichever the command path left it in).

### The Driver's Error-Handling Paths

With faults wired in, the driver's error-handling paths get exercised. Review them now to confirm they work correctly.

**Timeout on `wait_for_ready`** (from Section 5):

```c
error = myfirst_wait_for_ready(sc, sc->rdy_timeout_ms);
if (error != 0) {
        sc->stats.cmd_rdy_timeouts++;
        return (error);
}
```

When `FAULT_STUCK_BUSY` is set, this path triggers. The caller sees `ETIMEDOUT`. The driver does not try to issue the command because it cannot. The counter reflects the event.

**Timeout on `wait_for_data`**:

```c
error = myfirst_wait_for_data(sc, sc->cmd_timeout_ms);
if (error != 0) {
        sc->stats.cmd_data_timeouts++;
        myfirst_recover_from_stuck(sc);
        return (error);
}
```

When `FAULT_TIMEOUT` is set, this path triggers. The driver calls `myfirst_recover_from_stuck`, which clears the simulation's `command_pending` flag and `STATUS.BUSY`. The next command sees a clean state. The counter reflects the event.

**`STATUS.ERROR` detection**:

```c
status = CSR_READ_4(sc, MYFIRST_REG_STATUS);
if (status & MYFIRST_STATUS_ERROR) {
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_ERROR, 0);
        CSR_UPDATE_4(sc, MYFIRST_REG_STATUS,
            MYFIRST_STATUS_DATA_AV, 0);
        sc->stats.cmd_errors++;
        return (EIO);
}
```

When `FAULT_ERROR` is set, this path triggers. The driver clears `STATUS.ERROR` and `STATUS.DATA_AV`, increments the counter, returns `EIO`.

**Corrupted `DATA_OUT` handling** requires an addition. When `FAULT_READ_1S` is set, `DATA_OUT` is `0xFFFFFFFF`, but the driver currently does not check for this. Whether the driver should check depends on the protocol: for a sensor, `0xFFFFFFFF` is often a legitimate "read error" marker; for other devices, it might be a plausible data value. Chapter 17's driver treats it as a potential error for illustration:

```c
if (data_out == 0xFFFFFFFF) {
        /* Likely a bus read error. Real devices rarely produce this
         * value as legitimate data; treat as an error. */
        sc->stats.cmd_errors++;
        return (EIO);
}
```

This behaviour is a judgment call. A real driver for a real device would check the datasheet for the device's documented bad values and respond appropriately.

### Testing the Faults One at a Time

With the infrastructure in place, each fault can be tested in isolation:

**Test 1: Timeout fault at 100%.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x1    # MYFIRST_FAULT_TIMEOUT
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # 100%
# echo -n "test" > /dev/myfirst0
write: Operation timed out
# sysctl dev.myfirst.0.cmd_data_timeouts
dev.myfirst.0.cmd_data_timeouts: 1
# sysctl dev.myfirst.0.fault_injected
dev.myfirst.0.fault_injected: 1
```

The first byte triggers a timeout. The driver recovers. Subsequent bytes would also time out until the fault is cleared. The counters reflect the event.

Turn off the fault:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# echo -n "test" > /dev/myfirst0
# echo "write succeeded"
write succeeded
# sysctl dev.myfirst.0.cmd_successes
dev.myfirst.0.cmd_successes: 4   # or more, depending on history
```

Commands succeed again.

**Test 2: Error fault at 25%.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x4    # MYFIRST_FAULT_ERROR
# sysctl dev.myfirst.0.reg_fault_prob_set=2500   # 25%
# sysctl dev.myfirst.0.cmd_errors
dev.myfirst.0.cmd_errors: 0
# for i in $(seq 1 40); do echo -n "X" > /dev/myfirst0; done
write: Input/output error     (occurs roughly 10 times)
# sysctl dev.myfirst.0.cmd_errors
dev.myfirst.0.cmd_errors: 9    # approximately 25% of 40
```

About one in four commands errors out. The driver detects the error, clears the state, and returns `EIO`. The counter reflects the event.

**Test 3: Stuck-busy fault.**

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x8    # MYFIRST_FAULT_STUCK_BUSY
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # (prob doesn't matter; latch is always on)
# echo -n "X" > /dev/myfirst0
write: Operation timed out
# sysctl dev.myfirst.0.cmd_rdy_timeouts
dev.myfirst.0.cmd_rdy_timeouts: 1
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3    # READY|BUSY
```

The driver cannot issue a command because `BUSY` is latched on. Clear the fault:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sleep 1     # wait for the busy callout to stop re-asserting
# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1    # READY only
# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

### Integration: Random Faults Under Load

The most realistic test configures random faults at a low probability and runs the driver under load:

```sh
#!/bin/sh
# fault_stress.sh: random faults under load.

# 1% probability of TIMEOUT or ERROR (bits 0 and 2 both set).
sysctl dev.myfirst.0.reg_fault_mask_set=0x5
sysctl dev.myfirst.0.reg_fault_prob_set=100

# Fast commands so the test runs in reasonable time.
sysctl dev.myfirst.0.reg_delay_ms_set=10
sysctl dev.myfirst.0.cmd_timeout_ms=50    # very short, to spot timeouts fast

# Load: 8 parallel workers, each doing 200 round trips.
for i in $(seq 1 8); do
    (for j in $(seq 1 200); do
        echo -n "X" > /dev/myfirst0 2>/dev/null
        dd if=/dev/myfirst0 bs=1 count=1 of=/dev/null 2>/dev/null
    done) &
done
wait

# Report.
sysctl dev.myfirst.0 | grep -E 'cmd_|fault_|op_counter'

# Clean up.
sysctl dev.myfirst.0.reg_fault_mask_set=0
sysctl dev.myfirst.0.reg_fault_prob_set=0
sysctl dev.myfirst.0.reg_delay_ms_set=500
sysctl dev.myfirst.0.cmd_timeout_ms=2000
```

Expected counters after the run:

- `cmd_successes`: around 3168 (out of ~3200 expected round trips)
- `cmd_data_timeouts`: around 16 (1% of half the operations)
- `cmd_errors`: around 16 (1% of half the operations)
- `fault_injected`: around 32 (sum of the two)
- `cmd_recoveries`: around 16 (one per timeout)

The exact numbers vary with each run (the faults are random), but the proportions should be roughly 1% for each fault type. If the proportions are wildly off, there is either a bug in the fault injection or a bug in the driver's error-counting. Either way, the test is doing its job by surfacing anomalies.

### A Common Pitfall: Recovery That Never Clears Everything

A bug that shows up under fault testing is recovery logic that does not clear everything. Suppose `myfirst_recover_from_stuck` forgot to clear `STATUS.BUSY`:

```c
/* BUGGY version: */
static void
myfirst_recover_from_stuck(struct myfirst_softc *sc)
{
        MYFIRST_LOCK_ASSERT(sc);
        if (sc->sim != NULL) {
                sc->sim->command_pending = false;
                callout_stop(&sc->sim->command_callout);
        }
        /* Missing: the STATUS cleanup. */
}
```

Running the stress test would show:

- First timeout: recovery runs, `command_pending` clears.
- Second command attempt: `STATUS.BUSY` is still set from the first attempt. The command cannot issue. `cmd_rdy_timeouts` climbs.
- Third attempt: same.
- ... forever.

The counter `cmd_successes` stops incrementing. `cmd_rdy_timeouts` climbs without bound. A careful test notices this and flags it. A careless test just sees "lots of errors" and shrugs.

The lesson is that fault injection exposes incomplete recovery. A driver that works under fault-free conditions may still have bugs that only appear when faults happen. The stress test is what surfaces them.

### Combining Faults

The Chapter 17 framework allows multiple faults to be active simultaneously:

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0xf    # all four faults
# sysctl dev.myfirst.0.reg_fault_prob_set=1000   # 10%
```

Each operation has a 10% chance of triggering a fault. If it does, the simulation picks one of the enabled faults (in practice, the code checks them in order: TIMEOUT, ERROR, READ_1S, STUCK_BUSY). The driver's response must be robust against all of them.

An even harsher test:

```text
# sysctl dev.myfirst.0.reg_fault_prob_set=10000  # 100%
```

Every operation faults. The driver never completes a command successfully. Every error path runs. Run the driver for a minute under this setting. If the driver crashes, there is a bug in an error path. If the driver leaks memory (check `vmstat -m | grep myfirst`), there is a leak in an error path. If the driver deadlocks (check with `procstat -kk`), there is a deadlock in an error path.

Running under 100% faults for a sustained period is a standard test for fault-injection frameworks. It is intentionally hostile; a driver that passes is robust to real-world failure conditions.

### Observability for Fault Testing

The driver's existing observability (access log, stats counters) carries most of what fault testing needs. Three additions are worth making.

**Fault-injected log entries.** The access log records every register access; it should record that a fault was injected. Add a new access-log entry type:

```c
#define MYFIRST_CTX_FAULT   0x10   /* fault injected */
```

And log it when a fault fires:

```c
myfirst_access_log_push(sc, offset, 0, 4, true, MYFIRST_CTX_FAULT);
```

Now the access log shows, interleaved with normal accesses, entries that indicate "fault X was injected here". The tester can correlate fault injection with subsequent driver behaviour.

**Per-fault-type counters.** Instead of a single `fault_injected` counter, track each type:

```c
struct myfirst_stats {
        /* ... existing counters ... */
        uint64_t        fault_timeout;
        uint64_t        fault_read_1s;
        uint64_t        fault_error;
        uint64_t        fault_stuck_busy;
};
```

And increment the right one based on the fault:

```c
if (fault & MYFIRST_FAULT_TIMEOUT)
        sc->stats.fault_timeout++;
else if (fault & MYFIRST_FAULT_ERROR)
        sc->stats.fault_error++;
else if (fault & MYFIRST_FAULT_READ_1S)
        sc->stats.fault_read_1s++;
```

The sysctl exposes each counter. The tester can see, at a glance, which fault types have fired and which have not.

**A fault-injection summary sysctl.** A single sysctl that reports "fault mask is X, probability is Y, total injected is Z". Useful for quick checks during interactive debugging:

```c
static int
myfirst_sysctl_fault_summary(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        char buf[128];
        uint32_t mask, prob;
        uint64_t injected;

        MYFIRST_LOCK(sc);
        mask = CSR_READ_4(sc, MYFIRST_REG_FAULT_MASK);
        prob = CSR_READ_4(sc, MYFIRST_REG_FAULT_PROB);
        injected = sc->stats.fault_injected;
        MYFIRST_UNLOCK(sc);

        snprintf(buf, sizeof(buf),
            "mask=0x%x prob=%u/10000 injected=%ju",
            mask, prob, (uintmax_t)injected);
        return (sysctl_handle_string(oidp, buf, sizeof(buf), req));
}
```

Invocation:

```text
# sysctl dev.myfirst.0.fault_summary
dev.myfirst.0.fault_summary: mask=0x5 prob=100/10000 injected=47
```

This is the kind of summary that fits on one line and tells the whole story of the current fault configuration.

### Safe Fault Injection

The word "safe" needs qualification. Fault injection in the Chapter 17 simulation is safe because the simulation runs in the same kernel as the driver, and the faults are local: a corrupted `DATA_OUT` is a corrupted four-byte value in `malloc`'d memory, not a corrupted DMA descriptor that scribbles over random RAM. A stuck `BUSY` bit is a stuck bit in a simulated register, not a hardware queue that backs up until the system hangs.

Real hardware fault injection is a different story. A deliberately timed-out real device may hold a bus lock indefinitely, preventing other devices from making progress. A deliberately corrupted DMA transaction may write to arbitrary physical addresses. Real fault injection on production systems requires careful setup, dedicated test hardware, and usually a hypervisor or virtual device emulation.

Chapter 17's simulation sidesteps all of this by running in memory. The worst case of a misbehaved fault is a kernel panic (if a KASSERT fires) or a deadlock (if recovery code is incorrect). Both are survivable: a debug kernel catches panics cleanly, and a deadlocked driver can be killed by rebooting. No physical hardware is at risk.

This safety is why fault injection belongs in simulation, not just in production testing. Production testing catches the faults real hardware actually produces; simulation testing catches the faults real hardware sometimes produces and always hurts from.

### Hands-On: The Fault-Injection Checklist

A procedure for using fault injection effectively during development:

1. Write the driver's error path first. Before enabling the fault, read your code and identify what the recovery looks like.
2. Enable the fault at 100% probability briefly. Verify the driver's error path runs. Check the counters.
3. Enable the fault at low probability (1-10%). Run a stress test. Verify the driver still makes progress overall.
4. Check `vmstat -m | grep myfirst` before and after the stress test. Memory usage should not grow.
5. Check `WITNESS` output for lock-order warnings. A fault that causes an unusual path may expose a latent lock-order bug.
6. Check `INVARIANTS` output for assertion failures. A fault that corrupts state may trip an invariant.
7. Turn the fault off. Run the happy path. Verify everything returns to normal.
8. Document the fault in `HARDWARE.md` or `SIMULATION.md`.

This procedure turns fault injection into a disciplined activity, not a chaotic experiment. Each step has a clear goal, and each step validates a specific property of the driver.

### A Reflection: What Fault Injection Taught Us

Fault injection is a microcosm of the larger driver-development discipline. The driver must handle errors it cannot prevent, from sources it does not control, at times it cannot predict. Fault injection gives the developer a tool to exercise all three properties on demand.

More than that, fault injection reveals the structure of the driver's error-handling philosophy. A driver that treats every error as equally recoverable handles some real-world failures poorly. A driver that distinguishes "transient" (try again) from "catastrophic" (reset the device) has a clearer model of what the device is doing. A driver that logs every error with enough context to diagnose later is a driver whose bugs can be found by log analysis.

The Chapter 17 driver does not go all the way to this level of sophistication; it has a single error-handling path that treats all errors as transient and retries only if the caller does. But the foundation is in place. A challenge at the end of the chapter invites the reader to extend the driver with per-error-type handling, and the simulation has the hooks to exercise each type.

### Wrapping Up Section 7

Fault injection is how driver authors exercise error-handling code before real failures arrive. Chapter 17's framework provides four fault modes (timeout, read-1s, error, stuck-busy), a probability field, and enough observability to correlate injected faults with driver behaviour.

The driver's error paths are validated by enabling each fault in isolation and confirming the expected counter increments and the expected recovery. Combining faults and running under load at low probabilities exercises the composition of error paths. Running at 100% probability stresses every error path simultaneously.

The version tag becomes `1.0-sim-stage4`. The driver has now seen every error it is designed to handle, and the developer has observed each error path running correctly. The remaining work is organisational: refactor, version, document, and bridge to Chapter 18.



## Section 8: Refactoring and Versioning Your Simulated Hardware Driver

Stage 4 produced a driver that works correctly under both happy-path and fault-injection conditions. Stage 5 (Section 8) makes the driver maintainable. The changes are organisational: clean file boundaries, updated documentation, a version bump, and the regression pass that verifies nothing broke along the way.

A driver that works is valuable. A driver that works and reads cleanly to the next contributor is far more valuable. Section 8 is about that second step.

### The Final File Layout

Through Chapter 16, the driver was `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sync.h`, `cbuf.c`, and `cbuf.h`. Chapter 17 added `myfirst_sim.c` and `myfirst_sim.h`. The final file tree:

- `myfirst.c`: the driver lifecycle, the syscall handlers, the Chapter 11-15 synchronisation primitives.
- `myfirst_hw.c`: the Chapter 16 hardware-access layer. `CSR_*` macros, register accessors, access log, register-view sysctls.
- `myfirst_hw.h`: the register map, bit masks, fixed values, and API prototypes for the hardware layer.
- `myfirst_sim.c`: the Chapter 17 simulation backend. Sensor callout, command callout, busy callout, fault-injection logic, simulation API.
- `myfirst_sim.h`: the simulation's API prototypes and state structure.
- `myfirst_sync.h`: the Chapter 15 synchronisation-primitive header.
- `cbuf.c`, `cbuf.h`: the Chapter 10 ring buffer.

Seven source files, three headers, a Makefile, and three documentation files (`HARDWARE.md`, `LOCKING.md`, and the new `SIMULATION.md`). The split mirrors how production FreeBSD drivers organise themselves: one file per responsibility, with named interfaces between them.

### The Simulation-Specific Fields Move into the Sim Struct

Chapter 17 introduced the simulation state gradually. By the end of Section 7, some simulation-related fields were in the softc rather than in the `struct myfirst_sim`. Stage 5 tidies this up.

Fields that should live in `struct myfirst_sim`:

- The three callouts (`sensor_callout`, `command_callout`, `busy_callout`).
- The simulation-internal state (`pending_data`, `pending_fault`, `command_pending`, `running`, `sensor_baseline`, `sensor_tick`, `op_counter`).

Fields that should stay in the softc or in `struct myfirst_hw`:

- The register block and `bus_space` state (`regs_buf`, `regs_tag`, `regs_handle`): `myfirst_hw`.
- The driver's own timeouts (`cmd_timeout_ms`, `rdy_timeout_ms`): the softc.
- The statistics (`sc->stats`): the softc (shared by hw and sim paths).

The reasoning: hardware state belongs to the hardware layer; simulation state belongs to the simulation layer; the driver-visible behaviour belongs to the driver. Chapter 18 will replace the simulation with a real PCI backend, and the cleanest replacement is one where `myfirst_sim.c` and `myfirst_sim.h` disappear entirely, to be replaced by `myfirst_pci.c` and (optionally) `myfirst_pci.h`. The sim struct's fields do not need to survive that transition; the driver's fields do.

### The `SIMULATION.md` Document

A new markdown file, `SIMULATION.md`, captures the simulation's interface. The sections:

1. **Version and scope.** "SIMULATION.md version 1.0. Chapter 17 complete."
2. **What the simulation is.** One paragraph: "A kernel-memory-backed simulated device that mimics a minimal command-response hardware protocol, used to teach driver development without requiring real hardware."
3. **What the simulation does.** Enumerated behaviours: sensor autonomy, command cycle, fault injection, configurable delays.
4. **What the simulation does not do.** Enumerated limitations: no DMA, no interrupts, no PCI, no real timing precision.
5. **Callout map.** Table of each callout, its purpose, its default interval, its relationship to the driver lock.
6. **Fault modes.** Table of each fault type, its trigger, its effect, its recovery.
7. **Sysctl reference.** Table of each sysctl exposed by the simulation layer, its type, its default, its purpose.
8. **Development guidance.** How to add a new behaviour to the simulation (where to put the code, what locking discipline applies, how to document).
9. **Relationship to real hardware.** How the patterns in the simulation map to patterns in real FreeBSD drivers.

The document is perhaps 150 to 200 lines of markdown. It is the single source of truth for what the simulation promises. A developer reading it should be able to reason about the simulation without reading the code.

An example entry from the fault-modes table:

```text
| Fault              | Bit  | Trigger                          | Effect                        | Recovery            |
|--------------------|------|----------------------------------|-------------------------------|---------------------|
| MYFIRST_FAULT_TIMEOUT | 0  | should_fault() returns true      | Command callout not scheduled | driver timeout path |
| MYFIRST_FAULT_READ_1S | 1  | should_fault() returns true      | DATA_OUT = 0xFFFFFFFF          | driver error check  |
| MYFIRST_FAULT_ERROR   | 2  | should_fault() returns true      | STATUS.ERROR set instead of DATA_AV | driver error path |
| MYFIRST_FAULT_STUCK_BUSY | 3 | FAULT_MASK bit set             | STATUS.BUSY continuously latched | clear FAULT_MASK  |
```

Such tables make reading the code much easier: a reader sees `MYFIRST_FAULT_READ_1S` in the code and can look up the full story in one place.

### The Updated `HARDWARE.md`

Chapter 16's `HARDWARE.md` described a static register block. Chapter 17 extends it with the dynamic registers and the behaviours that drive them. The sections that change:

**Register Map.** The table now includes the Chapter 17 registers: `SENSOR` (0x28), `SENSOR_CONFIG` (0x2c), `DELAY_MS` (0x30), `FAULT_MASK` (0x34), `FAULT_PROB` (0x38), `OP_COUNTER` (0x3c). Each with its access type, default value, and one-line description.

**CTRL register fields.** A new bit: `MYFIRST_CTRL_GO` (bit 9). The table adds a row: "bit 9: `GO`, write 1 to trigger a command; hardware self-clears".

**STATUS register fields.** The table gains notes on which bits are dynamic in Chapter 17: `READY` is set at attach and stays on; `BUSY` is set by `start_command` and cleared by `command_cb`; `DATA_AV` is set by `command_cb` and cleared by the driver after reading `DATA_OUT`; `ERROR` is set by fault injection and cleared by the driver.

**SENSOR_CONFIG fields.** A new subsection explaining the two-field layout (low 16 bits interval, high 16 bits amplitude).

**FAULT_MASK fields.** A new subsection listing each fault bit and its effect.

**Dynamic behaviour summary.** A new subsection explaining what changes autonomously: `SENSOR` (every 100 ms by default), `STATUS.BUSY` (set/clear by command cycle or by `FAULT_STUCK_BUSY` callout), `STATUS.DATA_AV` (set by `command_cb`), `OP_COUNTER` (incremented on each command).

**Locking discipline for dynamic behaviour.** A sentence: "All dynamic updates happen under `sc->mtx`, which callouts acquire automatically via `callout_init_mtx`. The driver's command path acquires the mutex and drops it only during `pause_sbt` calls, where the simulation callouts may run."

The document grows from perhaps 80 lines (Chapter 16 version) to perhaps 140 lines. It remains scannable. The new sections are organised so a reader looking for Chapter 17 information finds it quickly.

### The Updated `LOCKING.md`

Chapter 15's `LOCKING.md` described a lock order of `sc->mtx -> sc->cfg_sx -> sc->stats_cache_sx` and a detach ordering that drained several primitives. Chapter 16 added the hardware layer's detach to the list. Chapter 17 adds the simulation layer's detach.

The updated detach ordering, from outermost to innermost:

1. Destroy `sc->cdev` and wait for any open file descriptors to close.
2. Stop and drain all driver-level callouts (heartbeat, watchdog, tick_source).
3. Disable and drain the simulation's callouts (sensor, command, busy).
4. Free the simulation state.
5. Detach the hardware layer (free `regs_buf`, free `hw`).
6. Destroy the driver's synchronisation primitives (mutex, sx, cv, sema).

Step 3 is new in Chapter 17. The simulation is stopped before the hardware layer is torn down, because the simulation's callouts touch the register block through the hardware layer's accessors. Freeing the register block while a callout is still in flight would produce a use-after-free.

The ordering of step 3 and step 2 matters. Driver-level callouts (heartbeat) may read registers, so they must drain before the simulation drains. A deliberate ordering: driver callouts first (they are the outermost consumers), then simulation callouts (the innermost), then the registers themselves.

### The Version Bump

In `myfirst.c`:

```c
#define MYFIRST_VERSION "1.0-simulated"
```

The version goes to `1.0` because Chapter 17 marks a real milestone: the driver now behaves, end to end, like a driver against a functional device. Chapter 16's `0.9-mmio` was a register-access chapter; Chapter 17's `1.0-simulated` is a fully-functional-driver chapter. The jump from 0.9 to 1.0 reflects this.

The top-of-file comment is updated:

```c
/*
 * myfirst: a beginner-friendly device driver tutorial vehicle.
 *
 * Version 1.0-simulated (Chapter 17): adds dynamic simulation of the
 * Chapter 16 register block.  Includes autonomous sensor updates,
 * command-triggered delayed events, read-to-clear semantics, and a
 * fault-injection framework.  Simulation code lives in myfirst_sim.c;
 * the driver sees it only through register accesses.
 *
 * ... (previous version notes preserved) ...
 */
```

The Chapter 17 comment is two sentences. It points at the new file and names the new capabilities. A future contributor reading it has enough context to understand what the version represents.

### The Regression Pass

Chapter 15 established the regression discipline: after every version bump, run the full stress suite from every previous chapter, confirm `WITNESS` is silent, confirm `INVARIANTS` is silent, confirm `kldunload` completes cleanly.

For Stage 5 that means:

- The Chapter 11 concurrency tests (multiple writers, multiple readers) pass.
- The Chapter 12 blocking tests (reader waits for data, writer waits for room) pass.
- The Chapter 13 callout tests pass.
- The Chapter 14 task tests pass.
- The Chapter 15 coordination tests pass.
- The Chapter 16 register-access tests pass.
- The Chapter 17 simulation tests (fault injection, timing, behaviour) pass.
- `kldunload myfirst` returns cleanly after the full suite.

No test is skipped. A regression in any previous chapter's test is a bug, not a deferred issue. The discipline is the same as it has been throughout Part 3.

The Chapter 17 test additions include:

- `sim_sensor_oscillates.sh`: confirms `SENSOR` changes over time.
- `sim_command_cycle.sh`: runs a series of write commands and verifies `OP_COUNTER` increments.
- `sim_timeout_fault.sh`: enables the timeout fault and verifies the driver recovers.
- `sim_error_fault.sh`: enables the error fault and verifies the driver reports `EIO`.
- `sim_stuck_busy_fault.sh`: enables the stuck-busy fault and verifies the driver times out on ready.
- `sim_mixed_faults_under_load.sh`: 10% fault probability, 8 parallel workers, 30 seconds.

Each script is a few dozen lines. Together they add about 300 lines of test infrastructure, which is small compared to the driver itself but significant in the confidence it provides.

### Running the Final Stage

```text
# cd examples/part-04/ch17-simulating-hardware/stage5-final
# make clean && make
# kldstat | grep myfirst
# kldload ./myfirst.ko
# kldstat -v | grep -i myfirst

myfirst: version 1.0-simulated

# dmesg | tail -5
# sysctl dev.myfirst.0 | head -40
```

The `kldstat -v` output shows `myfirst` at version `1.0-simulated`. The `dmesg` tail shows the device probe and attach with no errors. The `sysctl` output lists every Chapter 11 through Chapter 17 sysctl, including the simulation controls.

Run the stress suite:

```text
# ../labs/full_regression.sh
```

If every test passes, Chapter 17 is complete.

### A Small Rule for Chapter 17's Refactor

Chapter 16's refactor was about separating "driver business logic" from "hardware register mechanics". Chapter 17's refactor is about separating "simulation" from "hardware access". The rule generalises: when a subsystem acquires a new responsibility, give it its own file; when a file acquires multiple responsibilities, split them.

A rule of thumb: a file more than about 800 to 1000 lines often has more than one responsibility. A header that exports more than about ten functions often merits a split. A file that imports a header from a subsystem unrelated to its main purpose often signals a responsibility leak.

Chapter 17's `myfirst_sim.c` is about 300 lines at Stage 5. `myfirst_hw.c` is about 400 lines. `myfirst.c` is about 800 lines. Each file holds one responsibility. No file grew out of control. The split scales: Chapter 18 will add `myfirst_pci.c` (about 200 to 300 lines), Chapter 19 will add `myfirst_intr.c`, Chapter 20 will add `myfirst_dma.c`. Each subsystem lives in its own file. The main `myfirst.c` stays roughly constant in size; the subsystems grow around it.

### What Stage 5 Accomplished

The driver is now at `1.0-simulated`. Compared to `0.9-mmio`, it has:

- A simulation backend in `myfirst_sim.c` and `myfirst_sim.h`.
- A dynamic register block with autonomous sensor updates, command-triggered delayed events, and fault injection.
- Configurable timeouts and per-error-type counters.
- A `SIMULATION.md` document describing the simulation's interface and limits.
- An updated `HARDWARE.md` reflecting the new registers and dynamic behaviour.
- An updated `LOCKING.md` reflecting the new detach ordering.
- Regression tests that exercise every Chapter 17 behaviour.

The driver's code is recognisably FreeBSD. The layout is the layout real drivers use when they have distinct simulation, hardware, and driver responsibilities. The vocabulary is the vocabulary real drivers share. A contributor opening the driver for the first time finds a familiar structure, reads the documentation, and can navigate the code by subsystem.

### Real FreeBSD Drivers That Use the Same Patterns

The patterns this chapter exercises are not confined to simulated hardware. Three places in the FreeBSD tree are especially worth opening alongside Chapter 17, because each one uses a similar shape in a real subsystem and reading them turns the techniques from "how I wrote the simulation" into "how the kernel actually operates."

The first is the **`watchdog(4)`** subsystem in `/usr/src/sys/dev/watchdog/watchdog.c`. The core routine `wdog_kern_pat()` at the top of that file is a small state machine driven by a periodic "pat" from user space or from another kernel subsystem; if the pat does not arrive within the configured timeout, the subsystem fires a pretimeout handler and, ultimately, a system reset. The parallel with the Chapter 17 simulation is direct: a timeout value in ticks, a callout that advances state in the background, an ioctl surface (`WDIOC_SETTIMEOUT`) that changes the interval from user space, and a sysctl surface that exposes the last configured timeout for observation. It is also short enough to read end-to-end, which is rare for a production subsystem.

The second is **`random_harvestq`**, the entropy-collection path, in `/usr/src/sys/dev/random/random_harvestq.c`. The function `random_harvestq_fast_process_event()` and its surrounding queue discipline are the kernel's version of the "accept events from many sources and process them in the background" pattern this chapter exercised with a simulated sensor. The harvest queue uses a ring buffer, a worker thread, and explicit backpressure when consumers fall behind, and it is one of the cleaner examples in the tree of a driver-like subsystem that must never block the code paths that feed it. Reading it after Chapter 17 shows what the autonomous-update pattern looks like when the "sensor" is every entropy source in the system at once.

The third, worth mentioning briefly, is the **pseudo-random device** in `/usr/src/sys/dev/random/randomdev.c`. It uses a cdev surface, configurable sysctls, and careful separation between the harvest side and the output side. That separation is the same split Chapter 17 introduced between `myfirst_sim.c` and `myfirst.c`, and looking at how `randomdev.c` arranges its files is a useful second example of the discipline this section just applied to the simulated driver.

None of these are "toy" subsystems. They are live kernel code that has shipped for years. The point of the pointers is not to ask you to master them now, but to mark where the techniques you just practised in simulation live in production code, so that when you open `/usr/src/sys` later and a file looks familiar, you have already seen the pattern.

### Wrapping Up Section 8

The refactor is, again, small in code but significant in organisation. A new file split, a new documentation file, updates to two existing documentation files, a version bump, and a regression pass. Each step is cheap; together they turn a working driver into a maintainable one.

The Chapter 17 driver is done. The chapter closes with labs, challenges, troubleshooting, and a bridge to Chapter 18, where the simulated register block is replaced with a real PCI BAR.



## Hands-On Labs

Labs in Chapter 17 focus on exercising the simulation from multiple angles: watching autonomous behaviour, running commands under load, injecting faults, and observing driver reactions. Each lab takes 20 to 60 minutes.

### Lab 1: Watch the Sensor Breathe

Load the driver and watch the sensor value change without any driver activity.

```text
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.sim_running
dev.myfirst.0.sim_running: 1

# while true; do
    sysctl dev.myfirst.0.reg_sensor
    sleep 0.5
  done
```

You should see the value oscillating. Default baseline is `0x1000 = 4096`, amplitude is 64, so the value ranges from about 4096 to about 4160 and back, over several seconds.

Change the config to speed up the oscillation:

```text
# sysctl dev.myfirst.0.reg_sensor_config_set=0x01000020
```

This sets interval to `0x0100 = 256` ms and amplitude to `0x0020 = 32`. The sensor updates every 256 ms with a smaller range. Watch the output change.

Change it again to slow it down dramatically:

```text
# sysctl dev.myfirst.0.reg_sensor_config_set=0x03e80100
```

This sets interval to 1000 ms and amplitude to 256. The sensor changes once per second, with a larger range.

Stop the simulation:

```text
# sysctl dev.myfirst.0.sim_running=0   # (requires a writeable sysctl, added in the lab examples)
```

The sensor value freezes. Re-enable:

```text
# sysctl dev.myfirst.0.sim_running=1
```

It resumes. This lab exercises the autonomous-update path end to end.

### Lab 2: Run a Single Command

Issue a command by hand and observe every register change.

```text
# sysctl dev.myfirst.0.access_log_enabled=1
# sysctl dev.myfirst.0.reg_delay_ms_set=200

# echo -n "A" > /dev/myfirst0
# sysctl dev.myfirst.0.access_log | head -20
# sysctl dev.myfirst.0.access_log_enabled=0
```

The log should show:

1. Reads of `STATUS` (polling for BUSY to clear at command start).
2. A write to `DATA_IN` (the byte 'A' = 0x41).
3. A read of `CTRL` followed by a write of `CTRL` (setting the GO bit).
4. A write of `CTRL` (clearing the GO bit; the self-clearing logic).
5. Reads of `STATUS` (polling for DATA_AV).
6. An eventual transition where `DATA_AV` is set (from the simulation's callout).
7. A read of `DATA_OUT` (returning 0x41).
8. A write of `STATUS` (clearing DATA_AV).

Try to match each entry to the source code in `myfirst_sim.c` and `myfirst_write_cmd`. If any entry does not make sense, you have a gap to fill.

### Lab 3: Stress the Command Path

Run many concurrent commands and verify correctness.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=20
# sysctl dev.myfirst.0.cmd_successes     # note value

# for i in 1 2 3 4 5 6 7 8; do
    (for j in $(seq 1 50); do
        echo -n "X" > /dev/myfirst0
     done) &
  done
# wait

# sysctl dev.myfirst.0.cmd_successes     # should have grown by 400
# sysctl dev.myfirst.0.cmd_errors        # should still be 0
# sysctl dev.myfirst.0.reg_op_counter    # should match the growth in cmd_successes
```

Eight writers, 50 commands each, 400 total. With 20 ms per command, the test runs in about 8 seconds (serialised at the driver lock). `cmd_successes` should grow by 400. `cmd_errors` should remain at zero (no faults are enabled). `reg_op_counter` should match the increment in `cmd_successes`.

### Lab 4: Inject a Timeout Fault

Enable the timeout fault and observe the driver's recovery.

```text
# sysctl dev.myfirst.0.cmd_data_timeouts          # note value
# sysctl dev.myfirst.0.cmd_recoveries              # note value

# sysctl dev.myfirst.0.reg_fault_mask_set=0x1     # TIMEOUT bit
# sysctl dev.myfirst.0.reg_fault_prob_set=10000   # 100%

# echo -n "X" > /dev/myfirst0
write: Operation timed out

# sysctl dev.myfirst.0.cmd_data_timeouts          # should have grown by 1
# sysctl dev.myfirst.0.cmd_recoveries              # should have grown by 1

# sysctl dev.myfirst.0.reg_status                 # should be 1 (READY, BUSY cleared by recovery)

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

The first write times out; the driver recovers; a subsequent write succeeds. The counters confirm the sequence.

### Lab 5: Inject an Error Fault

Run a small batch with an error fault at 25% probability.

```text
# sysctl dev.myfirst.0.cmd_errors                 # note value

# sysctl dev.myfirst.0.reg_fault_mask_set=0x4     # ERROR bit
# sysctl dev.myfirst.0.reg_fault_prob_set=2500    # 25%

# for i in $(seq 1 40); do
    echo -n "X" > /dev/myfirst0 || echo "error on iteration $i"
  done

# sysctl dev.myfirst.0.cmd_errors                 # should have grown by ~10

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
```

About 10 out of 40 iterations should report an error. The driver's error-counter should reflect the count exactly. The driver should remain usable after the test (subsequent commands succeed).

### Lab 6: Inject Stuck-Busy and Watch the Driver Wait

Enable the stuck-busy fault, try a command, watch it time out, clear the fault, confirm recovery.

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x8     # STUCK_BUSY bit
# sleep 0.1                                         # let the busy callout assert

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 3                         # READY|BUSY

# sysctl dev.myfirst.0.cmd_rdy_timeouts            # note value

# echo -n "X" > /dev/myfirst0
write: Operation timed out                           # after sc->rdy_timeout_ms

# sysctl dev.myfirst.0.cmd_rdy_timeouts            # should have grown by 1

# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sleep 0.2                                          # let the busy callout stop

# sysctl dev.myfirst.0.reg_status
dev.myfirst.0.reg_status: 1                         # READY only

# echo -n "X" > /dev/myfirst0
# echo "write succeeded"
write succeeded
```

The fault latches `BUSY`; the driver cannot issue commands; the driver times out on `wait_for_ready`. Clearing the fault and waiting briefly lets `BUSY` clear (via the command path, if any commands slipped through, or via the natural state of no outstanding command). The driver recovers.

### Lab 7: Mixed Faults Under Load

Enable multiple faults at low probabilities, run a long stress test, analyse the results.

```text
# sysctl dev.myfirst.0.reg_fault_mask_set=0x5     # TIMEOUT | ERROR
# sysctl dev.myfirst.0.reg_fault_prob_set=200     # 2%
# sysctl dev.myfirst.0.reg_delay_ms_set=10
# sysctl dev.myfirst.0.cmd_timeout_ms=50

# # Record starting counters.
# sysctl dev.myfirst.0 | grep -E 'cmd_|fault_' > /tmp/before.txt

# # 8 workers, 100 commands each.
# for i in 1 2 3 4 5 6 7 8; do
    (for j in $(seq 1 100); do
        echo -n "X" > /dev/myfirst0 2>/dev/null
    done) &
  done
# wait

# # Record ending counters.
# sysctl dev.myfirst.0 | grep -E 'cmd_|fault_' > /tmp/after.txt
# diff /tmp/before.txt /tmp/after.txt

# # Clean up.
# sysctl dev.myfirst.0.reg_fault_mask_set=0
# sysctl dev.myfirst.0.reg_fault_prob_set=0
# sysctl dev.myfirst.0.reg_delay_ms_set=500
# sysctl dev.myfirst.0.cmd_timeout_ms=2000
```

The diff should show `cmd_successes` growing by about 784 (800 commands minus about 16 failures). `fault_injected` should grow by about 16. `cmd_data_timeouts` and `cmd_errors` should each grow by about 8. The exact numbers vary with each run (because of `arc4random`), but the proportions are stable.

Also check: `vmstat -m | grep myfirst` should show no growth in memory usage between before and after; the test was not supposed to leak.

### Lab 8: Observe Sensor Updates During Heavy Command Load

The sensor callout and the command callout share the same mutex. A long-running load test might starve the sensor of updates if the lock is held constantly. Check this.

```text
# sysctl dev.myfirst.0.reg_delay_ms_set=5          # very fast commands
# # Start a long load test in the background.
# (for i in $(seq 1 5000); do
    echo -n "X" > /dev/myfirst0 2>/dev/null
  done) &

# # Meanwhile, poll the sensor.
# while kill -0 $! 2>/dev/null; do
    sysctl dev.myfirst.0.reg_sensor
    sleep 0.2
  done
```

Each polled sensor value should differ from the previous (the sensor callout is running). If the values freeze during the load (all identical for many reads), the lock is being held too long and the sensor is starving. A correct driver sees smoothly updating sensor values even under heavy command load, because the command path drops the lock during `pause_sbt`.

### Lab 9: Build and Run the hwsim2 Module

A stand-alone version of the Chapter 17 simulation, `hwsim2`, lives in the companion sources. Build and load it.

```text
# cd examples/part-04/ch17-simulating-hardware/hwsim2-standalone
# make clean && make
# kldload ./hwsim2.ko

# # The hwsim2 module exposes a single register block with sensor
# # updates and command cycle; no cdev, just sysctls.
# sysctl dev.hwsim2.sensor
# sleep 1
# sysctl dev.hwsim2.sensor                         # different value
# sysctl dev.hwsim2.do_command=0x12345678
# sleep 0.6
# sysctl dev.hwsim2.result                         # 0x12345678

# kldunload hwsim2
```

The `hwsim2` module is about 150 lines of C. Reading it in one sitting is a useful consolidation of Chapter 17's material.

### Lab 10: Inject a Memory-Corruption Attack (Debug Kernel)

A deliberate break-and-observe exercise. Modify the simulation's sensor callback to write one byte past the end of the register block. This should fire the `KASSERT` on a debug kernel.

In `myfirst_sim.c`, temporarily add to `myfirst_sim_sensor_cb`:

```c
/* DELIBERATE BUG for Lab 10. Remove after testing. */
CSR_WRITE_4(sc, 0x80, 0xdead);   /* 0x80 is past the 64-byte region */
```

Rebuild and load. The kernel should panic within 100 ms (the sensor callback fires, the KASSERT catches the out-of-bounds offset, the panic string names offset `0x80`).

Remove the bug. Rebuild. Verify the driver runs cleanly again.

This lab shows the value of bounds assertions in the hardware accessors: an out-of-bounds access fires immediately instead of silently corrupting unrelated memory. Production code should never remove these assertions.



## Challenge Exercises

Challenges extend the Chapter 17 material with optional depth work. Each takes one to four hours and exercises judgment, not just keystrokes.

### Challenge 1: Read-to-Clear `INTR_STATUS`

Chapter 17's `INTR_STATUS` is still RW, not RC. Implement RC semantics: when the driver reads `INTR_STATUS`, return the current value and then clear it. Add a hook in the accessor path that recognises RC registers and treats them specially. Update `HARDWARE.md`.

Think about: how does the driver ensure the read happens under the lock? What happens if a sysctl reads `INTR_STATUS` (inadvertently clearing state the driver needed)?

### Challenge 2: Add a Queue of Commands

The current simulation rejects overlapping commands. Real devices often queue them. Implement a small command queue: if a command arrives while another is pending, add it to the queue; when the current command completes, start the next. Limit the queue to 4 entries.

Think about: how does the driver wait for queued commands? Should the driver have its own view of the queue, or rely on the simulation's view?

### Challenge 3: Simulate a Sampling Rate Drift

The sensor callout fires at a fixed interval. Real sensors often drift: the interval shifts slightly over time because of temperature or voltage changes. Modify the sensor callback to add a small random perturbation to each interval (say, +/- 10%). Observe how this affects the driver.

Think about: does the drift matter for a driver that reads the sensor value? Does it matter for a driver that cares about sample timing (the first driver does not; a later driver might)?

### Challenge 4: A Write-One-to-Clear `INTR_STATUS`

W1C semantics are a common variant of RC. Implement W1C for `INTR_STATUS`: writing a 1 to a bit clears that bit; writing a 0 has no effect; reads return the current value without side effects. Contrast with the RC implementation in Challenge 1.

Think about: which is more convenient for the driver (reading-to-clear or writing-to-clear)? Which is more defensive against accidental debug reads?

### Challenge 5: Error Recovery Without Reset

The Chapter 17 driver's recovery path clears simulation-specific state directly. Real hardware cannot do this; the driver must use only register-level operations. Rewrite `myfirst_recover_from_stuck` to use only register writes (for example, a `CTRL.RESET` bit). Adjust the simulation to honour `CTRL.RESET`.

Think about: how does the driver wait for the reset to complete? Does the reset affect other registers (for example, does it clear the fault mask)?

### Challenge 6: A User-Space Debugger

Write a small user-space program, `myfirstctl`, that opens `/dev/myfirst0`, issues a series of ioctls to control the simulation, and displays the register state. Make it interactive: the user can type commands like `status`, `go X`, `delay 100`, `fault timeout 50%`.

Think about: how does the user-space program communicate with the driver (ioctl, sysctl, read/write)? What is a sensible command syntax?

### Challenge 7: A DTrace-Driven Fault Injection

DTrace can provide dynamic probes. Add a DTrace probe at the top of `myfirst_sim_start_command` that takes a pointer to `sc` as its argument. Write a D script that, based on some user-specified condition (for example, "every 100th call"), sets `sc->sim->pending_fault = MYFIRST_FAULT_ERROR`. The script injects faults from user space without the kernel's fault-injection framework.

Think about: can DTrace safely modify kernel state like this? What are the limits of what DTrace can do?

### Challenge 8: Simulate Two Devices

The current driver has a single device instance. Modify it so that two `/dev/myfirstN` files exist, each with its own simulation. Verify that operations on one device do not affect the other.

Think about: what changes in attach? How does the per-device sysctl tree work? Does the simulation need per-device random state?



## Troubleshooting Reference

A quick reference for the problems Chapter 17's simulation is most likely to surface.

### The sensor does not change

- `sim_running` is 0. Toggle it with the writeable sysctl.
- `SENSOR_CONFIG`'s interval field is zero. Set it to a positive value.
- The sensor callout is not scheduled. Confirm `myfirst_sim_enable` was called. Check `dmesg` for error messages.
- `sc->sim` is NULL. Confirm `myfirst_sim_attach` ran successfully.

### Commands always time out

- `DELAY_MS` is set very high. Reduce it or increase the command timeout.
- The `FAULT_TIMEOUT` bit is set in `FAULT_MASK`. Clear it.
- The `FAULT_STUCK_BUSY` bit is set. Clear it and wait ~100 ms for the busy callout to stop re-asserting.
- The simulation is disabled (`sim_running=0`). Enable it.
- A previous command got stuck and was not recovered. Check `command_pending` (through the sim sysctl) and manually reset if needed.

### `WITNESS` warns about lock order

- The detach path is acquiring locks in the wrong order. Compare the stack trace against `LOCKING.md`.
- A new code path is taking `sc->mtx` after a sleep lock. The ordering from Chapter 15 must be preserved.

### Kernel panic on `kldunload`

- The simulation callouts were not drained before `free`. Check that `callout_drain` runs before `free(sim)`.
- The simulation state was freed while a callout was in flight. The drain is the fix.
- The hardware layer was freed before the simulation layer. The Chapter 17 ordering (sim detach, then hw detach) must be followed.

### Memory usage grows over time

- `vmstat -m | grep myfirst` shows the driver's allocation size climbing. This is a leak. Check the fault-injection error paths; they often neglect to free something.
- The access log is allocated dynamically in some configurations. Confirm it is freed on detach.

### The access log shows unexpected entries

- A callout is running on its own cadence; all Chapter 17 callouts are visible in the log. If the log shows callouts firing faster than expected, check the interval.
- A user-space tool (`watch -n 0.1 sysctl ...`) is polling very fast. Each sysctl access goes through the accessors.

### Fault injection probability does not match observations

- `FAULT_PROB` is scaled as 0 to 10000. A value of 50 is 0.5%, not 50%. For 50%, use 5000.
- The fault only applies to operations that trigger `myfirst_sim_should_fault`. Not every operation is a candidate; sensor updates, for example, do not go through that path.

### `sim_running` toggles ignored

- The sysctl handler may not call `myfirst_sim_enable` or `myfirst_sim_disable` under the lock. Confirm the handler acquires `sc->mtx` before delegating.

### Commands succeed but take much longer than expected

- `cmd_timeout_ms` is set very high; any command that stalls makes the timeout budget huge.
- The fault mask is set with `STUCK_BUSY` and a successful command unexpectedly becomes a `wait_for_ready` stall until the next fault-clear.

### Load tests are slower than expected

- Commands are serialised through the driver mutex. N writers at M commands each take N*M * DELAY_MS total time.
- Reducing `DELAY_MS` speeds up the simulation; reducing `cmd_timeout_ms` does not speed anything up unless timeouts are actually occurring.



## Wrapping Up

Chapter 17 opened with a static register block from Chapter 16 and closes with a driver that behaves, end to end, like a driver talking to a functional device. The simulation is dynamic: a sensor register updates on its own, commands schedule delayed completions, `STATUS` bits change over time, and fault injection produces controlled failures that exercise the driver's error paths. The driver handles all of this through the register vocabulary Chapter 16 taught, with the synchronisation discipline Part 3 built, and with the timing primitives Section 6 introduced.

What Chapter 17 deliberately did not do: real PCI (Chapter 18), real interrupts (Chapter 19), real DMA (Chapters 20 and 21). Each of those topics deserves its own chapter; each will extend the driver in specific ways while reusing the simulation framework where appropriate.

The version is `1.0-simulated`. The file layout has grown: `myfirst.c`, `myfirst_hw.c`, `myfirst_hw.h`, `myfirst_sim.c`, `myfirst_sim.h`, `myfirst_sync.h`, `cbuf.c`, `cbuf.h`. The documentation has grown: `LOCKING.md`, `HARDWARE.md`, and the new `SIMULATION.md`. The test suite now exercises every simulation behaviour, every fault mode, and every error path.

### A Reflection Before Chapter 18

A pause before the next chapter. Chapter 17 taught simulation as a technique that extends beyond learning. The patterns you practised here (callout-driven state changes, command-response protocols, fault injection, recovery paths) are patterns you will use throughout your driver-writing life. They apply as much to a production storage driver's test harness as to this tutorial driver. The simulation skill is permanent.

Chapter 17 also taught the discipline of thinking like hardware. Designing the register map, choosing access semantics, timing the responses, deciding what to latch and what to clear: these are decisions the hardware team makes for real devices, and a driver author who understands them reads datasheets differently. The next time you encounter a real device's datasheet, you will notice the decisions the designers made, and you will have a framework for evaluating whether the decisions were wise.

Chapter 18 shifts the scene entirely. The simulation goes away (temporarily); a real PCI device takes its place. The register block moves from `malloc(9)` memory to a PCI BAR. The accessors switch from `X86_BUS_SPACE_MEM` to `rman_get_bustag` and `rman_get_bushandle`. The driver's high-level code does not change at all, because the Chapter 16 abstraction is portable across the change. What does change is the device's behaviour: a real virtio device in a VM has its own protocol, its own timing, its own quirks. The patterns Chapter 17 taught are what let you handle all of that.

### What to Do If You Are Stuck

A few suggestions if the Chapter 17 material feels dense.

First, re-read Section 3. The callout pattern is the foundation of every dynamic behaviour in the chapter. If the interaction between `callout_init_mtx`, `callout_reset_sbt`, `callout_stop`, and `callout_drain` feels opaque, everything downstream is opaque. Chapter 13 is the other good reference.

Second, run Lab 1 and Lab 2 by hand, one step at a time. Watching the sensor breathe and tracing a single command through the access log is where the simulation's behaviour becomes concrete.

Third, skip the challenges on first pass. The labs are calibrated for Chapter 17; the challenges assume the chapter's material is already solid. Come back to them after Chapter 18 if they feel out of reach now.

Fourth, open `myfirst_sim.c` and read the three callouts (`sensor`, `command`, `busy`) in order. Each is a self-contained piece of code that illustrates one aspect of the simulation's design. If you can explain each one to a colleague (or to a rubber duck), you have internalised Chapter 17's core.

Chapter 17's goal was to make the register block come alive. If it has, the rest of Part 4 will feel like a natural progression: Chapter 18 trades the simulation for real hardware, Chapter 19 adds real interrupts, Chapters 20 and 21 add DMA. Each chapter builds on what Chapter 17 established.



## Bridge to Chapter 18

Chapter 18 is titled *Writing a PCI Driver*. Its scope is the real-hardware path that Chapter 17 deliberately did not take: a driver that probes a PCI bus, matches a real device by vendor and device ID, claims the device's BAR as a memory resource, maps it through `bus_alloc_resource_any`, and talks to the mapped region through the same `CSR_*` macros the Chapter 16 and Chapter 17 drivers already use.

Chapter 17 prepared the ground in four specific ways.

First, **you have a complete driver**. The Chapter 17 driver at `1.0-simulated` exercises every common protocol pattern: command-response, delayed completion, status polling, error recovery, timeout handling. Chapter 18 will swap the register backend without changing the driver's high-level logic. The swap is a one-function change in `myfirst_hw_attach`; everything else stays put.

Second, **you have a fault model**. The Chapter 17 fault-injection framework taught the driver to handle errors. Real PCI devices produce real errors: bus timeouts, uncorrectable ECC errors, power-state transitions, link drops. The discipline of handling simulated errors is what will let the Chapter 18 driver handle real ones.

Third, **you have a timing model**. Chapter 17 taught the reader when to use `DELAY(9)`, `pause_sbt(9)`, and `callout_reset_sbt(9)`. Real PCI devices have their own timing requirements, often documented in their datasheets. The discipline Chapter 17 built is what will let the driver respect those timings in Chapter 18.

Fourth, **you have a documentation habit**. `HARDWARE.md`, `LOCKING.md`, and `SIMULATION.md` are three living documents that the driver's contributors maintain. Chapter 18 will add a fourth: `PCI.md` or a similar document describing the specific PCI device the driver targets, its vendor and device ID, its BAR layout, and any quirks. The habit of documenting is established; Chapter 18 extends it.

Specific topics Chapter 18 will cover:

- The PCI subsystem in FreeBSD: `pci(4)`, the bus-device-function tuple, `pciconf -lv`.
- The probe and attach lifecycle: `DRIVER_MODULE`, vendor and device ID matching, the `probe` and `attach` methods.
- Resource allocation: `bus_alloc_resource_any`, the resource specification, `SYS_RES_MEMORY`, `RF_ACTIVE`.
- BAR mapping: how the PCI BAR becomes a `bus_space` region; `rman_get_bustag` and `rman_get_bushandle`.
- Enabling bus mastering: `pci_enable_busmaster` and when it is necessary.
- Attach-time initialisation and detach cleanup.
- Testing against a virtio device in a VM: the FreeBSD guest on `qemu` or `bhyve`, how to pass through a synthetic device, how to verify the driver attaches and behaves.

You do not need to read ahead. Chapter 17 is sufficient preparation. Bring your `myfirst` driver at `1.0-simulated`, your `LOCKING.md`, your `HARDWARE.md`, your `SIMULATION.md`, your `WITNESS`-enabled kernel, and your test kit. Chapter 18 starts where Chapter 17 ended.

A small closing reflection. Part 3 taught the synchronisation vocabulary and produced a driver that coordinated itself. Chapter 16 gave the driver a register vocabulary. Chapter 17 gave the register block dynamic behaviour and the driver a fault model. Chapter 18 will retire the simulation and connect the driver to real silicon. Each step has built on the last; the driver that walks into Chapter 18 is a driver that is almost production-grade, needing only the real-bus glue that Chapter 18 provides.

The hardware conversation is deepening. The vocabulary is yours; the protocol is yours; the discipline is yours. Chapter 18 adds the last missing piece.



## Reference: The Chapter 17 Simulation Cheat Sheet

A one-page summary of the Chapter 17 simulation API and the sysctls it exposes, for quick reference while coding.

### Simulation API (in `myfirst_sim.h`)

| Function                            | Purpose                                              |
|-------------------------------------|------------------------------------------------------|
| `myfirst_sim_attach(sc)`            | Allocate sim state, register callouts (not started). |
| `myfirst_sim_detach(sc)`            | Drain callouts, free sim state.                       |
| `myfirst_sim_enable(sc)`            | Start the callouts. Requires `sc->mtx` held.          |
| `myfirst_sim_disable(sc)`           | Stop the callouts (without drain).                    |
| `myfirst_sim_start_command(sc)`     | Triggered by `CTRL.GO` write; schedules completion.   |
| `myfirst_sim_add_sysctls(sc)`       | Register simulation-specific sysctls.                 |

### Register Additions

| Offset | Register         | Access | Purpose                                            |
|--------|------------------|--------|----------------------------------------------------|
| 0x28   | `SENSOR`         | RO     | Simulated sensor value, updated by callout.         |
| 0x2c   | `SENSOR_CONFIG`  | RW     | Interval (low 16) and amplitude (high 16).          |
| 0x30   | `DELAY_MS`       | RW     | Command processing delay in milliseconds.           |
| 0x34   | `FAULT_MASK`     | RW     | Bitmask of enabled fault types.                     |
| 0x38   | `FAULT_PROB`     | RW     | Fault probability, 0 to 10000 (10000 = 100%).       |
| 0x3c   | `OP_COUNTER`     | RO     | Count of commands processed.                        |

### CTRL Additions

| Bit | Name                     | Purpose                                      |
|-----|--------------------------|----------------------------------------------|
| 9   | `MYFIRST_CTRL_GO`        | Start a command. Self-clearing.              |

### Fault Modes

| Bit | Name                       | Effect                                          |
|-----|----------------------------|-------------------------------------------------|
| 0   | `MYFIRST_FAULT_TIMEOUT`    | Command never completes.                        |
| 1   | `MYFIRST_FAULT_READ_1S`    | `DATA_OUT` returns `0xFFFFFFFF`.                |
| 2   | `MYFIRST_FAULT_ERROR`      | `STATUS.ERROR` set instead of `DATA_AV`.        |
| 3   | `MYFIRST_FAULT_STUCK_BUSY` | `STATUS.BUSY` latched on continuously.          |

### New Sysctls

| Sysctl                                      | Type | Purpose                                             |
|---------------------------------------------|------|-----------------------------------------------------|
| `dev.myfirst.0.sim_running`                 | RW   | Enable/disable the simulation.                       |
| `dev.myfirst.0.sim_sensor_baseline`         | RW   | Baseline sensor value.                              |
| `dev.myfirst.0.sim_op_counter_mirror`       | RO   | Mirror of `OP_COUNTER`.                             |
| `dev.myfirst.0.reg_delay_ms_set`            | RW   | Writable `DELAY_MS`.                                |
| `dev.myfirst.0.reg_sensor_config_set`       | RW   | Writable `SENSOR_CONFIG`.                           |
| `dev.myfirst.0.reg_fault_mask_set`          | RW   | Writable `FAULT_MASK`.                              |
| `dev.myfirst.0.reg_fault_prob_set`          | RW   | Writable `FAULT_PROB`.                              |
| `dev.myfirst.0.cmd_timeout_ms`              | RW   | Command completion timeout.                         |
| `dev.myfirst.0.rdy_timeout_ms`              | RW   | Device-ready polling timeout.                       |
| `dev.myfirst.0.cmd_successes`               | RO   | Successful commands.                                |
| `dev.myfirst.0.cmd_rdy_timeouts`            | RO   | Ready-wait timeouts.                                |
| `dev.myfirst.0.cmd_data_timeouts`           | RO   | Data-wait timeouts.                                 |
| `dev.myfirst.0.cmd_errors`                  | RO   | Commands that reported an error.                    |
| `dev.myfirst.0.cmd_recoveries`              | RO   | Recovery invocations.                               |
| `dev.myfirst.0.fault_injected`              | RO   | Total faults injected.                              |



## Reference: Timing Primitive Cheat Sheet

| Primitive                | Cost                 | Cancelable | Appropriate Range       |
|--------------------------|----------------------|------------|-------------------------|
| `DELAY(us)`              | Busy-wait, full CPU  | No         | < 100 us                |
| `pause_sbt(..., sbt, ...)` | Sleep, yields CPU  | No (not interruptible in this form) | 100 us - seconds        |
| `callout_reset_sbt(...)` | Scheduled callback   | Yes        | fire-and-forget, periodic |
| `cv_timedwait_sbt(...)`  | Sleep on condition   | Yes (via cv_signal) | waits that can be shortened |

Contexts where each is legal:

- `DELAY`: any (including filter interrupts, spin mutexes).
- `pause_sbt`: process context, spin locks not held.
- `callout_reset_sbt`: any (the callback runs in callout context).
- `cv_timedwait_sbt`: process context, specific mutex held.



## Reference: Anatomy of a Simulation Callout

A template for adding a new simulation behaviour, modelled on the Chapter 17 sensor callout.

### Step 1: Declare the callout in the sim state

```c
struct myfirst_sim {
        /* ... existing fields ... */
        struct callout   my_new_callout;
        int              my_new_interval_ms;
};
```

### Step 2: Initialise the callout at attach

```c
/* In myfirst_sim_attach: */
callout_init_mtx(&sim->my_new_callout, &sc->mtx, 0);
sim->my_new_interval_ms = 200;   /* default */
```

### Step 3: Write the callback

```c
static void
myfirst_sim_my_new_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct myfirst_sim *sim = sc->sim;

        MYFIRST_LOCK_ASSERT(sc);

        if (!sim->running)
                return;

        /* Do the work: update a register, signal a condition, ...  */
        CSR_UPDATE_4(sc, MYFIRST_REG_SENSOR, 0, 0x100);

        /* Re-arm. */
        callout_reset_sbt(&sim->my_new_callout,
            sim->my_new_interval_ms * SBT_1MS, 0,
            myfirst_sim_my_new_cb, sc, 0);
}
```

### Step 4: Start the callout at enable

```c
/* In myfirst_sim_enable: */
callout_reset_sbt(&sim->my_new_callout,
    sim->my_new_interval_ms * SBT_1MS, 0,
    myfirst_sim_my_new_cb, sc, 0);
```

### Step 5: Stop it at disable, drain at detach

```c
/* In myfirst_sim_disable: */
callout_stop(&sim->my_new_callout);

/* In myfirst_sim_detach, after releasing the lock: */
callout_drain(&sim->my_new_callout);
```

### Step 6: Document in SIMULATION.md

Add an entry to the callout-map table:

```text
| Callout           | Interval         | Purpose                       |
|-------------------|------------------|-------------------------------|
| my_new_callout    | my_new_interval_ms | ... explain ...              |
```

Six steps. Each is mechanical. The pattern is the same for every simulation behaviour; once you have internalised the template, adding new behaviours is quick and reliable.



## Reference: Test Scripts Inventory

A short catalogue of the test scripts that ship under `examples/part-04/ch17-simulating-hardware/labs/`.

| Script                            | Purpose                                              | Typical Runtime |
|-----------------------------------|------------------------------------------------------|-----------------|
| `sim_sensor_oscillates.sh`        | Confirm `SENSOR` value changes over time.            | ~10 s           |
| `sim_command_cycle.sh`            | Run 50 commands, verify `OP_COUNTER` matches.        | ~30 s           |
| `sim_timeout_fault.sh`            | Enable timeout fault; verify recovery.               | ~5 s            |
| `sim_error_fault.sh`              | Enable error fault; verify `cmd_errors` grows.       | ~10 s           |
| `sim_stuck_busy_fault.sh`         | Enable stuck-busy fault; verify timeout on ready.    | ~10 s           |
| `sim_mixed_faults_under_load.sh`  | 2% random faults, 8 workers, 100 commands each.      | ~30 s           |
| `sim_sensor_during_load.sh`       | Verify sensor updates during heavy command load.     | ~30 s           |
| `full_regression_ch17.sh`         | All of the above plus Chapter 11-16 regression.      | ~3 minutes      |

Each script exits with a non-zero status if something unexpected happened. A successful `full_regression_ch17.sh` means every Chapter 17 behaviour has been validated and no regressions have been introduced.



## Reference: An Honest Accounting of Chapter 17's Simplifications

A chapter that teaches a slice of a large topic inevitably simplifies. For honesty with the reader, a catalogue of what Chapter 17 simplified and what the full story looks like.

### The Callout's Mutex Interaction

Chapter 17 uses `callout_init_mtx` so every simulation callback runs with `sc->mtx` held. This is the simplest and safest pattern but not the only one. Real drivers sometimes use `callout_init` (no associated mutex) and acquire the lock inside the callback; this gives finer control over locking but is more error-prone. Some real drivers use `callout_init_rm` (for reader-writer locks) or even `callout_init_mtx` with a spin mutex for cases where the callback must run in hardirq context.

The full story: FreeBSD's callout API supports several lock styles, each suited to a different concurrency model. The `callout_init_mtx` choice in Chapter 17 is pedagogically clean; production drivers pick the style that matches their specific locking graph. `/usr/src/sys/dev/e1000/if_em.c` uses `callout_init_mtx` for its tick callout and `callout_init` for its spinlock-guarded DMA-polling path, illustrating the difference.

### The Fault-Injection Framework

Chapter 17's fault injection is limited to four modes, a probability, and a simple `should_fault` check at the start of each operation. Real fault-injection frameworks (FreeBSD's own `fail(9)`, for example) support a richer vocabulary: callsite-specific injection, probability curves that change over time, deterministic injection for the Nth call, and injection paths that can be triggered from user space by name.

The full story: the `fail(9)` framework in `/usr/src/sys/kern/kern_fail.c` implements a production-grade fault-injection system. Chapter 17's framework is a simplified version that focuses on the simulation's specific needs. A challenge at the end of the chapter invites the reader to replace the chapter's ad-hoc framework with a `fail(9)`-based one.

### The Random Number Source

Chapter 17 uses `arc4random_uniform(10000)` as the fault-probability decision source. This is non-deterministic: two runs of the same test will produce different fault patterns. For reproducible testing, a driver author would use a deterministic source (a simple LCG seeded by a fixed value, or a counter) and expose the seed through a sysctl.

The full story: reproducible fault injection is important for regression testing. A driver that passes all faults at seed `0x12345` one day and fails the same seed next week has a bug that was not there before. Chapter 17's non-determinism is acceptable for teaching but insufficient for serious regression suites.

### The Command Queue

Chapter 17 allows only one command in flight at a time. Real devices often queue commands; a network driver might have thousands of in-flight descriptors. The single-command restriction simplifies the simulation's state machine and the driver's locking story, but it limits the throughput of the tutorial driver.

The full story: a queued command model requires per-command state (status, arrival time, result), a queue data structure with its own synchronisation, and a driver pattern that handles command coalescing, batch completion, and out-of-order completion. Chapter 20's DMA descriptor rings introduce the pattern for DMA-based devices; real command-queue drivers extend it to non-DMA contexts too.

### The Sensor Model

Chapter 17's sensor produces a triangle-wave value using a simple arithmetic formula. Real sensors produce values influenced by physical noise, temperature drift, sampling jitter, and device-specific non-linearities. A more realistic simulation would include Gaussian noise, a slow drift term, and occasional outliers.

The full story: sensor modelling is a discipline of its own, and real driver testing for sensor devices often uses captured real-world data played back through a simulation layer. Chapter 17's arithmetic sensor is pedagogically sufficient; a production simulation would be substantially richer.

### The Error Taxonomy

Chapter 17 distinguishes four fault types: timeout, read-1s, error, stuck-busy. Real hardware produces a much wider range of failure modes, including partial writes, stuck interrupt bits, inverted fields, off-by-one register values, and transient electrical noise. The four chapter faults are a starting point.

The full story: FreeBSD driver authors who work with specific device families develop much more elaborate fault taxonomies over time. The e1000 driver's error-handling code is a few hundred lines of its own, handling dozens of distinct failure modes. Chapter 17 teaches the pattern; applying it to real hardware produces a richer taxonomy by necessity.

### The Lack of Interrupts

Chapter 17's driver polls `STATUS` by reading the register in a loop (with `pause_sbt` between polls). Real drivers use interrupts: the device raises an interrupt when `STATUS` changes, the driver's interrupt handler wakes the waiting thread, the thread reads the new state. Polling is wasteful compared to interrupts.

The full story: Chapter 19 introduces interrupts. The Chapter 17 driver will get an `INTR_STATUS`-driven wake-up path in Chapter 19, and the polling loops will become much shorter (wake-once rather than wake-every-millisecond). Chapter 17's polling is a pedagogical stepping stone.

### The Timing Precision

Chapter 17's callouts run at approximately the configured interval, with precision on the order of 1 ms (the kernel's default timer resolution). Real devices sometimes require sub-microsecond timing. A driver that depends on such timing must use `DELAY(9)` for short waits and cannot rely on `callout` precision.

The full story: high-precision timing in drivers requires platform-specific mechanisms (the TSC, the ACPI-PM timer, the HPET, or hardware-specific timers on embedded platforms). Chapter 17's millisecond-scale simulation does not exercise sub-millisecond paths; a driver for a real device with such requirements would use `DELAY` or a hardware-assisted mechanism.

### Summary

Chapter 17 is a teaching simulation. Every simplification it makes is deliberate, named, and picked up by a later chapter or by a real-driver extension. The patterns Chapter 17 teaches are the patterns every later chapter extends; the discipline Chapter 17 builds is the discipline every later chapter relies on. The chapter is short of the full simulation story on purpose. Subsequent chapters and real-world practice fill in the rest.



## Reference: A Glossary of Chapter 17 Terms

A short glossary of terms introduced or used extensively in Chapter 17.

**Autonomous update.** A change to a register that happens without the driver initiating it. In Chapter 17, the sensor callout produces autonomous updates.

**Command cycle.** The full sequence of events from "driver decides to issue a command" to "command completes and the driver observes the result". In Chapter 17: write `DATA_IN`, set `CTRL.GO`, wait for `STATUS.DATA_AV`, read `DATA_OUT`.

**Callout.** A FreeBSD primitive that schedules a callback to run at a future time. The callback runs on a callout thread. Created with `callout_init_mtx`, armed with `callout_reset_sbt`, cancelled with `callout_stop`, drained with `callout_drain`.

**Fault injection.** The deliberate introduction of a failure condition into the system for testing purposes. Chapter 17's framework supports four fault modes configurable through `FAULT_MASK` and `FAULT_PROB`.

**Latched.** A register bit that, once set, remains set until explicitly cleared. Error bits in real devices are typically latched. Chapter 17's `STATUS.ERROR` is latched.

**Polling.** Repeatedly reading a register to detect a change. Chapter 17's `myfirst_wait_for_bit` polls `STATUS` with 1 ms sleep between reads.

**Pending command.** A command that has been started but not yet completed. Chapter 17's simulation tracks this with `sim->command_pending`.

**Protocol.** The rules the driver must follow when talking to the device. In Chapter 17: write `DATA_IN` before `CTRL.GO`; wait for `STATUS.DATA_AV` before reading `DATA_OUT`; clear `DATA_AV` after reading.

**Read-to-clear (RC).** A register semantic where reading the register returns its current value and then clears it. `INTR_STATUS` on many real devices is RC.

**Simulation backend.** The kernel code that provides the simulated device's behaviour. In Chapter 17, this is `myfirst_sim.c`.

**Side effect.** Behaviour that a register access triggers beyond the obvious read or write. `CTRL.RESET` writes have a side effect (they reset the device). `INTR_STATUS` reads have a side effect (they clear pending bits, under RC semantics).

**Sticky.** Similar to latched. A bit that persists until deliberately cleared.

**Timeout.** A bounded wait that returns an error if the expected event does not occur in time. Chapter 17's `wait_for_bit` timeouts after the configured number of milliseconds.



## Reference: The Chapter 17 Driver Diff Summary

A compact summary of what Chapter 17 changes in the driver, for readers who want to see at a glance how the driver evolved from `0.9-mmio` to `1.0-simulated`.

### New files

- `myfirst_sim.c` (about 300 lines).
- `myfirst_sim.h` (about 50 lines).
- `SIMULATION.md` (about 200 lines).

### Modified files

- `myfirst.c`: added `myfirst_sim_attach` and `myfirst_sim_detach` calls; added `myfirst_write_cmd`, `myfirst_sample_cmd`, `myfirst_wait_for_bit` helpers; added timeout fields to softc; added stats counters.
- `myfirst_hw.h`: added new register offsets (0x28 through 0x3c) and bit masks; added `MYFIRST_CTRL_GO`; added fault constants.
- `myfirst_hw.c`: no change to the accessor code; `myfirst_ctrl_update` extended to intercept `GO`.
- `HARDWARE.md`: added the new registers, the new `CTRL.GO` bit, the dynamic-behaviour notes.
- `LOCKING.md`: added the simulation detach step to the ordered detach sequence.
- `Makefile`: added `myfirst_sim.c` to `SRCS`.

### Line count evolution

The file sizes at each stage (approximate):

| Stage                  | myfirst.c | myfirst_hw.c | myfirst_sim.c | Total    |
|------------------------|-----------|--------------|---------------|----------|
| Chapter 16 Stage 4 (start) | 650        | 380           | (not yet)       | 1030      |
| Chapter 17 Stage 1     | 680        | 400           | 120           | 1200     |
| Chapter 17 Stage 2     | 800        | 400           | 150           | 1350     |
| Chapter 17 Stage 3     | 820        | 410           | 180           | 1410     |
| Chapter 17 Stage 4     | 840        | 410           | 250           | 1500     |
| Chapter 17 Stage 5 (final) | 800       | 400           | 300           | 1500     |

The driver grew from roughly 1030 lines to 1500 lines over the chapter. About 470 lines of new code, split roughly 150 in `myfirst.c`, 20 in `myfirst_hw.c`, and 300 in the new `myfirst_sim.c`. The Stage 5 refactor slightly reduced `myfirst.c` by moving some helpers into `myfirst_sim.c`.

### Behavioural additions

- Sensor autonomously updates every 100 ms.
- `CTRL.GO` triggers a command that completes after `DELAY_MS` milliseconds.
- `OP_COUNTER` increments on each command.
- `FAULT_MASK` and `FAULT_PROB` control injected failures.
- Four fault modes (timeout, read-1s, error, stuck-busy).
- Per-outcome statistics counters.
- Configurable command and ready timeouts.
- Command cycle integrated into the write and read paths.

### Test additions

- Eight new test scripts under `labs/`.
- An updated `full_regression.sh` that includes the new scripts.
- Expected runtime of the full regression: about 3 minutes.



## Reference: Reading a Callout-Driven Driver

A guided walk through a real FreeBSD driver whose design is similar to Chapter 17's simulation: a driver whose periodic work is scheduled through callouts, and whose main job is to respond to state changes. The driver is `/usr/src/sys/dev/led/led.c`, the LED pseudo-device driver.

Open it in one terminal. Follow along.

### The LED Driver's Structure

`led.c` is about 400 lines. Its job is to expose an interface for other drivers to announce an LED device through `/dev/led/NAME`, and for user space to blink the LED by writing patterns. The actual hardware work is delegated to whichever driver registered the LED; `led.c` does the scheduling and the pattern matching.

The key data structures:

- `struct led_softc`: per-LED state, including a `struct callout led_ch`, a `struct sbuf *spec` holding the blink pattern, and a function pointer to the driver's "set state" callback.
- `struct mtx led_mtx`: a global mutex protecting the list of all LEDs.
- A linked list of all LEDs.

### The Callout Callback

The blink-pattern interpreter runs in the callout callback:

```c
static void
led_timeout(void *p)
{
        struct ledsc *sc = p;
        char c;
        int count;

        if (sc->spec == NULL || sc->ptr == NULL || sc->count == 0) {
                /* no pattern; stop blinking */
                sc->func(sc->private, sc->on);
                return;
        }

        c = *(sc->ptr)++;
        if (c == '.') {
                /* Pattern complete, restart. */
                sc->ptr = sbuf_data(sc->spec);
                c = *(sc->ptr)++;
        }

        if (c >= 'a' && c <= 'j') {
                sc->func(sc->private, 0);   /* LED off */
                count = (c - 'a') + 1;
        } else if (c >= 'A' && c <= 'J') {
                sc->func(sc->private, 1);   /* LED on */
                count = (c - 'A') + 1;
        } else {
                count = 1;
        }

        callout_reset(&sc->led_ch, count * hz / 10, led_timeout, sc);
}
```

(The code is lightly abridged from the real source for presentation.)

### The Pattern

Look at the shape of the callback.

First, it checks whether there is anything to do. If `spec == NULL`, the pattern has been cleared; the function turns the LED off (to a known state) and returns without re-arming. This is exactly the `if (!sim->running) return;` pattern in Chapter 17's callouts.

Second, it advances through the pattern buffer one character at a time. The pattern language uses lowercase letters 'a' through 'j' to mean "LED off for 100 ms through 1000 ms" and uppercase 'A' through 'J' for "LED on". This is a small state machine driven by the callout.

Third, it calls `sc->func(sc->private, state)` to actually toggle the LED. This is the hardware work, delegated to whichever driver registered the LED. The `led_timeout` callback does not know whether the LED is a GPIO pin, an I2C-attached LED controller, or a message to a remote device; it just calls the function pointer.

Fourth, it re-arms the callout with an interval determined by the current pattern character. This is Chapter 17's `callout_reset_sbt` pattern, though `led.c` uses the older `callout_reset` with tick-based intervals.

### Lessons for Chapter 17

The `led.c` driver illustrates several Chapter 17 patterns:

- A callout carries a state machine forward one step per invocation.
- The callout re-arms itself with a variable interval based on the current state.
- A "no work to do" branch returns without re-arming; the detach path relies on this to eventually drain.
- The callback uses a function pointer for delegated work, keeping the callout logic separate from the hardware-specific work.

Chapter 17's simulation uses the same patterns in slightly different clothes. The sensor callback updates a register; the command callback triggers a completion; the busy callback re-asserts a status bit. Each is a small state machine advanced by the callout.

### An Exercise

Read `led.c` start to finish. It is one of the shortest drivers in `/usr/src/sys/dev/` and illustrates a clean callout-driven design. After reading, write a one-paragraph summary explaining how `led_timeout` interacts with `led_drvinit`, `led_destroy`, and `led_write`. If you can articulate the relationship, you have internalised the callout-driven-driver pattern.



## Reference: The Difference Between "In Simulation" and "In Reality"

Chapter 17 simulates a device. A natural question is: how different is the simulation from a real device, and what are the specific ways the driver would need to change when the two are swapped?

A short comparison.

### What the simulation and reality have in common

- The register map: the same offsets, the same widths, the same access types.
- The CSR macros: `CSR_READ_4`, `CSR_WRITE_4`, `CSR_UPDATE_4` expand to the same `bus_space_*` calls.
- The driver's command-cycle logic: write `DATA_IN`, set `CTRL.GO`, wait for `DATA_AV`, read `DATA_OUT`.
- The locking discipline: `sc->mtx` protects register access in both cases.
- The statistics: `cmd_successes`, `cmd_errors`, and so on track real events either way.

### What differs

- The tag and handle: simulation uses `X86_BUS_SPACE_MEM` and a `malloc(9)` address; reality uses the tag and handle that `rman_get_bustag` and `rman_get_bushandle` return for an allocated PCI BAR.
- The register-block lifetime: simulation's lives from `myfirst_sim_attach` to `myfirst_sim_detach`; reality's lives from `bus_alloc_resource_any` to `bus_release_resource`.
- The autonomous behaviour: simulation produces it through callouts; reality produces it through device internal logic. The driver does not see the difference.
- The fault modes: simulation's faults are deliberate and controlled; reality's faults happen when they happen.
- The timing: simulation's `DELAY_MS` is a register; reality's timing is determined by the device's design and cannot be changed.
- The `CTRL.GO` self-clear: simulation's is implemented by the write intercept; reality's is implemented in silicon. The driver expects the same behaviour.
- The error recovery: simulation's `myfirst_recover_from_stuck` directly manipulates the simulation state; reality's recovery path uses only register operations (typically a reset).

The list is short. That is the point of the abstraction: most of the driver is identical, and the parts that are not are well-localised.

### The Chapter 18 Change

When Chapter 18 replaces the simulation with real PCI, the changes to the driver are:

1. `myfirst_hw_attach` changes: instead of `malloc`-ing the register block, it calls `bus_alloc_resource_any` to allocate the PCI BAR.
2. `myfirst_hw_detach` changes: instead of `free`-ing the register block, it calls `bus_release_resource` to release the BAR.
3. The simulation files (`myfirst_sim.c`, `myfirst_sim.h`) are no longer built; the Makefile drops them.
4. The driver's probe logic gains a `probe` method that matches on the real device's vendor and device ID.
5. The `DRIVER_MODULE` registration changes from a pseudo-device style to a PCI attachment style.

Everything else (the command cycle, the statistics, the locking, the documentation) stays the same. The discipline Chapter 17 built is what makes this possible.



## Reference: When Simulation Is Not Enough

A balanced view of simulation's limits. Chapter 17 teaches simulation as a primary technique, but it is not a substitute for every kind of testing. Several scenarios require real hardware or a hypervisor-backed virtual device.

### When the bug is in the real-silicon timing

A race condition that manifests only at specific bus clock ratios, specific memory traffic patterns, or specific device firmware revisions cannot be reproduced in simulation. The simulation runs at kernel-thread speed, not at hardware speed, and its timing is dictated by the kernel's scheduler, not by the bus fabric.

### When the bug is in the platform-specific code

A driver that needs to handle IOMMU remapping, specific cache attributes, or architecture-specific memory barriers cannot be fully validated in simulation. The simulation's kernel memory has different cache behaviour than real device memory, and the architecture-specific paths are exercised only when real device memory is involved.

### When the bug depends on actual hardware resources

A driver that needs to configure a PCIe link width, manage power states, or interact with the device's firmware cannot be validated in simulation. The simulation has no PCIe link, no power states, and no firmware.

### When the bug is in the test itself

A test that exercises the simulation in a way that does not correspond to how the real device is used produces false confidence. The simulation might pass the test; the real device might still fail under realistic use. Testing against simulation must be paired with testing against reality whenever reality is available.

### What to do about it

The right approach is layered testing. Simulation catches protocol bugs, lock-order bugs, error-handling bugs, and the majority of logic errors. Hypervisor-backed virtual devices (virtio in bhyve or qemu) catch PCI-specific bugs, bus-mastering issues, and some timing issues. Real hardware on a lab system catches the last-mile bugs that nothing else can reproduce.

For Chapter 17 the focus is simulation. For Chapter 18 the focus is hypervisor-backed PCI. Later chapters on DMA and interrupts will introduce additional testing surfaces. Real-hardware testing on a dedicated lab system is a discipline the working driver author develops over time; this book can give you the vocabulary but not the hardware.



## Reference: Further Reading

If you want to go deeper into the topics Chapter 17 touched, the following are good next steps.

### FreeBSD manual pages

- `callout(9)`: the callout API in full.
- `pause(9)` and `pause_sbt(9)`: the sleep primitives in detail.
- `arc4random(9)`: the pseudo-random number generator.
- `bus_space(9)`: revisit with the simulation lens.
- `fail(9)`: the production fault-injection framework.

### Source files worth reading

- `/usr/src/sys/dev/led/led.c`: a callout-driven pseudo-device driver. Short, readable, and illustrative.
- `/usr/src/sys/dev/random/random_harvestq.c`: a callout-driven harvest queue. More complex than `led.c` but with a similar pattern.
- `/usr/src/sys/kern/kern_fail.c`: the `fail(9)` framework. About 1500 lines but highly modular.
- `/usr/src/sys/kern/kern_timeout.c`: the callout subsystem implementation. Read when you want to understand how `callout_reset_sbt` actually works.

### Real drivers with callouts similar to Chapter 17's

- `/usr/src/sys/dev/ale/if_ale.c`: `ale_tick` is a callout that periodically checks link state. Similar pattern to the Chapter 17 sensor callout.
- `/usr/src/sys/dev/e1000/if_em.c`: `em_local_timer` is a callout that updates statistics and handles watchdog events. Slightly more elaborate than the sensor callout.
- `/usr/src/sys/dev/iwm/if_iwm.c`: uses multiple callouts for different protocol state machines. An advanced but educational example.

### Related reading on hardware simulation

- The FreeBSD Handbook chapter on emulation (for understanding how virtio and bhyve relate to in-kernel simulation).
- The `bhyve(8)` manual page, for insight into hypervisor-backed virtual devices that will matter in Chapter 18.



## Reference: A Worked Example: The Full `myfirst_sim.h`

The complete `myfirst_sim.h` as it stands at the end of Chapter 17, for quick reference. The source is also in `examples/part-04/ch17-simulating-hardware/stage5-final/myfirst_sim.h`.

```c
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Edson Brandi
 *
 * myfirst_sim.h -- Chapter 17 simulation API.
 *
 * The simulation layer turns the Chapter 16 static register block into
 * a dynamic device: autonomous sensor updates, command-triggered
 * delayed completions, read-to-clear semantics for INTR_STATUS, and a
 * fault-injection framework. The API is small; most of the simulation
 * is in myfirst_sim.c.
 */

#ifndef _MYFIRST_SIM_H_
#define _MYFIRST_SIM_H_

#include <sys/callout.h>
#include <sys/stdbool.h>

struct myfirst_softc;

/*
 * Simulation state. One per driver instance. Allocated in
 * myfirst_sim_attach, freed in myfirst_sim_detach.
 */
struct myfirst_sim {
        /* The three simulation callouts. */
        struct callout       sensor_callout;
        struct callout       command_callout;
        struct callout       busy_callout;

        /* Last scheduled command's data. Saved so command_cb can
         * latch DATA_OUT when it fires. */
        uint32_t             pending_data;

        /* Saved fault state for this command. Set in start_command,
         * consumed by command_cb. */
        uint32_t             pending_fault;

        /* Whether a command is currently in flight. */
        bool                 command_pending;

        /* Baseline sensor value; the sensor callout oscillates
         * around this. */
        uint32_t             sensor_baseline;

        /* Counter used by the sensor oscillation algorithm. */
        uint32_t             sensor_tick;

        /* Local operation counter; mirrors OP_COUNTER register. */
        uint32_t             op_counter;

        /* Whether the simulation callouts are running. Checked by
         * every callout before doing work. */
        bool                 running;
};

/*
 * API. All functions assume sc->sim is valid (that is, 
 * myfirst_sim_attach has been called successfully) unless noted
 * otherwise.
 */

/*
 * Allocate and initialise the simulation state. Registers callouts
 * with sc->mtx. Does not start the callouts; call _enable for that.
 * Returns 0 on success, an errno on failure.
 */
int  myfirst_sim_attach(struct myfirst_softc *sc);

/*
 * Drain all simulation callouts, free the simulation state. Safe to
 * call with sc->sim == NULL. The caller must not hold sc->mtx (this
 * function sleeps in callout_drain).
 */
void myfirst_sim_detach(struct myfirst_softc *sc);

/*
 * Start the simulation callouts. Requires sc->mtx held.
 */
void myfirst_sim_enable(struct myfirst_softc *sc);

/*
 * Stop the simulation callouts. Does not drain (that is _detach's
 * job). Requires sc->mtx held.
 */
void myfirst_sim_disable(struct myfirst_softc *sc);

/*
 * Start a command. Called from the driver when CTRL.GO is written.
 * Reads DATA_IN and DELAY_MS, schedules the command completion
 * callout. Rejects overlapping commands. Requires sc->mtx held.
 */
void myfirst_sim_start_command(struct myfirst_softc *sc);

/*
 * Register simulation-specific sysctls on the driver's sysctl tree.
 * Called from myfirst_attach after sc->sysctl_tree is established.
 */
void myfirst_sim_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_SIM_H_ */
```



## Reference: A Comparison with Chapter 16 Patterns

A side-by-side comparison of where Chapter 17 extends Chapter 16 and where it introduces genuinely new material.

| Pattern                              | Chapter 16             | Chapter 17                                    |
|--------------------------------------|------------------------|-----------------------------------------------|
| Register access                      | `CSR_READ_4`, etc.     | Same API, unchanged                           |
| Access log                           | Introduced             | Reused, extended with fault-injection entries |
| Lock discipline                       | `sc->mtx` around each access | Same, plus callouts via `callout_init_mtx`   |
| File layout                          | `myfirst_hw.c` added   | `myfirst_sim.c` added                         |
| Register map                         | 10 registers, 40 bytes | 16 registers, 60 bytes (all in same 64-byte allocation) |
| CTRL bits                            | ENABLE, RESET, MODE, LOOPBACK | Same, plus GO (bit 9)                   |
| STATUS bits                          | READY, BUSY, ERROR, DATA_AV | Same, but dynamically changed by callouts |
| Callouts                             | One (reg_ticker_task as a task) | Three (sensor, command, busy)         |
| Timeouts                             | Not applicable         | Introduced (cmd_timeout_ms, rdy_timeout_ms)   |
| Error recovery                       | Minimal                | Full recovery path                            |
| Statistics                           | None                   | Per-outcome counters                          |
| Fault injection                      | None                   | Four fault modes                              |
| HARDWARE.md                          | Introduced             | Extended                                      |
| LOCKING.md                           | Extended from Ch15     | Extended                                      |
| SIMULATION.md                        | Not present            | Introduced                                    |

Chapter 17 builds on Chapter 16 without breaking anything. Every Chapter 16 capability is preserved; every new capability is added. The driver at `1.0-simulated` is a strict superset of the driver at `0.9-mmio`.



## Reference: A Closing Note on Simulation Philosophy

A paragraph to close the chapter with, which is worth returning to after you have worked through the labs.

Simulation is, at its heart, an act of modelling. You take a system you do not fully control (a real device) and build a smaller system you do control (a simulation) that behaves similarly. The simulation is never perfect. Its value comes not from matching the real system in every detail, but from preserving the properties that matter for the question you are asking.

For Chapter 17 the property that matters is protocol correctness: does the driver handle the command cycle, the timing, the error cases, the recovery? A simulation that preserves protocol correctness is a simulation that earns its keep, even if it gets every timing detail slightly wrong.

For later chapters and for your own future work, the properties that matter may be different. A performance test needs a simulation that preserves throughput behaviour. A power test needs a simulation that preserves idle and active states. A security test needs a simulation that can produce adversarial inputs.

The skill Chapter 17 teaches is not "how to simulate this particular device". It is "how to identify what a simulation must preserve, and how to build one that preserves it". That skill is transferable, and it is the skill that will serve you across every driver you write.
