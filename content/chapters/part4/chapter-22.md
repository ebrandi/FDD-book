---
title: "Power Management"
description: "Chapter 22 closes Part 4 by teaching how the myfirst driver survives suspend, resume, and shutdown. It explains what power management is from a device-driver perspective; how ACPI sleep states (S0-S5) and PCI device power states (D0-D3hot/D3cold) compose to form a full transition; what the DEVICE_SUSPEND, DEVICE_RESUME, DEVICE_SHUTDOWN, and DEVICE_QUIESCE methods do and in what order the kernel delivers them; how to quiesce interrupts, DMA, timers, and deferred work safely; how to restore state on resume without losing data; how runtime power management differs from full-system suspend; how to test power transitions from user space with acpiconf, zzz, and devctl; how to debug frozen devices, lost interrupts, and post-resume DMA failures; and how to refactor power-aware code into its own file. The driver grows from 1.4-dma to 1.5-power, gains myfirst_power.c and myfirst_power.h, gains a POWER.md document, and leaves Part 4 with a driver that handles suspend-resume cycles as cleanly as it handles attach-detach."
partNumber: 4
partName: "Hardware and Platform-Level Integration"
chapter: 22
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Power Management

## Reader Guidance & Outcomes

Chapter 21 ended with the driver at version `1.4-dma`. That driver attaches to a PCI device, allocates MSI-X vectors, services interrupts through a filter-plus-task pipeline, moves data through a `bus_dma(9)` buffer, and cleans up its resources when it is asked to detach. For a driver that boots, runs, and eventually gets unloaded, those mechanics are complete. What the driver does not yet handle is the third kind of event a modern system throws at it: the moment when power itself is about to change.

Power changes are different from attach and detach. An attach starts from nothing and ends with a working device. A detach starts from a working device and ends with nothing. Both are one-shot transitions the driver itself can take its time over. A suspend is neither. The driver enters suspend already running, with live interrupts, live DMA transfers, live timers, and a device the kernel still expects to answer questions. The driver has to bring all of that to a stop within a narrow time window, hand the device over to a lower-power state, survive the power loss without forgetting what it needs to know, and then reassemble everything on the other side as if nothing happened. The user, ideally, notices nothing at all. The lid closes, a second later it opens, and the video conference resumes in the same browser tab as if the interruption had never happened.

Chapter 22 teaches how the driver earns that illusion. The chapter's scope is precisely this: what power management is at the driver level; how the kernel lets the driver see a power transition coming; what it means to quiesce a device so that no activity leaks across the transition; how to preserve the state the driver will need after resume; how to restore that state so the device comes back to the same behavior the user saw before; how to extend the same discipline to idle-device power saving through runtime suspend; how to test transitions from user space; how to debug the characteristic failures a power-aware driver faces; and how to organise the new code so that the driver stays readable as it grows. The chapter stops short of the later topics that build on this discipline. Chapter 23 teaches debugging and tracing in depth; the power-aware regression script in Chapter 22 is a first taste, not the full toolkit. The network-driver chapter in Part 6 (Chapter 28) adds iflib's power hooks and multi-queue suspend coordination; Chapter 22 stays with the single-queue `myfirst` driver. The advanced real-world chapters in Part 7 explore hotplug and power-domain management on embedded platforms; Chapter 22 focuses on the desktop and server cases where ACPI and PCIe dominate.

The arc of Part 4 closes here with a discipline rather than a new primitive. Chapter 16 gave the driver a vocabulary of register access through `bus_space(9)`. Chapter 17 taught it to think like a device by simulating one. Chapter 18 introduced it to a real PCI device. Chapter 19 gave it one pair of ears on a single IRQ. Chapter 20 gave it several ears, one per queue the device cares about. Chapter 21 gave it hands: the ability to hand the device a physical address and let the device run a transfer on its own. Chapter 22 teaches the driver to stop doing all of that on request, to wait politely while the system sleeps, and to pick up again cleanly when the system wakes. That discipline is the last missing ingredient before the driver can call itself production-ready in the Part 4 sense. The later chapters add observability, specialisation, and polish; they assume the power discipline is in place.

### Why Suspend and Resume Earn a Chapter of Their Own

A natural question at this stage is whether suspend and resume really need a full chapter, after the depth of Chapter 21. The `myfirst` driver already has a clean detach path. Detach already releases interrupts, drains tasks, tears down DMA, and returns the device to a quiet state. Can the driver not simply call detach on suspend and attach on resume, and be done?

The answer is no, for three linked reasons.

The first is that **suspend is not detach**. A detach is permanent. The driver does not need to remember anything about the device after detach finishes; when the device comes back, it is a fresh attach, from scratch. A suspend is temporary, and the driver does need to remember things across it. It needs to remember its software state so the user's session can pick up where it left off. It needs to remember which interrupt vectors it had allocated. It needs to remember its configuration sysctls. It needs to remember which clients had the device open. Detach forgets all of that; suspend must not. The two paths share cleanup steps in their middle, but they diverge at the ends. Treating suspend as a detach plus a later attach would be correct in the narrow mechanical sense and wrong in every other sense: it would drop the user's session, invalidate open file descriptors on `/dev/myfirst0`, lose the sysctl state, and ask the kernel to re-probe the device from its raw PCI identity on every resume. That is not how modern FreeBSD drivers work, and Chapter 22 shows the better pattern.

The second is that **the time budget is different**. A detach can afford to be thorough. A driver that takes five hundred milliseconds to detach has no user-facing impact; detaches happen at boot, at module unload, or at device removal, and those moments are understood to be slow. A suspend has to finish within a budget measured in tens of milliseconds per device on a laptop with a hundred devices, because the sum is what the user notices as lid-close latency. A driver that does a full detach-like cleanup, waits for queues to drain at their natural pace, unwinds every allocation, and rebuilds everything on resume will be measurably slow across the whole fleet of devices the system has. The Chapter 22 pattern is to stop activity fast, save what needs to be saved, leave allocations in place, and restore from saved state. That pattern is what keeps suspend-resume under a second on a typical laptop.

The third is that **the kernel gives the driver a specific contract for power transitions**, and that contract has its own vocabulary, its own order of operations, and its own failure modes. The `DEVICE_SUSPEND` and `DEVICE_RESUME` kobj methods are not just "detach and attach with different names". They are called at specific points in the system-wide suspend sequence, with the driver tree traversed in a specific order, and they interact with the PCI layer's automatic config-space save and restore, with ACPI's sleep-state machinery, with the interrupt subsystem's mask and unmask calls, and with the `bus_generic_suspend` and `bus_generic_resume` helpers that walk the device tree. A driver that ignores the contract may still look correct during detach, during DMA, and during interrupt handling, and then fail only when the user closes the lid. That class of failure is notoriously hard to debug because it is hard to reproduce, and Chapter 22 invests time in making the contract explicit so the failures do not happen in the first place.

Chapter 22 earns its place by teaching those three ideas together, concretely, with the `myfirst` driver as the running example. A reader who finishes Chapter 22 can add `device_suspend`, `device_resume`, and `device_shutdown` methods to any FreeBSD driver, knows which of the chapter's discipline applies where, and understands the interactions between the ACPI layer, the PCI layer, and the driver's own state. That skill transfers directly to every FreeBSD driver the reader will ever work on.

### Where Chapter 21 Left the Driver

A short checkpoint before we continue. Chapter 22 extends the driver produced at the end of Chapter 21 Stage 4, tagged as version `1.4-dma`. If any of the items below is uncertain, return to Chapter 21 before starting this chapter.

- Your driver compiles cleanly and identifies itself as `1.4-dma` in `kldstat -v`.
- The driver allocates one or three MSI-X vectors (depending on platform), registers per-vector filters and tasks, binds each vector to a CPU, and prints an interrupt banner during attach.
- The driver allocates a `bus_dma` tag, allocates a 4 KB DMA buffer, loads it into a map, and exposes the bus address through `dev.myfirst.N.dma_bus_addr`.
- Writing to `dev.myfirst.N.dma_test_write=0xAA` triggers a host-to-device transfer; writing to `dev.myfirst.N.dma_test_read=1` triggers a device-to-host transfer; both log success to `dmesg`.
- The detach path drains the rx task, drains the simulation's callout, waits for any in-flight DMA to complete, calls `myfirst_dma_teardown`, tears down MSI-X vectors in reverse order, and releases resources.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, and `DMA.md` are current in your working tree.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel.

That driver is what Chapter 22 extends. The additions are modest in lines but important in discipline: a new `myfirst_power.c` file, a matching `myfirst_power.h` header, a small set of new softc fields to track suspend state and saved runtime state, new `myfirst_suspend` and `myfirst_resume` entry points wired into the `device_method_t` table, a new `myfirst_shutdown` method, a call to the Chapter 21 quiesce primitives from the new suspend path, a restoration path that reinitialises the device without repeating attach, a version bump to `1.5-power`, a new `POWER.md` document, and updates to the regression test. The mental model grows too: the driver starts thinking about its own lifecycle as attach, run, quiesce, sleep, wake, run again, and eventually detach, rather than just attach, run, detach.

### What You Will Learn

Walking away from this chapter, you should be able to:

- Describe what power management means for a device driver, distinguish system-level and device-level power saving, and name the difference between a full suspend-resume cycle and a runtime power transition.
- Recognise the ACPI system sleep states (S0, S1, S3, S4, S5) and the PCI device power states (D0, D1, D2, D3hot, D3cold), explain how they compose in a single transition, and identify which parts of each are the driver's responsibility.
- Explain the role of PCIe link states (L0, L0s, L1, L1.1, L1.2) and active-state power management (ASPM), at a level sufficient to read a datasheet and recognise what the platform controls automatically versus what the driver must configure explicitly.
- Add `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, and (optionally) `DEVICE_QUIESCE` entries to a driver's `device_method_t` table, and implement each of them so they compose with `bus_generic_suspend(9)` and `bus_generic_resume(9)` in the device tree.
- Explain what it means to quiesce a device safely before a power transition, and apply the pattern: mask interrupts at the device, stop submitting new DMA work, drain the in-flight transfers, drain the callouts and taskqueues, flush or discard buffers as policy dictates, and leave the device in a defined, quiet state.
- Explain why the PCI layer automatically saves and restores configuration space around `device_suspend` and `device_resume`, when the driver has to supplement that with its own `pci_save_state`/`pci_restore_state` calls, and when it should not.
- Implement a clean resume path that re-enables bus-mastering, restores device registers from the driver's saved state, re-arms the interrupt mask, revalidates device identity, and re-admits clients to the device without losing data or raising spurious interrupts.
- Recognise the cases where a device silently resets across suspend, how to detect the reset, and how to rebuild only the state that was actually lost.
- Implement a runtime power management helper that puts an idle device into D3 and wakes it back to D0 on demand, and discuss the latency versus power tradeoff.
- Trigger a full-system suspend from user space with `acpiconf -s 3` or `zzz`, a per-device suspend with `devctl suspend` and `devctl resume`, and observe the transitions through `devinfo -v`, `sysctl hw.acpi.*`, and the driver's own counters.
- Debug the characteristic failures of power-aware code: frozen devices, lost interrupts, bad DMA after resume, missed PME# wake events, and WITNESS complaints about sleep-with-locks-held inside suspend. Apply the matching recovery patterns.
- Refactor the driver's power-management code into a dedicated `myfirst_power.c`/`myfirst_power.h` pair, bump the driver's version to `1.5-power`, extend the regression test to cover suspend and resume, and produce a `POWER.md` document that explains the subsystem to the next reader.
- Read the power-management code in a real driver such as `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/xl/if_xl.c`, or `/usr/src/sys/dev/virtio/block/virtio_blk.c`, and map each call back to the concepts introduced in Chapter 22.

The list is long. The items are narrow. The chapter's goal is the composition, not any single item in isolation.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 22 stays focused on the driver-side discipline.

- **Advanced ACPI internals** such as the AML interpreter, the SSDT/DSDT tables, the `_PSW`/`_PRW`/`_PSR` method semantics, and the ACPI button subsystem. The chapter uses ACPI only through the layer the kernel exposes to a driver; the internals belong in a later, platform-oriented chapter.
- **Hibernate-to-disk mechanics (S4)**. FreeBSD's S4 support has historically been partial on x86, and the driver-side contract is essentially a stricter version of S3. The chapter mentions S4 for completeness and treats it like S3 for driver purposes.
- **Cpufreq, powerd, and CPU frequency scaling**. These affect CPU power, not device power. A driver whose device is in D0 is unaffected by the CPU's P-state; the chapter does not pursue CPU power management.
- **SR-IOV suspend coordination between PF and VFs**. Virtual Function suspend has its own ordering constraints and belongs in a specialised chapter.
- **Hotplug and surprise removal**. Removing a device by physical unplug is similar in spirit to suspend but uses different code paths (`BUS_CHILD_DELETED`, `device_delete_child`). Part 7 covers hotplug in depth; Chapter 22 mentions the relationship and moves on.
- **Thunderbolt and USB-C dock suspend**. These compose ACPI, PCIe hotplug, and USB power management and belong in a dedicated later section.
- **Embedded-platform power-domain and clock-gating frameworks** such as the device-tree `power-domains` and `clocks` properties on arm64 and RISC-V. The chapter uses x86 ACPI and PCI conventions throughout, and mentions the embedded counterparts in passing when the concept is parallel.
- **Custom wake-on-LAN policy, wake-on-pattern policy, and application-specific wake sources**. The chapter explains how a wake source is plumbed (PME#, USB remote wakeup, GPIO wake) without trying to teach every hardware-specific variation.
- **The internals of the `ksuspend`/`kresume` paths and the kernel's cpuset migration around suspend**. The driver does not see those directly; they affect interrupt routing and CPU offlining, not the driver's visible contract.

Staying inside those lines keeps Chapter 22 a chapter about driver-side power discipline. The vocabulary transfers; the specialisations add detail in later chapters without needing a new foundation.

### Estimated Time Investment

- **Reading only**: four to five hours. The power management conceptual model is neither as dense as DMA nor as mechanical as interrupts; much of the time is in building the mental picture of how ACPI, PCI, and the driver compose during a transition.
- **Reading plus typing the worked examples**: ten to twelve hours over two or three sessions. The driver evolves in three stages: skeleton suspend and resume with logging, full quiesce and restore, and finally refactor into `myfirst_power.c`. Each stage is short, but the testing is deliberate: a forgotten `bus_dmamap_sync` or a missed interrupt mask can produce silent corruption that only shows up on the fifth or sixth suspend-resume cycle.
- **Reading plus all labs and challenges**: fifteen to twenty hours over four or five sessions, including the lab that stresses the driver through repeated suspend-resume cycles, the lab that forces an intentional post-resume failure and debugs it, and the challenge material that extends the driver with runtime idle detection.

Sections 3 and 4 are the densest. If the quiesce discipline or the resume ordering feels opaque on first pass, that is normal. Stop, re-read the corresponding diagram, run the matching exercise on the simulated device, and continue when the shape has settled. Power management is one of those topics where a working mental model pays off repeatedly; it is worth building slowly.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 21 Stage 4 (`1.4-dma`). The starting point assumes every Chapter 21 primitive: the DMA tag and buffer, the `dma_in_flight` tracker, the `dma_cv` condition variable, and the clean teardown path.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` is built, installed, and booting cleanly. The `WITNESS` option is especially valuable for suspend and resume work, because the code paths run under non-obvious locks and the kernel's power machinery tightens several invariants during the transition.
- `bhyve(8)` or `qemu-system-x86_64` is available. Chapter 22's labs work on either target. The suspend-resume testing does not require real hardware; `devctl suspend` and `devctl resume` let you drive the driver's power methods directly without involving ACPI.
- The `devinfo(8)`, `sysctl(8)`, `pciconf(8)`, `procstat(1)`, `devctl(8)`, `acpiconf(8)` (if on real hardware with ACPI) and `zzz(8)` commands are in your path.

If any item above is shaky, fix it now. Power management, like DMA, is a topic where latent weaknesses show up under stress. A driver that almost works on detach often breaks on suspend; a driver that handles one suspend cleanly often fails on the tenth cycle because a counter wrapped, a map leaked, or a condition variable was reinitialised incorrectly. The `WITNESS`-enabled debug kernel is what surfaces those mistakes at development time.

### How to Get the Most Out of This Chapter

Four habits will pay off quickly.

First, keep `/usr/src/sys/kern/device_if.m` and `/usr/src/sys/kern/subr_bus.c` bookmarked. The first file defines the `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, and `DEVICE_QUIESCE` methods; the second contains `bus_generic_suspend`, `bus_generic_resume`, `device_quiesce`, and the devctl machinery that turns user-space requests into method calls. Reading both once at the start of Section 2 and returning to them as you work each section is the single most useful thing you can do for fluency.

Second, keep three real-driver examples close to hand: `/usr/src/sys/dev/re/if_re.c`, `/usr/src/sys/dev/xl/if_xl.c`, and `/usr/src/sys/dev/virtio/block/virtio_blk.c`. Each illustrates a different power-management style. `if_re.c` is a full network driver with wake-on-LAN support, config-space save and restore, and a careful resume path. `if_xl.c` is simpler: its `xl_shutdown` just calls `xl_suspend`, and `xl_suspend` stops the chip and sets up wake-on-LAN. `virtio_blk.c` is minimal: `vtblk_suspend` sets a flag and quiesces the queue, `vtblk_resume` clears the flag and restarts I/O. Chapter 22 will refer back to each of them at the moment where their pattern best illustrates what the `myfirst` driver is doing.

Third, type the changes by hand and exercise each stage with `devctl suspend` and `devctl resume`. Power management is where small omissions produce characteristic failures: a forgotten interrupt mask causes a stuck resume; a forgotten `bus_dmamap_sync` causes stale data; a forgotten state variable causes the driver to think a transfer is still in flight. Typing carefully and running the regression script after each stage exposes those mistakes at the moment they happen.

Fourth, after finishing Section 4, re-read Chapter 21's detach path. The quiesce discipline in Section 3 and the restore discipline in Section 4 both share infrastructure with Chapter 21's detach: `callout_drain`, `taskqueue_drain`, `bus_dmamap_sync`, `pci_release_msi`. Seeing suspend-resume and attach-detach side by side is what makes the differences visible. Suspend is not detach; resume is not attach; but they use the same building blocks, composed differently, and seeing that composition in two passes is worth the extra half-hour.

### Roadmap Through the Chapter

The sections in order are:

1. **What Is Power Management in Device Drivers?** The big picture: why a driver cares about power, how system-level and device-level power management differ, what the ACPI S-states and PCI D-states mean, what PCIe link states and ASPM add, and what wake sources look like on the systems the reader is most likely to have. Concepts first, APIs next.
2. **FreeBSD's Power Management Interface.** The kobj methods: `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`. The order in which the kernel delivers them. The `bus_generic_suspend` helper, the `pci_suspend_child` path, and the interaction with ACPI. The first running code: Stage 1 of the Chapter 22 driver (`1.5-power-stage1`), with skeleton handlers that only log.
3. **Quiescing a Device Safely.** Stopping activity before a power transition. Masking interrupts, stopping DMA submission, draining in-flight work, draining callouts and taskqueues, flushing policy-sensitive buffers. Stage 2 (`1.5-power-stage2`) turns the skeletons into a real quiesce.
4. **Restoring State on Resume.** Reinitialising the device from saved state. What PCI save/restore does for you and what it does not. Re-enabling bus-master, restoring device registers, re-arming interrupts, validating identity, handling device reset. Stage 3 (`1.5-power-stage3`) adds the resume path that matches the Stage 2 quiesce.
5. **Handling Runtime Power Management.** Idle-device power saving. Detecting idleness. Putting the device into D3 and bringing it back to D0 on demand. Latency versus power. The chapter's optional section, but with a practical sketch a reader can experiment with.
6. **Interacting With the Power Framework.** Testing transitions from user space. `acpiconf -s 3` and `zzz` for full-system suspend. `devctl suspend` and `devctl resume` for per-device suspend. `devinfo -v` for observing power states. The regression script that wraps all of it.
7. **Debugging Power Management Issues.** The characteristic failure modes: frozen device, lost interrupts, bad DMA after resume, missing PME# wake, WITNESS complaints. The debugging patterns that find each of them.
8. **Refactoring and Versioning Your Power-Aware Driver.** The final split into `myfirst_power.c` and `myfirst_power.h`, the updated Makefile, the `POWER.md` document, and the version bump. Stage 4 (`1.5-power`).

After the eight sections come an extended walkthrough of `if_re.c`'s power-management code, several deeper looks at ACPI sleep states, PCIe link states, wake sources, and the devctl user-space interface, a set of hands-on labs, a set of challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 22's story and opens Chapter 23's, a bridge, and the usual quick-reference and glossary material at the end of the chapter. The reference material is meant to be re-read as you work through the next few chapters; Chapter 22's vocabulary (suspend, resume, quiesce, shutdown, D0, D3, ASPM, PME#) is the foundation every production FreeBSD driver shares.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 3, 4, and 7 stand alone and make good single-sitting reads.



## Section 1: What Is Power Management in Device Drivers?

Before the code, the picture. Section 1 teaches what power management means at the level the driver sees: the layers of the system that cooperate to save power, the sleep states and device power states those layers define, the PCIe link-level machinery that happens below the driver's visibility, and the wake sources that bring the system back. A reader who finishes Section 1 can read the rest of the chapter with the vocabulary of ACPI and PCI power management as concrete objects rather than as vague acronyms.

### Why the Driver Must Care About Power

The reader has spent the previous six chapters teaching a driver how to talk to a device. Each chapter added a capability: memory-mapped registers, simulated backends, real PCI, interrupts, multi-vector interrupts, DMA. In every chapter, the device was always ready to respond. The BAR was always mapped. The interrupt vector was always armed. The DMA engine was always on. That assumption is convenient for teaching, and it is also the assumption the driver makes during normal operation. It is not, however, the assumption the user makes. The user assumes that when the laptop sleeps, the battery drains slowly; that when the NVMe is idle, it cools itself; that when the Wi-Fi card has nothing to transmit, it is not pulling watts from the power supply. Those assumptions are real platform engineering, and the driver is one of the layers that has to cooperate for the engineering to work.

Cooperating means acknowledging that the device's power state can change under the driver's feet. The change is always announced: the kernel calls a method in the driver to tell it what is about to happen. But the announcement only works if the driver handles it correctly. A driver that ignores the announcement leaves the device in an inconsistent state, and the cost shows up in ways that are specific to power transitions: a laptop that does not wake up, a server whose RAID controller refuses to respond after a device-level reset, a USB dock that loses connectivity across a lid-close. Each of these failures maps back to a driver that treated a power event as optional when the platform's correctness assumed it was mandatory.

The stakes are not only about idle power. A driver that does not quiesce DMA before the system suspends can corrupt memory at the moment the CPU stops. A driver that does not mask interrupts before a bus enters a lower power state can cause spurious wake events. A driver that does not restore its configuration after resume can read zeros from a register that used to hold a valid address. Each of those is a kernel bug whose symptom is reported by the user as "sometimes my machine doesn't wake up". Chapter 22's discipline is what prevents that class of bug.

### System-Level Versus Device-Level Power Management

Two words that sound similar describe different things. It is worth pinning them down now, because the chapter uses both and the distinction matters throughout.

**System-level power management** is a transition of the whole computer. The user presses the power button, closes the lid, or issues `shutdown -p now`. The kernel walks the device tree, asks every driver to suspend its device, and then either parks the CPU in a low-power state (S1, S3), writes the memory contents to disk (S4), or turns the power off (S5). Every driver in the system participates in the transition. If any driver refuses, the whole transition fails; the kernel prints a message like `DEVICE_SUSPEND(foo0) failed: 16`, the system stays awake, and the user sees a laptop whose screen went dark for half a second and then came back.

**Device-level power management** is a transition of one device. The kernel decides (or is told by `devctl suspend`) that one specific device can be put into a lower power state, independent of any other device. The PCIe NIC, for example, can go to D3 when its link has been idle for a few seconds, and come back to D0 when a packet arrives. The whole system stays in S0 throughout. The rest of the devices continue to work. The user does not notice anything except a slight increase in the latency of the first packet after an idle period, because the NIC had to wake up from D3.

System-level and device-level transitions use the same driver methods. `DEVICE_SUSPEND` is called both for a full S3 transition (where every device suspends together) and for a targeted `devctl suspend myfirst0` (where only the `myfirst0` device suspends). The driver usually does not distinguish the two; the same quiesce discipline works for both. What differs is the context around the call: a system-wide suspend also disables all CPUs except the boot CPU, stops most kernel threads, and expects every driver to be done quickly; a per-device suspend leaves the rest of the system running and is called from normal kernel context. The driver mostly does not have to care. It does, however, need to be aware that the two contexts exist, because a driver that only tests per-device suspend may miss a bug that only shows up in a full-system suspend, and vice versa.

Chapter 22 exercises both paths. The labs use `devctl suspend` and `devctl resume` for fast iteration, because they take milliseconds and do not involve ACPI. The integration test uses `acpiconf -s 3` (or `zzz`) to exercise the full path through the ACPI layer and the bus hierarchy. A driver that passes both tests is much more likely to be correct in production than one that passes only the first.

### ACPI System Sleep States (S0 to S5)

On the x86 laptops and servers most readers will work with, the system's power state is described by the ACPI specification as a small set of letters and numbers called the **S-states**. Each S-state defines a distinct level of "how much of the system is still running". The driver does not choose an S-state (the user, the BIOS, or the kernel's policy chooses), but it has to know which ones exist and what each one implies for the device.

**S0** is the working state. The CPU is running, RAM is powered, all devices are in whatever device power state they need. Everything the reader has done so far has been in S0. This is the state the system boots into and the state it leaves only when sleep is requested.

**S1** is called "standby" or "light sleep". The CPU stops executing, but the CPU's registers and caches are preserved, RAM stays powered, and most devices stay in D0 or D1. Wake-up is fast (usually a second or less). On modern hardware S1 is rarely used, because S3 is more power-efficient and almost as fast to wake from. FreeBSD supports S1 if the platform advertises it; most platforms no longer do.

**S2** is a rarely-implemented state between S1 and S3. On most platforms it is not advertised, and when it is, FreeBSD treats it similarly to S1. The chapter does not return to S2.

**S3** is "suspend to RAM", also known as "standby" or "sleep" in user-facing language. The CPU stops, the CPU context is lost and must be saved before the transition, RAM stays powered through its self-refresh mechanism, and most devices go to D3 or D3cold. Wake-up takes one to three seconds on a typical laptop. This is the state the user enters by closing the lid on a laptop. On a server, S3 is the state `acpiconf -s 3` or `zzz` produces. Chapter 22's main test is an S3 transition.

**S4** is "suspend to disk" or "hibernate". The full contents of RAM are written to a disk image, power is removed, and the system comes back by reading the image on the next boot. On FreeBSD, S4 support has historically been partial on x86 (the memory image can be produced but the restore path is not as polished as Linux or Windows). For driver purposes, S4 looks like S3 with an extra step at the end: the driver suspends exactly as it would for S3. The difference is invisible to the driver.

**S5** is "soft off". The system is powered off; only the wake-up circuitry (power button, wake-on-LAN) is receiving power. From the driver's point of view, S5 is similar to a system shutdown; the `DEVICE_SHUTDOWN` method is what gets called, not `DEVICE_SUSPEND`.

On real hardware, the reader can see which sleep states the platform supports with:

```sh
sysctl hw.acpi.supported_sleep_state
```

A typical laptop prints something like:

```text
hw.acpi.supported_sleep_state: S3 S4 S5
```

A server might print only `S5`, because ACPI suspend is rarely meaningful on a datacenter machine. A VM might print varied combinations depending on the hypervisor. The `sysctl hw.acpi.s4bios` tells whether S4 is BIOS-assisted (it rarely is on modern systems). The `sysctl hw.acpi.sleep_state` lets the reader enter a sleep state manually; `acpiconf -s 3` is the preferred command-line wrapper.

For Chapter 22's purposes, the reader needs to be aware of S3 (the common case) and S5 (the shutdown case). S1 and S2 are handled by the driver the same way as S3; S4 is a superset of S3. The chapter treats S3 as the canonical example throughout.

### PCI and PCIe Device Power States (D0 to D3cold)

A device's power state is described by the **D-states**, which the PCI specification defines independently from the system's S-state. The driver's methods most directly control the D-state of its device, and it is worth understanding each state in detail.

**D0** is the fully-on state. The device is powered, clocked, accessible through its configuration space and BARs, and able to perform any operation the driver asks for. All the Chapter 16 to 21 work has been done with the device in D0. `PCI_POWERSTATE_D0` is the symbolic constant in `/usr/src/sys/dev/pci/pcivar.h`.

**D1** and **D2** are intermediate low-power states that the PCI spec defines but does not tightly constrain. A device in D1 still has its configuration registers accessible and can respond to some I/O; a device in D2 may have lost more context. These states are rarely used on modern PCs because the jump from D0 to D3 is usually preferable for power savings. Most drivers do not bother with D1 and D2.

**D3hot** is the low-power state in which the device is effectively off, but the bus is still powered, the configuration space can still be accessed (reads return mostly zero or the preserved configuration), and the device can raise a PME# signal if it is configured to do so. Most devices enter D3hot during suspend.

**D3cold** is the even-lower power state in which the bus itself has lost power. The device cannot be accessed at all; reads from its configuration space return all-ones. The only way out of D3cold is for the platform to restore power, which usually happens under platform (not driver) control. D3cold is common during full-system S3 and S4.

When a driver calls `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`, the PCI layer transitions the device from its current state to D3 (D3hot specifically; the transition to D3cold is the platform's job). When the driver calls `pci_set_powerstate(dev, PCI_POWERSTATE_D0)`, the PCI layer brings the device back to D0.

The PCI layer in FreeBSD also automatically manages these transitions during system suspend and resume. The `pci_suspend_child` function, which the PCI bus driver registers for `bus_suspend_child`, calls the driver's `DEVICE_SUSPEND` first, and then (if the `hw.pci.do_power_suspend` sysctl is true, which it is by default) transitions the device to D3. On resume, `pci_resume_child` transitions the device back to D0, restores the configuration space from a cached copy, clears any pending PME# signal, and then calls the driver's `DEVICE_RESUME`. The reader can observe the behavior with:

```sh
sysctl hw.pci.do_power_suspend
sysctl hw.pci.do_power_resume
```

Both default to 1. A reader who wants to disable the automatic D-state transition (for debugging, or for a device that misbehaves in D3) can set them to 0, in which case the driver's `DEVICE_SUSPEND` and `DEVICE_RESUME` run but the device stays in D0 through the transition.

For Chapter 22, the important facts are:

- The driver's `DEVICE_SUSPEND` method runs before the D-state changes. The driver quiesces while the device is still in D0.
- The driver's `DEVICE_RESUME` method runs after the device has been returned to D0. The driver restores while the device is accessible.
- The driver does not (usually) call `pci_set_powerstate` directly during suspend and resume. The PCI layer handles that automatically.
- The driver does not (usually) call `pci_save_state` and `pci_restore_state` directly. The PCI layer handles those automatically too, through `pci_cfg_save` and `pci_cfg_restore`.
- The driver *does* save and restore its own device-specific state: BAR-local register contents that the hardware may have lost, softc fields that track runtime configuration, interrupt mask values. The PCI layer does not know about these.

The boundary is where the PCI configuration space ends and the BAR-accessed registers begin. The PCI layer protects the first; the driver protects the second.

### PCIe Link States and Active-State Power Management (ASPM)

At a layer below the device's D-state sits the PCIe link itself. A link between a root complex and an endpoint can be in one of several **L-states**, and transitions between L-states happen automatically when the traffic on the link is low enough.

**L0** is the fully-on link state. Data flows normally. Latency is at its minimum. This is the state the link is in whenever the device is active.

**L0s** is a low-power state the link enters when it has been idle for a few microseconds. The transmitter turns off its output drivers on one side; the link is bidirectional, so the other side's L0s is independent. Recovery from L0s takes hundreds of nanoseconds. This is cheap power saving that the platform can do automatically when traffic is bursty.

**L1** is a deeper low-power state the link enters after a longer idle period (tens of microseconds). Both sides turn off more of their physical-layer circuitry. Recovery takes microseconds. This is used during light-load periods when the latency penalty is acceptable.

**L1.1** and **L1.2** are PCIe 3.0 and later refinements of L1 that add further power gating, allowing even lower idle current at the cost of slower wake-up.

**L2** is a near-off link state used during D3cold and S3; the link is effectively turned off, and wake-up requires a full re-negotiation. The driver typically does not manage L2 directly; it is a side-effect of the device entering D3cold.

The mechanism that controls the transitions between L0 and L0s/L1 is called **Active-State Power Management (ASPM)**. ASPM is a per-link feature configured through the PCIe capability registers of both ends of the link. It can be enabled, disabled, or restricted to L0s-only by platform policy. On FreeBSD, ASPM is usually controlled by the firmware through ACPI (the `_OSC` method tells the OS which capabilities to manage); the kernel does not second-guess the firmware's policy unless explicitly told to.

For Chapter 22 and for most FreeBSD drivers, ASPM is a platform concern, not a driver concern. The driver does not configure ASPM; the platform does. The driver does not need to save or restore ASPM state around suspend; the PCI layer handles the PCIe capability registers as part of the automatic config-space save and restore. A driver that wants to disable ASPM for a specific device (for example, because the device has a known errata that makes L0s unsafe) can do so by reading and writing the PCIe Link Control register explicitly, but this is rare and specific.

The reader does not need to add ASPM code to the `myfirst` driver. It is enough to be aware that L-states exist, that they transition automatically based on traffic, that the driver's D-state and the link's L-state are related but distinct, and that the platform handles the ASPM configuration. If a future driver the reader works on has a datasheet that specifies ASPM errata, the reader will know where to look.

### The Anatomy of a Suspend-Resume Cycle

Putting the pieces together, a full system suspend-resume cycle looks like this from the driver's point of view, tracing the `myfirst` driver through an S3 transition:

1. The user closes the laptop lid. The ACPI button driver (`acpi_lid`) notices the event and, based on the system's policy, triggers a sleep request to state S3.
2. The kernel starts the suspend sequence. Userland daemons are paused; the kernel freezes non-essential threads.
3. The kernel walks the device tree and calls `DEVICE_SUSPEND` on each device, in reverse child order. The PCI bus driver's `bus_suspend_child` calls the `myfirst` driver's `device_suspend` method.
4. The `myfirst` driver's `device_suspend` runs. It masks interrupts on the device, stops accepting new DMA requests, waits for any in-flight DMA to complete, drains its task queue, logs the transition, and returns 0 to indicate success.
5. The PCI layer notes that `myfirst`'s suspend succeeded. It calls `pci_cfg_save` to cache the PCI configuration space. If `hw.pci.do_power_suspend` is 1 (the default), it transitions the device to D3hot via `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`.
6. Higher in the tree, the PCI bus itself, the host bridge, and eventually the platform go through their own `DEVICE_SUSPEND` calls. ACPI arms its wake events. The CPU enters the low-power state corresponding to S3. The memory subsystem enters self-refresh. The PCIe link goes to L2 or thereabouts.
7. Time passes. On the scale the driver observes, no time passes at all; the kernel is not running.
8. The user opens the lid. The platform's wake circuitry wakes the CPU. ACPI performs the early resume steps: the CPU context is restored, memory is refreshed, the platform's firmware reinitialises what it must.
9. The kernel's resume sequence starts. It walks the device tree in forward order, calling `DEVICE_RESUME` on each device.
10. For `myfirst`, the PCI bus driver's `bus_resume_child` transitions the device back to D0 via `pci_set_powerstate(dev, PCI_POWERSTATE_D0)`. It calls `pci_cfg_restore` to write the cached configuration space back into the device. It clears any pending PME# signal with `pci_clear_pme`. Then it calls the driver's `device_resume` method.
11. The driver's `device_resume` runs. The device is in D0, its configuration space is restored, its BAR registers are zero or default values. The driver re-enables bus-mastering if needed, writes its device-specific registers back from saved state, re-arms the interrupt mask, and marks the device as resumed. It returns 0.
12. The kernel's resume sequence continues up the tree. Userland threads are unfrozen. The user sees a working system, usually in one to three seconds.

Every step at which the driver has work is step 4 and step 11. Everything else is either platform or generic kernel machinery. The driver's job is to make those two steps correct, and to understand enough about the steps around them to interpret the behavior it observes.

### Wake Sources

A device that is suspended can be the reason the system wakes. The way that happens depends on the bus:

- On **PCIe**, a device in D3hot can assert the **PME#** signal (Power Management Event). The platform's root complex translates PME# into a wake event, the ACPI `_PRW` method identifies which GPE (General-Purpose Event) it uses, and the ACPI subsystem translates the GPE into a wake from S3. On FreeBSD, the `pci_enable_pme(dev)` function enables the device's PME# output; `pci_clear_pme(dev)` clears any pending signal. The `pci_has_pm(dev)` helper tells whether the device has a power-management capability at all.
- On **USB**, a device can request **remote wakeup** through its standard USB descriptor. The host controller (`xhci`, `ohci`, `uhci`) translates the wake into a PME# or equivalent signal upstream. The driver does not usually handle this directly; the USB stack does.
- On **embedded platforms**, a device can assert a **GPIO** pin that the platform wires to its wake logic. The device-tree `interrupt-extended` or `wakeup-source` properties identify which pins are wake sources. FreeBSD's GPIO intr framework handles this.
- On **Wake-on-LAN**, a network controller watches for magic packets or pattern matches while suspended and asserts PME# when one is seen. Both the driver and the platform have to be configured; `re_setwol` in `if_re.c` is a good example of the driver side.

For the `myfirst` driver, the simulated device does not really have a wake source (it has no physical existence outside the simulation). The chapter explains the mechanism at the appropriate point in Section 4, shows what `pci_enable_pme` does and where it would be called, and leaves the actual wake-triggering to the sim backend's manual triggers. A real-hardware driver would call `pci_enable_pme` in its suspend path when wake-on-X is requested, and `pci_clear_pme` in its resume path to acknowledge any pending signal.

### Real-World Examples: Wi-Fi, NVMe, USB

It helps to anchor the ideas in devices the reader has actually used. Consider three.

A **Wi-Fi adapter** like those handled by `iwlwifi` on Linux or `iwn` on FreeBSD is a constant power-management citizen. In S0, it spends most of its time in a low-power idle state on the chip itself, associated with the access point but not actively exchanging packets; when a packet is seen, it wakes to D0 for a few milliseconds, exchanges the packet, and goes back to idle. On system suspend (S3), the kernel asks the driver to save its state, the driver tells the chip to disassociate cleanly (or to set up WoWLAN patterns if the user wants wake-on-wireless), and the PCI layer transitions the chip to D3. On resume, the reverse happens: the chip comes back to D0, the driver restores its state, and re-associates to the access point. The user perceives a one- or two-second delay after lid-open before Wi-Fi is back, which is almost entirely the re-association time and not the driver's resume time.

An **NVMe SSD** handles power states internally through its own power-state machinery (defined in the NVMe spec as PSx states, where PS0 is full power and higher numbers are lower power). The NVMe driver participates in system suspend by flushing its queues, waiting for in-flight commands to complete, and then telling the controller to enter a low-power state. On resume, the driver restores the queue configuration, tells the controller to come back to PS0, and the system resumes disk I/O. Because NVMe queues are large and DMA-heavy, the NVMe suspend path is a classic place where a missed `bus_dmamap_sync` or a missed queue drain shows up as filesystem corruption after resume.

A **USB device** is handled by the USB host controller driver (`xhci`, typically). The host controller driver is the one that implements `DEVICE_SUSPEND` and `DEVICE_RESUME`; individual USB drivers (for keyboards, storage, audio, etc.) are notified through the USB framework's own suspend and resume mechanism. A driver for a USB device rarely needs its own `DEVICE_SUSPEND` method; the USB framework handles the translation.

The `myfirst` driver in Chapter 22 uses a PCI endpoint model, which is the most common case and the one whose contract the other cases specialise. Learning the PCI pattern first gives the reader what is needed to understand the Wi-Fi pattern, the NVMe pattern, and the USB pattern when those drivers are looked at later.

### What the Reader Has Gained

Section 1 is conceptual. The reader should not feel obligated to remember every detail of every state mentioned. What the reader should take from it is:

- Power management is a layered system. ACPI defines the system state. PCI defines the device state. PCIe defines the link state. The driver sees its device state most directly.
- Each layer's state can transition, and the transitions compose. A system S3 implies every device's D3 implies every link's L2. A per-device D3 (from runtime PM) does not imply system S3; the system stays in S0.
- The driver has a specific contract with the PCI and ACPI layers. The driver is responsible for quiescing its device's activity on suspend and restoring its device's state on resume. The PCI layer automatically handles configuration-space save, D-state transitions, and PME# wake signalling. ACPI handles the system-wide wake up.
- Wake sources exist and are plumbed through a specific chain (PME#, remote wakeup, GPIO). The driver usually enables and disables them through a helper API; it does not talk to the wake hardware directly.
- Testing is layered too. `devctl suspend`/`devctl resume` exercises only the driver methods. `acpiconf -s 3` exercises the whole system. A good regression script uses both.

With that picture in place, Section 2 can introduce the FreeBSD-specific APIs the driver uses to join this system.

### Wrapping Up Section 1

Section 1 established why a driver has to care about power, what system-level and device-level power management mean, what the ACPI S-states and the PCI D-states look like, what the PCIe L-states add, how a suspend-resume cycle flows from the driver's point of view, and what wake sources are. It did not show any driver code; that is Section 2's job. What the reader now has is a vocabulary and a mental model: suspend is a transition the platform announces, the driver quiesces; the PCI layer moves the device to D3; the system sleeps; on wake, the PCI layer moves the device back to D0; the driver restores.

With that picture in mind, the next section introduces FreeBSD's concrete API for all of this: the four kobj methods (`DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`), how the kernel invokes them, how they compose with `bus_generic_suspend` and `pci_suspend_child`, and how the `myfirst` driver's method table grows to include them.



## Section 2: FreeBSD's Power Management Interface

Section 1 described the layered world of ACPI, PCI, PCIe, and wake sources. Section 2 narrows the view to the FreeBSD kernel's interface: the specific kobj methods the driver implements, the way the kernel dispatches them, and the generic helpers that make the whole scheme tractable. By the end of this section the `myfirst` driver has a skeleton power-management implementation that compiles, logs transitions, and can be exercised with `devctl suspend` and `devctl resume`. The skeleton does not yet quiesce DMA or restore state; that is Section 3's and Section 4's work. Section 2's job is to get the kernel to call the driver's methods so the rest of the chapter has something concrete to build on.

### The Four Kobj Methods

The FreeBSD device framework treats a driver as an implementation of a kobj interface defined in `/usr/src/sys/kern/device_if.m`. That file is a small domain-specific language (`make -V` rules turn it into a header of function pointers and wrappers), and it defines the set of methods every driver can implement. The Chapter 16 to 21 work has populated the common methods: `DEVICE_PROBE`, `DEVICE_ATTACH`, `DEVICE_DETACH`. Power management adds four more, all documented in the same file with comments the reader can read directly:

1. **`DEVICE_SUSPEND`** is called when the kernel has decided to put the device into a suspended state. The method runs with the device still in D0 and with the driver still responsible for it. The method's job is to stop activity and, if necessary, save any state that will not be restored automatically. Returning 0 indicates success. Returning non-zero vetoes the suspend.

2. **`DEVICE_RESUME`** is called after the device has been returned to D0 on the way back from suspend. The method's job is to restore any state the hardware lost and to resume activity. Returning 0 indicates success. Returning non-zero causes the kernel to log a complaint; the resume cannot be meaningfully vetoed at this stage because the rest of the system has already come back.

3. **`DEVICE_SHUTDOWN`** is called during system shutdown to let the driver leave the device in a safe state for reboot or power-off. Many drivers implement this by calling their suspend method, because the two tasks are similar (stop the device cleanly). Returning 0 indicates success.

4. **`DEVICE_QUIESCE`** is called when the framework wants the driver to stop accepting new work but has not yet decided to detach. It is a softer form of detach: the device is still attached, the resources are still allocated, but the driver should refuse new submissions and let in-flight work drain. This method is optional and less commonly implemented than the other three; `device_quiesce` is called automatically before `device_detach` by the devctl layer, so a driver that implements both suspend and quiesce often shares code between them.

The file also contains default no-op implementations: `null_suspend`, `null_resume`, `null_shutdown`, `null_quiesce`. A driver that does not implement one of the methods gets the no-op, which returns 0 and does nothing. That is why Chapter 16 through Chapter 21 did not explicitly mention these methods: the no-ops were quietly being used, and for a driver whose device stays powered up forever and whose detach happens only at module unload, the no-ops give correct behavior for most workloads.

Chapter 22's first step is to replace those no-ops with real implementations.

### Adding the Methods to the Driver's Method Table

The `device_method_t` array in a FreeBSD driver lists the kobj methods the driver implements. The `myfirst` driver's current method array (in `myfirst_pci.c`) looks something like this:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,   myfirst_pci_probe),
        DEVMETHOD(device_attach,  myfirst_pci_attach),
        DEVMETHOD(device_detach,  myfirst_pci_detach),

        DEVMETHOD_END
};
```

Adding power management is mechanically simple: the driver adds three (or four) `DEVMETHOD` lines. The names on the left are the kobj method names from `device_if.m`; the names on the right are the driver's implementations. A complete set looks like this:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,    myfirst_pci_probe),
        DEVMETHOD(device_attach,   myfirst_pci_attach),
        DEVMETHOD(device_detach,   myfirst_pci_detach),
        DEVMETHOD(device_suspend,  myfirst_pci_suspend),
        DEVMETHOD(device_resume,   myfirst_pci_resume),
        DEVMETHOD(device_shutdown, myfirst_pci_shutdown),

        DEVMETHOD_END
};
```

The `myfirst_pci_suspend`, `myfirst_pci_resume`, and `myfirst_pci_shutdown` functions are new; they do not exist yet. The rest of Section 2 shows what each of them does at the skeleton level.

### Prototypes and Return Values

Each of the four methods has the same signature: an `int` return value and one `device_t` argument. The `device_t` is the device the method is being called on, and the driver can use `device_get_softc(dev)` to recover the softc pointer.

```c
static int myfirst_pci_suspend(device_t dev);
static int myfirst_pci_resume(device_t dev);
static int myfirst_pci_shutdown(device_t dev);
static int myfirst_pci_quiesce(device_t dev);  /* optional */
```

Return values follow the usual FreeBSD convention. Zero means success. Non-zero is a regular errno value that indicates what went wrong: `EBUSY` if the driver cannot suspend because the device is busy, `EIO` if the hardware reported an error, `EINVAL` if the driver was called in an impossible state. The kernel's reaction differs per method.

For `DEVICE_SUSPEND`, a non-zero return **vetoes** the suspend. The kernel aborts the suspend sequence and calls `DEVICE_RESUME` on the drivers that had already suspended successfully, unwinding the partial suspend. This is the mechanism that prevents a system from going to S3 while a critical device is in the middle of something the driver cannot interrupt. It should be used sparingly; returning `EBUSY` from every suspend whenever something is happening is a sure way to make suspend unreliable. A good driver only vetoes when the device is in a state that truly cannot be suspended.

For `DEVICE_RESUME`, a non-zero return is logged but mostly ignored. By the time resume is running, the system is coming back whether the driver likes it or not. The driver should log the error, mark its device as broken so that subsequent I/O fails cleanly, and return. A veto at resume time is too late to be useful.

For `DEVICE_SHUTDOWN`, a non-zero return is likewise mostly informational. The system is shutting down; the driver should try its best to leave the device in a safe state, but a failed shutdown is not an emergency.

For `DEVICE_QUIESCE`, a non-zero return prevents the subsequent operation (usually detach) from proceeding. A driver that returns `EBUSY` from `DEVICE_QUIESCE` forces the user to wait or to use `devctl detach -f` to force the detach.

### The Order of Event Delivery

The kernel does not call `DEVICE_SUSPEND` on every driver at once. It walks the device tree and calls the method in a specific order, usually **reverse child order** on suspend and **forward child order** on resume. This is because a suspend is safest when each device is suspended *after* the devices that depend on it, and each device is resumed *before* the devices that depend on it.

Consider a simplified tree:

```text
nexus0
  acpi0
    pci0
      pcib0
        myfirst0
      pcib1
        em0
      xhci0
        umass0
```

On an S3 suspend, the traversal for the subtree under `pci0` suspends `myfirst0` before `pcib0`, `em0` before `pcib1`, and `umass0` before `xhci0`. Then `pcib0`, `pcib1`, and `xhci0` are suspended. Then `pci0`. Then `acpi0`. Then `nexus0`. Each parent is suspended only after all its children are suspended.

On resume, the order is reversed. `nexus0` resumes first, then `acpi0`, then `pci0`, then `pcib0`, `pcib1`, `xhci0`. Each of those calls `pci_resume_child` on its children, which transitions the child back to D0 before calling the child driver's `DEVICE_RESUME`. So `myfirst0`'s `device_resume` runs with `pcib0` already active and `pci0` already reconfigured.

The practical consequence for the driver is that during `DEVICE_SUSPEND` it can still access the device normally (the parent bus is still up), and during `DEVICE_RESUME` it can also access the device normally (the parent bus has been resumed first). The driver does not need to handle the edge case of a suspended parent.

There is one subtlety: if a parent bus reports that its children must be suspended in a specific order (ACPI can do this to express implicit dependencies), the generic helper `bus_generic_suspend` respects that order. The `myfirst` driver, whose parent is a PCI bus, does not need to worry about order beyond "children before parent"; the PCI bus has no strong ordering between its child devices.

### bus_generic_suspend, bus_generic_resume, and the PCI Bus

A **bus driver** is itself a device driver, and when the kernel calls `DEVICE_SUSPEND` on a bus, the bus usually has to suspend all its children before it can itself go quiet. Implementing that by hand would be repetitive, so the kernel provides two helpers in `/usr/src/sys/kern/subr_bus.c`:

```c
int bus_generic_suspend(device_t dev);
int bus_generic_resume(device_t dev);
```

The first iterates over the bus's children in reverse order and calls `BUS_SUSPEND_CHILD` on each. The second iterates forward and calls `BUS_RESUME_CHILD`. If any child's suspend fails, `bus_generic_suspend` unwinds by resuming the children that had already suspended.

A typical bus driver uses these helpers directly:

```c
static device_method_t mybus_methods[] = {
        /* ... */
        DEVMETHOD(device_suspend, bus_generic_suspend),
        DEVMETHOD(device_resume,  bus_generic_resume),
        DEVMETHOD_END
};
```

The `virtio_pci_modern` bus driver does exactly this in `/usr/src/sys/dev/virtio/pci/virtio_pci_modern.c`, where `vtpci_modern_suspend` and `vtpci_modern_resume` each just call `bus_generic_suspend(dev)` and `bus_generic_resume(dev)`.

The **PCI bus itself** does something more sophisticated: its `bus_suspend_child` is `pci_suspend_child`, and its `bus_resume_child` is `pci_resume_child`. These helpers (in `/usr/src/sys/dev/pci/pci.c`) do exactly what Section 1 described: on suspend they call `pci_cfg_save` to cache the configuration space, then call the driver's `DEVICE_SUSPEND`, and then if `hw.pci.do_power_suspend` is true they call `pci_set_powerstate(child, PCI_POWERSTATE_D3)`. On resume they reverse the sequence: transition back to D0, restore configuration from cache, clear pending PME#, and call the driver's `DEVICE_RESUME`.

The `myfirst` driver, which attaches directly to a PCI device, does not implement bus methods itself; it is a leaf driver. Its power methods are the ones that matter for its own device's state. But the reader should be aware that the PCI bus has already done work on both sides of the driver's methods: on suspend, by the time the driver's `DEVICE_SUSPEND` runs, the PCI layer has already saved the configuration; on resume, by the time the driver's `DEVICE_RESUME` runs, the PCI layer has already restored the configuration and brought the device back to D0.

### pci_save_state and pci_restore_state: When the Driver Calls Them

The automatic save and restore handled by `pci_cfg_save`/`pci_cfg_restore` cover the standard PCI configuration registers: the BAR assignments, the command register, the cache-line size, the interrupt line, the MSI/MSI-X state. For most drivers, this is enough, and the driver does not need to call `pci_save_state` or `pci_restore_state` explicitly.

There are, however, situations where a driver does want to save the configuration manually. The PCI API exposes two helper functions for this:

```c
void pci_save_state(device_t dev);
void pci_restore_state(device_t dev);
```

`pci_save_state` is a wrapper around `pci_cfg_save` that caches the current configuration. `pci_restore_state` writes the cached configuration back; if the device is not in D0 when `pci_restore_state` is called, the helper transitions it to D0 before restoring.

A driver typically calls these in two scenarios:

1. **Before and after a manual `pci_set_powerstate` that the driver itself initiates**, for example in a runtime power-management helper. If the driver decides to put an idle device into D3 while the system is in S0, it calls `pci_save_state`, then `pci_set_powerstate(dev, PCI_POWERSTATE_D3)`. When it wakes the device back up, it calls `pci_set_powerstate(dev, PCI_POWERSTATE_D0)` followed by `pci_restore_state`.

2. **Inside `DEVICE_SUSPEND` and `DEVICE_RESUME`, when the automatic save/restore is disabled**. Some drivers set `hw.pci.do_power_suspend` to 0 for devices that misbehave in D3, and manage the power state themselves. In that case the driver is also responsible for saving and restoring configuration. This is an uncommon pattern.

Chapter 22's `myfirst` driver uses scenario 1 in Section 5 (runtime PM), where the driver chooses to park the device in D3 while the system stays in S0. For system suspend, the driver does not call these helpers directly; the PCI layer handles it.

### The pci_has_pm Helper

Not every PCI device has the PCI power-management capability. Older devices and some special-purpose ones do not advertise the capability, which means the driver cannot rely on `pci_set_powerstate` or `pci_enable_pme` working. The kernel provides a helper to check:

```c
bool pci_has_pm(device_t dev);
```

Returns true if the device exposes a power-management capability, false otherwise. Most modern PCIe devices return true. Drivers that want to be robust against unusual hardware guard their power-related calls:

```c
if (pci_has_pm(sc->dev))
        pci_enable_pme(sc->dev);
```

The Realtek driver in `/usr/src/sys/dev/re/if_re.c` uses this pattern in its `re_setwol` and `re_clrwol` functions: if the device does not have a PM capability, the function returns early without trying to touch power management.

### PME#: Enabling, Disabling, and Clearing

On a device that does have the PM capability, the driver can ask the hardware to raise PME# when a wake-relevant event occurs. The API is three short functions:

```c
void pci_enable_pme(device_t dev);
void pci_clear_pme(device_t dev);
/* there is no explicit pci_disable_pme; pci_clear_pme both clears pending
 * events and disables the PME_En bit. */
```

`pci_enable_pme` sets the PME_En bit in the device's power-management status/control register, so that the next power-management event the device detects causes it to assert PME#. `pci_clear_pme` clears any pending PME status bit and clears PME_En.

A driver that wants to enable wake-on-LAN, for example, typically:

1. Configures the device's own wake logic (set up pattern filters, set the magic-packet flag, etc.).
2. Calls `pci_enable_pme(dev)` in the suspend path so the device can actually assert PME#.
3. In the resume path, calls `pci_clear_pme(dev)` to acknowledge the wake event.

If `pci_enable_pme` is not called, the device will not raise PME# even if its own wake logic fires. If `pci_clear_pme` is not called on resume, a stale PME status bit can cause spurious future wake events.

The `myfirst` driver does not implement wake-on-X (the simulated device has nothing to wake on), so these calls do not appear in the main driver code. Section 4 includes a short sketch showing where they would go in a real driver.

### A First Skeleton: Stage 1

With all of the background in place, we can write the first version of `myfirst`'s suspend, resume, and shutdown methods. Stage 1 of the Chapter 22 driver does nothing of substance; it only logs and returns success. The point is to get the kernel to call the methods so the rest of the chapter can test progressively.

First, add prototypes near the top of `myfirst_pci.c`:

```c
static int myfirst_pci_suspend(device_t dev);
static int myfirst_pci_resume(device_t dev);
static int myfirst_pci_shutdown(device_t dev);
```

Next, extend the method table:

```c
static device_method_t myfirst_pci_methods[] = {
        DEVMETHOD(device_probe,    myfirst_pci_probe),
        DEVMETHOD(device_attach,   myfirst_pci_attach),
        DEVMETHOD(device_detach,   myfirst_pci_detach),
        DEVMETHOD(device_suspend,  myfirst_pci_suspend),
        DEVMETHOD(device_resume,   myfirst_pci_resume),
        DEVMETHOD(device_shutdown, myfirst_pci_shutdown),

        DEVMETHOD_END
};
```

Then implement the three functions at the end of the file:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "suspend (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_suspend_count, 1);
        return (0);
}

static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "resume (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_resume_count, 1);
        return (0);
}

static int
myfirst_pci_shutdown(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "shutdown (stage 1 skeleton)\n");
        atomic_add_64(&sc->power_shutdown_count, 1);
        return (0);
}
```

Add the counter fields to the softc in `myfirst.h`:

```c
struct myfirst_softc {
        /* ... existing fields ... */

        uint64_t power_suspend_count;
        uint64_t power_resume_count;
        uint64_t power_shutdown_count;
};
```

Expose them through sysctls next to the Chapter 21 counters, in whatever function already adds the `myfirst` sysctl tree:

```c
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_suspend_count",
    CTLFLAG_RD, &sc->power_suspend_count, 0,
    "Number of times DEVICE_SUSPEND has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_resume_count",
    CTLFLAG_RD, &sc->power_resume_count, 0,
    "Number of times DEVICE_RESUME has been called");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "power_shutdown_count",
    CTLFLAG_RD, &sc->power_shutdown_count, 0,
    "Number of times DEVICE_SHUTDOWN has been called");
```

Bump the version string in the Makefile:

```make
CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power-stage1\"
```

Build, load, and test:

```sh
cd /path/to/driver
make clean && make
sudo kldload ./myfirst.ko
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
sysctl dev.myfirst.0.power_suspend_count
sysctl dev.myfirst.0.power_resume_count
dmesg | tail -6
```

The expected output in `dmesg` is:

```text
myfirst0: suspend (stage 1 skeleton)
myfirst0: resume (stage 1 skeleton)
```

And the counters should each read 1. If they do not, one of three things is wrong: the driver did not rebuild, the method table did not include the new entries, or `devctl` reported an error because the kernel cannot find the device by that name.

### What the Skeleton Proves

Stage 1 looks trivial, but it proves three things that matter for the rest of the chapter:

1. **The kernel is delivering the methods.** If the counters increment, the kobj dispatch from `devctl` through the PCI bus to the `myfirst` driver is wired correctly. Every later stage builds on this, and it is easier to catch wiring bugs now than after adding real quiesce code.

2. **The method table is sourced correctly.** The `DEVMETHOD` lines with the method name and the driver's function pointer were typed correctly, the header includes are right, and the `DEVMETHOD_END` terminator is in place. A mistake here produces a kernel panic at load time, not a subtle runtime failure.

3. **The driver counts transitions.** The counters will be useful throughout the chapter as a cheap invariant check. `power_suspend_count` should always equal `power_resume_count` once the system is idle; any drift indicates a bug in one of the two methods.

With the skeleton in place, Section 3 can turn the suspend method from a log-only call into a real quiesce of the device's activity.

### A Note on Detach, Quiesce, and Suspend

The reader may wonder how `DEVICE_DETACH`, `DEVICE_QUIESCE`, and `DEVICE_SUSPEND` relate. They look similar; each asks the driver to stop doing something. Here is the practical distinction, as the kernel enforces it:

- **`DEVICE_QUIESCE`** is the softest. It asks the driver to stop accepting new work and to drain in-flight work, but the device stays attached, its resources stay allocated, and another request can reactivate it. The kernel calls this before `DEVICE_DETACH` to give the driver a chance to refuse if the device is busy.
- **`DEVICE_SUSPEND`** is in the middle. It asks the driver to stop activity but leaves the resources allocated, because the driver will need them again on resume. The device's state is preserved (partly by the kernel through PCI config save, partly by the driver through its own saved state).
- **`DEVICE_DETACH`** is the hardest. It asks the driver to stop activity, release all resources, and forget the device. The only way to come back is through a fresh attach.

Many drivers implement `DEVICE_QUIESCE` by reusing parts of the suspend path (stop interrupts, stop DMA, drain queues) and `DEVICE_SHUTDOWN` by calling the suspend method directly. `/usr/src/sys/dev/xl/if_xl.c` does exactly that: `xl_shutdown(dev)` just calls `xl_suspend(dev)`. The relationship is:

- `shutdown` ≈ `suspend` (for most drivers that do not distinguish shutdown-specific behavior like wake-on-LAN defaults differently)
- `quiesce` ≈ half of `suspend` (stop activity, do not save state)
- `suspend` = quiesce + save state
- `resume` = restore state + un-quiesce
- `detach` = quiesce + free resources

Chapter 22 implements suspend, resume, and shutdown in full. It does not implement `DEVICE_QUIESCE` for the `myfirst` driver, because the Chapter 21 detach path already quiesces correctly, and `device_quiesce` would be redundant. A driver that wanted to allow a "stop I/O but keep the device attached" state (for example, to support `devctl detach -f` gracefully) would add `DEVICE_QUIESCE` as a separate method. The chapter mentions this for completeness and moves on.

### The Detach Path as Inspiration

The Chapter 21 detach path is a useful reference because it already does most of what suspend needs to do. The detach path masks interrupts, drains the rx task and the simulation callout, waits for any in-flight DMA to complete, and calls `myfirst_dma_teardown`. The suspend path will do the first three (mask, drain, wait) and skip the last (teardown). A well-structured driver factors those common steps into shared helpers so that both paths use the same code.

Section 3 introduces exactly those helpers: `myfirst_stop_io`, `myfirst_drain_workers`, `myfirst_mask_interrupts`. Each is a small function extracted from the Chapter 21 detach path. Suspend uses them without tearing down resources; detach uses them and then tears down. The reuse keeps the two paths correct by construction.

### Observing the Skeleton with WITNESS

A reader who has built the debug kernel with `WITNESS` can now run the skeleton and watch for any lock-order warnings. There should be none, because the skeleton does not acquire any locks. Section 3 will add lock acquisition in the suspend path, and `WITNESS` will immediately notice if the order disagrees with the order used in detach. That is one of the benefits of working in stages: the baseline is quiet, so any later warnings are clearly attributable to the stage that introduced them.

### Wrapping Up Section 2

Section 2 established the FreeBSD-specific API for power management: the four kobj methods (`DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, `DEVICE_QUIESCE`), their return-value semantics, the order the kernel delivers them, the generic helpers (`bus_generic_suspend`, `bus_generic_resume`) that walk the device tree, and the PCI-specific helpers (`pci_suspend_child`, `pci_resume_child`) that automatically save and restore configuration space around the driver's methods. The Stage 1 skeleton gave the driver log-only implementations of the three main methods, added counters to the softc, and verified that `devctl suspend` and `devctl resume` invoke them correctly.

What the skeleton does not do is interact with the device at all. The interrupt handlers keep firing; DMA transfers can still be in flight when suspend returns; the device does not go quiet. Section 3 fixes all of that: it introduces the quiesce discipline, factors the common stop-I/O helpers out of the Chapter 21 detach path, and turns the Stage 1 skeleton into a Stage 2 driver that actually quiesces the device before reporting success.



## Section 3: Quiescing a Device Safely

Section 2 gave the driver skeleton suspend, resume, and shutdown methods that only log. Section 3 turns the suspend skeleton into a real suspend: one that stops interrupts, stops DMA, drains deferred work, and leaves the device in a defined quiet state before returning. Doing that correctly is the hardest single piece of power management, and it is where the Chapter 21 primitives pay off. If you have a clean DMA teardown path, a clean taskqueue drain, and a clean callout drain, you have most of what you need; quiesce is the art of applying them in the right order without tearing down the resources you will need back on resume.

### What Quiesce Really Means

The word "quiesce" appears in several places in FreeBSD (`DEVICE_QUIESCE`, `device_quiesce`, `pcie_wait_for_pending_transactions`), and it means something specific: **to bring a device to a state where no activity is ongoing, no activity can start, and the hardware is not about to raise any more interrupts or do any more DMA**. The device is still fully attached, still has all its resources, still has its interrupt handlers registered, but it is not doing anything and is not going to do anything until something tells it to start again.

Quiescing is different from detach because detach unwinds the resource allocations. Quiescing is also different from a simple "set a flag that says the device is busy, so that future requests block", because that flag only protects against new work entering the driver; it does not stop the hardware itself or the kernel-side infrastructure (tasks, callouts) from doing anything.

A quiesced device, in Chapter 22's sense, has these properties:

1. The device is not asserting, and cannot be provoked into asserting, an interrupt. Any interrupt mask the device has is set to suppress all sources. The interrupt handler, if it is called, has nothing to do.
2. The device is not performing DMA. Any in-flight DMA transfer has either completed or been aborted. The engine's control register is either in an idle state or has been explicitly reset.
3. The driver's deferred work has drained. Any task queued to the taskqueue has been executed or explicitly waited for. Any callout has been drained and will not fire.
4. The driver's softc fields reflect the quiesced state. The `dma_in_flight` flag is false. Any in-flight counter the driver keeps is zero. The `suspended` flag is true, so any new request the user space or another driver might submit gets an error.

Only when all four properties are true is the device actually quiet. A driver that masks interrupts but forgets to drain the taskqueue still has a task running in the background. A driver that drains the taskqueue but forgets the callout still has a timer that can fire. A driver that drains both but forgets to stop DMA can have a transfer commit bytes to memory after the CPU stopped looking. Each omission produces its own characteristic failure, and the cheapest way to avoid them all is to have one function that does the whole discipline in a known order.

### The Order Matters

The four steps above are not independent. They have a dependency order that the driver has to respect, because the kernel-side infrastructure and the device interact. Consider what happens if the order is wrong.

**If DMA is stopped before interrupts are masked**, an interrupt can arrive between the DMA stop and the mask. The filter runs, sees a stale status bit, schedules a task. The task runs, expects a DMA buffer to be populated, finds stale data, and may corrupt the driver's internal state. Better to mask interrupts first so no new interrupts arrive while the stop is in progress.

**If the taskqueue is drained before the device stops producing interrupts**, a new interrupt can fire after the drain returns and schedule a task on a queue that was just drained. The task runs later, out of sync with the suspend sequence. Better to stop interrupts first so no new tasks are scheduled.

**If the callout is drained before DMA is stopped**, a simulated engine driven by a callout might see its callout torn down while a transfer is still in progress. The transfer never completes; `dma_in_flight` stays true; the driver hangs waiting for a completion that cannot come. Better to stop DMA first, wait for completion, then drain the callout.

The safe order, used by Chapter 21's detach path and adapted for Chapter 22's suspend, is:

1. Mark the driver as suspended (set a softc flag so new requests bounce).
2. Mask all interrupts at the device (write the interrupt-mask register).
3. Stop DMA: if a transfer is in flight, abort it and wait for it to reach a terminal state.
4. Drain the taskqueue (any task already running gets to finish; no new tasks start).
5. Drain any callouts (any fire in flight gets to finish; no new firings happen).
6. Verify the invariant (`dma_in_flight == false`, `softc->busy == 0`, etc.).

Each step builds on the previous one. By step 6, the device is quiet.

### Helpers, Not Inline Code

A naive implementation would put all six steps inline in `myfirst_pci_suspend`. That works, but it duplicates code the detach path already has, and it makes both paths harder to maintain. The chapter's preferred pattern is to factor the steps into small helper functions that both paths call.

Three helpers are enough to cover the whole discipline:

```c
static void myfirst_mask_interrupts(struct myfirst_softc *sc);
static int  myfirst_drain_dma(struct myfirst_softc *sc);
static void myfirst_drain_workers(struct myfirst_softc *sc);
```

Each has one job:

- `myfirst_mask_interrupts` writes the device's interrupt-mask register to disable every vector the driver cares about. After it returns, no interrupt can arrive from this device.
- `myfirst_drain_dma` asks any in-flight DMA transfer to stop (sets the ABORT bit) and waits until `dma_in_flight` is false. It returns 0 on success, a non-zero errno if the device did not stop within the timeout.
- `myfirst_drain_workers` calls `taskqueue_drain` on the driver's taskqueue and `callout_drain` on the simulation's callout. After it returns, no deferred work is pending.

The suspend path calls all three in order. The detach path also calls all three, plus `myfirst_dma_teardown` and the resource-release calls. The two paths share the quiesce steps and differ only at the end.

Here is the quiesce entry point the suspend path uses:

```c
static int
myfirst_quiesce(struct myfirst_softc *sc)
{
        int err;

        MYFIRST_LOCK(sc);
        if (sc->suspended) {
                MYFIRST_UNLOCK(sc);
                return (0);  /* already quiet, nothing to do */
        }
        sc->suspended = true;
        MYFIRST_UNLOCK(sc);

        myfirst_mask_interrupts(sc);

        err = myfirst_drain_dma(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "quiesce: DMA did not stop cleanly (err %d)\n", err);
                /* Do not fail the quiesce; we still have to drain workers. */
        }

        myfirst_drain_workers(sc);

        return (err);
}
```

Note the design choice: `myfirst_quiesce` does not unwind on a DMA drain failure. A DMA that will not stop is a hardware problem, and the driver cannot undo the mask or unmark the suspend flag in response. The driver logs the problem, reports the error to the caller, and continues draining workers so that the rest of the state is still consistent. The caller (`myfirst_pci_suspend`) decides what to do with the error.

### Implementing myfirst_mask_interrupts

For the `myfirst` driver, masking interrupts means writing the device's interrupt-mask register. The Chapter 19 simulation backend already has an `INTR_MASK` register at a known offset; the driver writes all-ones to it to disable every source.

```c
static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
        MYFIRST_ASSERT_UNLOCKED(sc);

        /*
         * Disable all interrupt sources at the device. After this write,
         * the hardware will not assert any interrupt vector. Any already-
         * pending status bits remain, but the filter will not be called
         * to notice them.
         */
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);

        /*
         * For real hardware: also clear any pending status bits so we
         * don't see a stale interrupt on resume.
         */
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}
```

The function does not hold the softc lock. It talks only to the device through `CSR_WRITE_4`, which is a wrapper around `bus_write_4` that does not require any particular lock discipline. The `MYFIRST_ASSERT_UNLOCKED` call is a WITNESS-enabled invariant that catches a caller who mistakenly held the lock when calling this function; it is cheap and useful.

The mask value `0xFFFFFFFF` assumes the simulation's INTR_MASK register uses 1-means-masked semantics. The Chapter 19 simulation uses that convention; a real driver should consult its device's datasheet. The register map for `myfirst` is documented in `INTERRUPTS.md`; the reader can double-check the convention there.

A subtlety: on some real devices, masking interrupts only prevents the device from asserting new interrupts; the currently-active one continues to assert until the driver acknowledges it through the status register. That is why the function also clears INTR_STATUS: to make sure no stale bit is left asserted that could fire again after resume. On the simulation, the status register behaves similarly, so the same write is correct.

### Implementing myfirst_drain_dma

Draining DMA is the most delicate of the three helpers, because it has to wait for the device. The Chapter 21 driver tracks in-flight DMA with `dma_in_flight` and notifies completion through `dma_cv`. The suspend path reuses that exact machinery.

```c
static int
myfirst_drain_dma(struct myfirst_softc *sc)
{
        int err = 0;

        MYFIRST_LOCK(sc);
        if (sc->dma_in_flight) {
                /*
                 * Tell the engine to abort. The abort bit produces an
                 * ERR status that the filter will translate into a task
                 * that wakes us via cv_broadcast.
                 */
                CSR_WRITE_4(sc, MYFIRST_REG_DMA_CTRL,
                    MYFIRST_DMA_CTRL_ABORT);

                /*
                 * Wait up to one second for the abort to land. The
                 * cv_timedwait drops the lock while we sleep.
                 */
                err = cv_timedwait(&sc->dma_cv, &sc->mtx, hz);
                if (err == EWOULDBLOCK) {
                        device_printf(sc->dev,
                            "drain_dma: timeout waiting for abort\n");
                        /*
                         * Force the state forward. The device is
                         * beyond our reach at this point; treat the
                         * transfer as failed.
                         */
                        sc->dma_in_flight = false;
                }
        }
        MYFIRST_UNLOCK(sc);

        return (err == EWOULDBLOCK ? ETIMEDOUT : 0);
}
```

The function asks the DMA engine to abort, then sleeps on the condition variable used by the Chapter 21 completion path. If the completion arrives, the filter acknowledges it and enqueues a task; the task calls `myfirst_dma_handle_complete`, which does the POSTREAD/POSTWRITE sync, clears `dma_in_flight`, and broadcasts the CV. The suspend path's `cv_timedwait` returns, the drain function returns 0, and the suspend continues.

If the completion does not arrive within one second (a second is a generous timeout for a simulated device whose callout fires every few milliseconds), the function logs a warning and forces `dma_in_flight` to false. This is a defensive choice: a real device that does not honor abort within a second is misbehaving, and the driver has to move on. Leaving `dma_in_flight` true would deadlock the suspend. The cost of the defensive clear is that a very slow transfer could in principle complete after the suspend returned, writing into a buffer the driver no longer expects to be live. On the simulation, that cannot happen because the callout is drained in the next step. On real hardware, the risk is hardware-specific and a real driver would add device-specific recovery here.

The return value is 0 on a clean drain (including the timeout case, which has been force-cleared) and `ETIMEDOUT` if the caller needs to know a timeout happened. The suspend path logs the error but does not veto the suspend; by the time the drain has timed out, the device is effectively broken anyway.

### Implementing myfirst_drain_workers

Draining deferred work is easier because it does not involve the device. The rx task and the simulation's callout each have well-known drain primitives.

```c
static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
        /*
         * Drain the per-vector rx task. Any task currently running is
         * allowed to finish; no new tasks will be scheduled because
         * interrupts are already masked.
         */
        if (sc->rx_vector.has_task)
                taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);

        /*
         * Drain the simulation's DMA callout. Any fire in flight is
         * allowed to finish; no new firings will happen.
         *
         * This is a simulation-only call; a real-hardware driver would
         * omit it.
         */
        if (sc->sim != NULL)
                myfirst_sim_drain_dma_callout(sc->sim);
}
```

The function is safe to call with the lock released. `taskqueue_drain` is documented to do its own synchronisation; `callout_drain` (which `myfirst_sim_drain_dma_callout` wraps internally) is similarly safe.

An important property of both drain calls: they wait for running work to finish, but they do not cancel it in the middle. A task that is halfway through `myfirst_dma_handle_complete` will complete its work, including any `bus_dmamap_sync` and counter update, before the drain returns. That is the behavior we want: suspend should not interrupt a task partway through, because the task's invariants must hold for the resume path to be correct.

### Updating the Suspend Method

With the three helpers in place, the suspend method is short:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        device_printf(dev, "suspend: starting\n");

        err = myfirst_quiesce(sc);
        if (err != 0) {
                device_printf(dev,
                    "suspend: quiesce returned %d; continuing anyway\n",
                    err);
        }

        atomic_add_64(&sc->power_suspend_count, 1);
        device_printf(dev,
            "suspend: complete (dma in flight=%d, suspended=%d)\n",
            sc->dma_in_flight, sc->suspended);
        return (0);
}
```

The suspend method does not return the quiesce error to the kernel. That is a policy decision, and it deserves explanation.

Returning a non-zero value from `DEVICE_SUSPEND` vetoes the suspend, which has a large downstream effect: the kernel unwinds the partial suspend, reports a failure to the user, and leaves the system in S0. For Chapter 22's driver, a quiesce timeout does not justify that level of disruption. The device is still accessible; masking the interrupt and marking the suspended flag is enough to prevent any activity. A real driver might choose differently: a storage controller with an outstanding write might veto the suspend until the write completes, because losing that write across the transition would corrupt the filesystem. Each driver makes its own call.

The logging is verbose at this stage. Section 7 will introduce the ability to turn off the verbose logging (or crank it up for debugging) through a sysctl. For now, the extra detail helps when stepping through the suspend sequence for the first time.

### Saving Runtime State the Hardware Loses

So far the suspend path has only stopped activity. It has not saved any state. For most devices that is correct: the PCI layer saves configuration space automatically, and the device's runtime registers (the ones the driver writes through BARs) are either restored from the driver's softc on resume or regenerated from the driver's software state. The `myfirst` simulation does not have any BAR-local state the driver cares about preserving; the simulation starts fresh on resume, and the driver writes whatever registers it needs at that point.

A real driver may have more. Consider the `re(4)` driver: its `re_setwol` function writes wake-on-LAN-related registers into the NIC's EEPROM-backed configuration space before suspend. Those values are device-private; the PCI layer does not know about them. If the driver did not write them in suspend, the NIC would not know it was supposed to wake on a magic packet, and wake-on-LAN would not work.

For Chapter 22, the only state the `myfirst` driver saves is the interrupt mask's pre-suspend value. The Stage 2 suspend writes `0xFFFFFFFF` to the mask register, but the Stage 2 resume needs to know what value was there before (which determines which vectors are enabled in normal operation). The driver stores that in a softc field:

```c
struct myfirst_softc {
        /* ... */
        uint32_t saved_intr_mask;
};
```

And the mask helper saves it:

```c
static void
myfirst_mask_interrupts(struct myfirst_softc *sc)
{
        sc->saved_intr_mask = CSR_READ_4(sc, MYFIRST_REG_INTR_MASK);

        CSR_WRITE_4(sc, MYFIRST_REG_INTR_MASK, 0xFFFFFFFF);
        CSR_WRITE_4(sc, MYFIRST_REG_INTR_STATUS, 0xFFFFFFFF);
}
```

The resume path will write `sc->saved_intr_mask` back to the mask register after the device has been reinitialised. This is a minimal example of state-save-and-restore; Section 4 elaborates when it shows the full resume flow.

### The suspended Flag as a User-Facing Invariant

Setting `sc->suspended = true` during quiesce has a second purpose beyond suppressing new requests: it makes the state observable to user space. The driver can expose the flag through a sysctl:

```c
SYSCTL_ADD_BOOL(ctx, kids, OID_AUTO, "suspended",
    CTLFLAG_RD, &sc->suspended, 0,
    "Whether the driver is in the suspended state");
```

After `devctl suspend myfirst0`, the reader sees:

```sh
# sysctl dev.myfirst.0.suspended
dev.myfirst.0.suspended: 1
```

After `devctl resume myfirst0`, the value should go back to 0 (Section 4 wires the resume path to clear it). This is a quick way to check the driver's state without inferring it from other counters.

### Handling the Case Where DMA Is Not in Flight

The `myfirst_drain_dma` helper handles the case where a transfer is actively running. It should also handle the far more common case where nothing is in flight at the moment of suspend, without doing anything unnecessary.

The pseudocode above does handle this: the `if (sc->dma_in_flight)` guard skips the abort and wait entirely when the flag is false. The function returns 0 immediately and the suspend proceeds.

That path is fast: on an idle device, `myfirst_drain_dma` is a lock acquire, a flag check, and a lock release. The cost of the quiesce is dominated by `taskqueue_drain` (which does a full round-trip through the taskqueue thread) and `callout_drain` (similar). A typical idle-device suspend takes a few hundred microseconds to a few milliseconds, dominated by the deferred-work drains, not by the device.

### Testing Stage 2

With the quiesce code in place, the Stage 2 test is more interesting. The reader runs a transfer, then immediately suspends, and observes:

```sh
# Start with no activity.
sysctl dev.myfirst.0.dma_transfers_read
# 0

# Trigger a transfer. The transfer should complete quickly.
sudo sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.dma_transfers_read
# 1

# Now stress the path: start a transfer and immediately suspend.
sudo sysctl dev.myfirst.0.dma_test_read=1 &
sudo devctl suspend myfirst0

# Check the state.
sysctl dev.myfirst.0.suspended
# 1

sysctl dev.myfirst.0.power_suspend_count
# 1

sysctl dev.myfirst.0.dma_in_flight
# 0 (the transfer completed or was aborted)

dmesg | tail -8
```

The expected `dmesg` output shows the suspend log, the in-flight DMA being aborted, and the completion. If `dma_in_flight` is still 1 after the suspend returned, the abort did not take effect, and the reader should check the simulation's abort handling.

Then resume:

```sh
sudo devctl resume myfirst0

sysctl dev.myfirst.0.suspended
# 0 (after Section 4 is implemented)

sysctl dev.myfirst.0.power_resume_count
# 1

# Try another transfer to check the device is back.
sudo sysctl dev.myfirst.0.dma_test_read=1
dmesg | tail -4
```

The last transfer should succeed; if it does not, the suspend left the device in a state the resume did not recover. Section 4 teaches the resume path that makes this correct.

### A Cautious Note on Locking

The quiesce code runs from the `DEVICE_SUSPEND` method, which is called by the kernel's power-management path. That path holds no driver lock when it calls the method; the driver is responsible for its own synchronisation. The helpers in this section follow a specific discipline:

- `myfirst_mask_interrupts` holds no lock. It only writes hardware registers, which are atomic on PCIe.
- `myfirst_drain_dma` takes the softc lock to read `dma_in_flight` and uses `cv_timedwait` to sleep while holding the lock (which is the correct use of a sleep-mutex CV).
- `myfirst_drain_workers` holds no lock. `taskqueue_drain` and `callout_drain` do their own synchronisation and must be called without the lock to avoid deadlock (the task being drained may try to acquire the lock itself).

The full quiesce sequence thus acquires and releases the lock multiple times: once briefly at the top of `myfirst_quiesce` to set `suspended`, once inside `myfirst_drain_dma` for the sleep, and never inside `myfirst_drain_workers`. That is intentional. Holding the lock across `taskqueue_drain` would deadlock, because the drained task acquires the same lock on entry.

A reader who runs this code under `WITNESS` will not see any lock-order warnings, because the lock is only held over the CV sleep and no other locks are acquired during that window. If later work adds more locks to the driver (for example, a per-vector lock), the quiesce code should remain careful about which locks are held around the drain calls.

### Integrating with the Shutdown Method

The shutdown method shares almost all of its logic with suspend. A reasonable implementation is:

```c
static int
myfirst_pci_shutdown(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "shutdown: starting\n");
        (void)myfirst_quiesce(sc);
        atomic_add_64(&sc->power_shutdown_count, 1);
        device_printf(dev, "shutdown: complete\n");
        return (0);
}
```

The only difference from suspend is the absence of state-save calls (shutdown is final; there is no resume to save state for) and the absence of a return-value check on the quiesce (shutdown cannot be vetoed meaningfully). Many real drivers follow the same pattern; `/usr/src/sys/dev/xl/if_xl.c`'s `xl_shutdown` just calls `xl_suspend`. The `myfirst` driver can use either style; the chapter prefers the slightly more explicit version above because it makes the intent clearer in the code.

### Wrapping Up Section 3

Section 3 turned the Stage 1 skeleton into a Stage 2 driver that actually quiesces the device on suspend. It introduced the quiesce discipline (mark suspended, mask interrupts, stop DMA, drain workers, verify), factored the steps into three helper functions (`myfirst_mask_interrupts`, `myfirst_drain_dma`, `myfirst_drain_workers`), explained the order in which they must run and why, showed how to integrate them into the suspend and shutdown methods, and discussed the locking discipline.

What the Stage 2 driver does not yet do is come back correctly. The resume method is still the Stage 1 skeleton; it logs and returns 0 without restoring anything. If the suspended device had any state the hardware lost, that state is gone, and a subsequent transfer will fail. Section 4 fixes the resume path so that a full suspend-resume cycle leaves the device in the same state it was in before.



## Section 4: Restoring State on Resume

Section 3 gave the driver a correct suspend path. Section 4 writes the matching resume. The resume is the complement of the suspend: every thing suspend stopped, resume restarts; every value suspend saved, resume writes back; every flag suspend set, resume clears. The sequence is not an exact mirror (resume runs in a different kernel context, with the PCI layer having already done work, and the device in a different state than where suspend left it), but the contents correspond one-to-one. Doing the resume correctly is a matter of respecting the contract the PCI layer has already partly fulfilled, and filling in the rest.

### What the PCI Layer Has Already Done

When the kernel's `DEVICE_RESUME` method is called on the driver, several things have already happened:

1. The CPU has come out of the S-state (resumed from S3 or S4 back to S0).
2. Memory has been refreshed and the kernel has re-established its own state.
3. The parent bus has been resumed. For `myfirst`, that means the PCI bus driver has already handled the host bridge and the PCIe root complex.
4. The PCI layer has called `pci_set_powerstate(dev, PCI_POWERSTATE_D0)` on the device, transitioning it from whatever low-power state it was in (typically D3hot) back to full power.
5. The PCI layer has called `pci_cfg_restore(dev, dinfo)`, which writes the cached configuration space values (BARs, command register, cache-line size, etc.) back into the device.
6. The PCI layer has called `pci_clear_pme(dev)` to clear any pending power-management event bits.
7. The MSI or MSI-X configuration, which is part of the cached state, has been restored. The driver's interrupt vectors are usable again.

At this point the PCI bus driver calls into `myfirst`'s `DEVICE_RESUME`. The device is in D0, with its BARs mapped, its MSI/MSI-X table restored, and its generic PCI state intact. What the driver has to restore is the device-specific state that the PCI layer did not know about: the BAR-local registers the driver wrote during or after attach.

For the `myfirst` simulation, the relevant BAR-local registers are the interrupt mask (which the suspend path deliberately set to all-masked) and the DMA registers (which may have been left in an aborted state). The driver needs to put them back to values that reflect normal operation.

### The Resume Discipline

A correct resume path does four things, in order:

1. **Re-enable bus-mastering**, in case the configuration-space restore did not do so or the PCI layer's automatic restore was disabled. This is `pci_enable_busmaster(dev)`. On modern FreeBSD it is usually redundant but harmless; older code paths or buggy BIOSes sometimes leave bus-mastering disabled. Calling it defensively is cheap.

2. **Restore any device-specific state** the driver saved during suspend. For `myfirst`, that means writing `saved_intr_mask` back to the INTR_MASK register. A real driver would also restore things like vendor-specific configuration bits, DMA engine programming, hardware timers, etc.

3. **Unmask interrupts and clear the suspended flag**, so the device can resume activity. This is the pivot point: before it, the device is still quiet; after it, the device can raise interrupts and accept work.

4. **Log the transition and update counters**, for observability and regression testing.

Here is what the pattern looks like in code:

```c
static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        device_printf(dev, "resume: starting\n");

        err = myfirst_restore(sc);
        if (err != 0) {
                device_printf(dev,
                    "resume: restore failed (err %d)\n", err);
                atomic_add_64(&sc->power_resume_errors, 1);
                /*
                 * Continue anyway. By the time we're here, the system
                 * is coming back whether we like it or not.
                 */
        }

        atomic_add_64(&sc->power_resume_count, 1);
        device_printf(dev, "resume: complete\n");
        return (0);
}
```

The helper `myfirst_restore` does the three real steps:

```c
static int
myfirst_restore(struct myfirst_softc *sc)
{
        /* Step 1: re-enable bus-master (defensive). */
        pci_enable_busmaster(sc->dev);

        /* Step 2: restore device-specific state.
         *
         * For myfirst, this is just the interrupt mask. A real driver
         * would restore more: DMA engine programming, hardware timers,
         * vendor-specific configuration, etc.
         */
        if (sc->saved_intr_mask == 0xFFFFFFFF) {
                /*
                 * Suspend saved a fully-masked mask, which means the
                 * driver had no idea what the mask should be. Use the
                 * default: enable DMA completion, disable everything
                 * else.
                 */
                sc->saved_intr_mask = ~MYFIRST_INTR_COMPLETE;
        }
        CSR_WRITE_4(sc->dev, MYFIRST_REG_INTR_MASK, sc->saved_intr_mask);

        /* Step 3: clear the suspended flag and unmask the device. */
        MYFIRST_LOCK(sc);
        sc->suspended = false;
        MYFIRST_UNLOCK(sc);

        return (0);
}
```

The function returns 0 because no step above can fail in the `myfirst` simulation. A real driver would check the return values of its hardware initialisation calls and propagate any errors.

### Why pci_enable_busmaster Matters

Bus-mastering is a bit in the PCI command register that controls whether the device can issue DMA transactions. Without it, the device cannot read or write host memory; any DMA trigger would be silently ignored by the PCI host bridge.

Chapter 18 enabled bus-mastering during attach. The PCI layer's automatic config-space restore writes the command register back to its saved value, which includes the bus-master bit. So in principle the driver does not need to call `pci_enable_busmaster` again on resume. In practice, several things can go wrong:

- The platform firmware may reset the command register as part of waking the device.
- The `hw.pci.do_power_suspend` sysctl may be 0, in which case the PCI layer does not save and restore the config space.
- A device-specific quirk might clear bus-mastering as a side effect of the D3-to-D0 transition.

Calling `pci_enable_busmaster` unconditionally defensively in resume is a low-cost safety net. Several production FreeBSD drivers follow this pattern; `if_re.c`'s resume path is one example. The call is idempotent: if bus-mastering is already on, the call just re-asserts it.

### Restoring Device-Specific State

The `myfirst` simulation does not have much state the driver needs to restore manually. The BAR-local registers are:

- The interrupt mask (restored from `saved_intr_mask`).
- The interrupt status bits (were cleared in suspend; they should stay cleared until new activity arrives).
- The DMA engine registers (DMA_ADDR_LOW, DMA_ADDR_HIGH, DMA_LEN, DMA_DIR, DMA_CTRL, DMA_STATUS). These are transient: they hold the parameters of the current transfer. After resume, no transfer is in progress, so the values do not matter; the next transfer will overwrite them.

A real driver would have more. Consider a few examples:

- A storage driver might have a DMA descriptor ring whose base address the device learned during attach. After resume, the BAR-level register holding that base address may have been reset; the driver needs to reprogram it.
- A network driver might have filter tables (MAC addresses, multicast lists, VLAN tags) programmed into device registers. After resume, those tables may be empty; the driver rebuilds them from softc-side copies.
- A GPU driver might have register state for display timing, colour tables, hardware cursors. After resume, the driver restores the active mode.

For `myfirst`, the interrupt mask is the only BAR-local state that needs restoring. The pattern shown above is the template a real driver would adapt to its device.

### Validating Device Identity After Resume

Some devices are reset completely across a suspend-to-D3-cold cycle. The device that comes back is functionally the same, but its entire state has been reinitialised as if it had just powered on. A driver that assumed nothing changed would silently get wrong behaviour.

A defensive resume path can detect this by reading a known register value and comparing to what it read at attach time. For a PCI device, the vendor ID and device ID in configuration space are always the same (the PCI layer restored them), but some device-private register (a revision ID, a self-test register, a firmware version) can be checked:

```c
static int
myfirst_validate_device(struct myfirst_softc *sc)
{
        uint32_t magic;

        magic = CSR_READ_4(sc->dev, MYFIRST_REG_MAGIC);
        if (magic != MYFIRST_MAGIC_VALUE) {
                device_printf(sc->dev,
                    "resume: device identity mismatch (got %#x, "
                    "expected %#x)\n", magic, MYFIRST_MAGIC_VALUE);
                return (EIO);
        }
        return (0);
}
```

For the `myfirst` simulation, there is no magic register (the simulation was not built with post-resume validation in mind). A reader who wants to add one as a challenge can extend the simulation backend's register map with a read-only `MAGIC` register, and have the driver check it. The chapter's Lab 3 includes this as an option.

A real driver whose device truly does reset across D3cold needs this check, because without it a subtle failure can occur: the driver assumes the device's internal state machine is in state `IDLE`, but after the reset the state machine is actually in state `RESETTING`. Any command the driver sends is rejected, the driver interprets the rejection as a hardware fault, and the device is marked broken. Catching the reset explicitly and rebuilding state is cleaner.

### Detecting and Recovering from a Device Reset

If the validation finds a mismatch, the driver's recovery options depend on the hardware. For the `myfirst` simulation, the simplest response is to log, mark the device broken, and fail subsequent operations:

```c
if (myfirst_validate_device(sc) != 0) {
        MYFIRST_LOCK(sc);
        sc->broken = true;
        MYFIRST_UNLOCK(sc);
        return (EIO);
}
```

The softc grows a `broken` flag, and any user-facing request checks the flag and fails with an error. The detach path still works (detach always succeeds, even on a broken device), so the user can unload the driver and reload it.

A real driver that detects a reset has more options. A network driver might re-run its attach sequence from the point after `pci_alloc_msi` (which has been restored by the PCI layer). A storage driver might re-initialise its controller using the same code path attach used. The implementation depends heavily on the device; the pattern is "detect, then do whatever attach-time initialisation is still required".

The chapter's `myfirst` driver takes the simpler approach: it does not implement reset detection for the simulation, and the resume path does not include the validation call by default. The code above is provided as reference for a reader who wants to extend the driver as an exercise.

### Restoring DMA State

The Chapter 21 DMA setup allocates a tag, allocates memory, loads the map, and retains the bus address in the softc. None of that is visible in the BAR-local register map; the DMA engine learns the bus address only when the driver writes it to `DMA_ADDR_LOW` and `DMA_ADDR_HIGH` as part of starting a transfer.

This means the DMA state does not need restoration in the sense of "write registers". The tag, map, and memory are all kernel-side data structures; they survive suspend intact. The next transfer will program the DMA registers as part of its normal submission.

What might need restoration on a real device is:

- **The DMA descriptor ring base address**, if the device keeps a persistent pointer. A real NIC writes a base-address register once at attach and points the device at a ring of descriptors; after D3cold, that register may have been reset and the driver must reprogram it.
- **The DMA engine's enable bit**, if it is separate from individual transfers.
- **Any per-channel configuration** (burst size, priority, etc.) that is held in registers the PCI layer did not cache.

For `myfirst`, none of this applies. The DMA engine is programmed per transfer. Resume does not need any DMA-specific restoration beyond what the generic state restoration already covered.

### Re-Arming Interrupts

Masking interrupts was step 2 of suspend. Unmasking them is step 3 of resume. The Stage 3 resume writes `saved_intr_mask` back to the `INTR_MASK` register, which (by convention) writes 0 to the bits corresponding to enabled vectors and 1 to the bits for disabled vectors. After the write, the device is ready to assert interrupts on the enabled vectors as soon as there is reason to.

There is a subtlety around ordering. The resume path unmasks interrupts before it clears the `suspended` flag. That means a very unfortunate interrupt could arrive, call the filter, and find `suspended == true`. The filter would refuse to handle it and return `FILTER_STRAY`, which would leave the interrupt asserted.

To avoid that, the resume path takes the softc lock around the state change and does the unmask and the flag clear in the opposite order: clear `suspended` first, then unmask. That way any interrupt the device raises after the mask clears sees `suspended == false` and is handled normally.

The code in the previous snippet does this correctly: `myfirst_restore` writes the mask, then acquires the lock, clears the flag, and releases the lock. The order is important; reversing it creates a narrow window where interrupts could be lost.

### Wake Source Cleanup

If the driver enabled a wake source during suspend (`pci_enable_pme`), the resume path should clear any pending wake event (`pci_clear_pme`). The PCI layer's `pci_resume_child` helper already calls `pci_clear_pme(child)` before the driver's `DEVICE_RESUME`, so the driver does not usually need to call it again.

The one case where the driver might want to call `pci_clear_pme` explicitly is in a runtime-PM context where the driver is resuming the device while the system stays in S0. In that case `pci_resume_child` was not involved, and the driver is responsible for clearing the PME status itself.

A hypothetical sketch for a driver with wake-on-X:

```c
static int
myfirst_pci_resume(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        if (pci_has_pm(dev))
                pci_clear_pme(dev);  /* defensive; PCI layer already did this */

        /* ... rest of the resume path ... */
}
```

For `myfirst`, there is no wake source, so the call does nothing useful; the chapter omits it from the main code and mentions the pattern here for completeness.

### Updating the Stage 3 Driver

Stage 3 brings together everything above into a single working resume. The diff against Stage 2 is:

- `myfirst.h` grows a `saved_intr_mask` field (added for Stage 2) and a `broken` flag.
- `myfirst_pci.c` gets a `myfirst_restore` helper and a rewritten `myfirst_pci_resume`.
- The Makefile version bumps to `1.5-power-stage3`.

Build and test:

```sh
cd /path/to/driver
make clean && make
sudo kldunload myfirst     # unload any previous version
sudo kldload ./myfirst.ko

# Quiet baseline.
sysctl dev.myfirst.0.dma_transfers_read
# 0
sysctl dev.myfirst.0.suspended
# 0

# Full cycle.
sudo devctl suspend myfirst0
sysctl dev.myfirst.0.suspended
# 1

sudo devctl resume myfirst0
sysctl dev.myfirst.0.suspended
# 0

# A transfer after resume should work.
sudo sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.dma_transfers_read
# 1

# Do it several times to make sure the path is stable.
for i in 1 2 3 4 5; do
  sudo devctl suspend myfirst0
  sudo devctl resume myfirst0
  sudo sysctl dev.myfirst.0.dma_test_read=1
done
sysctl dev.myfirst.0.dma_transfers_read
# 6 (1 + 5)

sysctl dev.myfirst.0.power_suspend_count dev.myfirst.0.power_resume_count
# should be equal, around 6 each
```

If the counters drift (suspend count not equal to resume count) or if `dma_test_read` starts failing after a suspend, something in the restore path is not putting the device back into a usable state. The first debugging step is to read the INTR_MASK and compare against `saved_intr_mask`; the second is to trace the DMA engine's status register and see if it is reporting an error.

### Interaction with the Chapter 20 MSI-X Setup

The `myfirst` driver from Chapter 20 uses MSI-X when available, with a three-vector layout (admin, rx, tx). The MSI-X configuration lives in the device's MSI-X capability registers and in a kernel-side table. The PCI layer's config-space save-and-restore covers the capability registers; the kernel-side state is not affected by the D-state transition.

This means the `myfirst` driver does not need to do anything special to restore its MSI-X vectors. The interrupt resources (`irq_res`) remain allocated, the cookies remain registered, the CPU bindings remain in place. When the device raises an MSI-X vector on resume, the kernel delivers it to the filter that was registered at attach time.

A reader who wants to verify this can write to one of the simulate sysctls after resume and observe that the corresponding per-vector counter increments:

```sh
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
sudo sysctl dev.myfirst.0.intr_simulate_admin=1
sysctl dev.myfirst.0.vec0_fire_count
# should be incremented
```

If the counter does not increment, the MSI-X path has been disturbed. The most likely cause is a bug in the driver's own state management (the `suspended` flag was not cleared, or the filter is rejecting the interrupt for a different reason). The chapter's troubleshooting section has more detail.

### Handling a Failed Resume Gracefully

If some step of the resume fails, the driver has limited options. It cannot veto the resume (the kernel has no unwind path at this point). It cannot usually retry (the hardware state is uncertain). The best it can do is:

1. Log the failure prominently with `device_printf` so the user sees it in dmesg.
2. Increment a counter (`power_resume_errors`) that a regression script or an observability tool can check.
3. Mark the device broken so that subsequent requests fail cleanly rather than silently corrupting data.
4. Keep the driver attached, so the device-tree state stays consistent and the user can eventually unload and reload the driver.
5. Return 0 from `DEVICE_RESUME`, because the kernel expects it to succeed.

The "mark broken, keep attached" pattern is common in production drivers. It moves the failure from "mysterious later corruption" to "immediate user-visible error", which is a better debugging experience.

### A Short Detour: pci_save_state / pci_restore_state in Runtime PM

Section 2 mentioned that `pci_save_state` and `pci_restore_state` are sometimes called by the driver itself, typically in a runtime power-management helper. This is worth a concrete sketch before Section 5 builds it out.

A runtime PM helper that puts an idle device into D3 looks like:

```c
static int
myfirst_runtime_suspend(struct myfirst_softc *sc)
{
        int err;

        err = myfirst_quiesce(sc);
        if (err != 0)
                return (err);

        pci_save_state(sc->dev);
        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
        if (err != 0) {
                /* roll back */
                pci_restore_state(sc->dev);
                myfirst_restore(sc);
                return (err);
        }

        return (0);
}

static int
myfirst_runtime_resume(struct myfirst_softc *sc)
{
        int err;

        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
        if (err != 0)
                return (err);
        pci_restore_state(sc->dev);

        return (myfirst_restore(sc));
}
```

The pattern is similar to the system suspend/resume but uses the explicit PCI helpers because the PCI layer is not in the loop. Section 5 will turn this sketch into a real implementation and wire it to an idle-detection policy.

### A Reality Check Against a Real Driver

Before moving on, it is worth pausing and looking at a real driver's resume path. `/usr/src/sys/dev/re/if_re.c`'s `re_resume` function is about thirty lines. Its structure is:

1. Lock the softc.
2. If a MAC-sleep flag is set, take the chip out of sleep mode by writing a GPIO register.
3. Clear any wake-on-LAN patterns so normal receive filtering is not interfered with.
4. If the interface is administratively up, re-initialise it via `re_init_locked`.
5. Clear the `suspended` flag.
6. Unlock the softc.
7. Return 0.

The `re_init_locked` call is the substantive work: it reprograms the MAC address, resets the receive and transmit descriptor rings, re-enables interrupts on the NIC, and starts the DMA engines. For `myfirst`, the equivalent work is much shorter because the device is much simpler, but the shape is the same: acquire state, do hardware-specific reinitialisation, unlock, return.

A reader who reads `re_resume` after implementing `myfirst`'s resume will recognise the structure immediately. The vocabulary is the same; only the details differ.

### Wrapping Up Section 4

Section 4 completed the resume path. It showed what the PCI layer has already done by the time `DEVICE_RESUME` is called (D0 transition, config-space restore, PME# clear, MSI-X restore), what the driver still has to do (re-enable bus-master, restore device-specific registers, clear the suspended flag, unmask interrupts), and why each step is important. The Stage 3 driver can now do a full suspend-resume cycle and continue operating normally; the regression test can run several cycles in a row and verify the counters are consistent.

With Sections 3 and 4 together, the driver is power-aware in the system-suspend sense: it handles S3 and S4 transitions cleanly. What it still does not do is any device-level power saving while the system is running. That is runtime power management, and Section 5 teaches it.



## Section 5: Handling Runtime Power Management

System suspend is a big, visible transition: the lid closes, the screen goes dark, the battery saves power for hours. Runtime power management is the opposite: dozens of small, invisible transitions a second, each saving a little, together saving much of the idle power a modern system draws. The user never notices them; the platform engineer lives or dies by their correctness.

This section is marked optional in the chapter outline because not every driver needs runtime PM. A driver for a device that is always active (a NIC on a busy server, a disk controller for the root filesystem) does not save power by attempting to suspend its device; the device is busy, and trying to suspend it wastes cycles setting up transitions that never complete. A driver for a device that is frequently idle (a webcam, a fingerprint reader, a WLAN card on a laptop) does benefit. Whether to add runtime PM is a policy decision driven by the device's usage profile.

For Chapter 22, we implement runtime PM on the `myfirst` driver as a learning exercise. The device is already simulated; we can pretend it is idle whenever no sysctl has been written in the last few seconds, and watch the driver go through the motions. The implementation is short, and it teaches the PCI-level primitives that a real runtime-PM driver uses.

### What Runtime PM Means in FreeBSD

FreeBSD does not currently have a centralised runtime-PM framework the way Linux does. There is no kernel-side "if the device has been idle for N milliseconds, call its idle hook" machinery. Instead, runtime PM is driver-local: the driver decides when to suspend and resume its device, using the same PCI-layer primitives (`pci_set_powerstate`, `pci_save_state`, `pci_restore_state`) it would use inside `DEVICE_SUSPEND` and `DEVICE_RESUME`.

This has two consequences. First, every driver that wants runtime PM implements its own policy: how long the device must be idle before suspending, what counts as idle, how quickly the device must wake on demand. Second, the driver must integrate its runtime PM with its system PM; the two paths share a lot of code and must not step on each other.

The pattern Chapter 22 uses is straightforward:

1. The driver adds a small state machine with states `RUNNING` and `RUNTIME_SUSPENDED`.
2. When the driver observes idleness (Section 5 uses a callout-based "no requests in the last 5 seconds" policy), it calls `myfirst_runtime_suspend`.
3. When the driver observes a new request while in `RUNTIME_SUSPENDED`, it calls `myfirst_runtime_resume` before processing the request.
4. On system suspend, if the device is in `RUNTIME_SUSPENDED`, the system-suspend path adjusts for it (the device is already quiesced; the system-suspend quiesce is a no-op, but the system resume has to bring the device back to D0).
5. On system resume, the driver returns to `RUNNING` unless it was explicitly runtime-suspended and wants to stay that way.

This is simpler than Linux's runtime PM framework, which has richer concepts (parent/child ref-counting, autosuspend timers, barriers). For a single driver on simple hardware, the FreeBSD approach is enough.

### The Runtime-PM State Machine

The softc gains a state variable and a timestamp:

```c
enum myfirst_runtime_state {
        MYFIRST_RT_RUNNING = 0,
        MYFIRST_RT_SUSPENDED = 1,
};

struct myfirst_softc {
        /* ... */
        enum myfirst_runtime_state runtime_state;
        struct timeval             last_activity;
        struct callout             idle_watcher;
        int                        idle_threshold_seconds;
        uint64_t                   runtime_suspend_count;
        uint64_t                   runtime_resume_count;
};
```

The `idle_threshold_seconds` is a policy knob exposed through a sysctl; defaulting to five seconds gives quick observability without being so aggressive as to cause unnecessary wake-ups during normal use. A production driver would tune this per-device; five seconds is a learning-friendly value that makes the transitions visible without requiring hours of waiting.

The `idle_watcher` callout fires once a second to check the idle time. If the device has been idle longer than `idle_threshold_seconds` and is currently in `RUNNING`, the callout triggers `myfirst_runtime_suspend`.

### Implementation

The attach path starts the idle watcher:

```c
static void
myfirst_start_idle_watcher(struct myfirst_softc *sc)
{
        sc->idle_threshold_seconds = 5;
        microtime(&sc->last_activity);
        callout_init_mtx(&sc->idle_watcher, &sc->mtx, 0);
        callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}
```

The callout is initialised with the softc mutex, so it acquires the mutex automatically when firing. That simplifies the callback: it runs under the lock.

The callback checks the time since the last activity and suspends if needed:

```c
static void
myfirst_idle_watcher_cb(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct timeval now, diff;

        MYFIRST_ASSERT_LOCKED(sc);

        if (sc->runtime_state == MYFIRST_RT_RUNNING && !sc->suspended) {
                microtime(&now);
                timersub(&now, &sc->last_activity, &diff);

                if (diff.tv_sec >= sc->idle_threshold_seconds) {
                        /*
                         * Release the lock while suspending. The
                         * runtime_suspend helper acquires it again as
                         * needed.
                         */
                        MYFIRST_UNLOCK(sc);
                        (void)myfirst_runtime_suspend(sc);
                        MYFIRST_LOCK(sc);
                }
        }

        /* Reschedule. */
        callout_reset(&sc->idle_watcher, hz, myfirst_idle_watcher_cb, sc);
}
```

Note the lock-drop around `myfirst_runtime_suspend`. The suspend helper calls `myfirst_quiesce`, which acquires the lock itself. Holding the lock across it would deadlock.

Activity is recorded whenever the driver services a request. The Chapter 21 DMA path is a good hook: every time a user writes to `dma_test_read` or `dma_test_write`, the sysctl handler records activity:

```c
static int
myfirst_dma_sysctl_test_write(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        /* ... existing code ... */

        /* Mark the device active before processing. */
        myfirst_mark_active(sc);

        /* If runtime-suspended, bring the device back before running. */
        if (sc->runtime_state == MYFIRST_RT_SUSPENDED) {
                int err = myfirst_runtime_resume(sc);
                if (err != 0)
                        return (err);
        }

        /* ... proceed with the transfer ... */
}
```

The `myfirst_mark_active` helper is a one-liner:

```c
static void
myfirst_mark_active(struct myfirst_softc *sc)
{
        MYFIRST_LOCK(sc);
        microtime(&sc->last_activity);
        MYFIRST_UNLOCK(sc);
}
```

### The Runtime-Suspend and Runtime-Resume Helpers

These were sketched in Section 4. Here are the fleshed-out versions:

```c
static int
myfirst_runtime_suspend(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "runtime suspend: starting\n");

        err = myfirst_quiesce(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime suspend: quiesce failed (err %d)\n", err);
                /* Undo the suspended flag the quiesce set. */
                MYFIRST_LOCK(sc);
                sc->suspended = false;
                MYFIRST_UNLOCK(sc);
                return (err);
        }

        pci_save_state(sc->dev);
        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime suspend: set_powerstate(D3) failed "
                    "(err %d)\n", err);
                pci_restore_state(sc->dev);
                myfirst_restore(sc);
                return (err);
        }

        MYFIRST_LOCK(sc);
        sc->runtime_state = MYFIRST_RT_SUSPENDED;
        MYFIRST_UNLOCK(sc);
        atomic_add_64(&sc->runtime_suspend_count, 1);

        device_printf(sc->dev, "runtime suspend: device in D3\n");
        return (0);
}

static int
myfirst_runtime_resume(struct myfirst_softc *sc)
{
        int err;

        MYFIRST_LOCK(sc);
        if (sc->runtime_state != MYFIRST_RT_SUSPENDED) {
                MYFIRST_UNLOCK(sc);
                return (0);  /* nothing to do */
        }
        MYFIRST_UNLOCK(sc);

        device_printf(sc->dev, "runtime resume: starting\n");

        err = pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime resume: set_powerstate(D0) failed "
                    "(err %d)\n", err);
                return (err);
        }
        pci_restore_state(sc->dev);

        err = myfirst_restore(sc);
        if (err != 0) {
                device_printf(sc->dev,
                    "runtime resume: restore failed (err %d)\n", err);
                return (err);
        }

        MYFIRST_LOCK(sc);
        sc->runtime_state = MYFIRST_RT_RUNNING;
        MYFIRST_UNLOCK(sc);
        atomic_add_64(&sc->runtime_resume_count, 1);

        device_printf(sc->dev, "runtime resume: device in D0\n");
        return (0);
}
```

The shape is identical to system suspend/resume except that the driver explicitly calls `pci_set_powerstate` and `pci_save_state`/`pci_restore_state`. The PCI layer's automatic transitions are not in the loop for runtime PM because the kernel is not coordinating a system-wide power change; the driver is on its own.

### Interaction Between Runtime PM and System PM

The two paths have to cooperate. Consider what happens if the device is runtime-suspended (in D3) when the user closes the laptop lid:

1. The kernel starts system suspend.
2. The PCI bus calls `myfirst_pci_suspend`.
3. Inside `myfirst_pci_suspend`, the driver notices that the device is already runtime-suspended. The quiesce is a no-op (nothing is happening). The PCI layer's automatic config-space save runs; it reads the config space (which is still accessible in D3) and caches it.
4. The PCI layer transitions the device from D3 to... wait, it is already in D3. The transition to D3 is a no-op.
5. The system sleeps.
6. On wake, the PCI layer transitions the device back to D0. The driver's `myfirst_pci_resume` runs. It restores state. But now the driver thinks the device is `RUNNING` (because system resume cleared the `suspended` flag), while conceptually it was runtime-suspended before. The next activity will use the device normally and set `last_activity`; the idle watcher will eventually re-suspend it if still idle.

The interaction is mostly benign; the worst that happens is that the device gets one extra trip through D0 before the idle watcher re-suspends it. A more polished implementation would remember the runtime-suspended state across the system suspend and restore it, but for a learning driver the simple approach is enough.

The reverse (system-suspending a device that is already runtime-suspended) is already correct in our implementation because `myfirst_quiesce` checks `suspended` and returns 0 if already set. The runtime-suspended path set `suspended = true` as part of its quiesce, so the system suspend's quiesce sees the flag and skips.

### Exposing Runtime-PM Controls Through Sysctl

The driver's runtime-PM policy can be controlled and observed through sysctls:

```c
SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "idle_threshold_seconds",
    CTLFLAG_RW, &sc->idle_threshold_seconds, 0,
    "Runtime PM idle threshold (seconds)");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_suspend_count",
    CTLFLAG_RD, &sc->runtime_suspend_count, 0,
    "Runtime suspends performed");
SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "runtime_resume_count",
    CTLFLAG_RD, &sc->runtime_resume_count, 0,
    "Runtime resumes performed");
SYSCTL_ADD_INT(ctx, kids, OID_AUTO, "runtime_state",
    CTLFLAG_RD, (int *)&sc->runtime_state, 0,
    "Runtime state: 0=running, 1=suspended");
```

A reader can now do this:

```sh
# Watch the device idle out.
while :; do
        sysctl dev.myfirst.0.runtime_state dev.myfirst.0.runtime_suspend_count
        sleep 1
done &
```

After five seconds of inactivity, `runtime_state` flips from 0 to 1 and `runtime_suspend_count` increments. A write to any active sysctl triggers a resume and flips the state back:

```sh
sudo sysctl dev.myfirst.0.dma_test_read=1
# The log shows: runtime resume, then the test read
```

### Tradeoffs

Runtime PM trades wake-up latency for idle power. Every D3-to-D0 transition costs time (tens of microseconds on a PCIe link, including the ASPM exit), and on some devices costs energy (the transition itself draws current). For a device that is idle most of the time with rare bursts of activity, the trade is favorable. For a device that is active most of the time with rare idle periods, the cost of the transitions dominates.

The `idle_threshold_seconds` knob lets the platform tune this. A value of 0 or 1 is aggressive and suitable for a webcam that is used for seconds at a time and idle for minutes. A value of 60 is conservative and suitable for a NIC whose idle periods are short but frequent. A value of 0 (if allowed) would disable runtime PM entirely, which is appropriate for devices that should stay on at all times.

A second tradeoff is in code complexity. Runtime PM adds a state machine, a callout, an idle watcher, two more kobj-like helpers, and additional ordering concerns between the runtime and system PM paths. Each of those is small, but together they increase the surface area for bugs. Many FreeBSD drivers deliberately omit runtime PM for this reason; they let the device stay in D0 and rely on the device's internal low-power states (clock gating, PCIe ASPM) to save power. That is a defensible choice, and for drivers where correctness matters more than milliwatts, it is the right one.

Chapter 22's `myfirst` driver keeps runtime PM as an optional feature, gated by a build-time flag:

```make
CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM
```

A reader can build with or without the flag; the Section 5 code is only compiled in when the flag is defined. The default for Stage 3 is to leave runtime PM off; Stage 4 enables it in the consolidated driver.

### A Word on Platform Runtime PM

Some platforms provide their own runtime-PM mechanism alongside the driver-local one. On arm64 and RISC-V embedded systems, the device tree may describe `power-domains` and `clocks` properties that the driver uses to turn off power domains and gate clocks. FreeBSD's `ext_resources/clk`, `ext_resources/regulator`, and `ext_resources/power` subsystems handle these.

Runtime PM on such a platform is more capable than PCI-only runtime PM because the platform can turn off entire SoC blocks (a USB controller, a display engine, a GPU) rather than just moving the PCI device to D3. The driver uses the same pattern (mark idle, turn off resources on idle, turn back on for activity) but through different APIs.

Chapter 22 stays with the PCI path because that is where the `myfirst` driver lives. A reader who later works on an embedded platform will find the same conceptual structure with platform-specific APIs. The chapter mentions the distinction here so the reader knows the territory exists.

### Wrapping Up Section 5

Section 5 added runtime power management to the driver. It defined a two-state machine (`RUNNING`, `RUNTIME_SUSPENDED`), a callout-based idle watcher, a pair of helpers (`myfirst_runtime_suspend`, `myfirst_runtime_resume`) that use the PCI layer's explicit power-state and state-save APIs, the activity-recording hooks in the DMA sysctl handlers, and the sysctl knobs that expose the policy to user space. It also discussed the interaction between runtime PM and system PM, the latency-vs-power tradeoff, and the alternative of platform-level runtime PM on embedded systems.

With Sections 2 through 5 in place, the driver now handles system suspend, system resume, system shutdown, runtime suspend, and runtime resume. What it does not yet do cleanly is explain how the reader tests all of these from user space. Section 6 turns to the user-space interface: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v`, and the regression test that wraps them together.



## Section 6: Interacting With the Power Framework

A driver that handles suspend and resume correctly is only half of the story. The other half is being able to *test* that correctness, repeatedly and deliberately, from user space. Section 6 surveys the tools FreeBSD provides for that purpose, explains how each fits the driver's state model, and shows how to combine them into a regression script that exercises every path Sections 2 through 5 built.

### The Four User-Space Entry Points

Four commands cover almost everything a driver developer needs:

- **`acpiconf -s 3`** (and its variants) asks ACPI to put the whole system into sleep state S3. This is the most realistic test; it exercises the full path from user space through the kernel's suspend machinery through the PCI layer to the driver's methods.
- **`zzz`** is a thin wrapper around `acpiconf -s 3`. It reads `hw.acpi.suspend_state` (defaulting to S3) and enters the corresponding sleep state. For most users it is the most convenient way to suspend from a shell.
- **`devctl suspend myfirst0`** and **`devctl resume myfirst0`** trigger per-device suspend and resume through the `DEV_SUSPEND` and `DEV_RESUME` ioctls on `/dev/devctl2`. These only call the driver's methods; the rest of the system stays in S0. This is the fastest iteration target and what Chapter 22 uses for most development.
- **`devinfo -v`** lists all devices in the device tree with their current state. It shows whether a device is attached, suspended, or detached.

Each has strengths and weaknesses. `acpiconf` is realistic but slow (one to three seconds per cycle on typical hardware) and disruptive (the system actually sleeps). `devctl` is fast (milliseconds per cycle) but exercises only the driver, not the ACPI or platform code. `devinfo -v` is passive and cheap; it observes without changing state.

A good regression strategy uses all three: `devctl` for unit testing of the driver's methods, `acpiconf` for integration testing of the full suspend path, and `devinfo -v` as a quick sanity check.

### Using acpiconf to Suspend the System

On a machine with working ACPI, `acpiconf -s 3` is what Section 1 called a full system suspend. The command:

```sh
sudo acpiconf -s 3
```

does the following:

1. It opens `/dev/acpi` and checks that the platform supports S3 via the `ACPIIO_ACKSLPSTATE` ioctl.
2. It sends the `ACPIIO_REQSLPSTATE` ioctl to request S3.
3. The kernel begins the suspend sequence: paused userland, frozen threads, device tree traversal with `DEVICE_SUSPEND` on each device.
4. Assuming no driver vetoes, the kernel enters S3. The machine sleeps.
5. A wake event (the lid opens, the power button is pressed, a USB device sends a remote-wakeup signal) wakes the platform.
6. The kernel runs the resume sequence: `DEVICE_RESUME` on each device, unfreezing threads, resuming userland.
7. The shell prompt returns. The machine is back in S0.

For the `myfirst` driver to be exercised, the driver must be loaded before the suspend. The entire sequence from user perspective looks like:

```sh
sudo kldload ./myfirst.ko
sudo sysctl dev.myfirst.0.dma_test_read=1  # exercise it a bit
sudo acpiconf -s 3
# [laptop sleeps; user opens lid]
dmesg | grep myfirst
```

The `dmesg` output should show two lines from Chapter 22's logging:

```text
myfirst0: suspend: starting
myfirst0: suspend: complete (dma in flight=0, suspended=1)
myfirst0: resume: starting
myfirst0: resume: complete
```

If those lines are present and in that order, the driver's methods were called correctly by the full system path.

If the machine does not come back, the suspend path broke at some layer below `myfirst`. If the machine comes back but the driver is in a strange state (the sysctls return errors, the counters have strange values, DMA transfers fail), the problem is in `myfirst`'s suspend or resume implementation.

### Using zzz

On FreeBSD, `zzz` is a small shell script that reads `hw.acpi.suspend_state` and calls `acpiconf -s <state>`. It is not a binary; it is usually installed at `/usr/sbin/zzz` and is a few lines long. A typical invocation is:

```sh
sudo zzz
```

The default `hw.acpi.suspend_state` is `S3` on machines that support it. A reader who wants to test S4 (hibernate) can:

```sh
sudo sysctl hw.acpi.suspend_state=S4
sudo zzz
```

S4 support on FreeBSD has historically been partial; whether it works depends on the platform firmware and the filesystem layout. For Chapter 22's purposes, S3 is sufficient, and `zzz` is the convenient shorthand.

### Using devctl for Per-Device Suspend

The `devctl(8)` command was built to let a user manipulate the device tree from user space. It supports attach, detach, enable, disable, suspend, resume, and more. For Chapter 22, `suspend` and `resume` are the two that matter.

```sh
sudo devctl suspend myfirst0
sudo devctl resume myfirst0
```

The first command issues `DEV_SUSPEND` through `/dev/devctl2`; the kernel translates that into a call to `BUS_SUSPEND_CHILD` on the parent bus, which for a PCI device ends up calling `pci_suspend_child`, which saves config space, puts the device in D3, and calls the driver's `DEVICE_SUSPEND`. The reverse happens for resume.

The key differences from `acpiconf`:

- Only the target device and its children go through the transition. The rest of the system stays in S0.
- The CPU does not park. Userland does not freeze. The kernel does not sleep.
- The PCI device actually goes to D3hot (assuming `hw.pci.do_power_suspend` is 1). The reader can verify with `pciconf`:

```sh
# Before suspend: device should be in D0
pciconf -lvbc | grep -A 2 myfirst

# After devctl suspend myfirst0: device should be in D3
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 2 myfirst
```

The power state is usually shown in the `powerspec` line of `pciconf -lvbc`. Moving from `D0` to `D3` is the observable signal that the transition really happened.

### Using devinfo to Inspect Device State

The `devinfo(8)` utility lists the device tree with various levels of detail. The `-v` flag shows verbose information, including the device state (attached, suspended, or not present).

```sh
devinfo -v | grep -A 5 myfirst
```

Typical output:

```text
myfirst0 pnpinfo vendor=0x1af4 device=0x1005 subvendor=0x1af4 subdevice=0x0004 class=0x008880 at slot=5 function=0 dbsf=pci0:0:5:0
    Resource: <INTERRUPT>
        10
    Resource: <MEMORY>
        0xfeb80000-0xfeb80fff
```

The state is implicit in the output: if the device is suspended, the line shows the device and its resources without the "active" marker. An explicit state query can be done through the softc sysctl; the `dev.myfirst.0.%parent` and `dev.myfirst.0.%desc` keys tell the user where the device sits.

For Chapter 22, `devinfo -v` is most useful as a sanity check after a failed transition: if the device is missing from the output, the detach path ran; if the device is present but the resources are wrong, the attach or resume path left the device in an inconsistent state.

### Inspecting Power States Through sysctl

The PCI layer exposes power-state information through `sysctl` under `hw.pci`. Two variables are most relevant:

```sh
sysctl hw.pci.do_power_suspend
sysctl hw.pci.do_power_resume
```

Both default to 1, meaning the PCI layer transitions devices to D3 on suspend and back to D0 on resume. Setting either to 0 disables the automatic transition for debugging.

The ACPI layer exposes system-state information:

```sh
sysctl hw.acpi.supported_sleep_state
sysctl hw.acpi.suspend_state
sysctl hw.acpi.s4bios
```

The first lists which sleep states the platform supports (typically something like `S3 S4 S5`). The second is the state `zzz` enters (usually `S3`). The third says whether S4 is implemented through BIOS assistance.

For per-device observation, the driver exposes its own state through `dev.myfirst.N.*`. The Chapter 22 driver adds:

- `dev.myfirst.N.suspended`: 1 if the driver considers itself suspended, 0 otherwise.
- `dev.myfirst.N.power_suspend_count`: number of times `DEVICE_SUSPEND` has been called.
- `dev.myfirst.N.power_resume_count`: number of times `DEVICE_RESUME` has been called.
- `dev.myfirst.N.power_shutdown_count`: number of times `DEVICE_SHUTDOWN` has been called.
- `dev.myfirst.N.runtime_state`: 0 for `RUNNING`, 1 for `RUNTIME_SUSPENDED`.
- `dev.myfirst.N.runtime_suspend_count`, `dev.myfirst.N.runtime_resume_count`: runtime-PM counters.
- `dev.myfirst.N.idle_threshold_seconds`: runtime-PM idle threshold.

Between these sysctls and `dmesg`, a reader can see in full detail what the driver did during any transition.

### A Regression Script

The labs directory grows a new script: `ch22-suspend-resume-cycle.sh`. The script:

1. Records the baseline values of every counter.
2. Runs one DMA transfer to confirm the device is working.
3. Calls `devctl suspend myfirst0`.
4. Verifies `dev.myfirst.0.suspended` is 1.
5. Verifies `dev.myfirst.0.power_suspend_count` has incremented by 1.
6. Calls `devctl resume myfirst0`.
7. Verifies `dev.myfirst.0.suspended` is 0.
8. Verifies `dev.myfirst.0.power_resume_count` has incremented by 1.
9. Runs one more DMA transfer to confirm the device still works.
10. Prints a PASS/FAIL summary.

The full script is in the examples directory; a short outline of the logic:

```sh
#!/bin/sh
set -e

DEV="dev.myfirst.0"

if ! sysctl -a | grep -q "^${DEV}"; then
    echo "FAIL: ${DEV} not present"
    exit 1
fi

before_s=$(sysctl -n ${DEV}.power_suspend_count)
before_r=$(sysctl -n ${DEV}.power_resume_count)
before_xfer=$(sysctl -n ${DEV}.dma_transfers_read)

# Baseline: run one transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

# Suspend.
devctl suspend myfirst0
[ "$(sysctl -n ${DEV}.suspended)" = "1" ] || {
    echo "FAIL: device did not mark suspended"
    exit 1
}

# Resume.
devctl resume myfirst0
[ "$(sysctl -n ${DEV}.suspended)" = "0" ] || {
    echo "FAIL: device did not clear suspended"
    exit 1
}

# Another transfer.
sysctl -n ${DEV}.dma_test_read=1 > /dev/null

after_s=$(sysctl -n ${DEV}.power_suspend_count)
after_r=$(sysctl -n ${DEV}.power_resume_count)
after_xfer=$(sysctl -n ${DEV}.dma_transfers_read)

if [ $((after_s - before_s)) -ne 1 ]; then
    echo "FAIL: suspend count did not increment by 1"
    exit 1
fi
if [ $((after_r - before_r)) -ne 1 ]; then
    echo "FAIL: resume count did not increment by 1"
    exit 1
fi
if [ $((after_xfer - before_xfer)) -ne 2 ]; then
    echo "FAIL: expected 2 transfers (pre+post), got $((after_xfer - before_xfer))"
    exit 1
fi

echo "PASS: one suspend-resume cycle completed cleanly"
```

Running the script repeatedly (say a hundred times in a tight loop) is a good stress test. A driver that passes one cycle but fails on the fiftieth usually has a resource leak or an edge case that only shows up under repetition. That class of bug is exactly what a regression script is meant to find.

### Running the Stress Test

The chapter's `labs/` directory also includes `ch22-suspend-stress.sh`, which runs the cycle script a hundred times:

```sh
#!/bin/sh
N=100
i=0
while [ $i -lt $N ]; do
    if ! sh ./ch22-suspend-resume-cycle.sh > /dev/null; then
        echo "FAIL on iteration $i"
        exit 1
    fi
    i=$((i + 1))
done
echo "PASS: $N cycles"
```

On a modern machine with the simulation-only myfirst driver, a hundred cycles takes about a second. If any iteration fails, the script stops and reports the iteration number. Running this after each change during development catches regressions immediately.

### Combining Runtime PM and User-Space Testing

The runtime-PM path needs a different test, because it is not triggered by user commands; it is triggered by idleness. The test looks like:

```sh
# Ensure runtime_state is running.
sysctl dev.myfirst.0.runtime_state
# 0

# Do nothing for 6 seconds.
sleep 6

# The callout should have fired and runtime-suspended the device.
sysctl dev.myfirst.0.runtime_state
# 1

# Counter should have incremented.
sysctl dev.myfirst.0.runtime_suspend_count
# 1

# Any activity should bring it back.
sysctl dev.myfirst.0.dma_test_read=1
sysctl dev.myfirst.0.runtime_state
# 0

sysctl dev.myfirst.0.runtime_resume_count
# 1
```

A reader watching `dmesg` during this will see the "runtime suspend: starting" and "runtime suspend: device in D3" lines after about five seconds of inactivity, then "runtime resume: starting" when the sysctl write arrives.

The chapter's lab directory includes `ch22-runtime-pm.sh` to automate this sequence.

### Interpreting Failure Modes

When a user-space test fails, the diagnostic path depends on which layer failed:

- **If `devctl suspend` returns a non-zero exit code**: the driver's `DEVICE_SUSPEND` returned a non-zero value, vetoing the suspend. Check `dmesg` for the driver's log output; the suspend method should be logging what went wrong.
- **If `devctl suspend` succeeds but `dev.myfirst.0.suspended` is 0 afterwards**: the driver's quiesce set the flag briefly but something cleared it. This usually means the quiesce is re-entering itself, or the detach path is racing the suspend.
- **If `devctl resume` succeeds but the next transfer fails**: the restore path did not fully reinitialise the device. Most commonly, an interrupt mask or a DMA register was not written; check the per-vector fire counters before and after resume to see whether interrupts are reaching the driver.
- **If `acpiconf -s 3` succeeds but the system does not come back**: a driver below `myfirst` in the tree is blocking resume. This is unusual in a test VM; it is the classic failure mode on real hardware with new drivers.
- **If `acpiconf -s 3` returns `EOPNOTSUPP`**: the platform does not support S3. Check `sysctl hw.acpi.supported_sleep_state`.

In all cases, the first source of information is `dmesg`. The Chapter 22 driver logs every transition; if the log lines do not appear, the method was not called, and the problem is at a layer below the driver.

### A Minimal Troubleshooting Flow

A compact flowchart for a failed suspend-resume cycle:

1. Is the driver loaded? `kldstat | grep myfirst`.
2. Is the device attached? `sysctl dev.myfirst.0.%driver`.
3. Do the suspend and resume methods log? `dmesg | tail`.
4. Did `dev.myfirst.0.suspended` toggle correctly? `sysctl dev.myfirst.0.suspended`.
5. Do the counters increment? `sysctl dev.myfirst.0.power_suspend_count dev.myfirst.0.power_resume_count`.
6. Does a post-resume transfer succeed? `sudo sysctl dev.myfirst.0.dma_test_read=1; dmesg | tail -2`.
7. Do the per-vector interrupt counters increment? `sysctl dev.myfirst.0.vec0_fire_count dev.myfirst.0.vec1_fire_count dev.myfirst.0.vec2_fire_count`.

Any "no" answer points to a specific layer of the implementation. Section 7 goes deeper into the common failure modes and how to debug them.

### Wrapping Up Section 6

Section 6 surveyed the user-space interface to the kernel's power-management machinery: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v`, and the relevant `sysctl` variables. It showed how to combine these tools into a regression script that exercises one suspend-resume cycle, and a stress script that runs a hundred cycles in a row. It discussed the runtime-PM test flow, the interpretation of the most common failure modes, and the minimal troubleshooting flowchart a reader can follow when a test fails.

With the user-space tools in hand, the next section dives into the characteristic failure modes the reader is likely to encounter while writing power-aware code, and how to debug each one.



## Section 7: Debugging Power Management Issues

Power management code has a special class of bugs. The machine sleeps; the machine wakes; the bug shows up an unknown time after the wake and looks like a generic malfunction rather than anything related to the power transition. The chain of cause and effect is longer than with most driver bugs, the reproduction is slower, and the user's bug report is usually "my laptop doesn't wake up sometimes", which contains almost no information the driver developer can use.

Section 7 is about recognising the characteristic symptoms, tracing them back to their likely causes, and applying the matching debugging patterns. It draws on the Chapter 22 `myfirst` driver for concrete examples, but the patterns apply to any FreeBSD driver.

### Symptom 1: Frozen Device After Resume

The most common power-management bug, both in learning drivers and in production ones, is a device that stops responding after resume. The driver attaches correctly at boot, works normally in S0, handles a suspend-resume cycle without visible error, and then on the next command it is silent. Interrupts do not fire. DMA transfers do not complete. Any read from a device register returns stale values or zeros.

The usual cause is that the device's registers were not written after resume. The device came back in a default state (interrupt mask all-masked, DMA engine disabled, whatever registers the hardware resets on D0 entry), the driver did not reprogram them, and so from the device's perspective nothing is configured to run.

**Debugging pattern.** Compare the device's register values before and after suspend. The `myfirst` driver exposes several of its registers through sysctls (if the reader adds them); otherwise, the reader can write a short kernel-space helper that reads each register and prints it. After a suspend-resume cycle:

1. Read the interrupt mask register. If it is `0xFFFFFFFF` (all masked), the resume path did not restore the mask.
2. Read the DMA control register. If it has the ABORT bit set, the abort from the quiesce never cleared.
3. Read the device's configuration space via `pciconf -lvbc`. The command register should have the bus-master bit set; if not, `pci_enable_busmaster` was missed in the resume path.

**Fix pattern.** The resume path should include an unconditional reprogram of every device-specific register the driver's normal operation depends on. Saving them at suspend time into the softc and restoring them at resume time is one approach; re-deriving them from softc state (the approach `re_resume` takes) is another. Either works; the choice depends on which is easier to prove correct for the specific device.

### Symptom 2: Lost Interrupts

A subtler variant of the frozen-device problem is lost interrupts: the device is responding to some calls, but its interrupts are not reaching the driver. The DMA engine accepts a START command, performs the transfer, raises the completion interrupt... and the interrupt count does not increment. The task queue does not get an entry. The CV does not broadcast. The transfer eventually times out, and the driver reports EIO.

Several things can cause this:

- The **interrupt mask** at the device is still all-masked. The device wants to raise the interrupt but the mask suppresses it. (Resume path bug.)
- The **MSI or MSI-X configuration** was not restored. The device is raising the interrupt, but the kernel does not route it to the driver's handler. (Unusual; the PCI layer should handle this automatically.)
- The **filter function pointer** was corrupted. Extremely unusual; usually indicates memory corruption somewhere else in the driver.
- The **suspended flag** is still true, and the filter is returning early. (Resume path bug: flag not cleared.)

**Debugging pattern.** Read the per-vector fire counters before and after the suspend-resume cycle. If the counter does not increment, the interrupt is not reaching the filter. Then check, in order:

1. Is the suspended flag cleared? `sysctl dev.myfirst.0.suspended`.
2. Is the interrupt mask at the device correct? Read the register.
3. Is the MSI-X table in the device correct? `pciconf -c` dumps the capability registers.
4. Is the kernel's MSI dispatch state consistent? `procstat -t` shows the interrupt threads.

**Fix pattern.** Make sure the resume path (a) clears the suspended flag under the lock, (b) unmasks the device's interrupt register after clearing the flag, (c) does not rely on MSI-X restoration the driver must do itself (unless specifically disabled via sysctl).

### Symptom 3: Bad DMA After Resume

A more dangerous class of bug is DMA that appears to work but produces wrong data. The driver programs the engine, the engine runs, the completion interrupt fires, the task runs, the sync is called, the driver reads the buffer... and the bytes are wrong. Not zeros, not garbage, just subtly incorrect: the pattern written previously, or the pattern from two cycles ago, or a pattern that indicates the DMA addressed the wrong page.

Causes:

- The **bus address cached in the softc** is stale. This is unusual for a static allocation (the address is set once at attach and should not change), but it can happen if the driver re-allocates the DMA buffer at resume time (a bad idea; see below).
- The **DMA engine's base-address register** was not reprogrammed after resume, and it has a stale value that points elsewhere.
- The **`bus_dmamap_sync` calls are missing or mis-ordered**. This is the classic DMA-correctness bug, and it is worth being alert for in resume paths because the driver-side code adjacent to the sync calls is often edited during a refactor.
- The **IOMMU translation table** was not restored. Very rare on FreeBSD because the IOMMU configuration is per-session and survives suspend on most platforms; but if the driver is running on a system where `DEV_IOMMU` is unusual, this can bite.

**Debugging pattern.** Add a known-pattern write before each DMA, a verify after each DMA, and log both. Reducing the cycle to "write 0xAA, sync, read, expect 0xAA" makes data-corruption bugs visible immediately.

```c
memset(sc->dma_vaddr, 0xAA, MYFIRST_DMA_BUFFER_SIZE);
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_PREWRITE);
/* run transfer */
bus_dmamap_sync(sc->dma_tag, sc->dma_map, BUS_DMASYNC_POSTWRITE);
if (((uint8_t *)sc->dma_vaddr)[0] != 0xAA) {
        device_printf(sc->dev,
            "dma: corruption detected after transfer\n");
}
```

For the simulation, this should always succeed because the simulation does not modify the buffer on a write transfer. On real hardware, the pattern depends on the device. A reader debugging a real-hardware bug adapts the test.

**Fix pattern.** If the bus address is the problem, rebuild it on resume:

```c
/* In resume, after PCI restore is complete. */
err = bus_dmamap_load(sc->dma_tag, sc->dma_map,
    sc->dma_vaddr, MYFIRST_DMA_BUFFER_SIZE,
    myfirst_dma_single_map, &sc->dma_bus_addr,
    BUS_DMA_NOWAIT);
```

Only do this if the bus address actually changed, which is rare. More commonly, the fix is to write the base-address register at the start of every transfer (rather than relying on a persistent value) and to make sure the sync calls are in the right order.

### Symptom 4: Lost PME# Wake Events

On a device that supports wake-on-X, the symptom is "the device should have woken the system but did not". The driver reported a successful suspend; the system went to S3; the expected event (magic packet, button press, timer) happened; and the system stayed asleep.

Causes:

- **`pci_enable_pme` was not called** in the suspend path. The device's PME_En bit is 0, so even when the device would normally assert PME#, the bit is suppressed.
- **The device's own wake logic is not configured**. For a NIC, the wake-on-LAN registers must be programmed before suspend. For a USB host controller, the remote-wakeup capability must be enabled per-port.
- **The platform's wake GPE is not enabled**. This is usually a firmware matter; the ACPI `_PRW` method should have registered the GPE, but on some machines the BIOS disables it by default.
- **The PME status bit is set at the time of suspend**, and a stale PME# is what triggers the wake (instead of the expected event). The system appears to wake immediately after sleeping.

**Debugging pattern.** Read the PCI configuration space via `pciconf -lvbc`. The power-management capability's status/control register shows PME_En and the PME_Status bit. Before suspending, PME_Status should be 0 (no pending wake). After suspending with wake enabled, PME_En should be 1.

On a machine where the wake does not happen, check the BIOS settings for "wake on LAN", "wake on USB", etc. The driver can be perfect and the system still not wake if the platform is not configured.

**Fix pattern.** In the suspend path of a wake-capable driver:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int err;

        /* ... quiesce as before ... */

        if (sc->wake_enabled && pci_has_pm(dev)) {
                /* Program device-specific wake logic here. */
                myfirst_program_wake(sc);
                pci_enable_pme(dev);
        }

        /* ... rest of suspend ... */
}
```

In the resume path:

```c
static int
myfirst_pci_resume(device_t dev)
{
        if (pci_has_pm(dev))
                pci_clear_pme(dev);
        /* ... rest of resume ... */
}
```

The `myfirst` driver in Chapter 22 does not implement wake (the simulation has no wake logic). The pattern above is shown for reference.

### Symptom 5: WITNESS Complaints During Suspend

A debug kernel with `WITNESS` enabled often produces messages like:

```text
witness: acquiring sleepable lock foo_mtx @ /path/to/driver.c:123
witness: sleeping with non-sleepable lock bar_mtx @ /path/to/driver.c:456
```

These are lock-order violations or sleep-while-locked violations, and they often show up in suspend code because suspend does things the driver does not normally do: acquire locks, sleep, and coordinate multiple threads.

Causes:

- The suspend path acquires a lock and then calls a function that sleeps without explicit tolerance for sleeping-with-that-lock-held.
- The suspend path acquires locks in a different order than the rest of the driver, and `WITNESS` notices the reversal.
- The suspend path calls `taskqueue_drain` or `callout_drain` while holding the softc lock, which causes a deadlock if the task or callout tries to acquire the same lock.

**Debugging pattern.** Read the `WITNESS` message carefully. It includes the lock names and the source-line numbers where each was acquired. Trace the path from the acquisition to the sleep or lock reversal.

**Fix pattern.** The Chapter 22 `myfirst_quiesce` drops the softc lock before calling `myfirst_drain_workers` for exactly this reason. When extending the driver:

- Do not call `taskqueue_drain` with any driver lock held.
- Do not call `callout_drain` with the lock that the callout acquires.
- Sleep primitives (`pause`, `cv_wait`) must be called with only sleep-mutexes held (not spin-mutexes).
- If you need to drop a lock for a sleep, do so explicitly and reacquire after.

### Symptom 6: Counters That Do Not Match

The chapter's regression script expects `power_suspend_count == power_resume_count` after each cycle. When they drift, something is wrong.

Causes:

- The driver's `DEVICE_SUSPEND` was called but the driver returned early before incrementing the counter. (Often because of a sanity check that fired.)
- The driver's `DEVICE_RESUME` was not called, because `DEVICE_SUSPEND` returned non-zero and the kernel unwound.
- The counters are not atomic and a concurrent update lost an increment. (Unlikely if the code uses `atomic_add_64`.)
- The driver was unloaded and reloaded between counts, resetting them.

**Debugging pattern.** Run the regression script with `dmesg -c` cleared beforehand, and `dmesg` after each cycle. The log shows every method invocation; counting the log lines is an alternative to counting the counters, and any difference indicates a bug.

### Symptom 7: Hangs During Suspend

A hang during suspend is the worst diagnostic: the kernel is still running (the console still responds to break-to-DDB), but the suspend sequence is stuck in some driver's `DEVICE_SUSPEND`. Break into DDB and `ps` to see which thread is where:

```text
db> ps
...  0 myfirst_drain_dma+0x42 myfirst_pci_suspend+0x80 ...
```

**Debugging pattern.** Identify the hanging thread and the function it is stuck in. Usually it is a `cv_wait` or `cv_timedwait` that never completed, or a `taskqueue_drain` waiting on a task that will not finish.

**Fix pattern.** Add a timeout to any wait the suspend path does. The `myfirst_drain_dma` function uses `cv_timedwait` with a one-second timeout; a variant that uses `cv_wait` (no timeout) can hang indefinitely. The chapter's implementation always uses timed variants for this reason.

### Using DTrace to Trace Suspend and Resume

DTrace is an excellent tool for observing the power-management path at fine granularity without adding print statements. A simple D script that times each call:

```d
fbt::device_suspend:entry,
fbt::device_resume:entry
{
    self->ts = timestamp;
    printf("%s: %s %s\n", probefunc,
        args[0] != NULL ? stringof(args[0]->name) : "?",
        args[0] != NULL ? stringof(args[0]->desc) : "?");
}

fbt::device_suspend:return,
fbt::device_resume:return
/self->ts/
{
    printf("%s: returned %d after %d us\n",
        probefunc, arg1,
        (timestamp - self->ts) / 1000);
    self->ts = 0;
}
```

Save this as `trace-devpower.d` and run with `dtrace -s trace-devpower.d`. Any `devctl suspend` or `acpiconf -s 3` will produce output showing each device's suspend and resume times, and their return values.

For the `myfirst` driver specifically, `fbt::myfirst_pci_suspend:entry` and `fbt::myfirst_pci_resume:entry` are the probes. A D script focused on the driver:

```d
fbt::myfirst_pci_suspend:entry {
    self->ts = timestamp;
    printf("myfirst_pci_suspend: entered\n");
    stack();
}

fbt::myfirst_pci_suspend:return
/self->ts/ {
    printf("myfirst_pci_suspend: returned %d after %d us\n",
        arg1, (timestamp - self->ts) / 1000);
    self->ts = 0;
}
```

The `stack()` call prints the call stack at entry, which is useful to confirm that the method is being called from where you expect (the PCI bus's `bus_suspend_child`, for example).

### A Note on Logging Discipline

The Chapter 22 code logs generously during suspend and resume: every method logs entry and exit, and each helper logs its own events. That verbosity is helpful during development but annoying in production (every laptop suspend prints half a dozen lines to dmesg).

A good production driver exposes a sysctl that controls log verbosity:

```c
static int myfirst_power_verbose = 1;
SYSCTL_INT(_dev_myfirst, OID_AUTO, power_verbose,
    CTLFLAG_RWTUN, &myfirst_power_verbose, 0,
    "Verbose power-management logging (0=off, 1=on, 2=debug)");
```

And the logging becomes conditional:

```c
if (myfirst_power_verbose >= 1)
        device_printf(dev, "suspend: starting\n");
```

A reader who wants to enable debugging on a production system can set `dev.myfirst.power_verbose=2` temporarily, trigger the problem, and reset the variable. The Chapter 22 driver does not implement this tiering; the learning driver logs everything and accepts the noise.

### Using the INVARIANTS Kernel for Assertion Coverage

A debug kernel with `INVARIANTS` compiled in causes `KASSERT` macros to actually evaluate their conditions and panic on failure. The `myfirst_dma.c` and `myfirst_pci.c` code uses several KASSERTs; the power-management code adds more. For example, the quiesce invariant:

```c
static int
myfirst_quiesce(struct myfirst_softc *sc)
{
        /* ... */

        KASSERT(sc->dma_in_flight == false,
            ("myfirst: dma_in_flight still true after drain"));

        return (0);
}
```

On an `INVARIANTS` kernel, a bug that leaves `dma_in_flight` true causes an immediate panic with a useful message. On a production kernel, the assertion is compiled out and nothing happens. The learning driver deliberately runs on an `INVARIANTS` kernel to catch this class of bug.

Similarly, the resume path can assert:

```c
KASSERT(sc->suspended == true,
    ("myfirst: resume called but not suspended"));
```

This catches a bug where the driver somehow gets resume called without the matching suspend having happened (usually a bug in a parent bus driver, not in the `myfirst` driver itself).

### A Debugging Case Study

To bring the patterns together, consider a concrete scenario. The reader writes the Stage 2 suspend, runs a regression cycle, and sees:

```text
myfirst0: suspend: starting
myfirst0: drain_dma: timeout waiting for abort
myfirst0: suspend: complete (dma in flight=0, suspended=1)
myfirst0: resume: starting
myfirst0: resume: complete
```

Then:

```sh
sudo sysctl dev.myfirst.0.dma_test_read=1
# Returns EBUSY after a long delay
```

The user-visible symptom is that the post-resume transfer does not work. The log shows a drain timeout during suspend, which is the first anomaly.

**Hypothesis.** The DMA engine did not honor the ABORT bit. The driver force-cleared `dma_in_flight`, but the engine is still running; when the user triggers a new transfer, the engine is not ready.

**Test.** Check the engine's status register before and after the abort:

```c
/* In myfirst_drain_dma, after writing ABORT: */
uint32_t pre_status = CSR_READ_4(sc->dev, MYFIRST_REG_DMA_STATUS);
DELAY(100);  /* let the engine notice */
uint32_t post_status = CSR_READ_4(sc->dev, MYFIRST_REG_DMA_STATUS);
device_printf(sc->dev, "drain: status %#x -> %#x\n", pre_status, post_status);
```

Running the cycle again produces:

```text
myfirst0: drain: status 0x4 -> 0x4
```

Status 0x4 is RUNNING. The engine ignored the ABORT. That points to the simulation backend: the simulated engine might not implement abort, or might do so only when the simulation callout fires.

**Fix.** Look at the simulation's DMA engine code and verify the abort semantics. In this case, the simulation's engine handles the abort in its callout callback, which does not fire for a few milliseconds. Extend the drain timeout from 1 second (plenty) to... wait, 1 second is plenty for a callout that fires every few milliseconds. The real issue is elsewhere.

Further investigation reveals that the simulation's callout was drained *before* the DMA drain completed. The order in `myfirst_drain_workers` (task first, callout second) was wrong; it should be callout first, task second, because the callout is what drives the abort completion.

**Resolution.** Reorder the drain:

```c
static void
myfirst_drain_workers(struct myfirst_softc *sc)
{
        /*
         * Drain the callout first: it runs the simulated engine's
         * completion logic, and the drain-DMA path waits on that
         * completion. Draining the callout after drain_dma would let
         * drain_dma time out and force-clear the in-flight flag.
         *
         * Wait - actually, drain_dma has already completed by the time
         * we get here, because myfirst_quiesce calls it first. So the
         * order of the two drains inside this function does not matter
         * for that reason. But drain_workers is also called from detach,
         * where drain_dma may not have been called, and the order there
         * does matter.
         */
        if (sc->sim != NULL)
                myfirst_sim_drain_dma_callout(sc->sim);

        if (sc->rx_vector.has_task)
                taskqueue_drain(taskqueue_thread, &sc->rx_vector.task);
}
```

But wait: by the time `myfirst_drain_workers` is called from `myfirst_quiesce`, `myfirst_drain_dma` has already completed. The drain-dma wait is inside the drain-dma call; the drain-workers call only cleans up residual state. The order inside drain-workers is mostly aesthetic for suspend.

The real fix is earlier: `myfirst_drain_dma` itself should not have timed out. The 1-second timeout should have been plenty. The actual cause is different: perhaps the simulation's callout was not firing because the driver held a sysctl lock that blocked it. Or the writing of the ABORT bit did not reach the simulation because the simulation's MMIO handler was also blocked.

**Lesson.** Debugging power management issues is iterative. Each symptom suggests a hypothesis; each test narrows it down; the fix is often in a different layer than the one the symptom pointed to. The patience to follow the chain is what distinguishes good power-aware code from code that mostly works.

### Wrapping Up Section 7

Section 7 walked through the characteristic failure modes of power-aware drivers: frozen devices, lost interrupts, bad DMA, missed wake events, WITNESS complaints, counter drift, and outright hangs. For each, it showed the typical cause, a debugging pattern that narrows the problem down, and the fix pattern that removes it. It also introduced DTrace for measurement, discussed log discipline, and showed how `INVARIANTS` and `WITNESS` catch the class of bug that only shows up under specific conditions.

The debugging discipline in Section 7, like the quiesce discipline in Section 3 and the restore discipline in Section 4, is meant to stay with the reader beyond the `myfirst` driver. Every power-aware driver has some variation of these bugs lurking in its implementation; the patterns above are how to find them before they reach the user.

Section 8 brings Chapter 22 to a close by consolidating the Sections 2 through 7 code into a refactored `myfirst_power.c` file, bumping the version to `1.5-power`, adding a `POWER.md` document, and wiring up a final integration test.



## Section 8: Refactoring and Versioning Your Power-Aware Driver

Stages 1 through 3 added the power-management code inline in `myfirst_pci.c`. That was convenient for teaching, because every change appeared next to the attach and detach code the reader already knew. It is less convenient for readability: `myfirst_pci.c` now has attach, detach, three power methods, and several helpers, and the file is long enough that a first-time reader has to scroll to find things.

Stage 4, the final version of the Chapter 22 driver, pulls all of the power-management code out of `myfirst_pci.c` and into a new file pair, `myfirst_power.c` and `myfirst_power.h`. This follows the same pattern as Chapter 20's `myfirst_msix.c` split and Chapter 21's `myfirst_dma.c` split: the new file has a narrow, well-documented API, and the caller in `myfirst_pci.c` uses only that API.

### The Target Layout

After Stage 4, the driver's source files are:

- `myfirst.c` - top-level glue, shared state, sysctl tree.
- `myfirst_hw.c`, `myfirst_hw_pci.c` - register-access helpers.
- `myfirst_sim.c` - simulation backend.
- `myfirst_pci.c` - PCI attach, detach, method table, and thin forwarding to subsystem modules.
- `myfirst_intr.c` - single-vector interrupt (Chapter 19 legacy path).
- `myfirst_msix.c` - multi-vector interrupt setup (Chapter 20).
- `myfirst_dma.c` - DMA setup, teardown, transfer (Chapter 21).
- `myfirst_power.c` - power management (Chapter 22, new).
- `cbuf.c` - circular-buffer support.

The new `myfirst_power.h` declares the public API of the power subsystem:

```c
#ifndef _MYFIRST_POWER_H_
#define _MYFIRST_POWER_H_

struct myfirst_softc;

int  myfirst_power_setup(struct myfirst_softc *sc);
void myfirst_power_teardown(struct myfirst_softc *sc);

int  myfirst_power_suspend(struct myfirst_softc *sc);
int  myfirst_power_resume(struct myfirst_softc *sc);
int  myfirst_power_shutdown(struct myfirst_softc *sc);

#ifdef MYFIRST_ENABLE_RUNTIME_PM
int  myfirst_power_runtime_suspend(struct myfirst_softc *sc);
int  myfirst_power_runtime_resume(struct myfirst_softc *sc);
void myfirst_power_mark_active(struct myfirst_softc *sc);
#endif

void myfirst_power_add_sysctls(struct myfirst_softc *sc);

#endif /* _MYFIRST_POWER_H_ */
```

The `_setup` and `_teardown` pair initialise and tear down the subsystem-level state (the callout, the sysctls). The per-transition functions wrap the same logic the Section 3 through Section 5 code built. The runtime-PM functions are compiled only when the build-time flag is defined.

### The myfirst_power.c File

The new file is about three hundred lines. Its structure mirrors `myfirst_dma.c`: header includes, static helpers, public functions, sysctl handlers, and `_add_sysctls`.

The helpers are the three from Section 3:

- `myfirst_mask_interrupts`
- `myfirst_drain_dma`
- `myfirst_drain_workers`

Plus one from Section 4:

- `myfirst_restore`

And, if runtime PM is enabled, two from Section 5:

- `myfirst_idle_watcher_cb`
- `myfirst_start_idle_watcher`

The public functions `myfirst_power_suspend`, `myfirst_power_resume`, and `myfirst_power_shutdown` become thin wrappers that call the helpers in the right order and update counters. The sysctl handlers expose the policy knobs and the observability counters.

### Updating myfirst_pci.c

The `myfirst_pci.c` file is now much shorter. Its three power methods each just forward to the power subsystem:

```c
static int
myfirst_pci_suspend(device_t dev)
{
        return (myfirst_power_suspend(device_get_softc(dev)));
}

static int
myfirst_pci_resume(device_t dev)
{
        return (myfirst_power_resume(device_get_softc(dev)));
}

static int
myfirst_pci_shutdown(device_t dev)
{
        return (myfirst_power_shutdown(device_get_softc(dev)));
}
```

The method table stays the same as Stage 1 set it up. The three prototypes above are now the only power-related code in `myfirst_pci.c`, apart from the call to `myfirst_power_setup` from attach and `myfirst_power_teardown` from detach.

The attach path grows one call:

```c
static int
myfirst_pci_attach(device_t dev)
{
        /* ... existing attach code ... */

        err = myfirst_power_setup(sc);
        if (err != 0) {
                device_printf(dev, "power setup failed\n");
                /* unwind */
                myfirst_dma_teardown(sc);
                /* ... rest of unwind ... */
                return (err);
        }

        myfirst_power_add_sysctls(sc);

        return (0);
}
```

The detach path grows a matching call:

```c
static int
myfirst_pci_detach(device_t dev)
{
        /* ... existing detach code ... */

        myfirst_power_teardown(sc);

        /* ... rest of detach ... */

        return (0);
}
```

`myfirst_power_setup` initialises the `saved_intr_mask`, the `suspended` flag, the counters, and (if runtime PM is enabled) the idle watcher callout. `myfirst_power_teardown` drains the callout and cleans up any subsystem-level state. The teardown must be done before the DMA teardown because the callout may still reference DMA state.

### Updating the Makefile

The new source file goes into the `SRCS` list, and the version bumps:

```make
KMOD=  myfirst
SRCS=  myfirst.c \
       myfirst_hw.c myfirst_hw_pci.c \
       myfirst_sim.c \
       myfirst_pci.c \
       myfirst_intr.c \
       myfirst_msix.c \
       myfirst_dma.c \
       myfirst_power.c \
       cbuf.c

CFLAGS+= -DMYFIRST_VERSION_STRING=\"1.5-power\"

# Optional: enable runtime PM.
# CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM

.include <bsd.kmod.mk>
```

The `MYFIRST_ENABLE_RUNTIME_PM` flag is off by default in Stage 4; the runtime-PM code compiles but is wrapped in `#ifdef`. A reader who wants to experiment enables the flag at build time.

### Writing POWER.md

The Chapter 21 pattern set the precedent: every subsystem gets a markdown document that describes its purpose, its API, its state model, and its testing story. `POWER.md` is next.

A good `POWER.md` has these sections:

1. **Purpose**: a paragraph explaining what the subsystem does.
2. **Public API**: a table of function prototypes with one-line descriptions.
3. **State Model**: a diagram or text description of the states and transitions.
4. **Counters and Sysctls**: the read-only and read-write sysctls the subsystem exposes.
5. **Transition Flows**: what happens during each of suspend, resume, shutdown.
6. **Interaction with Other Subsystems**: how power management relates to DMA, interrupts, and the simulation.
7. **Runtime PM (optional)**: how runtime PM works and when it is enabled.
8. **Testing**: the regression and stress scripts.
9. **Known Limitations**: what the subsystem does not do yet.
10. **See Also**: cross-references to `bus(9)`, `pci(9)`, and the chapter text.

The full document is in the examples directory (`examples/part-04/ch22-power/stage4-final/POWER.md`); the chapter does not reproduce it inline, but a reader who wants to check the expected structure can open it.

### Regression Script

The Stage 4 regression script exercises every path:

```sh
#!/bin/sh
# ch22-full-regression.sh

set -e

# 1. Basic sanity.
sudo kldload ./myfirst.ko

# 2. One suspend-resume cycle.
sudo sh ./ch22-suspend-resume-cycle.sh

# 3. One hundred cycles in a row.
sudo sh ./ch22-suspend-stress.sh

# 4. A transfer before, during, and after a cycle.
sudo sh ./ch22-transfer-across-cycle.sh

# 5. If runtime PM is enabled, test it.
if sysctl -N dev.myfirst.0.runtime_state >/dev/null 2>&1; then
    sudo sh ./ch22-runtime-pm.sh
fi

# 6. Unload.
sudo kldunload myfirst

echo "FULL REGRESSION PASSED"
```

Each sub-script is a few dozen lines and tests one thing. Running the full regression after each change catches regressions immediately.

### Integration With the Existing Regression Tests

The Chapter 21 regression script checked:

- `dma_complete_intrs == dma_complete_tasks` (the task always sees every interrupt).
- `dma_complete_intrs == dma_transfers_write + dma_transfers_read + dma_errors + dma_timeouts`.

The Chapter 22 script adds:

- `power_suspend_count == power_resume_count` (every suspend has a matching resume).
- The `suspended` flag is 0 outside of a transition.
- After a suspend-resume cycle, the DMA counters still add up to the expected total (no phantom transfers).

The combined regression is the Chapter 22 full script. It exercises DMA, interrupts, MSI-X, and power management together. A driver that passes it is in good shape.

### Version History

The driver has now evolved through several versions:

- `1.0` - Chapter 16: MMIO-only driver, simulation backend.
- `1.1` - Chapter 18: PCI attach, real BAR.
- `1.2-intx` - Chapter 19: single-vector interrupt with filter+task.
- `1.3-msi` - Chapter 20: multi-vector MSI-X with fallback.
- `1.4-dma` - Chapter 21: `bus_dma` setup, simulated DMA engine, interrupt-driven completion.
- `1.5-power` - Chapter 22: suspend/resume/shutdown, refactored into `myfirst_power.c`, optional runtime PM.

Each version builds on the previous one. A reader who has the Chapter 21 driver working can apply the Chapter 22 changes incrementally and end up at `1.5-power` without having to rewrite any earlier code.

### A Final Integration Test on Real Hardware

If the reader has access to real hardware (a machine with a working S3 implementation), the Chapter 22 driver can be exercised through a full system suspend:

```sh
sudo kldload ./myfirst.ko
sudo sh ./ch22-suspend-resume-cycle.sh
sudo acpiconf -s 3
# [laptop sleeps; user opens lid]
# After resume, the DMA test should still work.
sudo sysctl dev.myfirst.0.dma_test_read=1
```

On most platforms where ACPI S3 works, the driver survives the full cycle. The `dmesg` output shows the suspend and resume lines just as `devctl` would trigger, confirming that the same method code runs in both contexts.

If the full-system test fails where the per-device test succeeded, the extra work that system suspend does (ACPI sleep-state transitions, CPU parking, RAM self-refresh) has exposed something the per-device test missed. The usual culprits are device-specific register values that the system's low-power state resets but the per-device D3 does not. A driver that tests only with `devctl` can miss these; a driver that tests with `acpiconf -s 3` at least once before claiming correctness is more reliable.

### The Chapter 22 Code in One Place

A compact summary of what the Stage 4 driver added:

- **One new file**: `myfirst_power.c`, about three hundred lines.
- **One new header**: `myfirst_power.h`, about thirty lines.
- **One new markdown document**: `POWER.md`, about two hundred lines.
- **Five new softc fields**: `suspended`, `saved_intr_mask`, `power_suspend_count`, `power_resume_count`, `power_shutdown_count`, plus the runtime-PM fields when that feature is enabled.
- **Three new `DEVMETHOD` lines**: `device_suspend`, `device_resume`, `device_shutdown`.
- **Three new helper functions**: `myfirst_mask_interrupts`, `myfirst_drain_dma`, `myfirst_drain_workers`.
- **Two new subsystem entry points**: `myfirst_power_setup`, `myfirst_power_teardown`.
- **Three new transition functions**: `myfirst_power_suspend`, `myfirst_power_resume`, `myfirst_power_shutdown`.
- **Six new sysctls**: the counter nodes and the suspended flag.
- **Several new lab scripts**: cycle, stress, transfer-across-cycle, runtime-PM.

The overall line increment is about seven hundred lines of code, plus a couple of hundred lines of documentation and script. For the capability the chapter added (a driver that correctly handles every power transition the kernel can throw at it), that is a proportionate investment.

### Wrapping Up Section 8

Section 8 closed the Chapter 22 driver's construction by splitting the power code into its own file, bumping the version to `1.5-power`, adding a `POWER.md` document, and wiring the final regression test. The pattern was familiar from Chapters 20 and 21: take the inline code, extract it into a subsystem with a small API, document the subsystem, and integrate it into the rest of the driver through function calls rather than direct field access.

The resulting driver is power-aware in every sense the chapter introduced: it handles `DEVICE_SUSPEND`, `DEVICE_RESUME`, and `DEVICE_SHUTDOWN`; it quiesces the device cleanly; it restores state correctly on resume; it optionally implements runtime power management; it exposes its state through sysctls; it has a regression test; and it survives full-system suspend on real hardware when the platform supports it.



## Deep Look: Power Management in /usr/src/sys/dev/re/if_re.c

The Realtek 8169 and compatible gigabit NICs are handled by the `re(4)` driver. It is an informative driver to read for Chapter 22 purposes because it implements the full suspend-resume-shutdown trio with wake-on-LAN support, and its code has been stable enough to represent a canonical FreeBSD pattern. A reader who has worked through Chapter 22 can open `/usr/src/sys/dev/re/if_re.c` and recognise the structure immediately.

> **Reading this walkthrough.** The paired `re_suspend()` and `re_resume()` listings in the subsections below are taken from `/usr/src/sys/dev/re/if_re.c`, and the method-table excerpt abbreviates the full `re_methods[]` array with a `/* ... other methods ... */` comment so the three power-related `DEVMETHOD` entries stand out. We kept the signatures, the lock-acquire and lock-release pattern, and the order of device-specific calls (`re_stop`, `re_setwol`, `re_clrwol`, `re_init_locked`) intact; the real method table has many more entries, and the surrounding file carries the helper implementations. Every symbol the listings name is a real FreeBSD identifier in `if_re.c` that you can find with a symbol search.

### The Method Table

The `re(4)` driver's method table includes the three power methods near the top:

```c
static device_method_t re_methods[] = {
        DEVMETHOD(device_probe,     re_probe),
        DEVMETHOD(device_attach,    re_attach),
        DEVMETHOD(device_detach,    re_detach),
        DEVMETHOD(device_suspend,   re_suspend),
        DEVMETHOD(device_resume,    re_resume),
        DEVMETHOD(device_shutdown,  re_shutdown),
        /* ... other methods ... */
};
```

This is exactly the pattern Chapter 22 teaches. The `myfirst` driver's method table looks the same.

### re_suspend

The suspend function is about a dozen lines:

```c
static int
re_suspend(device_t dev)
{
        struct rl_softc *sc;

        sc = device_get_softc(dev);

        RL_LOCK(sc);
        re_stop(sc);
        re_setwol(sc);
        sc->suspended = 1;
        RL_UNLOCK(sc);

        return (0);
}
```

Three calls do the work: `re_stop` quiesces the NIC (disables interrupts, halts DMA, stops the RX and TX engines), `re_setwol` programs the wake-on-LAN logic and calls `pci_enable_pme` if WoL is enabled, and `sc->suspended = 1` sets the softc flag.

Compare to `myfirst_power_suspend`:

```c
int
myfirst_power_suspend(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "suspend: starting\n");
        err = myfirst_quiesce(sc);
        /* ... error handling ... */
        atomic_add_64(&sc->power_suspend_count, 1);
        return (0);
}
```

The structure is identical. `re_stop` and `re_setwol` together are the equivalent of `myfirst_quiesce`; the chapter's driver does not have wake-on-X, so there is no analogue of `re_setwol`.

### re_resume

The resume function is about thirty lines:

```c
static int
re_resume(device_t dev)
{
        struct rl_softc *sc;
        if_t ifp;

        sc = device_get_softc(dev);

        RL_LOCK(sc);

        ifp = sc->rl_ifp;
        /* Take controller out of sleep mode. */
        if ((sc->rl_flags & RL_FLAG_MACSLEEP) != 0) {
                if ((CSR_READ_1(sc, RL_MACDBG) & 0x80) == 0x80)
                        CSR_WRITE_1(sc, RL_GPIO,
                            CSR_READ_1(sc, RL_GPIO) | 0x01);
        }

        /*
         * Clear WOL matching such that normal Rx filtering
         * wouldn't interfere with WOL patterns.
         */
        re_clrwol(sc);

        /* reinitialize interface if necessary */
        if (if_getflags(ifp) & IFF_UP)
                re_init_locked(sc);

        sc->suspended = 0;
        RL_UNLOCK(sc);

        return (0);
}
```

The steps map cleanly to Chapter 22's discipline:

1. **Take the controller out of sleep mode** (MAC sleep bit on some Realtek parts). This is a device-specific restore step.
2. **Clear any WOL patterns** via `re_clrwol`, which reverses what `re_setwol` did. This also calls `pci_clear_pme` implicitly through the clear.
3. **Re-initialise the interface** if it was up before suspend. `re_init_locked` is the same function attach calls to bring up the NIC; it reprograms the MAC, resets the descriptor rings, enables interrupts, and starts the DMA engines.
4. **Clear the suspended flag** under the lock.

The `myfirst_power_resume` equivalent:

```c
int
myfirst_power_resume(struct myfirst_softc *sc)
{
        int err;

        device_printf(sc->dev, "resume: starting\n");
        err = myfirst_restore(sc);
        /* ... */
        atomic_add_64(&sc->power_resume_count, 1);
        return (0);
}
```

Again the structure is identical. `myfirst_restore` corresponds to the combination of the MAC-sleep exit, `re_clrwol`, `re_init_locked`, and the flag clear.

### re_shutdown

The shutdown function is:

```c
static int
re_shutdown(device_t dev)
{
        struct rl_softc *sc;

        sc = device_get_softc(dev);

        RL_LOCK(sc);
        re_stop(sc);
        /*
         * Mark interface as down since otherwise we will panic if
         * interrupt comes in later on, which can happen in some
         * cases.
         */
        if_setflagbits(sc->rl_ifp, 0, IFF_UP);
        re_setwol(sc);
        RL_UNLOCK(sc);

        return (0);
}
```

Similar to `re_suspend`, plus the interface-flag clear (shutdown is final; marking the interface down prevents spurious activity). The pattern is nearly identical; `re_shutdown` is essentially a more defensive version of `re_suspend`.

### re_setwol

The wake-on-LAN setup is worth looking at because it shows how a real driver calls the PCI PM APIs:

```c
static void
re_setwol(struct rl_softc *sc)
{
        if_t ifp;
        uint8_t v;

        RL_LOCK_ASSERT(sc);

        if (!pci_has_pm(sc->rl_dev))
                return;

        /* ... programs device-specific wake registers ... */

        /* Request PME if WOL is requested. */
        if ((if_getcapenable(ifp) & IFCAP_WOL) != 0)
                pci_enable_pme(sc->rl_dev);
}
```

Three key patterns appear here that are worth copying into any power-aware driver that supports wake-on-X:

1. **`pci_has_pm(dev)` guard.** The function returns early if the device does not support power management. This prevents writes to registers that do not exist.
2. **Device-specific wake programming.** The bulk of the function writes Realtek-specific registers through `CSR_WRITE_1`. A driver for a different device would write different registers, but the placement (inside the suspend path, before `pci_enable_pme`) is the same.
3. **Conditional `pci_enable_pme`.** Only enable PME# if the user has actually asked for wake-on-X. If the user has not, the function still sets the relevant configuration bits (for consistency with the driver's interface capabilities) but does not call `pci_enable_pme`.

The inverse is `re_clrwol`:

```c
static void
re_clrwol(struct rl_softc *sc)
{
        uint8_t v;

        RL_LOCK_ASSERT(sc);

        if (!pci_has_pm(sc->rl_dev))
                return;

        /* ... clears the wake-related config bits ... */
}
```

Note that `re_clrwol` does not explicitly call `pci_clear_pme`; the PCI layer's `pci_resume_child` has already called it before the driver's `DEVICE_RESUME`. `re_clrwol` is responsible for undoing the driver-visible side of WoL configuration, not the kernel-visible PME status.

### What the Deep Look Shows

The Realtek driver is more complex than `myfirst` by every measure (more registers, more state, more device variants), and yet its power-management discipline is less complex. That is because complexity of the *device* does not map one-to-one to complexity of the *power-management code*. Chapter 22's discipline scales down as well as it scales up: a simple device has a simple power path; a complex device has a modestly more complex power path. The structure is the same.

A reader who has finished Chapter 22 can now open `if_re.c`, recognise every function and every pattern, and understand why each exists. That comprehension transfers: the same recognition applies to `if_xl.c`, `virtio_blk.c`, and hundreds of other FreeBSD drivers. Chapter 22 is not teaching a `myfirst`-specific API; it is teaching the FreeBSD power-management idiom, and the `myfirst` driver is the vehicle that made it concrete.



## Deep Look: Simpler Patterns in if_xl.c and virtio_blk.c

For contrast, two other FreeBSD drivers implement power management in even simpler ways.

### if_xl.c: Shutdown Calls Suspend

The 3Com EtherLink III driver in `/usr/src/sys/dev/xl/if_xl.c` has the minimal three-method setup:

```c
static int
xl_shutdown(device_t dev)
{
        return (xl_suspend(dev));
}

static int
xl_suspend(device_t dev)
{
        struct xl_softc *sc;

        sc = device_get_softc(dev);

        XL_LOCK(sc);
        xl_stop(sc);
        xl_setwol(sc);
        XL_UNLOCK(sc);

        return (0);
}

static int
xl_resume(device_t dev)
{
        struct xl_softc *sc;
        if_t ifp;

        sc = device_get_softc(dev);
        ifp = sc->xl_ifp;

        XL_LOCK(sc);

        if (if_getflags(ifp) & IFF_UP) {
                if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
                xl_init_locked(sc);
        }

        XL_UNLOCK(sc);

        return (0);
}
```

Two things stand out:

1. `xl_shutdown` is one line: it just calls `xl_suspend`. For this driver, shutdown and suspend do the same work, and the code does not need two copies.
2. There is no `suspended` flag in the softc. The driver assumes the normal lifecycle of attach → run → suspend → resume, and uses the `IFF_DRV_RUNNING` flag (which the TX path already checks) as the equivalent. This is a perfectly valid approach for a NIC whose main user-visible state is the interface's running state.

For the `myfirst` driver, the explicit `suspended` flag is preferred because the driver has no natural equivalent of `IFF_DRV_RUNNING`. A NIC driver can reuse what it already has; a learning driver declares what it needs.

### virtio_blk.c: Minimal Quiesce

The virtio block driver in `/usr/src/sys/dev/virtio/block/virtio_blk.c` has an even shorter suspend path:

```c
static int
vtblk_suspend(device_t dev)
{
        struct vtblk_softc *sc;
        int error;

        sc = device_get_softc(dev);

        VTBLK_LOCK(sc);
        sc->vtblk_flags |= VTBLK_FLAG_SUSPEND;
        /* XXX BMV: virtio_stop(), etc needed here? */
        error = vtblk_quiesce(sc);
        if (error)
                sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
        VTBLK_UNLOCK(sc);

        return (error);
}

static int
vtblk_resume(device_t dev)
{
        struct vtblk_softc *sc;

        sc = device_get_softc(dev);

        VTBLK_LOCK(sc);
        sc->vtblk_flags &= ~VTBLK_FLAG_SUSPEND;
        vtblk_startio(sc);
        VTBLK_UNLOCK(sc);

        return (0);
}
```

The comment `/* XXX BMV: virtio_stop(), etc needed here? */` is an honest acknowledgement that the author was not sure how thorough the quiesce should be. The existing code sets a flag, waits for the queue to drain (that is what `vtblk_quiesce` does), and returns. On resume, it clears the flag and restarts I/O.

For a virtio block device, this is enough because the virtio host (the hypervisor) implements its own quiesce when the guest says it is suspending. The driver only needs to stop submitting new requests; the host deals with the rest.

This shows an important pattern: **the driver's quiesce depth depends on how much of the hardware's state is the driver's responsibility**. A bare-metal driver (like `re(4)`) has to program hardware registers carefully because the hardware has no other ally. A virtio driver has the hypervisor as an ally; the host can handle most of the state for the guest. The `myfirst` driver, running on a simulated backend, is in a similar position: the simulation is an ally, and the driver's quiesce can be correspondingly simpler.

### What the Comparison Shows

Reading multiple drivers' power-management code side by side is one of the best ways to build fluency. Each driver adapts the Chapter 22 pattern to its context: `re(4)` handles wake-on-LAN, `xl(4)` reuses `xl_shutdown = xl_suspend`, `virtio_blk(4)` trusts the hypervisor. The common thread is the structure: stop activity, save state, flag suspended, return 0 from suspend; on resume, clear flag, restore state, restart activity, return 0.

A reader who has Chapter 22 in memory can open any FreeBSD driver, find its `device_suspend` and `device_resume` in the method table, and read the two functions. Within a few minutes the driver's power policy is clear. That skill transfers to every driver the reader will ever work on; it is the single most useful takeaway from the chapter.



## Deep Look: ACPI Sleep States in More Detail

Section 1 introduced the ACPI S-states as a list. It is worth revisiting them with the driver's point of view in focus, because the driver sees slightly different things depending on which S-state the kernel is entering.

### S0: Working

S0 is the state the reader has worked in throughout Chapters 16 to 21. The CPU is executing, RAM is refreshed, the PCIe links are up. From the driver's point of view, S0 is continuous; everything is normal.

Within S0, however, there can still be fine-grained power transitions. The CPU may enter idle states (C1, C2, C3, etc.) between scheduler ticks. The PCIe link may enter L0s or L1 based on ASPM. Devices may enter D3 based on runtime PM. None of these require the driver to do anything beyond its own runtime-PM logic; they are transparent.

### S1: Standby

S1 is historically the lightest sleep state. The CPU stops executing but its registers are preserved; RAM stays powered; device power stays at D0 or D1. Wake latency is fast (under a second).

On modern hardware, S1 is rarely supported. The platform's BIOS advertises only S3 and deeper. If the platform does advertise S1 and the user enters it, the driver's `DEVICE_SUSPEND` is still called; the driver does its usual quiesce. The difference is that the PCI layer typically does not transition to D3 for S1 (because the bus stays powered), so the device stays in D0 through the transition. The driver's save and restore are largely unused.

A driver that supports S1 cleanly also supports S3, because the driver-side work is a subset. No driver written for Chapter 22 needs to treat S1 specially.

### S2: Reserved

S2 is defined in the ACPI specification but almost never implemented. A driver can safely ignore it; FreeBSD's ACPI layer treats S2 as S1 or S3 depending on platform support.

### S3: Suspend to RAM

S3 is the canonical sleep state Chapter 22 targets. When the user enters S3:

1. The kernel's suspend sequence traverses the device tree, calling `DEVICE_SUSPEND` on each driver.
2. The PCI layer's `pci_suspend_child` caches configuration space for each PCI device.
3. The PCI layer transitions each PCI device to D3hot.
4. Higher-level subsystems (ACPI, the CPU's idle machinery) enter their own sleep states.
5. The CPU's context is saved to RAM; the CPU halts.
6. RAM enters self-refresh; the memory controller maintains the contents with minimal power.
7. The platform's wake circuitry is armed: the power button, lid switch, and any configured wake sources.
8. The system waits for a wake event.

When a wake event arrives:

1. The CPU resumes; its context is restored from RAM.
2. Higher-level subsystems resume.
3. The PCI layer walks the device tree and calls `pci_resume_child` for each device.
4. Each device is transitioned to D0; its configuration is restored; pending PME# is cleared.
5. Each driver's `DEVICE_RESUME` is called.
6. User space unfreezes.

The driver sees only steps 1 (suspend) and 5 (resume) of each sequence. The rest is kernel and platform machinery.

A subtle point: during S3, RAM is refreshed but the kernel is not running. This means any kernel-side state (the softc, the DMA buffer, the pending tasks) survives S3 unchanged. The only thing that may be lost is hardware state: configuration registers in the device may be reset; BAR-mapped registers may return to default values. The driver's job on resume is to re-program the hardware from the preserved kernel state.

### S4: Suspend to Disk (Hibernate)

S4 is the "hibernate" state. The kernel writes the full contents of RAM to a disk image, then enters S5. On wake, the platform boots, the kernel reads the image back, and the system continues from where it left off.

On FreeBSD, S4 has historically been partial. The kernel can produce the hibernation image on some platforms, but the restore path is not as mature as Linux's. For driver purposes, S4 is the same as S3: the `DEVICE_SUSPEND` and `DEVICE_RESUME` methods are called; the driver's quiesce and restore paths work without change. The extra platform-level work (writing the image) is transparent.

The one difference the driver might notice is that after S4 resume, the PCI configuration space is always restored from scratch (the platform has fully rebooted), so even if the driver were relying on `hw.pci.do_power_suspend` being 0 to keep the device in D0, after S4 the device will still have been through a full power cycle. This matters only for drivers that do platform-specific tricks during suspend; most drivers are oblivious.

### S5: Soft Off

S5 is system power-off. The power button, the battery (if any), and the wake circuitry still receive power; everything else is off.

From the driver's point of view, S5 looks like a shutdown: `DEVICE_SHUTDOWN` is called (not `DEVICE_SUSPEND`), the driver places the device in a safe state for power-off, and the system halts. There is no resume corresponding to S5; if the user presses the power button, the system boots from scratch.

Shutdown is not a power transition in the reversible sense; it is a termination. The driver's `DEVICE_SHUTDOWN` method is called once, and the driver does not expect to run again until the next boot. The chapter's `myfirst_power_shutdown` handles this correctly by quiescing the device (same as suspend) and not trying to save any state (because there is no resume to save for).

### Observing Which States the Platform Supports

On any FreeBSD 14.3 system with ACPI, the supported states are exposed through a sysctl:

```sh
sysctl hw.acpi.supported_sleep_state
```

Typical outputs:

- A modern laptop: `S3 S4 S5`
- A server: `S5` (suspend not supported on many server platforms)
- A VM on bhyve: varies; usually `S5` only
- A VM on QEMU/KVM with `-machine q35`: often `S3 S4 S5`

If a driver is meant to work on a specific platform, the supported-state list tells you which transitions you need to test. A driver that only runs on servers does not need S3 testing; a driver meant for laptops does.

### What to Test

For Chapter 22's purposes, the minimum test is:

- `devctl suspend` / `devctl resume`: always possible; tests the driver-side code path.
- `acpiconf -s 3` (if supported): tests the full system suspend.
- System shutdown (`shutdown -p now`): tests the `DEVICE_SHUTDOWN` method.

S4 and runtime PM are optional; they exercise less-used code paths. A driver that passes the minimum test on a platform that supports S3 is in good shape; extensions are icing.

### Mapping Sleep States to the Driver's Methods

A compact table of which kobj method is called for each transition:

| Transition          | Method             | Driver Action                                    |
|---------------------|--------------------|--------------------------------------------------|
| S0 → S1             | DEVICE_SUSPEND     | Quiesce; save state                              |
| S0 → S3             | DEVICE_SUSPEND     | Quiesce; save state (device likely goes to D3)   |
| S0 → S4             | DEVICE_SUSPEND     | Quiesce; save state (followed by hibernate)      |
| S0 → S5 (shutdown)  | DEVICE_SHUTDOWN    | Quiesce; leave in safe state for power-off       |
| S1/S3 → S0          | DEVICE_RESUME      | Restore state; unmask interrupts                 |
| S4 → S0 (resume)    | (attach from boot) | Normal attach, because the kernel booted fresh   |
| devctl suspend      | DEVICE_SUSPEND     | Quiesce; save state (device goes to D3)          |
| devctl resume       | DEVICE_RESUME      | Restore state; unmask interrupts                 |

The driver does not distinguish S1, S3, and S4 from its own code; it always does the same work. The differences are at the platform and kernel levels. That uniformity is what makes the pattern scalable: one suspend path, one resume path, multiple contexts.



## Deep Look: PCIe Link States and ASPM in Action

Section 1 sketched the PCIe link states (L0, L0s, L1, L1.1, L1.2, L2). It is worth seeing how they behave in practice, because understanding them helps the driver developer interpret latency measurements and power observations.

### Why the Link Has Its Own States

A PCIe link is a pair of high-speed differential lanes between two endpoints (root complex and device, or root complex and switch). Each lane has a transmitter and a receiver; each lane's transmitter consumes power to keep the channel in a known state. When traffic is low, the transmitters can be turned off in various degrees, and the link can be re-established quickly when traffic resumes. The L-states describe those degrees.

The link's state is separate from the device's D-state. A device in D0 can have its link in L1 (the link is idle; the device is not transmitting or receiving). A device in D3 has its link in L2 or similar (the link is off). A device in D0 with a busy link is in L0.

### L0: Active

L0 is the normal operating state. Both sides of the link are active; data can flow in either direction; latency is at its minimum (a few hundred nanoseconds round-trip on a modern PCIe).

When a DMA transfer is running or an MMIO read is pending, the link is in L0. The device's own logic and the PCIe host bridge both require L0 for the transaction.

### L0s: Transmitter Standby

L0s is a low-power state where one side of the link's transmitter is turned off. The receiver stays on; the link can be brought back to L0 in under a microsecond.

L0s is entered automatically by the link logic when no traffic has been sent for a few microseconds. The platform's PCIe host bridge and the device's PCIe interface cooperate: when the transmit FIFO is empty and ASPM is enabled, the transmitter goes off. When new traffic arrives, the transmitter comes back on.

L0s is "asymmetric": each side independently enters and exits the state. A device's transmitter can be in L0s while the root complex's transmitter is in L0. This is useful because traffic is typically bursty: the CPU sends a DMA trigger, then does not send anything else for a while; the CPU's transmitter enters L0s quickly, while the device's transmitter stays in L0 because it is actively sending the DMA response.

### L1: Both Sides Standby

L1 is a deeper state where both transmitters are off. Neither side can send anything until the link is brought back to L0; the latency is measured in microseconds (5 to 65, depending on platform).

L1 is entered after a longer idle period than L0s. The exact threshold is configurable through ASPM settings; typical values are tens of microseconds of inactivity. L1 saves more power than L0s but costs more to exit.

### L1.1 and L1.2: Deeper L1 Sub-States

PCIe 3.0 and later define sub-states of L1 that turn off additional parts of the physical layer. L1.1 (also called "L1 PM Substate 1") keeps the clock running but turns off more circuitry; L1.2 turns off the clock as well. The wake latencies increase (tens of microseconds for L1.1; hundreds for L1.2), but the idle power draws decrease.

Most modern laptops use L1.1 and L1.2 aggressively to extend battery life. A laptop that stays in L1.2 most of the idle time can have PCIe power draw in the single-digit milliwatts, compared to hundreds of milliwatts in L0.

### L2: Near-Off

L2 is the state the link enters when the device is in D3cold. The link is effectively off; re-establishing it requires a full link-training sequence (tens of milliseconds). L2 is entered as part of the full-system suspend sequence; the driver does not manage it directly.

### Who Controls ASPM

ASPM is a per-link feature configured through the PCIe Link Capability and Link Control registers in both the root complex and the device. The configuration specifies:

- Whether L0s is enabled (one-bit field).
- Whether L1 is enabled (one-bit field).
- The exit latency thresholds the platform considers acceptable.

On FreeBSD, ASPM is usually controlled by the platform firmware through ACPI's `_OSC` method. The firmware tells the OS which capabilities to manage; if the firmware keeps ASPM control, the OS does not touch it. If the firmware hands over control, the OS may enable or disable ASPM per link based on policy.

For Chapter 22's `myfirst` driver, ASPM is the platform's job. The driver does not configure ASPM; it does not need to know whether the link is in L0 or L1 at any moment. The link's state is invisible to the driver from a functional standpoint (latency is the only observable effect).

### When ASPM Matters to the Driver

There are specific situations where a driver does have to worry about ASPM:

1. **Known errata.** Some PCIe devices have bugs in their ASPM implementation that cause the link to wedge or produce corrupted transactions. The driver may need to explicitly disable ASPM for those devices. The kernel provides the PCIe Link Control register access through `pcie_read_config` and `pcie_write_config` for this purpose.

2. **Latency-sensitive devices.** A real-time audio or video device may not tolerate the microsecond-scale latency of L1. The driver may disable L1 while keeping L0s enabled.

3. **Power-sensitive devices.** A battery-powered device may want L1.2 always enabled. The driver may force L1.2 if the platform's default is less aggressive.

For the `myfirst` driver, none of these apply. The simulated device does not have a link at all; the real PCIe link (if any) is handled by the platform. The chapter mentions ASPM for completeness and moves on.

### Observing Link States

On a system where the platform supports ASPM observation, the link state is exposed through `pciconf -lvbc`:

```sh
pciconf -lvbc | grep -A 20 myfirst
```

Look for lines like:

```text
cap 10[ac] = PCI-Express 2 endpoint max data 128(512) FLR NS
             link x1(x1) speed 5.0(5.0)
             ASPM disabled(L0s/L1)
             exit latency L0s 1us/<1us L1 8us/8us
             slot 0
```

The "ASPM disabled" on this line says ASPM is not currently active. "disabled(L0s/L1)" says the device supports both L0s and L1 but neither is enabled. On a system with aggressive ASPM, the line would read "ASPM L1" or similar.

The exit latencies tell the driver how long the transition back to L0 takes; a latency-sensitive driver can decide whether L1 is tolerable by looking at this number.

### Link State and Power Consumption

A rough table of PCIe power draws (typical values; actual depend on implementation):

| State | Power (x1 link) | Exit Latency |
|-------|-----------------|--------------|
| L0    | 100-200 mW      | 0            |
| L0s   | 50-100 mW       | <1 µs        |
| L1    | 10-30 mW        | 5-65 µs      |
| L1.1  | 1-5 mW          | 10-100 µs    |
| L1.2  | <1 mW           | 50-500 µs    |
| L2    | near 0          | 1-100 ms     |

For a laptop with a dozen PCIe links all in L1.2 during idle, the aggregate savings relative to all-L0 can be in the watts. For a server with high-throughput links always in L0, ASPM is disabled and the power saving is zero.

Chapter 22 does not implement ASPM for `myfirst`. The chapter mentions it because understanding the link state machine is part of understanding the full power-management picture. A reader who later works on a driver with known ASPM errata will know where to look.



## Deep Look: Wake Sources Explained

Wake sources are the mechanisms that bring a suspended system or device back to active. Chapter 1 mentioned them briefly; this deeper look walks through the most common ones.

### PME# on PCIe

The PCI spec defines the `PME#` signal (Power Management Event). When asserted, it tells the upstream root complex that the device has an event worth waking for. The root complex converts PME# into an ACPI GPE or interrupt, which the kernel handles.

A device that supports PME# has a PCI power-management capability (checked via `pci_has_pm`). The capability's control register includes:

- **PME_En** (bit 8): enable PME# generation.
- **PME_Status** (bit 15): set by the device when PME# is raised, cleared by software.
- **PME_Support** (read-only, bits 11-15 in PMC register): which D-states the device can raise PME# from (D0, D1, D2, D3hot, D3cold).

The driver's job is to set PME_En at the right time (usually before suspend) and to clear PME_Status at the right time (usually after resume). The `pci_enable_pme(dev)` and `pci_clear_pme(dev)` helpers do both jobs.

On a typical laptop, the root complex routes PME# to an ACPI GPE, which the kernel's ACPI driver picks up as a wake event. The chain looks like:

```text
device asserts PME#
  → root complex receives PME
  → root complex sets GPE status bit
  → ACPI hardware interrupts CPU
  → kernel wakes from S3
  → kernel's ACPI driver services the GPE
  → eventually: DEVICE_RESUME on the device that woke
```

The whole chain takes one to three seconds. The driver's role is minimal: it enabled PME# before suspend, and it will clear PME_Status after resume. Everything else is platform.

### USB Remote Wakeup

USB has its own wake mechanism called "remote wakeup". A USB device requests wake capability through its standard descriptor; the host controller enables the capability at enumeration time; when the device asserts a resume signal on its upstream port, the host controller propagates it.

From a FreeBSD driver perspective, USB remote wakeup is almost entirely handled by the USB host controller driver (`xhci`, `ohci`, `uhci`). Individual USB device drivers (for keyboards, storage, audio, etc.) participate through the USB framework's suspend and resume callbacks, but they do not deal with PME# directly. The USB host controller's own PME# is what actually wakes the system.

For Chapter 22 purposes, USB wake is a black box that works through the USB host controller driver. A reader who eventually writes a USB device driver will learn the framework's conventions then.

### GPIO-Based Wake on Embedded Platforms

On embedded platforms (arm64, RISC-V), wake sources are typically GPIO pins connected to the SoC's wake logic. The device tree describes which pins are wake sources via `wakeup-source` properties and `interrupts-extended` pointing to the wake controller.

FreeBSD's GPIO intr framework handles these. A device driver whose hardware is wake-capable reads the device-tree `wakeup-source` property during attach, registers the GPIO as a wake source with the framework, and the framework does the rest. The mechanism is very different from PCIe PME#, but the driver-side API (mark wake enabled, clear wake status) is conceptually similar.

Chapter 22 does not exercise GPIO wake; the `myfirst` driver is a PCI device. Part 7 revisits embedded platforms and covers the GPIO path in detail.

### Wake on LAN (WoL)

Wake on LAN is a specific implementation pattern for a network controller. The controller watches incoming packets for a "magic packet" (a specific pattern containing the controller's MAC address repeated many times) or for user-configured patterns. When a match is detected, the controller asserts PME# upstream.

From the driver's perspective, WoL requires:

1. Configuring the NIC's wake logic (magic-packet filter, pattern filters) before suspend.
2. Enabling PME# via `pci_enable_pme`.
3. On resume, disabling the wake logic (because normal packet processing would otherwise be influenced by the filters).

The `re(4)` driver's `re_setwol` is the canonical FreeBSD example. A reader building a NIC driver copies its structure and adapts the device-specific register programming.

### Wake on Lid, Power Button, etc.

The laptop's lid switch, power button, keyboard (in some cases), and other platform inputs are wired to the platform's wake logic through ACPI. The ACPI driver handles the wake; individual device drivers are not involved.

The ACPI `_PRW` method on a device's object in the ACPI namespace declares which GPE that device's wake event uses. The OS reads `_PRW` during boot to configure the wake routing. The `myfirst` driver, as a simple PCI endpoint with no platform-specific wake source, does not have a `_PRW` method; its wake capability (if any) is purely through PME#.

### When the Driver Must Enable Wake

A simple heuristic: the driver must enable wake if the user has asked for it (through an interface capability flag like `IFCAP_WOL` for NICs) and the hardware supports it (`pci_has_pm` returns true, the device's own wake logic is operational). Otherwise, the driver leaves wake disabled.

A driver that enables wake for every device by default wastes platform power; the wake circuitry and PME# routing cost a few milliwatts continuously. A driver that never enables wake frustrates users who want their laptop to wake on a network packet. The policy is "enable only when asked".

FreeBSD's interface capabilities (set via `ifconfig em0 wol wol_magic`) are the standard way users express the desire. The NIC driver reads the flags and configures WoL accordingly.

### Testing Wake Sources

Testing wake is harder than testing suspend and resume, because testing wake requires the system to actually sleep and then an external event to wake it. Common approaches:

- **Magic packet from another machine.** Send a WoL magic packet to the suspended machine's MAC address. If WoL is working, the machine wakes in a few seconds.
- **Lid switch.** Close the lid, wait, open the lid. If the platform's wake routing is working, the machine wakes on open.
- **Power button.** Press the power button briefly while suspended. The machine should wake.

For a learning driver like `myfirst`, there is no meaningful wake source to test against. The chapter mentions wake mechanics for pedagogical completeness, not because the driver exercises them.



## Deep Look: The hw.pci.do_power_suspend Tunable

One of the most important tunables for power-management debugging is `hw.pci.do_power_suspend`. It controls whether the PCI layer automatically transitions devices to D3 during system suspend. Understanding what it does and when to change it is worth a dedicated look.

### What the Default Does

With `hw.pci.do_power_suspend=1` (the default), the PCI layer's `pci_suspend_child` helper, after calling the driver's `DEVICE_SUSPEND`, transitions the device to D3hot by calling `pci_set_power_child(dev, child, PCI_POWERSTATE_D3)`. On resume, `pci_resume_child` transitions back to D0.

This is the "power-save" mode. A device that supports D3 uses its lowest-power idle state during suspend. A laptop benefits because battery life during sleep is extended; a device that can sleep at a few milliwatts instead of a few hundred is worth the extra D-state transition.

### What hw.pci.do_power_suspend=0 Does

With the tunable set to 0, the PCI layer does not transition the device to D3. The device stays in D0 throughout the suspend. The driver's `DEVICE_SUSPEND` runs; the driver quiesces activity; the device stays powered.

From a power-saving perspective, this is worse: the device continues to draw its D0 power budget during sleep. From a correctness perspective, it can be better for some devices:

- A device with broken D3 implementation may misbehave when transitioned. Staying in D0 avoids the transition bug.
- A device whose context is expensive to save and restore may prefer to stay in D0 during a short suspend. If the suspend is only a few seconds, the context-save cost exceeds the power-saving benefit.
- A device that is critical to the machine's core function (a console keyboard, for example) may need to stay alert even during suspend.

### When to Change It

For development and debugging, setting `hw.pci.do_power_suspend=0` can isolate bugs:

- If a resume bug appears only with the tunable at 1, the bug is in the D3-to-D0 transition (either in the PCI layer's config restore, or in the driver's handling of a device that has been reset).
- If a resume bug appears with the tunable at 0 as well, the bug is in the driver's `DEVICE_SUSPEND` or `DEVICE_RESUME` code, not in the D-state machinery.

For production, the default (1) is almost always right. Changing it globally affects every PCI device on the system; a better approach is a per-device override if one is needed, which typically lives in the driver itself.

### Verifying the Tunable Is in Effect

A quick way to verify is to check the device's power state with `pciconf` before and after a suspend:

```sh
# Before suspend (device should be in D0):
pciconf -lvbc | grep -A 5 myfirst

# With hw.pci.do_power_suspend=1 (default):
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 5 myfirst
# "powerspec" should show D3

# With hw.pci.do_power_suspend=0:
sudo sysctl hw.pci.do_power_suspend=0
sudo devctl resume myfirst0
sudo devctl suspend myfirst0
pciconf -lvbc | grep -A 5 myfirst
# "powerspec" should show D0

# Reset to default.
sudo sysctl hw.pci.do_power_suspend=1
sudo devctl resume myfirst0
```

The `powerspec` line in `pciconf -lvbc` output shows the current power state. Watching it change between D0 and D3 confirms the automatic transition is happening.

### Interaction with pci_save_state

When `hw.pci.do_power_suspend` is 1, the PCI layer automatically calls `pci_cfg_save` before transitioning to D3. When it is 0, the PCI layer does not call `pci_cfg_save`.

This has a subtle implication: if the driver wants to save configuration explicitly in the 0 case, it must call `pci_save_state` itself. The Chapter 22 pattern assumes the default (1) and does not call `pci_save_state` explicitly; a driver that wants to support both modes would need additional logic.

### Does the Tunable Affect System Suspend or devctl suspend?

Both. `pci_suspend_child` is called for both `acpiconf -s 3` and `devctl suspend`, and the tunable gates the D-state transition in both cases. A reader debugging with `devctl suspend` will see the same behavior as with a full system suspend, modulo the other platform work (CPU park, ACPI sleep state entry).

### A Concrete Debugging Scenario

Suppose the `myfirst` driver's resume fails intermittently: sometimes it works, sometimes `dma_test_read` after resume returns EIO. The counters are consistent (suspend count = resume count), the logs show both methods ran, but the post-resume DMA fails.

**Hypothesis 1.** The D3-to-D0 transition is producing an inconsistent device state. Verify by setting `hw.pci.do_power_suspend=0` and retrying.

If the bug disappears with the tunable at 0, the D-state machinery is involved. The fix might be in the driver's resume path (add a delay after the transition to let the device stabilise), in the PCI layer's config restore, or in the device itself.

**Hypothesis 2.** The bug is in the driver's own suspend/resume code, independent of D3. Verify by setting the tunable to 0 and retrying.

If the bug persists with the tunable at 0, the driver's code is the problem. The D3 transition is innocent.

This kind of bisection is common in power-management debugging. The tunable is the tool that lets you isolate the variable.



## Deep Look: DEVICE_QUIESCE and When You Need It

Section 2 briefly mentioned `DEVICE_QUIESCE` as the third power-management method alongside `DEVICE_SUSPEND` and `DEVICE_SHUTDOWN`. It is rarely implemented explicitly in FreeBSD drivers; a search of `/usr/src/sys/dev/` shows only a handful of drivers define their own `device_quiesce`. Understanding when you do need it and when you do not is worth a short section.

### What DEVICE_QUIESCE Is For

The `device_quiesce` wrapper in `/usr/src/sys/kern/subr_bus.c` is called in several places:

- `devclass_driver_deleted`: when a driver is being unloaded, the framework calls `device_quiesce` on every instance before calling `device_detach`.
- `DEV_DETACH` via devctl: when the user runs `devctl detach myfirst0`, the kernel calls `device_quiesce` before `device_detach` unless the `-f` (force) flag is given.
- `DEV_DISABLE` via devctl: when the user runs `devctl disable myfirst0`, the kernel calls `device_quiesce` similarly.

In each case, the quiesce is a pre-check: "can the driver safely stop what it is doing?". A driver that returns EBUSY from `DEVICE_QUIESCE` prevents the subsequent detach or disable. The user gets an error, and the driver stays attached.

### What the Default Does

If a driver does not implement `DEVICE_QUIESCE`, the default (`null_quiesce` in `device_if.m`) returns 0 unconditionally. The kernel proceeds with detach or disable.

For most drivers, this is fine. The driver's detach path handles any in-flight work, so there is nothing the quiesce would do that detach does not also do.

### When You Would Implement It

A driver implements `DEVICE_QUIESCE` explicitly when:

1. **Returning EBUSY is more informative than waiting.** If the driver has a concept of "busy" (a transfer in flight, an open file descriptor count, a filesystem mount), and the user can wait for it to become non-busy, the driver might refuse quiesce until busy is zero. `DEVICE_QUIESCE` returning EBUSY tells the user "the device is busy; wait and retry".

2. **The quiesce can be done faster than a full detach.** If detach is expensive (frees large resource tables, drains slow queues) but the device can be stopped cheaply, `DEVICE_QUIESCE` lets the kernel probe for readiness without paying detach's cost.

3. **The driver wants to distinguish quiesce from suspend.** If the driver wants to stop activity but not save state (because no resume is coming), implementing quiesce separately from suspend is a way to express that distinction in code.

For the `myfirst` driver, none of these apply. The Chapter 21 detach path already handles in-flight work; the Chapter 22 suspend path handles quiesce in the power-management sense. Adding a separate `DEVICE_QUIESCE` would be redundant.

### An Example from bce(4)

The Broadcom NetXtreme driver in `/usr/src/sys/dev/bce/if_bce.c` has a commented-out `DEVMETHOD(device_quiesce, bce_quiesce)` entry in its method table. The comment suggests the author considered implementing quiesce but did not. This is common: many drivers keep the line commented as a TODO that never gets implemented, because the default handles their use case.

The implementation, if the driver enabled it, would stop the NIC's TX and RX paths without freeing the hardware resources. A subsequent `device_detach` would then do the actual freeing. The split between "stop" and "free" is what `DEVICE_QUIESCE` would express.

### Relation to DEVICE_SUSPEND

`DEVICE_QUIESCE` and `DEVICE_SUSPEND` do similar things: they stop the device's activity. The differences:

- **Lifecycle**: quiesce is between run and detach; suspend is between run and eventual resume.
- **Resources**: quiesce does not require the driver to save any state; suspend does.
- **Ability to veto**: both can return EBUSY; the consequences differ (quiesce prevents detach; suspend prevents the power transition).

A driver that implements both usually shares code: `foo_quiesce` might do "stop activity" and `foo_suspend` might do "call quiesce; save state; return". The `myfirst` driver's `myfirst_quiesce` helper is the shared code; the chapter does not wire it to a `DEVICE_QUIESCE` method, but doing so would be a small addition.

### An Optional Addition to myfirst

As a challenge, the reader can add `DEVICE_QUIESCE` to `myfirst`:

```c
static int
myfirst_pci_quiesce(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        device_printf(dev, "quiesce: starting\n");
        (void)myfirst_quiesce(sc);
        atomic_add_64(&sc->power_quiesce_count, 1);
        device_printf(dev, "quiesce: complete\n");
        return (0);
}
```

And the matching method-table entry:

```c
DEVMETHOD(device_quiesce, myfirst_pci_quiesce),
```

Testing it: `devctl detach myfirst0` calls quiesce before detach; the reader can verify by reading `dev.myfirst.0.power_quiesce_count` immediately before the detach takes effect.

The challenge is short and does not change the driver's overall structure; it just wires one more method. Chapter 22's consolidated Stage 4 does not include it by default, but the reader who wants the method can add it in a few lines.



## Hands-On Labs

Chapter 22 includes three hands-on labs that exercise the power-management path in progressively harder ways. Each lab has a script in `examples/part-04/ch22-power/labs/` that the reader can run as-is, plus extension ideas.

### Lab 1: Single-Cycle Suspend-Resume

The first lab is the simplest: one clean suspend-resume cycle with counter verification.

**Setup.** Load the Chapter 22 Stage 4 driver:

```sh
cd examples/part-04/ch22-power/stage4-final
make clean && make
sudo kldload ./myfirst.ko
```

Verify attach:

```sh
sysctl dev.myfirst.0.%driver
# Should return: myfirst
sysctl dev.myfirst.0.suspended
# Should return: 0
```

**Run.** Execute the cycle script:

```sh
sudo sh ../labs/ch22-suspend-resume-cycle.sh
```

Expected output:

```text
PASS: one suspend-resume cycle completed cleanly
```

**Verify.** Inspect the counters:

```sh
sysctl dev.myfirst.0.power_suspend_count
# Should return: 1
sysctl dev.myfirst.0.power_resume_count
# Should return: 1
```

Check `dmesg`:

```sh
dmesg | tail -6
```

Should show four lines (suspend start, suspend complete, resume start, resume complete) plus the pre-and-post transfer log lines.

**Extension.** Modify the cycle script to run two suspend-resume cycles instead of one, and verify that the counters increment by exactly 2 each.

### Lab 2: One-Hundred-Cycle Stress

The second lab runs the cycle script one hundred times in a row and checks that nothing drifts.

**Run.**

```sh
sudo sh ../labs/ch22-suspend-stress.sh
```

Expected output after a few seconds:

```text
PASS: 100 cycles
```

**Verify.** After the stress run, the counters should each be 100 (or 100 plus whatever was there before):

```sh
sysctl dev.myfirst.0.power_suspend_count
# 100 (or however many cycles were added)
```

**Observations to make.**

- How long does one cycle take? On the simulation, it should be a few milliseconds. On real hardware with D-state transitions, expect a few hundred microseconds to a few milliseconds.
- Does the system's load average change during the stress? The simulation is cheap; a hundred cycles on a modern machine should barely register.
- What happens if you run the DMA test during the stress? (`sudo sysctl dev.myfirst.0.dma_test_read=1` concurrently with the cycle loop.) A well-written driver should handle this gracefully; the DMA test succeeds if it happens during a `RUNNING` window and fails with EBUSY or similar if it happens during a transition.

**Extension.** Run the stress script with `dmesg -c` before to clear the log, then afterwards:

```sh
dmesg | wc -l
```

Should be close to 400 (four log lines per cycle, times 100 cycles). A log-line-per-cycle count lets you verify that every cycle actually executed through the driver.

### Lab 3: Transfer Across a Cycle

The third lab is the hardest: it starts a DMA transfer and immediately suspends in the middle of it, then resumes and verifies that the driver recovers.

**Setup.** The lab script is `ch22-transfer-across-cycle.sh`. It runs a DMA transfer in the background, sleeps a few milliseconds, calls `devctl suspend`, sleeps, calls `devctl resume`, and then starts another transfer.

**Run.**

```sh
sudo sh ../labs/ch22-transfer-across-cycle.sh
```

**Observations to make.**

- Does the first transfer complete, error out, or time out? The expected behavior is that the quiesce aborts it cleanly; the transfer reports EIO or ETIMEDOUT.
- Does the counter `dma_errors` or `dma_timeouts` increment? One of them should.
- Does `dma_in_flight` go back to false after the suspend?
- Does the post-resume transfer succeed normally? If yes, the driver's state is consistent and the cycle worked.

**Extension.** Reduce the sleep between the transfer start and the suspend to hit the corner case where the transfer is mid-execution at the moment of the suspend. That is where race conditions live; a driver that passes this test under aggressive timing has a solid quiesce implementation.

### Lab 4: Runtime PM (Optional)

For readers building with `MYFIRST_ENABLE_RUNTIME_PM`, a fourth lab exercises the runtime-PM path.

**Setup.** Rebuild with runtime PM enabled:

```sh
cd examples/part-04/ch22-power/stage4-final
# Uncomment the CFLAGS line in the Makefile:
#   CFLAGS+= -DMYFIRST_ENABLE_RUNTIME_PM
make clean && make
sudo kldload ./myfirst.ko
```

**Run.**

```sh
sudo sh ../labs/ch22-runtime-pm.sh
```

The script:

1. Sets the idle threshold to 3 seconds (instead of the default 5).
2. Records baseline counters.
3. Waits 5 seconds without any activity.
4. Verifies `runtime_state` is `RUNTIME_SUSPENDED`.
5. Triggers a DMA transfer.
6. Verifies `runtime_state` is back to `RUNNING`.
7. Prints PASS.

**Observations to make.**

- During the idle wait, `dmesg` should show the "runtime suspend" log line approximately 3 seconds in.
- The `runtime_suspend_count` and `runtime_resume_count` should each be 1 at the end.
- The DMA transfer should succeed normally after the runtime resume.

**Extension.** Set the idle threshold to 1 second. Run the DMA test repeatedly in a tight loop. You should see no runtime-suspend transitions during the loop (because each test resets the idle timer), but as soon as the loop stops, the runtime suspend fires.

### Lab Notes

All of the labs assume the driver is loaded and the system is idle enough that transitions happen on-demand. If another process is actively using the device (unlikely for `myfirst`, but common in real setups), the counters drift by unexpected amounts and the scripts' exact-increment checks fail. The scripts are designed for a quiet test environment, not a noisy one.

For realistic testing of the `re(4)` driver or other production drivers, the same script structure applies with the device name adjusted. The `devctl suspend`/`devctl resume` dance works for any PCI device the kernel manages.



## Challenge Exercises

The Chapter 22 challenge exercises push the reader beyond the baseline driver into territory that real-world drivers eventually have to handle. Each exercise is scoped to be achievable with the chapter's material and a few hours of work.

### Challenge 1: Implement a Wake-on-Sysctl Mechanism

Extend the `myfirst` driver with a simulated wake source. The simulation already has a callout that can fire; add a new simulation feature that sets a "wake" bit on the device while it is in D3, and have the driver's `DEVICE_RESUME` path log the wake event.

**Hints.**

- Add a `MYFIRST_REG_WAKE_STATUS` register to the simulation backend.
- Add a `MYFIRST_REG_WAKE_ENABLE` register the driver writes during suspend.
- Have the simulation callout set the wake status bit after a random delay.
- On resume, the driver reads the register and logs whether a wake was observed.

**Verification.** After `devctl suspend; sleep 1; devctl resume`, the log should show the wake status. A follow-up `sysctl dev.myfirst.0.wake_events` should increment.

**Why this matters.** Wake source handling is one of the trickiest parts of real-hardware power management. Building it into the simulation lets the reader exercise the full contract without needing hardware.

### Challenge 2: Save and Restore a Descriptor Ring

The Chapter 21 simulation does not yet use a descriptor ring (transfers are one-at-a-time). Extend the simulation with a small descriptor ring, program its base address through a register at attach, and have the suspend path save the ring's base address into softc state. Have the resume path write the saved base address back.

**Hints.**

- The ring's base address is a `bus_addr_t` held in the softc.
- The register is `MYFIRST_REG_RING_BASE_LOW`/`_HIGH`.
- Saving and restoring is trivial; the point is to verify that *not* saving and restoring would break things.

**Verification.** After suspend-resume, the ring base register should hold the same value as before. Without the restore, it should hold zero.

**Why this matters.** Descriptor rings are what real high-throughput drivers use; a power-aware driver with a ring has to restore the base address on every resume. This exercise is a stepping stone to the kind of state management that production drivers like `re(4)` and `em(4)` perform.

### Challenge 3: Implement a Veto Policy

Extend the suspend path with a policy knob that lets the user specify whether the driver should veto a suspend when the device is busy. Specifically:

- Add `dev.myfirst.0.suspend_veto_if_busy` as a read-write sysctl.
- If the sysctl is 1 and a DMA transfer is in flight, `myfirst_power_suspend` returns EBUSY without quiescing.
- If the sysctl is 0 (default), suspend always succeeds.

**Hints.** Set `suspend_veto_if_busy` to 1. Start a long DMA transfer (add a `DELAY` to the simulation's engine to make it last a second or two). Call `devctl suspend myfirst0` during the transfer. Verify that the suspend returns an error and `dev.myfirst.0.suspended` stays 0.

**Verification.** The kernel's unwind path runs; the driver is still in `RUNNING`; the transfer completes normally.

**Why this matters.** Vetoing is an effective tool and a dangerous one. Real-world policy decisions about whether to veto are nuanced (storage drivers often veto; NIC drivers usually do not). Implementing the mechanism makes the policy question tangible.

### Challenge 4: Add a Post-Resume Self-Test

After resume, do a minimum-viable test of the device: write a known pattern to the DMA buffer, trigger a write transfer, read it back with a read transfer, and verify. If the test fails, mark the device broken and fail subsequent operations.

**Hints.**

- Add the self-test as a helper that runs from `myfirst_power_resume` after `myfirst_restore`.
- Use a well-known pattern like `0xDEADBEEF`.
- Use the existing DMA path; the self-test is just one write and one read.

**Verification.** Under normal operation, the self-test always passes. To verify it catches failures, add an artificial "fail once" mechanism to the simulation and trigger it; the driver should log the failure and mark itself broken.

**Why this matters.** Self-tests are a lightweight form of reliability engineering. A driver that catches its own failures at well-defined points is easier to debug than one that silently corrupts data until a user notices.

### Challenge 5: Implement Manual pci_save_state / pci_restore_state

Most drivers let the PCI layer handle config-space save-and-restore automatically. Extend the Chapter 22 driver to optionally do it manually, gated by a sysctl `dev.myfirst.0.manual_pci_save`.

**Hints.**

- Read `hw.pci.do_power_suspend` and `hw.pci.do_power_resume` and set them to 0 when manual mode is enabled.
- Call `pci_save_state` explicitly in the suspend path, `pci_restore_state` in the resume path.
- Verify that the device still works after suspend-resume.

**Verification.** The device should function identically whether or not manual mode is enabled. Set the sysctl before a stress test and verify no drift.

**Why this matters.** Some real drivers need manual save/restore because the PCI layer's automatic handling interferes with device-specific quirks. Knowing when and how to take over the save/restore is a useful intermediate skill.



## Troubleshooting Reference

This section collects the common problems a reader may encounter while working through Chapter 22, with a short diagnostic and fix for each. The list is meant to be skimmable; if a problem matches, skip to the corresponding entry.

### "devctl: DEV_SUSPEND failed: Operation not supported"

The driver does not implement `DEVICE_SUSPEND`. Either the method table is missing the `DEVMETHOD(device_suspend, ...)` line, or the driver has not been rebuilt and reloaded.

**Fix.** Check the method table. Rebuild with `make clean && make`. Unload and reload.

### "devctl: DEV_SUSPEND failed: Device busy"

The driver returned `EBUSY` from `DEVICE_SUSPEND`, probably because of the veto logic from Challenge 3, or because the device is genuinely busy (DMA in flight, task running) and the driver chose to veto.

**Fix.** Check whether the `suspend_veto_if_busy` knob is set. Check `dma_in_flight`. Wait for activity to complete before suspending.

### "devctl: DEV_RESUME failed"

`DEVICE_RESUME` returned non-zero. The log should have more detail.

**Fix.** Check `dmesg | tail`. The resume log line should tell you what failed. Usually it is a hardware-specific init step that did not succeed.

### Device is suspended but `dev.myfirst.0.suspended` reads 0

The driver's flag is out of sync with the kernel's state. Probably a bug in the quiesce path: the flag was never set, or was cleared prematurely.

**Fix.** Add a `KASSERT(sc->suspended == true)` at the top of the resume path; run under `INVARIANTS` to catch the bug.

### `power_suspend_count != power_resume_count`

A cycle got one side but not the other. Check `dmesg` for errors; the log should show where the sequence broke.

**Fix.** Fix the code path that is missing. Usually an early return without the counter update.

### DMA transfers fail after resume

The restore path did not reinitialise the DMA engine. Check the INTR_MASK register, the DMA control registers, the `saved_intr_mask` value. Enable verbose logging to see the resume path's restoration sequence.

**Fix.** Add a missing register write to `myfirst_restore`.

### WITNESS complains about a lock held during suspend

The suspend path acquired a lock and then called a function that sleeps or tries to acquire another lock. Read the WITNESS message for the offending lock names.

**Fix.** Drop the lock before the sleeping call, or restructure the code so the lock is acquired only when needed.

### System does not wake from S3

A driver below `myfirst` is blocking resume. Unlikely to be `myfirst` itself unless the logs show an error from the driver specifically.

**Fix.** Boot into single-user mode, or load fewer drivers, and bisect. Check `dmesg` in the live system for the offending driver.

### Runtime PM never fires

The idle watcher callout is not running, or the `last_activity` timestamp is being updated too often.

**Fix.** Verify `callout_reset` is being called from the attach path. Verify `myfirst_mark_active` is not being called from unexpected code paths. Add logging to the callout callback to confirm it fires.

### Kernel panic during suspend

A KASSERT failed (on an `INVARIANTS` kernel) or a lock is held incorrectly. The panic message identifies the offending file and line.

**Fix.** Read the panic message. Match the file and line to the code. The fix is usually straightforward once the location is identified.



## Wrapping Up

Chapter 22 closes Part 4 by giving the `myfirst` driver the discipline of power management. At the start, `myfirst` at version `1.4-dma` was a capable driver: it attached to a PCI device, handled multi-vector interrupts, moved data through DMA, and cleaned up its resources on detach. What it lacked was the ability to participate in the system's power transitions. It would crash, leak, or silently fail if the user closed the laptop lid or asked the kernel to suspend the device. At the end, `myfirst` at version `1.5-power` handles every power transition the kernel can throw at it: system suspend to S3 or S4, per-device suspend through `devctl`, system shutdown, and optional runtime power management.

The eight sections walked the full progression. Section 1 established the big picture: why a driver cares about power, what ACPI S-states and PCI D-states are, what PCIe L-states and ASPM add, and what wake sources look like. Section 2 introduced FreeBSD's concrete APIs: the `DEVICE_SUSPEND`, `DEVICE_RESUME`, `DEVICE_SHUTDOWN`, and `DEVICE_QUIESCE` methods, the `bus_generic_suspend` and `bus_generic_resume` helpers, and the PCI layer's automatic config-space save and restore. The Stage 1 skeleton made the methods log and count transitions without doing any real work. Section 3 turned the suspend skeleton into a real quiesce: mask interrupts, drain DMA, drain workers, in that order, with helper functions shared between suspend and detach. Section 4 wrote the matching resume path: re-enable bus-master, restore device-specific state, clear the suspended flag, unmask interrupts. Section 5 added optional runtime power management with an idle-watcher callout and explicit `pci_set_powerstate` transitions. Section 6 surveyed the user-space interface: `acpiconf`, `zzz`, `devctl suspend`, `devctl resume`, `devinfo -v`, and the matching sysctls. Section 7 catalogued the characteristic failure modes and their debugging patterns. Section 8 refactored the code into `myfirst_power.c`, bumped the version to `1.5-power`, added `POWER.md`, and wired the final regression test.

What Chapter 22 did not do is scatter-gather power management for multi-queue drivers (that is a Part 6 topic, Chapter 28), hotplug and surprise-removal integration (a Part 7 topic), embedded-platform power domains (Part 7 again), or the internals of ACPI's AML interpreter (never covered in this book). Each of those is a natural extension built on Chapter 22's primitives, and each belongs in a later chapter where the scope matches. The foundation is in place; the specialisations add vocabulary without needing a new foundation.

The file layout has grown: 16 source files (including `cbuf`), 8 documentation files (`HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, `POWER.md`), and an extended regression suite that covers every subsystem. The driver is structurally parallel to production FreeBSD drivers; a reader who has worked through Chapters 16 through 22 can open `if_re.c`, `if_xl.c`, or `virtio_blk.c` and recognise every architectural part: register accessors, simulation backend, PCI attach, interrupt filter and task, per-vector machinery, DMA setup and teardown, sync discipline, power suspend, power resume, clean detach.

### A Reflection Before Chapter 23

Chapter 22 is the last chapter of Part 4, and Part 4 is the part that taught the reader how a driver talks to hardware. Chapters 16 through 21 introduced the primitives: MMIO, simulation, PCI, interrupts, multi-vector interrupts, DMA. Chapter 22 introduced the discipline: how those primitives survive power transitions. Together, the seven chapters take the reader from "no idea what a driver is" to "a working multi-subsystem driver that handles every hardware event the kernel can throw at it".

Chapter 22's teaching generalises. A reader who has internalised the suspend-quiesce-save-restore pattern, the interaction between driver and PCI layer, the runtime-PM state machine, and the debugging patterns will find similar shapes in every power-aware FreeBSD driver. The specific hardware differs; the structure does not. A driver for a NIC, a storage controller, a GPU, or a USB host controller applies the same vocabulary to its own hardware.

Part 5, which begins with Chapter 23, shifts focus. Part 4 was about the driver-to-hardware direction: how the driver talks to the device. Part 5 is about the driver-to-kernel direction: how the driver is debugged, traced, tooled, and stressed by the humans who maintain it. Chapter 23 starts that shift with debugging and tracing techniques that apply across every driver subsystem.

### What to Do If You Are Stuck

Three suggestions.

First, focus on the Stage 2 suspend and Stage 3 resume paths. If `devctl suspend myfirst0` followed by `devctl resume myfirst0` succeeds and a subsequent DMA transfer works, the core of the chapter is working. Every other piece of the chapter is optional in the sense that it decorates the pipeline, but if the pipeline fails, the chapter is not working and Section 3 or Section 4 is the right place to diagnose.

Second, open `/usr/src/sys/dev/re/if_re.c` and re-read `re_suspend`, `re_resume`, and `re_setwol`. Each function is about thirty lines. Every line maps to a Chapter 22 concept. Reading them once after completing the chapter should feel like familiar territory; the real driver's patterns will look like elaborations of the chapter's simpler ones.

Third, skip the challenges on the first pass. The labs are calibrated for Chapter 22's pace; the challenges assume the chapter's material is solid. Come back to them after Chapter 23 if they feel out of reach now.

Chapter 22's goal was to give the driver power-management discipline. If it has, Chapter 23's debugging and tracing machinery becomes a generalisation of what you already do instinctively rather than a new topic.

## Part 4 Checkpoint

Part 4 has been the longest and densest stretch of the book so far. Seven chapters covered hardware resources, register I/O, PCI attach, interrupts, MSI and MSI-X, DMA, and power management. Before Part 5 changes the mode from "writing drivers" to "debugging and tracing them," confirm that the hardware-facing story is internalized.

By the end of Part 4 you should be able to do each of the following without searching:

- Claim a hardware resource with `bus_alloc_resource_any` or `bus_alloc_resource_anywhere`, access it through the `bus_space(9)` read/write and barrier primitives, and release it cleanly in detach.
- Read and write device registers through the `bus_space(9)` abstraction rather than raw pointer dereferences, with correct barrier discipline around sequences that must not be reordered.
- Match a PCI device through vendor, device, subvendor, and subdevice IDs; claim its BARs; and survive a forced detach without leaking resources.
- Register a top-half filter together with a bottom-half task or ithread via `bus_setup_intr`, in the order the kernel requires, and tear them down in reverse order under detach.
- Set up MSI or MSI-X vectors with a graceful fallback ladder from MSI-X to MSI to legacy INTx, and bind vectors to specific CPUs when the workload calls for it.
- Allocate, map, sync, and release DMA buffers using `bus_dma(9)` including the bounce-buffer case.
- Implement `device_suspend` and `device_resume` with register save and restore, I/O quiescing, and a post-resume self-test.

If any of those still requires a lookup, the labs to revisit are:

- Registers and barriers: Lab 1 (Observe the Register Dance) and Lab 8 (The Watchdog-Meets-Register Scenario) in Chapter 16.
- Simulated hardware under load: Lab 6 (Inject Stuck-Busy and Watch the Driver Wait) and Lab 10 (Inject a Memory-Corruption Attack) in Chapter 17.
- PCI attach and detach: Lab 4 (Claim the BAR and Read a Register) and Lab 5 (Exercise the cdev and Verify Detach Cleanup) in Chapter 18.
- Interrupt handling: Lab 3 (Stage 2, Real Filter and Deferred Task) in Chapter 19.
- MSI and MSI-X: Lab 4 (Stage 3, MSI-X With CPU Binding) in Chapter 20.
- DMA: Lab 4 (Stage 3, Interrupt-Driven Completion) and Lab 5 (Stage 4, Refactor and Regression) in Chapter 21.
- Power management: Lab 2 (One-Hundred-Cycle Stress) and Lab 3 (Transfer Across a Cycle) in Chapter 22.

Part 5 will expect the following as a baseline:

- A hardware-capable driver with observability already baked in: counters, sysctls, and `devctl_notify` calls at the important transitions. Chapter 23's debugging machinery works best when the driver already reports on itself.
- A regression script that can cycle the driver reliably, since Part 5 turns reproducibility into a first-class skill.
- A kernel built with `INVARIANTS` and `WITNESS`. Part 5 leans on both even more heavily than Part 4, especially in Chapter 23.
- The understanding that a bug in driver code is a bug in kernel code, which means user-space debuggers alone will not be enough and Part 5 will teach the kernel-space tools.

If those hold, Part 5 is ready for you. If one still looks shaky, a short lap through the relevant lab will pay back its time several times over.

## Bridge to Chapter 23

Chapter 23 is titled *Debugging and Tracing*. Its scope is the professional practice of finding bugs in drivers: tools like `ktrace`, `ddb`, `kgdb`, `dtrace`, and `procstat`; techniques for analysing panics, deadlocks, and data corruption; strategies for turning vague user reports into reproducible test cases; and the mindset of a driver developer who has to debug code running in kernel space with limited visibility.

Chapter 22 prepared the ground in four specific ways.

First, **you have observability counters everywhere**. The Chapter 22 driver exposes suspend, resume, shutdown, and runtime-PM counters through sysctls. Chapter 23's debugging techniques rely on observability; a driver that already tracks its own state is much easier to debug than one that does not.

Second, **you have a regression test**. The cycle and stress scripts from Section 6 are a first taste of what Chapter 23 expands: the ability to reproduce a bug on demand. A bug you cannot reproduce is a bug you cannot fix; Chapter 22's scripts are a foundation for the heavier testing Chapter 23 adds.

Third, **you have a working INVARIANTS / WITNESS debug kernel**. Chapter 22 leaned on both throughout; Chapter 23 builds on the same kernel for `ddb` sessions, post-mortem analysis, and kernel-crash reproduction.

Fourth, **you understand that bugs in driver code are bugs in kernel code**. Chapter 22 ran into hangs, frozen devices, lost interrupts, and WITNESS complaints. Each of those is a kernel bug in the user-visible sense; each requires a kernel-space debugging approach. Chapter 23 teaches that approach systematically.

Specific topics Chapter 23 will cover:

- Using `ktrace` and `kdump` to observe a process's system call trace in real time.
- Using `ddb` to break into the kernel debugger for post-mortem analysis or live inspection.
- Using `kgdb` with a core dump to recover the state of a crashed kernel.
- Using `dtrace` for in-kernel tracing without modifying the source.
- Using `procstat`, `top`, `pmcstat`, and related tools for performance observation.
- Strategies for minimising a bug: shrinking a reproducer, bisecting a regression, hypothesising and testing.
- Patterns for instrumenting a driver in production without disturbing behaviour.

You do not need to read ahead. Chapter 22 is sufficient preparation. Bring your `myfirst` driver at `1.5-power`, your `LOCKING.md`, your `INTERRUPTS.md`, your `MSIX.md`, your `DMA.md`, your `POWER.md`, your `WITNESS`-enabled kernel, and your regression script. Chapter 23 starts where Chapter 22 ended.

Part 4 is complete. Chapter 23 opens Part 5 by adding the observability and debugging discipline that separates a driver you wrote last week from a driver you can maintain for years.

The vocabulary is yours; the structure is yours; the discipline is yours. Chapter 23 adds the next missing piece: the ability to find and fix bugs that only show up in production.



## Reference: Chapter 22 Quick-Reference Card

A compact summary of the vocabulary, APIs, flags, and procedures Chapter 22 introduced.

### Vocabulary

- **Suspend:** a transition from D0 (full operation) to a lower-power state from which the device can be brought back.
- **Resume:** the transition back from the lower-power state to D0.
- **Shutdown:** the transition to a final state from which the device will not return.
- **Quiesce:** to bring a device to a state with no activity and no pending work.
- **System sleep state (S0, S1, S3, S4, S5):** ACPI-defined levels of system power.
- **Device power state (D0, D1, D2, D3hot, D3cold):** PCI-defined levels of device power.
- **Link state (L0, L0s, L1, L1.1, L1.2, L2):** PCIe-defined levels of link power.
- **ASPM (Active-State Power Management):** automatic transitions between L0 and L0s/L1.
- **PME# (Power Management Event):** a signal a device asserts when it wants to wake the system.
- **Wake source:** a mechanism by which a suspended device can request wakeup.
- **Runtime PM:** device-level power saving while the system stays in S0.

### Essential Kobj Methods

- `DEVMETHOD(device_suspend, foo_suspend)`: called to quiesce the device before a power transition.
- `DEVMETHOD(device_resume, foo_resume)`: called to restore the device after the power transition.
- `DEVMETHOD(device_shutdown, foo_shutdown)`: called to leave the device in a safe state for reboot.
- `DEVMETHOD(device_quiesce, foo_quiesce)`: called to stop activity without tearing down resources.

### Essential PCI APIs

- `pci_has_pm(dev)`: true if the device has a power-management capability.
- `pci_set_powerstate(dev, state)`: transition to `PCI_POWERSTATE_D0`, `D1`, `D2`, or `D3`.
- `pci_get_powerstate(dev)`: current power state.
- `pci_save_state(dev)`: cache the configuration space.
- `pci_restore_state(dev)`: write the cached configuration space back.
- `pci_enable_pme(dev)`: enable PME# generation.
- `pci_clear_pme(dev)`: clear pending PME status.
- `pci_enable_busmaster(dev)`: re-enable bus-master after a reset.

### Essential Bus Helpers

- `bus_generic_suspend(dev)`: suspend all children in reverse order.
- `bus_generic_resume(dev)`: resume all children in forward order.
- `device_quiesce(dev)`: call the driver's `DEVICE_QUIESCE`.

### Essential Sysctls

- `hw.acpi.supported_sleep_state`: list of S-states the platform supports.
- `hw.acpi.suspend_state`: default S-state for `zzz`.
- `hw.pci.do_power_suspend`: automatic D0->D3 transition on suspend.
- `hw.pci.do_power_resume`: automatic D3->D0 transition on resume.
- `dev.N.M.suspended`: driver's own suspended flag.
- `dev.N.M.power_suspend_count`, `power_resume_count`, `power_shutdown_count`.
- `dev.N.M.runtime_state`, `runtime_suspend_count`, `runtime_resume_count`.

### Useful Commands

- `acpiconf -s 3`: enter S3.
- `zzz`: wrapper around `acpiconf`.
- `devctl suspend <device>`: per-device suspend.
- `devctl resume <device>`: per-device resume.
- `devinfo -v`: device tree with state.
- `pciconf -lvbc`: PCI devices with power state.
- `sysctl -a | grep acpi`: all ACPI-related variables.

### Common Procedures

**Method table addition:**

```c
DEVMETHOD(device_suspend,  foo_suspend),
DEVMETHOD(device_resume,   foo_resume),
DEVMETHOD(device_shutdown, foo_shutdown),
```

**Suspend skeleton:**

```c
int foo_suspend(device_t dev) {
    struct foo_softc *sc = device_get_softc(dev);
    FOO_LOCK(sc);
    sc->suspended = true;
    FOO_UNLOCK(sc);
    foo_mask_interrupts(sc);
    foo_drain_dma(sc);
    foo_drain_workers(sc);
    return (0);
}
```

**Resume skeleton:**

```c
int foo_resume(device_t dev) {
    struct foo_softc *sc = device_get_softc(dev);
    pci_enable_busmaster(dev);
    foo_restore_registers(sc);
    FOO_LOCK(sc);
    sc->suspended = false;
    FOO_UNLOCK(sc);
    foo_unmask_interrupts(sc);
    return (0);
}
```

**Runtime-PM helper:**

```c
int foo_runtime_suspend(struct foo_softc *sc) {
    foo_quiesce(sc);
    pci_save_state(sc->dev);
    return (pci_set_powerstate(sc->dev, PCI_POWERSTATE_D3));
}

int foo_runtime_resume(struct foo_softc *sc) {
    pci_set_powerstate(sc->dev, PCI_POWERSTATE_D0);
    pci_restore_state(sc->dev);
    return (foo_restore(sc));
}
```

### Files to Keep Bookmarked

- `/usr/src/sys/kern/device_if.m`: the kobj method definitions.
- `/usr/src/sys/kern/subr_bus.c`: `bus_generic_suspend`, `bus_generic_resume`, `device_quiesce`.
- `/usr/src/sys/dev/pci/pci.c`: `pci_suspend_child`, `pci_resume_child`, `pci_save_state`, `pci_restore_state`.
- `/usr/src/sys/dev/pci/pcivar.h`: `PCI_POWERSTATE_*` constants and inline API.
- `/usr/src/sys/dev/re/if_re.c`: production reference for suspend/resume with WoL.
- `/usr/src/sys/dev/xl/if_xl.c`: minimal suspend/resume pattern.
- `/usr/src/sys/dev/virtio/block/virtio_blk.c`: virtio-style quiesce.



## Reference: Glossary of Chapter 22 Terms

A short glossary of the chapter's new terms.

- **ACPI (Advanced Configuration and Power Interface):** the industry-standard interface between OS and platform firmware for power management.
- **ASPM (Active-State Power Management):** automatic PCIe link-state transitions.
- **D-state:** a device power state (D0 through D3cold).
- **DEVICE_QUIESCE:** the kobj method that stops activity without tearing down resources.
- **DEVICE_RESUME:** the kobj method called to restore a device to operation.
- **DEVICE_SHUTDOWN:** the kobj method called at system shutdown.
- **DEVICE_SUSPEND:** the kobj method called to quiesce a device before a power transition.
- **GPE (General-Purpose Event):** an ACPI wake event source.
- **L-state:** a PCIe link power state.
- **Link state machine:** the automatic transitions between L0 and L0s/L1.
- **PME# (Power Management Event):** the PCI signal a device asserts to request wake.
- **Power management capability:** the PCI capability structure that contains PM registers.
- **Quiesce:** to bring a device to a state with no activity and no pending work.
- **Runtime PM:** device-level power saving while the system stays in S0.
- **S-state:** an ACPI system sleep state (S0 through S5).
- **Shutdown:** final power-down, typically leading to reboot or power-off.
- **Sleep state:** see S-state.
- **Suspend:** temporary power-down from which the system or device can return.
- **Suspended flag:** a driver-local flag indicating the device is in a suspended state.
- **Wake source:** a mechanism by which a suspended system or device can be woken.
- **WoL (Wake on LAN):** a wake source triggered by a network packet.



## Reference: A Closing Note on Power-Management Philosophy

A paragraph to close the chapter with.

Power management is the discipline that separates a driver prototype from a production driver. Before power management, a driver assumes its device is always on and always available. After power management, the driver knows the device can be put to sleep and knows how to put it to sleep correctly, and the driver can be trusted in the kinds of environments real users run: laptops that close and open dozens of times a day, servers that suspend idle devices to save power, VMs that migrate between hosts, embedded systems that turn off whole power domains to extend battery life.

Chapter 22's lesson is that power management is disciplined, not magical. The FreeBSD kernel gives the driver a specific contract (the four kobj methods, the order of invocation, the interaction with the PCI layer), and following the contract is most of the work. The rest is hardware-specific: understanding which registers the device loses across a D-state transition, which wake sources the hardware supports, which policy the driver should apply for runtime PM. The pattern is the same across every power-aware driver in FreeBSD; internalising it once pays off across dozens of later chapters and thousands of lines of real driver code.

For this reader and for this book's future readers, the Chapter 22 power-management pattern is a permanent part of the `myfirst` driver's architecture and a permanent tool in the reader's toolkit. Chapter 23 assumes it: debugging a driver assumes the driver has the observability counters and the structured lifecycle Chapter 22 introduced. Part 6's specialisation chapters assume it: every production-style driver has a power path. Part 7's performance chapters (Chapter 33) assume it: every tuning measurement has to account for power-state transitions. The vocabulary is the vocabulary every production FreeBSD driver shares; the patterns are the patterns production drivers live by; the discipline is the discipline that keeps power-aware platforms correct.

The skill Chapter 22 teaches is not "how to add suspend and resume methods to a single PCI driver". It is "how to think about a driver's lifecycle as attach, run, quiesce, sleep, wake, run, and eventually detach, rather than just attach, run, detach". That skill applies across every driver the reader will ever work on.

Part 4 is complete. The `myfirst` driver is at `1.5-power`, structurally parallel to a production FreeBSD driver, and ready for the debugging, tooling, and specialisation chapters that follow in Parts 5 and 6. Chapter 23 starts there.
