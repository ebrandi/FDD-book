---
title: "Advanced Topics and Practical Tips"
description: "Chapter 25 closes Part 5 by teaching the engineering habits that turn a working, integrated FreeBSD driver into a robust, maintainable piece of kernel software. It covers rate-limited kernel logging and logging etiquette; a disciplined vocabulary of errno values and return conventions for read, write, ioctl, sysctl, and lifecycle callbacks; driver configuration through /boot/loader.conf tunables and writable sysctls; versioning and compatibility strategies for ioctls, sysctls, and user-visible behaviour; resource management in failure paths using the labelled goto cleanup pattern; modularisation of the driver into logically separated source files; the discipline of preparing a driver for production use with MODULE_DEPEND, MODULE_PNP_INFO, and sensible packaging; and the SYSINIT / SYSUNINIT / EVENTHANDLER mechanisms that extend a driver's lifecycle beyond simple MOD_LOAD and MOD_UNLOAD. The myfirst driver grows from 1.7-integration to 1.8-maintenance: it gains myfirst_log.c and myfirst_log.h with a ppsratecheck-backed DLOG_RL macro, a split between myfirst_cdev.c and myfirst_bus.c so the cdev callbacks live separately from the Newbus attach machinery, a MAINTENANCE.md document, a shutdown_pre_sync event handler, a MYFIRSTIOC_GETCAPS ioctl that lets user space negotiate feature bits, and a version-bumped regression script. The chapter leaves Part 5 complete: the driver can still be understood, can still be tuned without rebuilding, and can now absorb the next year of maintenance without growing unreadable."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 25
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 225
---

# Advanced Topics and Practical Tips

## Reader Guidance & Outcomes

Chapter 24 closed with a driver that the rest of the system can talk to. The `myfirst` driver at version `1.7-integration` has a clean `/dev/myfirst0` node created through `make_dev_s`, a public ioctl header shared between the kernel and user space, a per-instance sysctl subtree under `dev.myfirst.0`, a boot-time tunable for the debug mask, and an ioctl dispatcher that obeys the `ENOIOCTL` fallback rule so that kernel helpers like `FIONBIO` still reach the cdev layer correctly. The driver compiles, loads, runs under stress, and survives repeated `kldload` and `kldunload` cycles without leaking OIDs or cdev nodes. In every observable sense, the driver works.

Chapter 25 is about the difference between a driver that *works* and a driver that is *maintainable*. Those two qualities are not the same, and the difference shows up slowly. A working driver passes its first round of testing, attaches cleanly to its hardware, and goes into use. A maintainable driver also does that, and then absorbs the next year of bug fixes, feature additions, portability changes, new hardware revisions, and kernel API churn without slowly collapsing under its own weight. The first gives a developer a good day. The second gives the driver a good decade.

Chapter 25 is the closing chapter of Part 5. Where Chapter 23 taught observability and Chapter 24 taught integration, Chapter 25 teaches the engineering habits that preserve both of those qualities over time. Part 6 begins immediately after with transport-specific drivers (USB in Chapter 26, storage in Chapter 27, networking in Chapter 28 and beyond), and each of those chapters will assume the discipline introduced here. Without rate-limited logging, a USB hotplug storm fills the message buffer. Without a consistent error convention, a storage driver and its peripheral in CAM disagree about what `EBUSY` means. Without loader tunables, a network driver with a suboptimal default queue depth cannot be tuned on a production box without a rebuild. Without versioning discipline, a user-space tool written for this version of the driver silently misinterprets a new field added two months later. Each habit is small. Together, they are what make a driver a long-lived piece of FreeBSD rather than a short-lived lab experiment.

The chapter's running example remains the `myfirst` driver. At the start of the chapter it is at version `1.7-integration`. By the end of the chapter it is at version `1.8-maintenance`, split into more files than before, logging without flooding the message buffer, returning errors from a consistent vocabulary, configurable from `/boot/loader.conf`, shipping with a `MAINTENANCE.md` document that explains the ongoing maintenance contract, announcing its events through the `devctl` channel, and hooked into the kernel's shutdown and low-memory events through `EVENTHANDLER(9)`. None of those additions require new hardware knowledge. All of them require sharper discipline.

Part 5 closes here with the habits that keep the driver coherent as it grows. Chapter 22 made the driver survive a change of power state. Chapter 23 made the driver tell you what it is doing. Chapter 24 made the driver fit into the rest of the system. Chapter 25 makes the driver keep all of those qualities as it evolves. Chapter 26 will then open Part 6 by putting these qualities to work against a real transport, the Universal Serial Bus, where every shortcut in logging or in failure-path handling is exposed by the speed and variety of USB traffic.

### Why Maintenance Discipline Earns a Chapter of Its Own

Before we go further, it is worth pausing on whether rate-limited logging, errno vocabulary, and loader tunables really deserve a full chapter. The earlier chapters have already taught so much. Adding a `ppsratecheck(9)`-backed log macro looks small. Standardising error codes looks even smaller. Why spread the work across a long chapter when each habit looks like a handful of lines?

The answer is that each habit is small, and the absence of each habit is large. A driver that logs without rate limiting is fine in the lab and catastrophic in production the first time a flaky cable triggers ten thousand re-enumerations per second. A driver that returns `EINVAL` when it should return `ENXIO`, and `ENXIO` when it should return `ENOIOCTL`, is fine when the author is the only caller and a bug report waiting to happen when a second developer writes the first user-space helper. A driver that lets every configuration default be a compile-time constant is fine for one person and unworkable for a team maintaining the same module across several production boxes with different workloads. Chapter 25 spends time on each of these habits because the value is measured not in the lab but in the two-year maintenance cost that each habit reduces.

The first reason the chapter earns its place is that **these habits shape what the driver's code base looks like as it grows**. A reader who has followed Chapters 23 and 24 has already seen the driver split into multiple files: `myfirst.c`, `myfirst_debug.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`. That was modularisation done in the small, one surface at a time. Chapter 25 revisits modularisation with the question most readers have not yet asked: *what does a maintainable source layout look like once the driver has a dozen files and three developers?* The chapter answers that question with a conscious split between the Newbus attach layer, the cdev layer, the ioctl layer, the sysctl layer, and the logging layer, and then uses that split to support every other habit the chapter teaches.

The second reason is that **these habits determine whether the driver can be debugged in production**. A driver that logs judiciously and returns informative errors gives an operator enough information to file a useful bug report. A driver that logs too much or too little, or that invents its own errno conventions, forces the operator to reason from symptoms alone, and the developer ends up chasing intermittent problems blind. Chapter 23's debugging toolkit is effective, but it depends on the driver cooperating. The cooperation is built here.

The third reason is that **these habits make the driver extensible without breaking its callers**. The `myfirst_ioctl.h` header from Chapter 24 is already a contract between the driver and user space. Chapter 25 teaches the reader how to evolve that contract, add a new ioctl that older user-space programs can ignore safely, retire a deprecated sysctl without breaking administrators' scripts, and bump the driver's version in a way that external consumers can check at run time. Without those habits, the first v2 of the driver forces every caller to be rewritten. With them, the driver can add features for a decade and still run the user-space helpers that were compiled the first week the driver shipped.

Chapter 25 earns its place by teaching those three ideas together, concretely, with the `myfirst` driver as the running example. A reader who finishes Chapter 25 can make any FreeBSD driver ready for long-term maintenance, can read another driver's production-hardening patterns and recognise which are principled and which are ad hoc, can negotiate compatibility with existing user-space tools, and has a `myfirst` driver at version `1.8-maintenance` that is visibly ready to begin Part 6.

### Where Chapter 24 Left the Driver

A brief recap of where you should stand. Chapter 25 extends the driver produced at the end of Chapter 24 Stage 3, tagged as version `1.7-integration`. If any of the items below is uncertain, return to Chapter 24 and fix it before starting this chapter, because the new material assumes every Chapter 24 primitive is working.

- Your driver compiles cleanly and identifies itself as `1.7-integration` in the output of `kldstat -v`.
- A `/dev/myfirst0` node exists after `kldload`, has ownership `root:wheel` and mode `0660`, and disappears cleanly on `kldunload`.
- The module exports four ioctls: `MYFIRSTIOC_GETVER`, `MYFIRSTIOC_GETMSG`, `MYFIRSTIOC_SETMSG`, and `MYFIRSTIOC_RESET`. The small `myfirstctl` user-space program from Chapter 24 exercises each one and returns success on all four.
- The sysctl subtree `dev.myfirst.0` lists at least `version`, `open_count`, `total_reads`, `total_writes`, `message`, `message_len`, `debug.mask`, and `debug.classes`.
- `sysctl dev.myfirst.0.debug.mask=0xff` enables every debug class, and the driver's subsequent log output shows the expected tags.
- The boot-time tunable `hw.myfirst.debug_mask_default`, placed in `/boot/loader.conf`, applies before attach and sets the initial value of the sysctl.
- Repeated `kldload` and `kldunload` in a loop for a minute leave no residual OIDs, no orphan cdev, and no leaked memory as reported by `vmstat -m | grep myfirst`.
- Your working tree contains `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, `POWER.md`, `DEBUG.md`, and `INTEGRATION.md` from the earlier chapters.
- Your test kernel has `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS`, and `DDB_CTF` enabled. Chapter 25's labs rely on `WITNESS` and on `INVARIANTS` just as strongly as Chapter 24's did.

That driver is what Chapter 25 extends. The additions are smaller in code lines than in any previous Part 5 chapter, but larger in conceptual surface. The new pieces are: a `myfirst_log.c` and `myfirst_log.h` pair built around `ppsratecheck(9)`, a labelled-goto cleanup chain in `myfirst_attach`, a refined error vocabulary throughout the dispatcher, a pair of `SYSINIT`/`SYSUNINIT` hooks for driver-wide initialisation, a `shutdown_pre_sync` event handler, a new `MYFIRSTIOC_GETCAPS` ioctl that lets user space query feature bits, a modest refactor that splits the Newbus attach out of `myfirst.c` into `myfirst_bus.c` and the cdev callbacks out into `myfirst_cdev.c`, a `MAINTENANCE.md` document that explains the version-bumping policy, an updated regression script, and a version bump to `1.8-maintenance`.

### What You Will Learn

By the end of this chapter you will be able to:

- Explain why unrestricted kernel logging is a production hazard, describe how `ppsratecheck(9)` limits events per second, write a rate-limited logging macro that cooperates with the debug mask from Chapter 23, and recognise the three classes of log messages that deserve different throttling strategies.
- Audit a driver's `read`, `write`, `ioctl`, `open`, `close`, `attach`, `detach`, and sysctl-handler paths for correct errno usage. Distinguish `EINVAL` from `ENXIO`, `ENOIOCTL` from `ENOTTY`, `EBUSY` from `EAGAIN`, `EPERM` from `EACCES`, and `EIO` from `EFAULT`, and know when each is the right return.
- Add loader tunables through `TUNABLE_INT_FETCH`, `TUNABLE_LONG_FETCH`, `TUNABLE_BOOL_FETCH`, and `TUNABLE_STR_FETCH`, and combine them with writable sysctls so one knob can be set at boot or adjusted at runtime. Understand how `CTLFLAG_TUN` cooperates with the tunable fetchers.
- Expose configuration as a small, well-documented surface rather than as a pile of ad hoc environment variables. Choose between per-driver and per-instance tunables with discipline. Document every tunable's unit, range, and default.
- Version a driver's user-visible interface with a stable three-way split: the human-readable release string in `dev.myfirst.0.version`, the integer `MODULE_VERSION` used by the kernel's module dependency machinery, and the wire-format integer `MYFIRST_IOCTL_VERSION` baked into the public header.
- Add a new ioctl to an existing public header without breaking older callers, retire a deprecated ioctl with a correct deprecation period, and provide a capability bitmask through `MYFIRSTIOC_GETCAPS` so user-space programs can detect feature availability without trial and error.
- Structure a driver's attach and detach paths with the `goto fail;` pattern so that every allocation has exactly one cleanup site, every cleanup runs in reverse order of the allocation, and a partial attach never leaves behind a resource the detach path will not free.
- Split a driver into logical source files along the lines of responsibility rather than along the lines of file size. Choose between a single large file, a small collection of topic-focused files, and a full subsystem tree, and know when each is appropriate.
- Prepare a driver for production use with `MODULE_DEPEND`, `MODULE_PNP_INFO`, a well-behaved `modevent` handler that accepts `MOD_QUIESCE` when the driver can cleanly pause, a small build system that ships both the module and its documentation, and a `devd(8)`-ready pattern for announcing driver events through `devctl_notify`.
- Use `SYSINIT(9)` and `SYSUNINIT(9)` to hook driver-wide setup and teardown at specific kernel subsystem stages, and understand the difference between module event handlers and subsystem-level init hooks.
- Register and deregister callbacks on well-known kernel events through `EVENTHANDLER(9)`: `shutdown_pre_sync`, `shutdown_post_sync`, `shutdown_final`, `vm_lowmem`, `power_suspend_early`, and `power_resume`. Know how to choose a priority and how to guarantee deregistration on detach.

The list is long because maintenance discipline touches many small surfaces at once. Each item is narrow and teachable. The chapter's work is making them a habit.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 25 stays focused on maintenance discipline at the right level for a reader finishing Part 5.

- **Transport-specific production patterns** such as USB hotplug storms, SATA link-state events, and Ethernet media-change handling belong in Part 6 where each transport is taught in full. Chapter 25 teaches the *general* habits; Chapter 26 and later apply them to USB specifically.
- **Full test-framework design**, including regression harnesses that run across several kernel configurations and fault-injection scenarios, belongs in the hardware-free testing sections of Chapters 26, 27, and 28. Chapter 25 adds one more line to the existing regression script; it does not introduce a whole harness.
- **`fail(9)` and `fail_point(9)`**, the kernel's error-injection facilities, are deferred to Chapter 28 alongside the storage driver work where they are most frequently used.
- **Continuous integration, package signing, and distribution** are operational concerns for the project shipping the driver rather than for the driver's source code. The chapter says just enough about packaging to make the driver reproducible.
- **`MAC(9)` (Mandatory Access Control) hooks** are a specialised framework and are best introduced in a later security-focused chapter.
- **`kbi(9)` stability and ABI freezing** are release-engineering decisions made by the FreeBSD project, not by the driver author. The chapter notes the ABI implications of kernel-exported functions but does not cover release engineering in depth.
- **`capsicum(4)`** capability-mode integration for user-space helpers is a topic for user-space security, not for the driver itself. The chapter's `myfirstctl` remains a traditional UNIX tool.
- **Advanced concurrency patterns** such as `epoch(9)`, read-mostly locks, and lock-free queues. These are mentioned only in passing; the driver's single softc mutex continues to be sufficient at this stage.

Staying inside those lines keeps Chapter 25 a chapter about *maintenance discipline*, not a chapter about every technique a senior kernel developer might use on a senior kernel problem.

### Estimated Time Investment

- **Reading only**: three to four hours. Chapter 25's ideas are conceptually lighter than Chapter 24's, and much of the vocabulary is by now familiar. The chapter's job is to turn familiar primitives into discipline.
- **Reading plus typing the worked examples**: eight to ten hours over two or three sessions. The driver evolves through four short stages (rate-limited logging, error audit, tunable and version discipline, SYSINIT and EVENTHANDLER), each smaller than a single Chapter 24 stage. The refactor in Section 6 touches several files but changes little code; most of the work is moving existing code into its new home.
- **Reading plus all labs and challenges**: twelve to fifteen hours over three or four sessions. The labs include a log-flood reproduction and repair, an errno audit with `truss`, a tunable lab that boots a VM twice with different `/boot/loader.conf` values, a deliberate attach-failure lab that exercises every label in the `goto fail;` chain, a `shutdown_pre_sync` lab that confirms the callback really runs at the right moment, and a regression-script walkthrough that ties everything together.

Section 5 (failure-path management) is the densest in new discipline rather than new vocabulary. The `goto fail;` pattern itself is mechanical; the trick is reading a real FreeBSD attach function and seeing every allocation as a candidate for a new label. If the pattern feels mechanical on the first pass, that is the signal that it has become a habit.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 24 Stage 3 (`1.7-integration`). Every Chapter 24 primitive is assumed: the `make_dev_s`-based cdev creation, the `myfirst_ioctl.c` dispatcher, the `myfirst_sysctl.c` tree construction, the `MYFIRST_VERSION`, `MODULE_VERSION`, and `MYFIRST_IOCTL_VERSION` triple, and the per-device `sysctl_ctx_free`-free pattern.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS`, and `DDB_CTF` is built, installed, and booting cleanly.
- A snapshot of the `1.7-integration` state is saved in your VM. Chapter 25's labs include intentional attach-failure scenarios, and a snapshot makes recovery cheap.
- The following user-space commands are in your path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `devd`, `cc`, `make`, `dtrace`, `truss`, `ktrace`, `kdump`, and `procstat`.
- You are comfortable editing `/boot/loader.conf` and rebooting a VM to pick up new tunables.
- You have the `myfirstctl` companion program from Chapter 24 built and working.

If any item above is shaky, fix it now. Maintenance discipline is easier to learn on a driver that already obeys the earlier chapters' rules than on one that still has unresolved issues from earlier stages.

### How to Get the Most Out of This Chapter

Five habits pay off in this chapter more than in any of the previous Part 5 chapters.

First, keep four short manual-page files open in a browser tab or a terminal pane: `ppsratecheck(9)`, `style(9)`, `sysctl(9)`, and `module(9)`. The first is the canonical documentation of the rate-check API. The second is FreeBSD's coding style. The third explains the sysctl framework. The fourth is the module event handler contract. None of them is long; each is worth skimming once at the start of the chapter and referring back to when the prose says "consult the manual page for details."

Second, keep three real drivers close to hand. `/usr/src/sys/dev/mmc/mmcsd.c` shows `ppsratecheck` used to throttle a device-printf in production. `/usr/src/sys/dev/virtio/block/virtio_blk.c` shows a clean `goto fail;` chain in its attach path and a production-quality set of tunables. `/usr/src/sys/dev/e1000/em_txrx.c` shows how a complex driver splits logging, tunables, and dispatch across several files. Chapter 25 points back to each at the right moment; reading them once now gives the rest of the chapter concrete anchors.

> **A note on line numbers.** When the chapter points at a particular place in `mmcsd.c`, `virtio_blk.c`, or `em_txrx.c`, the pointer is a named symbol, not a numeric line. `ppsratecheck`, the `goto fail;` labels in `virtio_blk_attach`, and the `TUNABLE_*_FETCH` calls stay findable under those names in future tree revisions even as the lines around them move. The audit examples you will see later in the chapter use `file:line` notation purely as a sample tool transcript and carry the same health warning.

Third, type every change into the `myfirst` driver by hand. The additions in Chapter 25 are the kind of changes a developer makes by reflex after a year of maintenance work. Typing them out now builds the reflex; pasting them skips the lesson.

Fourth, after the tunable material in Section 3, reboot your VM at least once with a new `/boot/loader.conf` setting and watch the driver pick it up during attach. Tunables are one of those features that feels abstract until you see a real value flow from the bootloader through the kernel into your softc. Two reboots and one `sysctl` command is all it takes.

Fifth, when the section on `goto fail;` asks you to introduce a deliberate failure into `myfirst_attach`, actually do it. Injecting a single `return (ENOMEM);` into the middle of attach and watching the cleanup chain unwind correctly is the single best way to internalise the pattern. The chapter suggests a specific place to inject, and the regression script confirms the cleanup really ran.

### Roadmap Through the Chapter

The sections in order are:

1. **Rate Limiting and Logging Etiquette.** Why uncontrolled kernel logging is a production hazard; the three classes of driver log messages (lifecycle, error, debug); `ppsratecheck(9)` and `ratecheck(9)` as the FreeBSD answer to log-flood; a rate-limited `DLOG_RL` macro that cooperates with the Chapter 23 debug mask; `log(9)` priority levels and their relationship to `device_printf` and `printf`; what the kernel message buffer actually costs and how not to spend it carelessly.
2. **Error Reporting and Return Conventions.** Why errno discipline is a contract with every caller; the small vocabulary of kernel errnos a driver routinely uses; when each is appropriate; `ENOIOCTL` versus `ENOTTY` and why the driver must never return `EINVAL` from the ioctl default; sysctl handler return codes; module event handler return codes; a checklist the reader can apply to every driver they write from now on.
3. **Driver Configuration via Loader Tunables and sysctl.** The difference between `/boot/loader.conf` tunables and runtime sysctls; the `TUNABLE_*_FETCH` family and the `CTLFLAG_TUN` flag; per-driver versus per-instance tunables; how to document a tunable so operators can trust it; a worked lab that boots a VM with a tunable in three different positions and observes the effect in the driver's sysctl tree.
4. **Versioning and Compatibility Strategies.** The three-way version split (`MODULE_VERSION` integer, `MYFIRST_VERSION` human string, `MYFIRST_IOCTL_VERSION` wire-format integer); how each one is used; how to add a new ioctl without breaking older callers; how to retire a deprecated ioctl; `MYFIRSTIOC_GETCAPS` and the capability-bitmask idea; how a driver can deprecate a sysctl OID gracefully in the absence of a dedicated kernel flag; how `MODULE_DEPEND` enforces a minimum version of a dependency module.
5. **Managing Resources in Failure Paths.** The cleanup-on-failure problem in `myfirst_attach`; the `goto fail;` pattern and why linear unwinding beats nested `if` chains; label naming conventions (`fail_mtx`, `fail_cdev`, `fail_sysctl`); common mistakes (falling through after success, missing a label, adding a resource without adding its cleanup); a small helper-function discipline that reduces duplication; a deliberate-failure lab that tests the whole chain.
6. **Modularisation and Separation of Concerns.** Splitting a driver into files along the axes of responsibility; the canonical split for a character driver (`myfirst.c`, `myfirst_bus.c`, `myfirst_cdev.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`, `myfirst_debug.c`, `myfirst_log.c`); public versus private headers; how to organise the `Makefile` so all of these files build into one `.ko`; when modularisation helps and when it gets in the way; how a team of developers uses the split to reduce merge conflict.
7. **Preparing for Production Use.** `MODULE_DEPEND` and dependency enforcement; `MODULE_PNP_INFO` for auto-loading; `MOD_QUIESCE` and the pause-before-unload contract; a build-system pattern that installs the module and its documentation; a `devd(8)` rule that reacts to the driver's events; a small `MAINTENANCE.md` document that states the driver's maintenance contract in writing.
8. **SYSINIT, SYSUNINIT, and EVENTHANDLER.** The kernel's broader lifecycle machinery beyond `MOD_LOAD` and `MOD_UNLOAD`; `SYSINIT(9)` and `SYSUNINIT(9)` with subsystem IDs and order constants; real FreeBSD examples of each; `EVENTHANDLER(9)` for cross-cutting notifications (`shutdown_pre_sync`, `vm_lowmem`, `power_resume`); how to register and deregister cleanly; how a driver uses all three mechanisms without drifting into overengineering.

After the eight sections come a set of hands-on labs that exercise each discipline, a set of challenge exercises that stretch the reader without introducing new foundations, a troubleshooting reference for the symptoms most readers will hit, a Wrapping Up that closes Chapter 25's story and opens Chapter 26's, a bridge to the next chapter, a quick-reference card, and a glossary.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 1 and 5 stand alone and make good single-sitting reads. Section 8 is a short conceptual payoff at the end of the chapter; it depends lightly on Section 7's production-use material and is easy to save for a second sitting.

A small note before the technical work begins. Chapter 25 is the last chapter of Part 5. Its additions are smaller than Chapter 24's, but they touch almost every file in the driver. Expect to spend more time re-reading your own earlier code than writing new code. That, too, is maintenance discipline. A driver you re-read patiently is a driver you can change confidently; a driver you change confidently is a driver you can keep alive.

## Section 1: Rate Limiting and Logging Etiquette

The first discipline this chapter teaches is the discipline of not talking too much. The `myfirst` driver at the end of Chapter 24 logs when it attaches, when it detaches, when a client opens or closes the device, when a read or write crosses the boundary, when an ioctl is dispatched, and when the debug mask is adjusted. Every one of those log lines was introduced for a good reason, and every one of them is useful when a single event happens. What none of the Chapter 24 log lines accounts for is what happens when the same event fires a hundred thousand times a second.

This section explains why that question matters more than it looks, introduces the three categories of driver log message that behave differently under pressure, teaches the FreeBSD rate-check primitives (`ratecheck(9)` and `ppsratecheck(9)`), and shows how to build a small, disciplined macro on top of them that cooperates with the existing debug-mask machinery from Chapter 23. By the end of the section the `myfirst` driver has a `myfirst_log.c` and `myfirst_log.h` pair, and its message buffer no longer turns into noise under stress.

### The Problem With Unrestricted Logging

A kernel message is cheap to write and expensive to carry. `device_printf(dev, "something happened\n")` is a single function call, tens of nanoseconds on a modern CPU, and it returns almost immediately. The cost is not in the call; the cost is in everything that happens to the bytes afterwards. The formatted string is copied into the kernel message buffer, a circular area of kernel memory whose size is fixed at boot. It is delivered to the console device if the console is attached (often a serial port in a VM, with a finite bit rate). It is sent to the syslog daemon running in user space through the `log(9)` path if the driver uses that path, and then through `newsyslog(8)` into `/var/log/messages` on disk. Each of those steps has a cost, and every one of them is synchronous at the moment the driver writes the line.

When the driver writes one line, none of this matters. When the driver writes a million lines in a second, all of it matters. The kernel message buffer fills, and the oldest messages are overwritten before anyone reads them. The console, typically running at 115200 baud, falls behind and cannot catch up, which in turn backs pressure into the kernel path that wrote the line, which is your driver's fast path. The syslog daemon wakes up, does work, and goes back to sleep many times per second, stealing cycles from other processes. The disk where `/var/log/messages` lives fills at a predictable rate, and a driver that logs at ten thousand lines per second can fill a reasonably sized partition in an afternoon.

None of these symptoms is caused by a bug in the driver's logic. They are caused by the driver's *logging volume*, which in turn is caused by the driver firing a reasonable log line on every event. Reasonable log lines are fine as long as events are rare. They become a hazard when events are common. The whole craft of logging etiquette is knowing, at the moment you write a log line, whether the event behind it is rare or common, and writing the code so that uncontrolled repetition cannot turn a rare-event log into a common-event log.

A concrete example from a real driver illustrates the point. Consider a PCIe SSD controller that notifies its driver of a recoverable queue full condition. On a healthy system that condition is rare enough that logging each occurrence is useful. On a sick system it may happen hundreds of times per second until someone replaces the hardware. If the driver writes a line every time, the message buffer fills with nearly identical lines, all the earlier messages from that boot are overwritten and lost, and the operator who tries to diagnose the problem by reading `dmesg` sees only the last page of the flood. The hardware's actual behaviour is obscured by the driver's reaction to it. A rate-limited log line would have shown the first few occurrences, the rate, and then a periodic reminder; the earlier context in `dmesg` would have survived; the operator would have had something to work with.

The lesson generalises. The right log discipline is not "log less" and not "log more" but "log at a rate that remains useful regardless of how often the underlying event fires." The rest of this section teaches that discipline concretely.

### Three Categories of Driver Log Message

Before picking the right throttling policy, it helps to name the three categories of log message a driver typically emits. Each category has a different throttling story.

The first category is **lifecycle events**. These are the messages that mark attach, detach, suspend, resume, module load, and module unload. They occur once per lifecycle transition, typically a few times in the lifetime of a module. No throttling is needed; the volume is naturally low. Rate-limiting lifecycle messages would be a mistake because it would hide important state transitions.

The second category is **error and warning messages**. These are messages that report something the driver thinks is wrong. By construction each of these should be rare; if a warning is firing a hundred times a second, the warning is telling you something about the rate of underlying events, and that information is worth preserving even when the event repeats. Error and warning messages benefit strongly from rate limiting, but the rate limit should preserve at least one message per burst and should make the *rate* itself visible.

The third category is **debug and trace messages**. These are the messages under the `DPRINTF` macros from Chapter 23. They are intentionally verbose when the debug mask is on and silent when the mask is off. Throttling them at the emit site adds noise to what is already a low-signal path; the better discipline is to avoid emitting them when the mask is off, which is already what the existing `DPRINTF` does. Debug and trace messages do not need additional rate limiting, but they need the user to be able to turn them off entirely with a single `sysctl` command. The existing Chapter 23 plumbing already provides that.

With the three categories named, the rest of the section focuses on the second category. Lifecycle messages are fine as they are. Debug messages are handled by the existing mask. Error and warning messages are where the real discipline goes.

### Introducing `ratecheck` and `ppsratecheck`

FreeBSD's kernel provides two closely related primitives for rate-limited output. Both live in `/usr/src/sys/kern/kern_time.c` and are declared in `/usr/src/sys/sys/time.h`.

`ratecheck(struct timeval *lasttime, const struct timeval *mininterval)` is the simpler of the two. The caller holds a `struct timeval` remembering when the event last fired, along with a minimum interval between allowed prints. On each call, `ratecheck` compares the current time to `*lasttime`, and if `mininterval` has passed, it updates `*lasttime` and returns 1. Otherwise it returns 0. The calling code prints only when the return is 1. The result is a simple floor on the rate of prints: at most one print per `mininterval`.

`ppsratecheck(struct timeval *lasttime, int *curpps, int maxpps)` is the more commonly used form in drivers. Its name is a legacy of the pulse-per-second telemetry use case it was originally written for. The kernel source exposes it through a `#define` in `/usr/src/sys/sys/time.h`:

```c
int    eventratecheck(struct timeval *, int *, int);
#define ppsratecheck(t, c, m) eventratecheck(t, c, m)
```

The call accepts a pointer to a timestamp, a pointer to a counter of events in the current one-second window, and the maximum allowed events per second. On each call, if the second has not yet rolled over, the counter is incremented. If the counter exceeds `maxpps`, the function returns 0 and the caller suppresses its output. When a new second begins, the counter is reset to 1 and the function returns 1, allowing one print for the new second. A special value of `maxpps == -1` disables rate limiting entirely (useful for debug paths).

Both primitives are cheap: a comparison and an arithmetic update, no locks. Both are safe to call from any context where the driver currently calls `device_printf`, including interrupt handlers, as long as the storage they access is stable in that context. In practice drivers keep the `struct timeval` and the counter inside their softc, protected by the same lock that protects the log site, or they use per-CPU state where that is convenient.

A short example from the FreeBSD tree shows the pattern in real use. The MMC SD card driver, `/usr/src/sys/dev/mmc/mmcsd.c`, rate-limits a complaint about write errors so that a bad card does not flood the log:

```c
if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
        device_printf(dev, "Error indicated: %d %s\n",
            err, mmcsd_errmsg(err));
```

The driver stores `log_time` and `log_count` in its softc, picks a reasonable `LOG_PPS` (typically 5 to 10), and wraps the `device_printf` call in the rate check. The first few errors in any second produce log lines; the next several hundred in the same second produce nothing.

That is the whole idea. Everything that follows in this section is about doing the same thing with more structure, more discipline, and less repetition.

### A Simple Rate-Limited Log Macro

The goal is a macro the driver can use instead of bare `device_printf` in any error or warning path where the event might repeat. The macro should:

1. Silently discard output when the rate limit is exceeded.
2. Allow a different rate per call site, or at least per category.
3. Cooperate with the existing debug-mask machinery from Chapter 23 so that debug output remains controlled by the mask rather than by the rate limiter.
4. Compile out of non-debug builds if the driver chooses, with no runtime cost.

A minimal implementation looks like this. In a new `myfirst_log.h`:

```c
#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
        struct timeval rl_lasttime;
        int            rl_curpps;
};

/*
 * Default rate for warning messages: at most 10 per second per call
 * site.  Chosen to keep the log readable under a burst while still
 * showing the rate itself.
 */
#define MYF_RL_DEFAULT_PPS  10

/*
 * DLOG_RL - rate-limited device_printf.
 *
 * rlp must point at a per-call-site struct myfirst_ratelimit stored in
 * the driver (typically in the softc).  pps is the maximum allowed
 * prints per second.  The remaining arguments match device_printf.
 */
#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
        if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps))  \
                device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)

#endif /* _MYFIRST_LOG_H_ */
```

In the softc, reserve one or more rate-limit structures:

```c
struct myfirst_softc {
        /* ... existing fields ... */
        struct myfirst_ratelimit sc_rl_ioerr;
        struct myfirst_ratelimit sc_rl_short;
};
```

At each error site, replace the bare `device_printf` with `DLOG_RL`:

```c
/* Old:
 * device_printf(sc->sc_dev, "I/O error on read, ENXIO\n");
 */
DLOG_RL(sc, &sc->sc_rl_ioerr, MYF_RL_DEFAULT_PPS,
    "I/O error on read, ENXIO\n");
```

The macro uses a comma operator inside a `do { ... } while (0)` block so it fits anywhere a statement fits, including inside `if` and `else` bodies without braces. The `ppsratecheck` call is inexpensive; when the rate limit is exceeded, the `device_printf` is simply not called. When the rate limit is not exceeded, the behaviour is identical to a direct `device_printf`.

A small but important point: each call site should have its own `struct myfirst_ratelimit`. Sharing one structure across multiple unrelated call sites means the first path that fires in each second suppresses every other path for the rest of that second. In a driver with a handful of rare-but-possible errors, reserve one rate-limit structure per category, name it after the category, and use it consistently.

### Cooperating With the Chapter 23 Debug Mask

The rate-limited macro solves the error-and-warning case. The debug case already has its own mechanism from Chapter 23:

```c
DPRINTF(sc, MYF_DBG_IO, "read: %zu bytes requested\n", uio->uio_resid);
```

The `DPRINTF` macro expands to nothing when the corresponding bit in `sc_debug` is clear, so debug output at the quiet mask (`mask = 0`) has no runtime cost. There is no need to rate-limit debug output: the operator turns it on when they want to see it and turns it off when they do not. If the operator turns on `MYF_DBG_IO` on a busy device and sees a flood of output, that is the intended behaviour; they wanted the flood. The rate-limit macro and the debug macro serve different purposes and should not be combined.

Where the two do meet is in the occasional log line that is conceptually a warning but that the developer wants the ability to silence entirely. For those, the right pattern is to gate the `DLOG_RL` call on a debug bit:

```c
if ((sc->sc_debug & MYF_DBG_IO) != 0)
        DLOG_RL(sc, &sc->sc_rl_short, MYF_RL_DEFAULT_PPS,
            "short read: %d bytes\n", n);
```

The rate-limit fires under debug mask, and the output is both opt-in and bounded. This is a minority pattern; most warnings should fire unconditionally at a rate limit, and most debug prints should be mask-gated without a rate limit.

### `log(9)` Priority Levels

A third logging primitive deserves mention here: `log(9)`. Unlike `device_printf`, which always routes through the kernel message buffer, `log` routes through the syslog path with a syslog priority. The function lives in `/usr/src/sys/kern/subr_prf.c` and takes a priority from `/usr/src/sys/sys/syslog.h`:

```c
void log(int level, const char *fmt, ...);
```

Common priorities: `LOG_EMERG` (0) for system-unusable conditions, `LOG_ALERT` (1) for immediate action, `LOG_CRIT` (2) for critical conditions, `LOG_ERR` (3) for error conditions, `LOG_WARNING` (4) for warnings, `LOG_NOTICE` (5) for notable but normal conditions, `LOG_INFO` (6) for informational messages, `LOG_DEBUG` (7) for debug-level messages. A driver that uses `log(LOG_WARNING, ...)` for its warning path rather than `device_printf` gains the ability to be filtered by `syslog.conf(5)` into a separate log file without the driver author having to do anything else.

The tradeoff is that `log(9)` does not prepend the device name. A driver using `log` has to format the device name into the message manually, which is verbose. Most FreeBSD drivers therefore prefer `device_printf` for driver-specific messages and reserve `log` for cross-cutting notifications. The `myfirst` driver follows the same convention: `device_printf` for everything the operator is meant to read with `dmesg`, `log` for nothing at this stage.

A pragmatic guideline: use `device_printf` when the message is *about this device*. Use `log(9)` when the message is *about a cross-cutting condition* that the syslog infrastructure is the right place to see, such as an authentication event or a policy violation. Driver code rarely needs the second kind.

### The Kernel Message Buffer and What It Costs

One more technical detail before the section closes. The kernel message buffer (`msgbuf`) is a fixed-size circular buffer inside the kernel, allocated at boot. Its size is controlled by the `kern.msgbufsize` tunable, which defaults to 96 KiB on amd64 and can be raised in `/boot/loader.conf`. Every `printf`, `device_printf`, and `log` call routes through the buffer. When the buffer fills, the oldest messages are overwritten. The buffer's contents are what `dmesg` prints.

Two practical consequences follow. First, a flood of short messages can evict the earlier messages an operator needs. A line that says "hello" uses a few dozen bytes; a buffer of 96 KiB holds maybe three thousand such lines; a loop that prints at ten thousand lines per second evicts the entire boot log in less than half a second. Second, a formatted message is not free to produce. `printf`-style formatting costs CPU, and inside an interrupt handler or a hot path that cost shows up directly in latency numbers. The rate-limited macro helps with the first consequence. The second is why debug messages are mask-gated: a `DPRINTF` at mask zero compiles down to an empty statement at runtime, skipping both the formatting and the storage.

Increasing `kern.msgbufsize` is a reasonable response to a machine that repeatedly loses boot logs, but it is not a substitute for rate limiting. A bigger buffer simply buys more room before the flood evicts the older messages; rate limiting reduces the flood itself. Both are worth doing. `kern.msgbufsize=262144` in `/boot/loader.conf` is a common operator choice on production machines. It is not a Chapter 25 action, because the driver cannot change the buffer size at run time.

### A Worked Example: The `myfirst` Read Path

Putting the pieces together, consider the existing `myfirst_read` callback. A simplified version from Chapter 24 looked like this:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        int error = 0;

        mtx_lock(&sc->sc_mtx);
        if (uio->uio_resid == 0) {
                device_printf(sc->sc_dev, "read: empty request\n");
                goto out;
        }
        /* copy bytes into user space, update counters ... */
out:
        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

That code has a latent rate-flood problem. Under stress, a buggy or malicious user-space program can call `read(fd, buf, 0)` in a tight loop and fill the message buffer with "empty request" lines. The event is not an error in the driver; it is a strange-but-legal syscall pattern. Logging it at all is marginal, but if the driver logs it, the log line must be rate-limited.

After the refactor, the same path looks like this:

```c
static int
myfirst_read(struct cdev *cdev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = cdev->si_drv1;
        int error = 0;

        mtx_lock(&sc->sc_mtx);
        if (uio->uio_resid == 0) {
                DLOG_RL(sc, &sc->sc_rl_short, MYF_RL_DEFAULT_PPS,
                    "read: empty request\n");
                goto out;
        }
        /* copy bytes into user space, update counters ... */
out:
        mtx_unlock(&sc->sc_mtx);
        return (error);
}
```

The change is three lines. The effect is that the log can no longer flood, and the first occurrence in any second still produces a line for the operator to notice. The softc gains one `struct myfirst_ratelimit sc_rl_short` field; no other code moves.

Apply the same transformation to every `device_printf` in every error or warning path, reserve one `struct myfirst_ratelimit` per category, and the driver is rate-limited. The diff is mechanical; the discipline is what makes the diff possible in the first place.

### Common Mistakes and How to Avoid Them

Three mistakes are common when first applying rate limiting. Each is easy to spot once you know to look.

The first mistake is **sharing a single rate-limit structure across unrelated call sites**. If site A and site B both use `sc->sc_rl_generic`, a burst at site A silences site B for the rest of the second, and the operator sees only one category. The correct discipline is one rate-limit structure per logical category. Two or three categories per driver is usual; ten is a sign the driver is logging too many kinds of events.

The second mistake is **rate-limiting lifecycle messages**. The driver loads and prints a banner. That banner fires once. Wrapping it in `ppsratecheck` adds noise for no gain, and on a second load during an unfortunate second-boundary it can skip the banner entirely. Reserve rate limiting for the messages that can actually repeat.

The third mistake is **forgetting that the rate-limit counter lives in the softc**. A call site that fires before attach completes, or after detach starts, can touch a softc whose rate-limit structure is not yet initialised (or has been zeroed by `bzero(sc, sizeof(*sc))`). The `struct timeval` and `int` are both value types; a zero-initialised structure is fine for the first call, because `ppsratecheck` handles the `lasttime == 0` case correctly. But an uninitialised heap allocation that later holds garbage is not fine, because the `lasttime` field may hold a large value that makes the code think the last event was in the distant future, and every subsequent call returns 0 until the kernel's clock passes that future time, which may be never. The fix is to ensure the softc is zero-initialised, which in `myfirst` it already is (newbus allocates softc with `MALLOC(... M_ZERO)`). A driver that allocates its own state with `M_NOWAIT` without `M_ZERO` must call `bzero` explicitly.

### When Not to Rate-Limit

Rate limiting is a discipline for paths that can fire often. Some paths cannot. A `KASSERT` failure panics the kernel, so rate-limiting the pre-panic message is wasted effort. An error that aborts a module load ends the load, so only one copy of the message can ever appear. A `device_printf` at attach time fires at most once per instance. For all of these, bare `device_printf` is correct and the extra wrapper is clutter.

A useful rule of thumb: if the call site runs after `attach` has completed and before `detach` has run, and if the event can be caused by something external to the driver (a misbehaving user-space program, a flaky device, a stressed kernel), then rate-limit. Otherwise, do not.

### What the `myfirst` Driver Now Contains

After this section the `myfirst` driver's working tree has two new files:

```text
myfirst_log.h   - the DLOG_RL macro and struct myfirst_ratelimit definition
myfirst_log.c   - any non-trivial rate-check helpers (empty for now)
```

The `myfirst.h` header still holds the softc. The softc gains two or three `struct myfirst_ratelimit` fields, named for the call-site categories that use them. The `read`, `write`, and `ioctl` paths replace their bare `device_printf` calls at error sites with `DLOG_RL`. The attach, detach, open, and close paths keep their bare `device_printf` calls, because those are lifecycle messages and do not repeat.

The `Makefile` gains one line:

```makefile
SRCS= myfirst.c myfirst_debug.c myfirst_ioctl.c myfirst_sysctl.c \
      myfirst_log.c
```

The module builds, loads, and behaves exactly as before in the common case. Under stress the driver no longer floods the message buffer. That is the whole contribution of Section 1.

### Wrapping Up Section 1

A driver that logs without discipline is a driver that fails gracefully in the lab and fails loudly in production. The FreeBSD rate-check primitives `ratecheck(9)` and `ppsratecheck(9)` are small enough to understand in an hour and effective enough to pay back their cost for the rest of the driver's life. Combined with the existing debug-mask machinery from Chapter 23, they give the `myfirst` driver a clean three-way logging story: lifecycle messages go through plain `device_printf`, error and warning messages go through `DLOG_RL`, and debug messages go through `DPRINTF` under the mask.

In the next section, we turn from what the driver says to what it returns. A log line is for the operator; an errno is for the caller. A driver that says the right thing to the operator but the wrong thing to the caller is still a broken driver.

## Section 2: Error Reporting and Return Conventions

The second discipline the chapter teaches is the discipline of returning the right errno. An errno is a small number. The set of possible errnos is defined in `/usr/src/sys/sys/errno.h`, and at time of writing FreeBSD defines fewer than a hundred of them. A driver that is careless about which errno it returns looks fine in the moment, because the caller mostly only checks whether the return value was non-zero, and any non-zero value passes that test. It looks much less fine a few months later when the first user-space helper tries to distinguish *why* a call failed, and the driver's errno choices turn out to be inconsistent. This section teaches the small vocabulary of errnos a driver routinely uses, shows how to choose between them, and walks through an audit of the `myfirst` driver's existing paths.

### Why Errno Discipline Matters

A driver's errno return is a contract with every caller. User-space programs use errnos through `strerror(3)` and through direct comparison (`if (errno == EBUSY)`). Kernel code that invokes driver callbacks uses the return value to decide what to do next: a `d_open` that returns `EBUSY` causes the kernel to fail the `open(2)` syscall with `EBUSY`; a `d_ioctl` that returns `ENOIOCTL` causes the kernel to fall through to the generic ioctl layer; a `device_attach` that returns a non-zero value causes Newbus to roll back the attach and disown the device. Each of those consumers expects a specific value to mean a specific thing. A driver that returns `EINVAL` where `ENXIO` was expected does not necessarily fail; often it just misleads, and the misleading errno shows up as a mystifying diagnostic somewhere the driver author will never see.

The discipline is cheap. The costs of ignoring it compound over time. A driver that chose its errnos well from the start is a driver that produces accurate manual pages, accurate user-space helper error messages, and accurate bug reports. A driver that was sloppy about errnos starts producing `strerror` output that is slightly wrong in many places, and the user-space side of the ecosystem inherits the sloppiness.

### The Small Vocabulary

The full list of errnos is long. The subset a typical character driver uses is short. The table below is the vocabulary most often needed in a FreeBSD driver, grouped by when each is appropriate.

| Errno | Numeric value | When to return |
|-------|--------------|----------------|
| `0` | 0 | Success. The only non-error return. |
| `EPERM` | 1 | The caller lacks privilege for the requested operation, even though the call itself is well-formed. Example: a non-root user requesting a privileged ioctl. |
| `ENOENT` | 2 | The requested object does not exist. Example: a lookup by name or ID that finds nothing. |
| `EIO` | 5 | Generic I/O error from hardware. Use when the hardware returned a failure and there is no more specific errno. |
| `ENXIO` | 6 | The device is gone, detached, or otherwise unreachable. Example: an ioctl on a file descriptor whose underlying device was removed. Different from `ENOENT`: the object was there and is now gone. |
| `EBADF` | 9 | The file descriptor is not opened correctly for the operation. Example: a `MYFIRSTIOC_SETMSG` call made on a file descriptor opened read-only. |
| `ENOMEM` | 12 | Allocation failed. Use for `malloc(M_NOWAIT)` failures and similar. |
| `EACCES` | 13 | The caller lacks permission at the file-system level. Different from `EPERM`: `EACCES` is about file permissions, `EPERM` is about privilege. |
| `EFAULT` | 14 | A user pointer is invalid. Returned by failed `copyin` or `copyout`. Drivers should forward `copyin`/`copyout` failures unchanged. |
| `EBUSY` | 16 | The resource is in use. Use for `detach` that cannot proceed because a client still holds the device open, or for mutex-like acquire attempts that cannot wait. |
| `EINVAL` | 22 | The arguments are recognised but invalid. Use when the driver understood the request but the inputs are malformed. |
| `EAGAIN` | 35 | Try again later. Returned from non-blocking I/O when the operation would block, or from allocation failures that may succeed on retry. |
| `EOPNOTSUPP` | 45 | The operation is not supported by this driver. Use when the call is well-formed but the driver has no code to handle it. |
| `ETIMEDOUT` | 60 | A wait timed out. Use for hardware commands that did not complete within the driver's timeout budget. |
| `ENOIOCTL` | -3 | The ioctl command is unknown to this driver. **Use this for the default case in `d_ioctl`; the kernel translates it to `ENOTTY` for user space.** |
| `ENOSPC` | 28 | No space left, whether on the device, in a buffer, or in an internal table. |

Three pairs in this table are famously easy to confuse: `EPERM` versus `EACCES`, `ENOENT` versus `ENXIO`, and `EINVAL` versus `EOPNOTSUPP`. Each is worth looking at in turn.

`EPERM` versus `EACCES`. `EPERM` is about privilege: the caller is not privileged enough to perform the operation. `EACCES` is about permission: the file-system ACL or mode bits forbid the access. A non-root user trying to write to `/dev/myfirst0` when the node mode is `0600 root:wheel` gets `EACCES` from the kernel before the driver is consulted. A root user trying to call a privileged ioctl that the driver rejects because the caller is not in a particular jail gets `EPERM` from the driver. The distinction matters because the administrator's remedy differs: `EACCES` asks the administrator to adjust device permissions, while `EPERM` asks the administrator to adjust the caller's privileges.

`ENOENT` versus `ENXIO`. `ENOENT` is *there is no such object*. `ENXIO` is *the object is gone, or the device is unreachable*. On a lookup into a driver's internal table, `ENOENT` is the right answer when the requested key is not present. On an operation against a device that has been detached or that has signalled a surprise-removal condition, `ENXIO` is the right answer. The distinction matters because operational tools treat them differently: `ENOENT` suggests the caller gave the wrong key; `ENXIO` suggests the device needs reattachment.

`EINVAL` versus `EOPNOTSUPP`. `EINVAL` is *I understood what you asked but the arguments are wrong*. `EOPNOTSUPP` is *I do not support what you asked*. A `MYFIRSTIOC_SETMSG` call with a buffer that is too long is `EINVAL`. A `MYFIRSTIOC_SETMODE` call for a mode the driver never implements is `EOPNOTSUPP`. The distinction matters because `EOPNOTSUPP` tells the caller to use a different approach, while `EINVAL` tells the caller to fix the arguments and retry.

A fourth confusion deserves a paragraph of its own: `ENOIOCTL` versus `ENOTTY`. `ENOIOCTL` is a negative value (`-3`) defined for the ioctl code path inside the kernel. A driver's `d_ioctl` default case returns `ENOIOCTL` to tell the kernel "I do not recognise this command; please fall through to the generic layer." The generic layer handles `FIONBIO`, `FIOASYNC`, `FIOGETOWN`, `FIOSETOWN`, and similar cross-device ioctls. If the generic layer also does not recognise the command, it translates `ENOIOCTL` into `ENOTTY` (positive 25) for delivery to user space. The common mistake is to return `EINVAL` from the default case of a `d_ioctl` switch, which suppresses the generic fallback entirely. The Chapter 24 driver already returns `ENOIOCTL` correctly; Chapter 25's audit confirms it and checks every other errno in the driver for similar issues.

### The Ioctl Dispatcher Audit

The first audit pass targets `myfirst_ioctl.c`. Each case in the switch statement produces at most one non-zero return. The audit looks at each and asks whether the returned errno is correct.

Case `MYFIRSTIOC_GETVER`: returns 0 on success, never fails. Nothing to audit.

Case `MYFIRSTIOC_GETMSG`: returns 0 on success. The current code does not reject on `fflag` because the message is public. That is a design choice, not a bug. If the driver wanted to restrict `GETMSG` to readers (i.e., require `FREAD`), it would return `EBADF` on the fflag check, consistent with the `SETMSG` and `RESET` cases.

Case `MYFIRSTIOC_SETMSG`: returns `EBADF` when the file descriptor lacks `FWRITE`, which is correct. The second audit question is what happens when the input is not NUL-terminated: `strlcpy` in the kernel tolerates it (copies up to `MYFIRST_MSG_MAX - 1` and terminates), so the driver does not need to check. The third question is whether the length should be validated before copy. The kernel's automatic `copyin` already enforced the fixed length baked into the ioctl encoding, so there is no user-space buffer to validate; the value is in `data` and has been copied already.

Case `MYFIRSTIOC_RESET`: returns `EBADF` when the file descriptor lacks `FWRITE`. The Chapter 25 audit raises a second question: should the reset be privileged? A driver that lets any writer call `RESET` and zero out statistics is exposing a minor denial-of-service surface. The simple fix is to check `priv_check(td, PRIV_DRIVER)` before executing the reset:

```c
case MYFIRSTIOC_RESET:
        if ((fflag & FWRITE) == 0) {
                error = EBADF;
                break;
        }
        error = priv_check(td, PRIV_DRIVER);
        if (error != 0)
                break;
        /* ... existing reset body ... */
        break;
```

If `priv_check` fails, the errno is `EPERM` (the kernel returns `EPERM` rather than `EACCES` because the check is about privilege, not file-system permissions). The `myfirstctl` program running as root sees 0; a non-root program running as the `_myfirst` user sees `EPERM`.

Default case: returns `ENOIOCTL`, which is correct. Leave it alone.

### The Read and Write Path Audit

The second audit pass targets the read and write callbacks.

For `myfirst_read`, the current code returns 0 on success, `EFAULT` when `uiomove` fails, and 0 when `uio_resid == 0`. The 0 return on an empty request is the standard UNIX behaviour (a `read` of zero bytes is allowed and returns 0 bytes) and is correct. No errno change is needed.

For `myfirst_write`, similarly, 0 on success, `EFAULT` on `uiomove` failure, 0 on zero-byte write. Correct.

Neither callback needs `EIO`: the driver does not do hardware I/O at this point, so there is no hardware failure to propagate. A future version of the driver that drives real hardware would return `EIO` from the read or write callback when the hardware indicated a transport-level failure. Adding that return now would be premature; it is the kind of thing Chapter 28's storage work will treat concretely.

### The Open and Close Path Audit

The open callback currently returns 0 unconditionally. The audit question is whether it should ever fail. Three failure modes are conventionally possible: the device is exclusive-open and already has a user (`EBUSY`), the device is powered down and not currently acceptable to open (`ENXIO`), or the driver is being detached right now (`ENXIO`). The simple `myfirst` driver does not enforce exclusive open, and it always accepts opens except during detach. During detach the kernel destroys the cdev before the detach returns, so any open arriving after `destroy_dev` begins is rejected by the kernel itself before the driver's `d_open` is called. The `myfirst` driver therefore does not need explicit `ENXIO` logic. Leaving the open callback returning 0 is correct.

The close callback returns 0 unconditionally. That is correct. The only conceivable reason for `d_close` to return non-zero is a hardware operation during close that failed; since the `myfirst` driver does no such operation, 0 is the right return.

### The Attach and Detach Path Audit

Attach and detach are the callbacks Newbus invokes. Their return values tell Newbus whether to roll back or proceed.

`myfirst_attach`'s non-zero return means "attach failed; please roll back." Every error path in attach must return a positive errno. The current code returns the `error` value from `make_dev_s`, which is positive on failure; that is correct. The additions in Section 5 of this chapter will introduce more error paths with labelled gotos; each of them will use the correct errno for the failed step (`ENOMEM` for allocation failure, `ENXIO` for resource allocation failure, etc.).

`myfirst_detach`'s non-zero return means "cannot detach right now; please leave the device attached." The current code returns `EBUSY` when `sc_open_count > 0`, which is correct. Newbus translates `EBUSY` from detach into a `devctl detach` failure with the same errno, which is the right user-visible behaviour.

The module event handler (`myfirst_modevent`) returns non-zero to reject the event. `MOD_UNLOAD` that cannot proceed because some device instance is still in use returns `EBUSY`. `MOD_LOAD` that cannot proceed because of a sanity-check failure returns an appropriate errno (`ENOMEM`, `EINVAL`, etc.). The current code is correct.

### The Sysctl Handler Audit

Sysctl handlers have their own errno conventions. The Chapter 24 driver has one custom handler, `myfirst_sysctl_message_len`. Its body is:

```c
static int
myfirst_sysctl_message_len(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        u_int len;

        mtx_lock(&sc->sc_mtx);
        len = (u_int)sc->sc_msglen;
        mtx_unlock(&sc->sc_mtx);

        return (sysctl_handle_int(oidp, &len, 0, req));
}
```

The handler reads its input with `sysctl_handle_int`, which returns 0 on success and a positive errno on failure. The handler forwards that errno unchanged, which is correct. No audit change is needed.

A sysctl handler that writes (rather than only reads) should check `req->newptr` to distinguish a read from a write, and should return `EPERM` if the write is attempted on a read-only OID. The existing `debug.mask` OID is declared with `CTLFLAG_RW`, so the kernel allows writes automatically; the handler does not need a privilege check because the OID is already restricted to root by the sysctl MIB permissions. The Chapter 25 driver does not add more custom sysctl handlers at this stage.

### Error Path Messages

Returning the right errno is half the contract. Emitting the right log message is the other half. The discipline combines the rate-limited logging of Section 1 with the errno vocabulary of Section 2. A warning path looks like this:

```c
if (input_too_large) {
        DLOG_RL(sc, &sc->sc_rl_inval, MYF_RL_DEFAULT_PPS,
            "ioctl: SETMSG buffer too large (%zu > %d)\n",
            length, MYFIRST_MSG_MAX);
        error = EINVAL;
        break;
}
```

Three properties make this a good error path. First, the log message names the call ("ioctl: SETMSG"), the reason ("buffer too large"), and the numeric values involved. Second, the errno returned is `EINVAL`, which is the right value for "I understood but the argument is wrong." Third, the whole path is rate-limited so that a buggy user-space program calling the ioctl in a tight loop cannot flood the message buffer.

A bad error path looks like this:

```c
if (input_too_large) {
        device_printf(sc->sc_dev, "ioctl failed\n");
        return (-1);
}
```

Three properties make this a bad error path. The log message is uninformative: "ioctl failed" says nothing the caller did not already know. The return value is `-1`, which is not a valid kernel errno. And the log line is not rate-limited, so a misbehaving caller can fill the message buffer with noise.

The good path takes nine lines and the bad path takes three, which is a good trade. An error log line is only printed when something is wrong; spending a few extra seconds to make it informative when it does print is worth the time.

### Module Event Handler Conventions

The module event handler has its own errno convention. The handler signature is:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
        switch (what) {
        case MOD_LOAD:
                /* Driver-wide init. */
                return (0);
        case MOD_UNLOAD:
                /* Driver-wide teardown. */
                return (0);
        case MOD_QUIESCE:
                /* Pause and prepare for unload. */
                return (0);
        case MOD_SHUTDOWN:
                /* System shutting down. */
                return (0);
        default:
                return (EOPNOTSUPP);
        }
}
```

Each case returns 0 on success, or a positive errno to reject the event. The specific errnos by case:

- `MOD_LOAD` returns `ENOMEM` if a global allocation failed, `ENXIO` if the driver is not compatible with the current kernel, or `EINVAL` if a tunable's value is out of range.
- `MOD_UNLOAD` returns `EBUSY` if the driver cannot be unloaded right now because some instance is still in use. The kernel respects this and leaves the module loaded.
- `MOD_QUIESCE` returns `EBUSY` if the driver cannot pause. A driver that does not support quiescence simply returns 0 from this case, because quiescence is an optional feature and returning success says "I am paused" in the trivial sense of having no work in flight.
- `MOD_SHUTDOWN` rarely fails; it returns 0 unless the driver has a specific reason to object to shutdown. A driver that wants to flush persistent state uses an `EVENTHANDLER` on `shutdown_pre_sync` rather than rejecting `MOD_SHUTDOWN`.
- The default case returns `EOPNOTSUPP` to indicate the driver does not recognise the event type. This is not an error; it is the standard way to say "I do not implement this event."

### An Errno Checklist

To close the section, a checklist the reader can run against any driver they write. Each item is a question whose answer should be yes.

1. Every non-zero return in a callback is a positive errno from `errno.h`, except for `d_ioctl` which may return `ENOIOCTL` (a negative value).
2. `copyin` and `copyout` failures propagate their errno unchanged (typically `EFAULT`).
3. The default case of `d_ioctl` returns `ENOIOCTL`, not `EINVAL`.
4. `d_detach` returns `EBUSY` if the device is still in use, not `ENXIO` or some other value.
5. `d_open` returns `ENXIO` if the underlying hardware is gone or if the driver is being detached, not `EIO`.
6. `d_write` returns `EBADF` if the file descriptor lacks `FWRITE`, not `EPERM`.
7. Every error path logs a message that names the call, the reason, and the relevant values, using the rate-limited macro.
8. No error path logs *and* returns a generic errno. If the driver has enough context to log the specific reason, it has enough context to return a specific errno.
9. The driver distinguishes `EINVAL` (arguments wrong) from `EOPNOTSUPP` (feature absent) consistently.
10. The driver distinguishes `ENOENT` (no such key) from `ENXIO` (device unreachable) consistently.

A driver that passes this checklist has a consistent errno surface, and the surface is small enough that the manual page can list every errno the driver returns and say exactly when each occurs.

### Wrapping Up Section 2

Errnos are a small vocabulary and a contract. Sloppiness about either shows up as mystifying behaviour in user space; discipline about both shows up as accurate diagnostics and shorter bug reports. Combined with Section 1's rate-limited logging, the `myfirst` driver now talks carefully to both the operator (through log lines) and the caller (through errnos). 

In the next section, we look at the third audience the driver owes a contract to: the administrator who configures the driver through `/boot/loader.conf` and `sysctl`. Configuration is a third kind of contract, and discipline about it is how the driver stays useful across workloads without needing a rebuild.

## Section 3: Driver Configuration via Loader Tunables and sysctl

The third discipline is the discipline of externalising decisions. Any driver has values that someone might reasonably want to change without rebuilding the module: a timeout, a retry count, an internal buffer size, a verbosity level, a feature toggle. A driver that bakes those values into the source forces every change to go through a full compile, install, and reboot cycle. A driver that exposes them as loader tunables and sysctls lets an operator adjust behaviour at boot or at run time with a single edit or a single command. The cost of offering the knobs is small; the cost of not offering them is paid by the operator.

This section teaches the two FreeBSD mechanisms for externalising configuration: loader tunables (read from `/boot/loader.conf` and applied before the kernel reaches `attach`) and sysctls (read and written at run time through `sysctl(8)`). It explains how they cooperate through the `CTLFLAG_TUN` flag, shows how to choose between per-driver and per-instance tunables, walks through the `TUNABLE_*_FETCH` family, and ends with a short lab in which the `myfirst` driver gains three new tunables and the reader boots the VM with each.

### The Difference Between a Tunable and a Sysctl

A tunable and a sysctl look similar to an operator. Both are strings in a namespace like `hw.myfirst.debug_mask_default` or `dev.myfirst.0.debug.mask`. Both take values the operator sets. Both end up in kernel memory. They differ in when and how.

A **tunable** is a variable set in the bootloader environment. The bootloader (`loader(8)`) reads `/boot/loader.conf`, collects its `key=value` pairs into an environment, and hands that environment to the kernel when the kernel starts. The kernel exposes this environment through the `getenv(9)` family and through the `TUNABLE_*_FETCH` macros. Tunables are read during boot, usually before the corresponding driver attaches. They cannot be changed at run time (changing `/boot/loader.conf` requires a reboot for the change to take effect). They are appropriate for values that must be known before `attach` runs: the size of a statically-allocated table, a feature flag that controls which code paths are compiled into the attach path, the initial value of a debug mask.

A **sysctl** is a variable in the kernel's hierarchical configuration tree, accessible at run time through the `sysctl(2)` syscall and the `sysctl(8)` tool. Sysctls can be read-only (`CTLFLAG_RD`), read-write (`CTLFLAG_RW`), or read-only-root-writable (various combinations of flags). They are appropriate for values that make sense to change after the driver has attached: a verbosity level, a throttle rate, a counter reset command, a writable status knob.

The useful feature is that the two mechanisms can share a variable. A sysctl declared with `CTLFLAG_TUN` tells the kernel to read a tunable of the same name at boot and use its value as the initial value of the sysctl. The operator can then adjust the sysctl at run time, and the tunable persists across reboots as the default. The `myfirst` driver already uses this pattern for its debug mask: `debug.mask` is a `CTLFLAG_RW | CTLFLAG_TUN` sysctl, and `hw.myfirst.debug_mask_default` is the matching tunable in `/boot/loader.conf`. Section 3 generalises that pattern to every configuration knob the driver wants to expose.

### The `TUNABLE_*_FETCH` Family

FreeBSD provides a family of macros for reading tunables from the bootloader environment. Each macro reads the named tunable, parses it into the right C type, and stores the result. If the tunable is not set, the variable keeps its existing value; the caller must therefore initialise the variable to the right default before calling the fetch macro.

The macros, declared in `/usr/src/sys/sys/kernel.h`:

```c
TUNABLE_INT_FETCH(path, pval)        /* int */
TUNABLE_LONG_FETCH(path, pval)       /* long */
TUNABLE_ULONG_FETCH(path, pval)      /* unsigned long */
TUNABLE_INT64_FETCH(path, pval)      /* int64_t */
TUNABLE_UINT64_FETCH(path, pval)     /* uint64_t */
TUNABLE_BOOL_FETCH(path, pval)       /* bool */
TUNABLE_STR_FETCH(path, pval, size)  /* char buffer of given size */
```

Each expands to the matching `getenv_*` call. For `TUNABLE_INT_FETCH`, for example, the expansion is `getenv_int(path, pval)`, which reads the bootloader environment and parses the value as an integer.

The path is a string, conventionally of the form `hw.<driver>.<knob>` for per-driver tunables and `hw.<driver>.<unit>.<knob>` for per-instance tunables. The `hw.` prefix is a convention for hardware-related tunables; other prefixes (`kern.`, `net.`) exist for different subsystems but are less common in driver code.

A worked example from the `myfirst` driver shows the pattern:

```c
static int
myfirst_attach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);
        int error;

        /* Initialise defaults. */
        sc->sc_debug = 0;
        sc->sc_timeout_sec = 30;
        sc->sc_max_retries = 3;

        /* Read tunables.  The variables keep their default values if
         * the tunables are not set. */
        TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default", &sc->sc_debug);
        TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
        TUNABLE_INT_FETCH("hw.myfirst.max_retries", &sc->sc_max_retries);

        /* ... rest of attach ... */
}
```

The operator sets the tunables in `/boot/loader.conf`:

```ini
hw.myfirst.debug_mask_default="0xff"
hw.myfirst.timeout_sec="15"
hw.myfirst.max_retries="5"
```

After reboot, every instance of `myfirst` attaches with `sc_debug=0xff`, `sc_timeout_sec=15`, `sc_max_retries=5`. No rebuild was required; the values live outside the driver source.

### Per-Driver Versus Per-Instance Tunables

A driver that might attach more than once needs a decision: should its tunables apply to every instance, or should each instance have its own?

The per-driver form uses a path of the form `hw.myfirst.debug_mask_default`. Every instance of `myfirst` at attach time reads this single variable, so all instances start with the same default. This is the simpler form and is correct when the tunable has the same meaning on every instance.

The per-instance form uses a path of the form `hw.myfirst.0.debug_mask_default`, where `0` is the unit number. Each instance reads its own variable, so instance 0 and instance 1 can have different defaults. This is the right form when the hardware behind each instance can reasonably need different configuration, for example two PCI adapters on the same system with different workloads.

The decision is a design choice, not a correctness question. Most drivers use the per-driver form for most tunables, with per-instance forms reserved for the few cases where per-instance configuration actually matters. For `myfirst`, a fictional pseudo-device, per-driver is the right default for every tunable. The Chapter 25 driver therefore adds three per-driver tunables (`timeout_sec`, `max_retries`, `log_ratelimit_pps`) and keeps the existing per-driver `debug_mask_default`.

A pattern that combines both forms, if the driver needs it, is to read the per-driver tunable first as a baseline and then read the per-instance tunable as an override:

```c
int defval = 30;

TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &defval);
sc->sc_timeout_sec = defval;
TUNABLE_INT_FETCH_UNIT("hw.myfirst", unit, "timeout_sec",
    &sc->sc_timeout_sec);
```

FreeBSD does not have a `TUNABLE_INT_FETCH_UNIT` macro out of the box; a driver that needs this has to compose the path with `snprintf` and then call `getenv_int` by hand. The effort is small but the need is rare, so `myfirst` does not go there.

### The `CTLFLAG_TUN` Flag

The second half of the externalisation story is that a tunable by itself is read only at boot. To make the same value adjustable at run time, the driver declares the matching sysctl with `CTLFLAG_TUN`:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "debug.mask",
    CTLFLAG_RW | CTLFLAG_TUN,
    &sc->sc_debug, 0,
    "Bitmask of enabled debug classes");
```

`CTLFLAG_TUN` tells the kernel that the initial value of this sysctl should be taken from the bootloader environment variable of the same name, using the OID name as the key. The match is textual and automatic; the driver does not need to call `TUNABLE_INT_FETCH` separately.

There is a subtle rule about when `CTLFLAG_TUN` is honoured. The flag applies to the OID's *initial* value, which is read from the environment when the sysctl is created. If the driver calls `TUNABLE_INT_FETCH` explicitly before creating the sysctl, the explicit fetch wins and `CTLFLAG_TUN` is effectively redundant. If the driver does not call `TUNABLE_INT_FETCH` and relies on `CTLFLAG_TUN` alone, the sysctl's initial value comes from the environment automatically.

In practice, the `myfirst` driver uses both mechanisms for clarity. The explicit `TUNABLE_INT_FETCH` in attach makes the driver's intent visible in the source; the `CTLFLAG_TUN` on the sysctl gives the operator a clear hint in the sysctl documentation that the OID respects a loader tunable. Either mechanism alone would work; using both is a small duplication that pays off in readability.

### Declaring a Tunable as a Static Sysctl

For driver-wide sysctls that do not belong to a specific instance, FreeBSD offers compile-time macros that bind a sysctl to a static variable and read its default from the environment in one declaration. The canonical form:

```c
SYSCTL_NODE(_hw, OID_AUTO, myfirst, CTLFLAG_RW, NULL,
    "myfirst pseudo-driver");

static int myfirst_verbose = 0;
SYSCTL_INT(_hw_myfirst, OID_AUTO, verbose,
    CTLFLAG_RWTUN, &myfirst_verbose, 0,
    "Enable verbose driver logging");
```

The `SYSCTL_NODE` declares a new parent node `hw.myfirst`. The `SYSCTL_INT` declares an integer OID `hw.myfirst.verbose` with `CTLFLAG_RWTUN` (which combines `CTLFLAG_RW` and `CTLFLAG_TUN`). The `myfirst_verbose` variable is the driver's global verbosity level. The operator sets `hw.myfirst.verbose=1` in `/boot/loader.conf` to enable verbose output at boot, or runs `sysctl hw.myfirst.verbose=1` to toggle it at run time.

The static declaration is appropriate for driver-wide state. The per-instance state (`sc_debug`, counters) continues to live under `dev.myfirst.<unit>.*` and is declared dynamically through `device_get_sysctl_ctx`.

### A Small Side Note on `SYSCTL_INT` Versus `SYSCTL_ADD_INT`

The static form `SYSCTL_INT(parent, OID_AUTO, ...)` is a compile-time declaration. The dynamic form `SYSCTL_ADD_INT(ctx, list, OID_AUTO, ...)` is a runtime call. Both produce a sysctl OID. The static form is appropriate for driver-wide sysctls whose existence does not depend on attaching to hardware. The dynamic form is appropriate for per-instance sysctls created in attach and destroyed on detach.

A common beginner mistake is to use the dynamic form for driver-wide sysctls, which works but requires a driver-wide `sysctl_ctx_list` that must be initialised at `MOD_LOAD` and freed at `MOD_UNLOAD`. The static form avoids all of that: the sysctl exists from the moment the module loads until the moment it unloads, and the kernel handles the registration and deregistration automatically.

### Documenting a Tunable

A tunable that the operator does not know about is a tunable that does not get used. The discipline is to document every tunable the driver exposes, in three places.

First, the tunable's declaration in the source should include a one-line description string. For `SYSCTL_ADD_UINT` and friends, the last argument is the description:

```c
SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "timeout_sec",
    CTLFLAG_RW | CTLFLAG_TUN,
    &sc->sc_timeout_sec, 0,
    "Timeout in seconds for hardware commands (default 30, min 1, max 3600)");
```

The description string is what `sysctl -d` prints when an operator asks for documentation. A good description names the unit, the default, and the acceptable range.

Second, the driver's `MAINTENANCE.md` (introduced in Section 7) should list every tunable with a paragraph each. The paragraph explains what the tunable does, when to change it, what the default is, and what side effects setting it has.

Third, the driver's manual page (typically `myfirst(4)`) should list every tunable under a `LOADER TUNABLES` section and every sysctl under a `SYSCTL VARIABLES` section. The `myfirst` driver does not yet have a manual page; the chapter treats the manual page as a later concern. The `MAINTENANCE.md` document carries the full documentation in the meantime.

### A Worked Example: `hw.myfirst.timeout_sec`

The `myfirst` driver does not have real hardware at this stage, but the chapter introduces a fictional `timeout_sec` knob that future chapters will use. The complete mini-workflow is:

1. In `myfirst.h`, add the field to the softc:
   ```c
   struct myfirst_softc {
           /* ... existing fields ... */
           int   sc_timeout_sec;
   };
   ```

2. In `myfirst_bus.c` (the new file introduced in Section 6, which holds attach and detach), initialise the default and read the tunable:
   ```c
   sc->sc_timeout_sec = 30;
   TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
   ```

3. In `myfirst_sysctl.c`, expose the knob as a runtime sysctl:
   ```c
   SYSCTL_ADD_INT(ctx, child, OID_AUTO, "timeout_sec",
       CTLFLAG_RW | CTLFLAG_TUN,
       &sc->sc_timeout_sec, 0,
       "Timeout in seconds for hardware commands");
   ```

4. In `MAINTENANCE.md`, document the tunable:
   ```
   hw.myfirst.timeout_sec
       Timeout in seconds for hardware commands.  Default 30.
       Acceptable range 1 through 3600.  Values below 1 are
       clamped to 1; values above 3600 are clamped to 3600.
       Adjustable at run time via sysctl dev.myfirst.<unit>.
       timeout_sec.
   ```

5. In the regression script, add a line that verifies the tunable picks up its default:
   ```
   [ "$(sysctl -n dev.myfirst.0.timeout_sec)" = "30" ] || fail
   ```

The driver now has a timeout knob that the operator can set at boot through `/boot/loader.conf`, can adjust at run time through `sysctl`, and can find documented in `MAINTENANCE.md`. Every future chapter that introduces a new configurable value will follow the same five-step workflow.

### Range Checks and Validation

A tunable that the operator can set to any value is a tunable that can be set to an out-of-range value, either by accident (a typo in `/boot/loader.conf`) or by a misguided attempt at tuning. The driver must validate the value it reads and clamp or reject it.

For tunables read at boot with `TUNABLE_INT_FETCH`, the validation happens inline:

```c
sc->sc_timeout_sec = 30;
TUNABLE_INT_FETCH("hw.myfirst.timeout_sec", &sc->sc_timeout_sec);
if (sc->sc_timeout_sec < 1 || sc->sc_timeout_sec > 3600) {
        device_printf(dev,
            "tunable hw.myfirst.timeout_sec out of range (%d), "
            "clamping to default 30\n",
            sc->sc_timeout_sec);
        sc->sc_timeout_sec = 30;
}
```

For sysctls with runtime write support, the validation happens in the handler. A simple `CTLFLAG_RW` sysctl on an int variable accepts any int; to reject out-of-range writes, the driver declares a custom handler:

```c
static int
myfirst_sysctl_timeout(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;
        int v;
        int error;

        v = sc->sc_timeout_sec;
        error = sysctl_handle_int(oidp, &v, 0, req);
        if (error != 0 || req->newptr == NULL)
                return (error);
        if (v < 1 || v > 3600)
                return (EINVAL);
        sc->sc_timeout_sec = v;
        return (0);
}
```

The handler reads the current value, calls `sysctl_handle_int` to perform the actual I/O, and applies the new value only if it is in range. A write of 0 or 7200 returns `EINVAL` to the operator without changing the sysctl's value. This is the right behaviour: the operator gets clear feedback that the write was rejected.

The `myfirst` driver at this stage does not validate its integer sysctls because none of them can meaningfully go out of range (the debug mask is a bitmask, and any 32-bit value is a legal mask). Future drivers that introduce timeouts, retry counts, and buffer sizes will use the custom-handler pattern consistently.

### When to Expose a Tunable and When to Leave It Internal

Exposing a tunable is a commitment. Once the operator sets `hw.myfirst.timeout_sec=15` in `/boot/loader.conf`, the driver has made a promise that the meaning of that knob will not change in a later version. Removing the tunable breaks production deployments. Changing its interpretation silently breaks them worse.

The right discipline is to expose a value as a tunable only when all three of these are true:

1. The value has an operational use case. Someone might reasonably need to change it on a real deployment.
2. The range of reasonable values is known. The driver can document it in `MAINTENANCE.md`.
3. The cost of supporting the knob for the life of the driver is worth the operational value it provides.

A driver that asks itself those three questions and answers yes to all three exposes a small, purposeful set of tunables. A driver that exposes every internal constant as a tunable because "operators might want to tune it" ends up with a sprawling configuration surface that nobody can document and nobody can test across its full range.

For `myfirst`, the initial set of tunables is deliberately small: `debug_mask_default`, `timeout_sec`, `max_retries`, `log_ratelimit_pps`. Each has a clear operational case, a clear default, and a clear range. The driver is not trying to expose every int field in the softc as a tunable; it is trying to expose the ones that an operator might actually want to touch.

### A Cautionary Note on `CTLFLAG_RWTUN` for Strings

The `TUNABLE_STR_FETCH` macro reads a string from the bootloader environment into a fixed-size buffer. The matching sysctl flag, `CTLFLAG_RWTUN` on a `SYSCTL_STRING`, works, but it has a trap: the string's storage must be a static buffer, not a per-instance `char[]` field in the softc. A string sysctl that writes into a softc field can outlive the softc if the sysctl framework does not deregister the OID before the softc is freed, which leads to use-after-free bugs.

The safer pattern is to expose strings as read-only and handle writes through a custom handler that copies the new value into the softc under a lock. The `myfirst` driver follows this pattern: `dev.myfirst.0.message` is exposed with `CTLFLAG_RD` only, and writes go through the `MYFIRSTIOC_SETMSG` ioctl instead. The ioctl path takes the softc mutex, copies the new value, and unlocks; there is no lifetime issue with sysctl OIDs.

String tunables and sysctls are useful enough for some drivers to be worth the care, but the Chapter 25 driver does not need them. The principle is worth naming because the trap surfaces later in real drivers.

### Tunables Versus Kernel Modules: Where They Live

Two small but important details about the loader environment are worth naming.

First, a tunable in `/boot/loader.conf` applies from the moment the kernel starts. It is available to any module that calls `TUNABLE_*_FETCH` or has a `CTLFLAG_TUN` sysctl, even if the module is not loaded at boot. A module loaded later with `kldload` still sees the tunable's value. This is convenient: the operator sets the tunable once and forgets about it until the module loads.

Second, a tunable is read from the environment but cannot be written back. Changing `hw.myfirst.timeout_sec` at run time (with `kenv`) does not affect any driver that has already read it; the variable in the softc is what matters, not the environment. To change a value at run time, the operator uses the matching sysctl.

These two details together explain why `CTLFLAG_TUN` is the right shape for most configuration knobs: the tunable sets the boot default, the sysctl handles run-time adjustment, and the operator's toolkit (`/boot/loader.conf` plus `sysctl(8)`) works as expected.

### Wrapping Up Section 3

Configuration is a conversation with the operator. A driver that externalises the right values through tunables and sysctls can be tuned without rebuilding; a driver that hides every value inside the source forces a rebuild for every change. The `TUNABLE_*_FETCH` family and the `CTLFLAG_TUN` flag together cover boot-time and run-time adjustment, and the per-driver versus per-instance choice fits the driver to its operational reality. The `myfirst` driver now has three new tunables in addition to the existing `debug_mask_default`, each with a documented range and each with a matching sysctl.

In the next section, we turn from what the driver exposes to how the driver evolves. A configuration knob that works today must still work tomorrow when the driver changes. Versioning discipline is what keeps that promise.

## Section 4: Versioning and Compatibility Strategies

The fourth discipline is the discipline of evolving without breaking. Every public surface the `myfirst` driver offers, the `/dev/myfirst0` node, the ioctl interface, the sysctl tree, the tunable set, is a contract with someone. A change that silently alters the meaning of any of those is a breaking change, and breaking changes that slip past the developer's notice are the source of a disproportionate number of real-world driver bugs. This section teaches how to version a driver's public surface deliberately, so that changes are visible to callers and so that older callers keep working when the driver adds new features.

The chapter uses three distinct version numbers for the `myfirst` driver. Each has a specific purpose. Conflating them is a source of confusion worth avoiding before it takes root.

### The Three Version Numbers

The `myfirst` driver has three version identifiers, introduced across Chapters 23, 24, and 25. Each lives in a different place and changes for a different reason.

The first is the **human-readable release string**. For `myfirst`, this is `MYFIRST_VERSION`, defined in `myfirst_sysctl.c` and exposed through the `dev.myfirst.0.version` sysctl. Its current value is `"1.8-maintenance"`. The release string is for humans: an operator who runs `sysctl dev.myfirst.0.version` sees a short label that identifies this particular checkpoint of the driver's history. The release string is not parsed by programs; it is read by people. It changes whenever the driver reaches a new milestone the author wants to mark, which in this book is at the end of each chapter.

The second is the **kernel module version integer**. This is `MODULE_VERSION(myfirst, N)`, where `N` is an integer used by the kernel's dependency machinery. Another module that declares `MODULE_DEPEND(other, myfirst, 1, 18, 18)` requires `myfirst` to be present at version 18 or above (and below or equal to 18, which in this declaration means exactly 18). The module version integer changes only when an in-kernel caller of the module would need a recompile, for example when a shared symbol's signature changes. For a driver that exports no public kernel symbols (like `myfirst`), the module version number is mostly symbolic; the chapter bumps it at each milestone to keep the reader's mental model aligned across the three version identifiers.

The third is the **ioctl interface version integer**. For `myfirst`, this is `MYFIRST_IOCTL_VERSION` in `myfirst_ioctl.h`. Its current value is 1. The ioctl interface version changes when the ioctl header changes in a way that an older user-space program compiled against the previous version would misinterpret. A renumbered ioctl command, a changed payload layout, a changed semantics of an existing ioctl: each of these is a breaking change to the ioctl interface and must bump the version. Adding a new ioctl command, extending a payload with a field at the end without reinterpreting existing fields, adding a feature that does not affect older commands: these are compatible changes and do not require a bump.

A simple rule of thumb keeps the three straight. The release string is what the operator reads. The module version integer is what other modules check. The ioctl version integer is what user-space programs check. Each moves on its own schedule.

### Why Users Need to Query the Version

A user-space program that talks to the driver through ioctls has a problem. The header `myfirst_ioctl.h` defines a set of commands, layouts, and version-1 semantics. A new version of the driver may add commands, change layouts, or change semantics. When the user-space program runs on a system with a newer or older driver than the one it was compiled against, it has no way to know the driver's actual version unless it asks.

The solution is an ioctl whose only purpose is to return the driver's ioctl version. The `myfirst` driver already has one: `MYFIRSTIOC_GETVER`, defined as `_IOR('M', 1, uint32_t)`. A user-space program calls this ioctl immediately after opening the device, compares the returned version to the version it was compiled against, and decides whether it can proceed safely.

The pattern in user space:

```c
#include "myfirst_ioctl.h"

int fd = open("/dev/myfirst0", O_RDWR);
uint32_t ver;
if (ioctl(fd, MYFIRSTIOC_GETVER, &ver) < 0)
        err(1, "getver");
if (ver != MYFIRST_IOCTL_VERSION)
        errx(1, "driver version %u, tool expects %u",
            ver, MYFIRST_IOCTL_VERSION);
```

The tool refuses to run if the versions do not match. That is one possible policy. A more forgiving policy would allow the tool to run against a newer driver if the driver's new ioctls are supersets of the old ones, and would allow the tool to run against an older driver if the tool can fall back to the older command set. A more rigid policy would require exact match. The tool's author chooses among these based on how much effort is worth spending on backward compatibility.

### Adding a New Ioctl Without Breaking Older Callers

The common case is adding a new feature to the driver, which usually means adding a new ioctl. The discipline is straightforward as long as two rules are followed.

First, **do not reuse an existing ioctl number**. Each ioctl command has a unique `(magic, number)` pair encoded by `_IO`, `_IOR`, `_IOW`, or `_IOWR`. The current assignments in `myfirst_ioctl.h`:

```c
#define MYFIRSTIOC_GETVER   _IOR('M', 1, uint32_t)
#define MYFIRSTIOC_GETMSG   _IOR('M', 2, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_SETMSG   _IOW('M', 3, char[MYFIRST_MSG_MAX])
#define MYFIRSTIOC_RESET    _IO('M', 4)
```

A new ioctl takes the next available number under the same magic letter: `MYFIRSTIOC_GETCAPS = _IOR('M', 5, uint32_t)`. The number 5 has not been used before and cannot conflict with an older program's compiled binary. An older program compiled against a version without `GETCAPS` simply never sends that ioctl, so the older program is unaffected by the addition.

Second, **do not bump `MYFIRST_IOCTL_VERSION` for a pure addition**. A new ioctl that does not change the meaning of the old ones is a compatible change. An older user-space program that never heard of the new ioctl still speaks the same language; the version integer should stay the same. Bumping the version for every addition would force every caller to rebuild whenever the driver gains a new command, which defeats the purpose of versioning.

A new ioctl that replaces an existing one with different semantics does require a bump. If the driver adds `MYFIRSTIOC_SETMSG_V2` with a new layout and retires `MYFIRSTIOC_SETMSG`, older programs that call the retired command see a changed behaviour (the driver may return `ENOIOCTL` or may behave differently). That is a breaking change, and the bump signals it.

### Retiring a Deprecated Ioctl

Retirement is the politely-managed form of removal. When a command is to be removed, the driver announces the intention, keeps the command working for a transition period, and removes it in a later version. A typical deprecation sequence:

- Version N: announce deprecation in `MAINTENANCE.md`. The command still works.
- Version N+1: the command works but logs a rate-limited warning each time it is used. Users see the warning and know to migrate.
- Version N+2: the command returns `EOPNOTSUPP` and logs a rate-limited error. Most users have migrated by now; the few who have not are forced to.
- Version N+3: the command is removed from the header. Programs that still reference it no longer compile.

The transition period should be measured in releases (typically one or two major versions) rather than in calendar time. A driver that keeps its deprecation contract predictable gives consumers a stable target to aim at.

For `myfirst` in this chapter, no command is yet deprecated. The chapter introduces the pattern for the future. The same discipline applies to the sysctl tree: a rate-limited warning in the OID's handler tells operators that the name is on its way out, and a note in `MAINTENANCE.md` records the planned removal date.

### The Capability Bitmask Pattern

For drivers that evolve over several releases, a single version integer tells callers which version they are talking to but not which specific features that version supports. A feature-rich driver benefits from a more fine-grained mechanism: a capability bitmask.

The idea is simple. The driver defines a set of capability bits in `myfirst_ioctl.h`:

```c
#define MYF_CAP_RESET       (1U << 0)
#define MYF_CAP_GETMSG      (1U << 1)
#define MYF_CAP_SETMSG      (1U << 2)
#define MYF_CAP_TIMEOUT     (1U << 3)
#define MYF_CAP_MAXRETRIES  (1U << 4)
```

A new ioctl, `MYFIRSTIOC_GETCAPS`, returns a `uint32_t` with the bits set for the features this driver actually supports:

```c
#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)
```

In the kernel:

```c
case MYFIRSTIOC_GETCAPS:
        *(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
            MYF_CAP_SETMSG;
        break;
```

In user space:

```c
uint32_t caps;
ioctl(fd, MYFIRSTIOC_GETCAPS, &caps);
if (caps & MYF_CAP_TIMEOUT)
        set_timeout(fd, 60);
else
        warnx("driver does not support timeout configuration");
```

The capability bitmask allows a user-space program to discover features without trial and error. If the caller wants to know whether a feature exists, it checks the bit; if the bit is set, the caller knows the driver supports the feature and the relevant ioctls. An older driver that does not define the bit does not pretend to support a feature it never heard of.

The pattern scales well as the driver grows. Each release adds new bits for new features. Retired features keep their bit reserved as unused; recycling a bit for a new meaning would be a breaking change. The bitmask itself is a `uint32_t`, giving the driver 32 features before it needs to add a second word. If the driver reaches 32 features, adding a second word is a compatible change (the new bits are in a new field, so older programs that read only the first word see the same bits).

Chapter 25 adds `MYFIRSTIOC_GETCAPS` to the `myfirst` driver with three bits set: `MYF_CAP_RESET`, `MYF_CAP_GETMSG`, and `MYF_CAP_SETMSG`. The `myfirstctl` user-space program is extended to query the capabilities at startup and to refuse to invoke an unsupported feature.

### Sysctl Deprecation

FreeBSD does not offer a dedicated `CTLFLAG_DEPRECATED` flag on the sysctl tree. The related flag `CTLFLAG_SKIP`, defined in `/usr/src/sys/sys/sysctl.h`, hides an OID from default listings (it is still readable if named explicitly), but it is primarily used for purposes other than announcing retirement. The polite way to retire a sysctl OID is therefore to replace its handler with one that does the intended work *and* logs a rate-limited warning the first few times the OID is touched.

```c
static int
myfirst_sysctl_old_counter(SYSCTL_HANDLER_ARGS)
{
        struct myfirst_softc *sc = arg1;

        DLOG_RL(sc, &sc->sc_rl_deprecated, MYF_RL_DEFAULT_PPS,
            "sysctl dev.myfirst.%d.old_counter is deprecated; "
            "use new_counter instead\n",
            device_get_unit(sc->sc_dev));
        return (sysctl_handle_int(oidp, &sc->sc_old_counter, 0, req));
}
```

The operator sees the warning in `dmesg` the first few times the OID is read, which is a strong hint to migrate. The sysctl still works, so scripts that reference it explicitly do not break during the transition. After a release or two, the OID itself is removed. A note in `MAINTENANCE.md` records the intention and the target release.

For `myfirst`, no sysctl is yet deprecated. The Chapter 25 driver introduces the pattern in documentation and keeps it ready for future use.

### User-Visible Behaviour Changes

Not every breaking change is a rename or a renumbering. Sometimes the driver keeps the same ioctl, the same sysctl, the same tunable, and quietly changes what the operation does. A `MYFIRSTIOC_RESET` that used to zero counters but now also clears the message is a behaviour change. A sysctl that used to report total bytes written but now reports kilobytes is a behaviour change. A tunable that used to be an absolute value and is now a multiplier is a behaviour change.

Behaviour changes are the hardest breaking changes to catch because they do not show up in diffs of header files or sysctl listings. The discipline is to document every behaviour change in `MAINTENANCE.md` under a "Change Log" section, to bump the ioctl interface version integer when an ioctl's semantics change, and to announce sysctl semantics changes in the description string itself.

A good pattern for behaviour changes is to introduce a new named command or a new sysctl rather than redefining an existing one. `MYFIRSTIOC_RESET` keeps the old semantics. `MYFIRSTIOC_RESET_ALL` is a new command with the new semantics. The old command is eventually deprecated. The cost is a slightly larger public surface for the transition period; the benefit is that no caller is broken by a silent behaviour change.

### `MODULE_DEPEND` and Inter-Module Compatibility

The `MODULE_DEPEND` macro declares that one module depends on another and requires a specific version range:

```c
MODULE_DEPEND(myfirst, dependency, 1, 2, 3);
```

The three integers are the minimum, preferred, and maximum version of `dependency` that `myfirst` is compatible with. The kernel refuses to load `myfirst` if `dependency` is not present or is outside the range.

For drivers that do not publish in-kernel symbols, `MODULE_DEPEND` is most often used to depend on a standard subsystem module:

```c
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
```

This declares that the USB version of `myfirst` needs the USB stack at version 1 exactly. The version numbers for subsystem modules are managed by the subsystem authors; a driver author finds the current values in the subsystem's header (for USB, `/usr/src/sys/dev/usb/usbdi.h`) or in another driver that already depends on it.

For `myfirst` at the end of Chapter 25, no `MODULE_DEPEND` is needed because the pseudo-driver does not require a subsystem. Chapter 26's USB chapter will add the first real `MODULE_DEPEND` when the driver is turned into a USB-attached version.

### A Worked Example: The 1.7 to 1.8 Transition

The Chapter 25 driver bumps three version identifiers at the end of the chapter:

- `MYFIRST_VERSION`: from `"1.7-integration"` to `"1.8-maintenance"`.
- `MODULE_VERSION(myfirst, N)`: from 17 to 18.
- `MYFIRST_IOCTL_VERSION`: stays at 1, because the ioctl additions in this chapter are pure additions (new commands, no removal, no semantic changes).

The `GETCAPS` ioctl is added with command number 5, which was previously unused. Older `myfirstctl` binaries, compiled against the Chapter 24 version of the header, do not know about `GETCAPS` and do not send it; they continue to work unchanged. New `myfirstctl` binaries, compiled against the Chapter 25 header, query `GETCAPS` at startup and behave accordingly.

The `MAINTENANCE.md` document gains a Change Log entry for 1.8:

```text
## 1.8-maintenance

- Added MYFIRSTIOC_GETCAPS (command 5) returning a capability
  bitmask.  Compatible with all earlier user-space programs.
- Added tunables hw.myfirst.timeout_sec, hw.myfirst.max_retries,
  hw.myfirst.log_ratelimit_pps.  Each has a matching writable
  sysctl under dev.myfirst.<unit>.
- Added rate-limited logging through ppsratecheck(9).
- No breaking changes from 1.7.
```

A user of the driver reading `MAINTENANCE.md` sees at a glance what changed and can evaluate whether they need to update their tools. A user who does not read `MAINTENANCE.md` can still query the capabilities at run time and discover the new features programmatically.

### Common Mistakes in Versioning

Three mistakes are common when first applying version discipline. Each is worth naming.

The first mistake is **reusing an ioctl number**. A number that was once assigned and later retired stays retired. A new command gets the next available number, not the number of a retired command. Reusing a number silently breaks older callers that had compiled in the old meaning; the compiler has no way to detect the conflict because the retired command's header was removed.

The second mistake is **bumping the version integer for every change**. If every patch bumps `MYFIRST_IOCTL_VERSION`, user-space tools have to rebuild constantly or the version check fails. The integer should change only for genuine breaking changes. Pure additions leave it alone.

The third mistake is **treating the release string as a semantic version**. The release string is for humans; it can be anything. The module version integer and the ioctl version integer are parsed by programs and should follow a discipline (monotonically increasing, bumped only for specific reasons). Confusing the two leads to confusing version numbers.

### Wrapping Up Section 4

Versioning is the discipline of evolving without breaking. A driver that keeps its three version identifiers distinct, its ioctl additions compatible, its deprecations announced, and its capability bits accurate gives its callers a stable target over the long life of the driver. The `myfirst` driver now has a working `GETCAPS` ioctl, a documented deprecation policy in `MAINTENANCE.md`, and three version identifiers that each change for their own reason. Everything a future developer needs to add a feature or retire a command is already in place.

In the next section, we turn from the driver's public surface to its private resource discipline. A driver that crashes on attach failure is a driver that cannot recover from any error. The labelled-goto pattern is how FreeBSD drivers make every allocation reversible.

## Section 5: Managing Resources in Failure Paths

Every attach routine is an ordered sequence of acquisitions. It allocates a lock, creates a cdev, hangs a sysctl tree on the device, perhaps registers an event handler or a timer, and on more complex drivers it allocates bus resources, maps I/O windows, attaches an interrupt, and sets up DMA. Each acquisition can fail. And each acquisition that succeeded before the failure must be released in the reverse order, or the kernel leaks memory, leaks a lock, leaks a cdev, and in the worst case keeps a device node alive with a stale pointer inside it.

The `myfirst` driver has been growing its attach path one section at a time since Chapter 17. Attach started small: a lock and a cdev. Chapter 24 added the sysctl tree. Chapter 25 is about to add rate-limit state, tunable-fetched defaults, and a counter or two. The order in which those resources are acquired now matters to the cleanup path. Every new acquisition has to know where in the unwind ordering it belongs, and the unwind itself has to be structured so that adding a new resource next week does not force a rewrite of the attach function.

Chapter 20 introduced the pattern informally; this section gives it a name, a vocabulary, and a discipline strong enough to survive the full Chapter 25 shape of `myfirst_attach`.

### The Problem: Nested `if` Paths Do Not Scale

The naive shape of an attach routine is a ladder of nested `if` statements. Each success condition contains the next step. Each failure returns. The problem is that every failure has to unwind whatever the previous steps already did, and the unwind code is duplicated at every level of the ladder:

```c
/*
 * Naive attach.  DO NOT WRITE DRIVERS THIS WAY.  This example shows
 * how the nested-if pattern forces duplicated cleanup at every level
 * and why it becomes unmaintainable as soon as a fourth resource is
 * added to the chain.
 */
static int
myfirst_attach_bad(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	sc->sc_dev = dev;
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error == 0) {
		myfirst_sysctl_attach(sc);
		if (myfirst_log_attach(sc) == 0) {
			/* all resources held; we succeeded */
			return (0);
		} else {
			/* log allocation failed: undo sysctl and cdev */
			/* but wait, sysctl is owned by Newbus, so skip it */
			destroy_dev(sc->sc_cdev);
			mtx_destroy(&sc->sc_mtx);
			return (ENOMEM);
		}
	} else {
		mtx_destroy(&sc->sc_mtx);
		return (error);
	}
}
```

Even in this small example the unwinding logic appears in two different places, the reader has to read a branch to know which resources have been acquired at each point, and adding a fourth resource forces another level of nesting and another duplicated cleanup block. Real drivers have seven or eight resources. A driver like `if_em` at `/usr/src/sys/dev/e1000/if_em.c` has over a dozen. Nested `if` is not an option there.

The failure modes of the nested pattern are not theoretical. A common bug pattern in older FreeBSD drivers was a missing `mtx_destroy` or a missing `bus_release_resource` in one of the cleanup branches: one branch destroyed the lock, another forgot. Every branch was a chance to make a mistake, and the bug only showed up when that specific failure fired, which meant it often did not show up until a customer reported a panic on a device that failed to attach.

### The `goto fail;` Pattern

FreeBSD's answer to the nested-cleanup problem is the labelled-goto pattern. The attach function is written as a linear sequence of acquisitions. Each acquisition that can fail is followed by a test that either falls through on success or jumps to a cleanup label on failure. The cleanup labels are ordered from most-acquired back to least-acquired. Each label releases the resources that were held at that point and then falls through to the next label. The function ends with a single `return (0)` on success and a single `return (error)` at the bottom of the cleanup chain:

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/* Resource 1: softc basics.  Cannot fail. */
	sc->sc_dev = dev;

	/* Resource 2: the lock.  Cannot fail on DEF mutex. */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/* Resource 3: the cdev.  Can fail. */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/* Resource 4: the sysctl tree.  Cannot fail (Newbus owns it). */
	myfirst_sysctl_attach(sc);

	/* Resource 5: the log state.  Can fail. */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	/* All resources held.  Announce and return. */
	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance ready\n");
	return (0);

fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

Read the function top to bottom. Each step is a resource acquisition. Each failure check is a two-line block: if the acquisition failed, jump to the label named after the previously acquired resource. The labels at the bottom release the resources in reverse order and fall through to the next label. The final `return (error)` returns the errno from whichever acquisition failed.

This shape scales. Adding a sixth resource means adding one acquisition block at the top, one `goto` target at the bottom, and one line of cleanup code. No nesting, no duplication, no branch tree. The same rule that governs the attach path governs every future addition to the attach path: acquire, test, go-to-the-previous-label, release in the reverse order.

### Why Linear Unwinding Is the Right Shape

The value of the labelled-goto pattern is not purely stylistic. It maps directly onto the structural property that the attach sequence is a stack of resources, and cleanup is the pop operation on that stack.

A stack has three properties that are easy to state and easy to violate. First, resources are released in the reverse order of acquisition. Second, a failed acquisition does not add a resource to the stack, so cleanup starts from the previously acquired resource, not from the one that just failed. Third, every resource on the stack is released exactly once: not zero times, not twice.

Each of these properties has a visible correlate in the `goto fail;` pattern. The cleanup labels appear in the file in the reverse order of the acquisitions: the last acquisition's cleanup label is at the top of the cleanup chain. A failed acquisition jumps to the label named after the previous acquisition, not after itself; the name of the label is literally the name of the resource that now has to be undone. And because each label falls through to the next, and each resource appears in exactly one label, every resource is freed exactly once on every failure path.

The stack discipline is what makes the pattern robust. If a reader wants to audit the cleanup path for correctness, they do not have to read branches. They just count the labels, count the acquisitions, and compare.

### Label Naming Conventions

Labels in FreeBSD drivers traditionally begin with `fail_` followed by the name of the resource that is about to be undone. The name of the resource matches the name of the field in the softc or the name of the function called to acquire it. Common patterns seen across the tree:

- `fail_mtx` undoes `mtx_init`
- `fail_sx` undoes `sx_init`
- `fail_cdev` undoes `make_dev_s`
- `fail_ires` undoes `bus_alloc_resource` for an IRQ
- `fail_mres` undoes `bus_alloc_resource` for a memory window
- `fail_intr` undoes `bus_setup_intr`
- `fail_dma_tag` undoes `bus_dma_tag_create`
- `fail_log` undoes a driver-private allocation (the rate-limit block in `myfirst`)

Some older drivers use numbered labels (`fail1`, `fail2`, `fail3`). Numbered labels are legal but inferior: adding a resource in the middle of the sequence forces renumbering every label after the insertion point, and the label numbers do not tell the reader which resource is being cleaned up. Named labels survive insertions gracefully and document themselves.

Whatever convention a driver picks, it should be consistent across all of its files. `myfirst` uses the `fail_<resource>` convention for every attach function from this chapter forward.

### The Rule of Falling Through

The single rule that every cleanup chain must obey is that each cleanup label falls through to the next. A stray `return` in the middle of the chain, or a missing label, skips cleanup for the resources that should have been released. The compiler does not warn about either mistake.

Consider what happens if a developer edits the cleanup chain and accidentally writes this:

```c
fail_cdev:
	destroy_dev(sc->sc_cdev);
	return (error);          /* BUG: skips mtx_destroy. */
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

The first `return` prevents the `mtx_destroy` from running on the `fail_cdev` path. The lock is leaked. The kernel's witness code will not complain, because the leaked lock is never acquired again. The leak persists until the machine reboots. It is invisible in normal operation and only shows up as a slow memory bloat on a system where the driver attaches and fails repeatedly (a hot-pluggable device, for example).

The way to prevent this kind of bug is to write the cleanup chain with a single `return` at the bottom and no intermediate returns. The labels in the middle contain only the cleanup call for their resource. Fall-through is the default and intended behaviour:

```c
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

A reader auditing the chain reads it as a simple list: destroy cdev, destroy lock, return. There is no branching to follow, and adding a label means adding a single line of cleanup code and optionally a single new target.

### What the Success Path Looks Like

The attach function succeeds with a single `return (0)`, placed immediately before the first cleanup label. This is the point at which every acquisition has succeeded and no cleanup is needed. The `return (0)` separates the acquisition chain from the cleanup chain visually: everything above it is acquisition, everything below it is cleanup.

Some drivers forget this separation and fall through from the last acquisition into the first cleanup label, freeing resources they just acquired. A stray missing `return (0)` is the simplest way to produce this bug:

```c
	/* Resource N: the final acquisition. */
	...

	/* Forgot to put a return here. */

fail_cdev:
	destroy_dev(sc->sc_cdev);
```

Without the `return (0)`, control falls through into `fail_cdev` after every successful attach, destroying the cdev on the success path. The driver then reports attach as failed because `error` is zero and the kernel sees the successful return, but the cdev it just created is gone. The result is a device node that disappears seconds after it appears. Debugging this requires noticing that the attach message prints but the device does not respond; not an easy bug to find in a busy log.

The defence is discipline. Every attach function ends its acquisition chain with `return (0);` on its own line, followed by a blank line, followed by the cleanup labels. No exceptions. A linter like `igor` or a reviewer's eye catches violations quickly when the shape is always the same.

### When an Acquisition Cannot Fail

Some acquisitions cannot fail. `mtx_init` for a default-style mutex cannot return an error. `sx_init` cannot. `callout_init_mtx` cannot. `SYSCTL_ADD_*` calls cannot return an error that a driver is expected to check (a failure there is a kernel-internal problem, not a driver problem).

For acquisitions that cannot fail, there is no goto. The acquisition is followed by the next step without a test. The cleanup label for the acquisition is still required, because the cleanup chain has to release the resource if a later acquisition fails:

```c
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	error = make_dev_s(&args, &sc->sc_cdev, ...);
	if (error != 0)
		goto fail_mtx;       /* undoes the lock. */
```

`fail_mtx` exists even though `mtx_init` itself had no failure path, because the lock still needs to be destroyed if anything below it fails.

The pattern holds: every acquired resource has a label, whether or not its acquisition can fail.

### Helpers to Reduce Duplication

When several acquisitions share the same shape (allocate, check, goto on error), it is tempting to hide them behind a helper function. The helper's job is to consolidate the acquisition and the check; the caller only sees a single `if (error != 0) goto fail_X;` line. This is fine as long as the helper follows the same discipline: on failure, it releases nothing that it acquired partially, and it returns a meaningful errno so the caller's goto target can rely on it.

In `myfirst`, Section 5's companion examples introduce a helper called `myfirst_log_attach` that allocates the rate-limit state, initialises its fields, and returns 0 on success or a non-zero errno on failure. The attach function calls it with a single line:

```c
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;
```

The helper itself follows the same pattern internally. If it allocates two resources and the second fails, the helper unwinds the first before returning. The caller sees the helper as a single atomic acquisition: it either fully succeeded or fully failed, and the caller never has to worry about the helper's intermediate state.

Helpers that are too eager to simplify, however, break the pattern. A helper that allocates a resource and stores it into the softc is fine. A helper that allocates a resource, stores it into the softc, and also releases it on error is not fine: the caller's cleanup label will also try to release it, leading to a double-free. The rule is that acquisition helpers either succeed and leave the resource in the softc, or they fail and leave the softc unchanged. They do not half-succeed.

### Detach as the Mirror of Attach

The detach routine is the cleanup chain of a successful attach. It has to release exactly the resources that attach acquired, in the reverse order. The shape of the detach function is the shape of the cleanup chain with the labels removed and the acquisitions deleted:

```c
static int
myfirst_detach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);

	/* Check for busy first. */
	mtx_lock(&sc->sc_mtx);
	if (sc->sc_open_count > 0) {
		mtx_unlock(&sc->sc_mtx);
		return (EBUSY);
	}
	mtx_unlock(&sc->sc_mtx);

	/* Release resources in the reverse order of attach. */
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	/* Sysctl is cleaned up by Newbus after detach returns. */
	mtx_destroy(&sc->sc_mtx);

	return (0);
}
```

Read side by side with the attach function, the correspondence is exact. Every resource named in attach has a release in detach. Every new acquisition added to attach has a matching addition in detach. A reviewer auditing a patch that adds a new resource to the driver should be able to find both additions in the diff, one in the attach chain and one in the detach chain; a diff that adds only to attach is incomplete.

A useful discipline when touching the attach chain is to open the detach function in an adjacent editor buffer and add the release immediately after adding the acquisition. This is the simplest way to make sure the two functions stay in sync: they are edited together as a single operation.

### Deliberate Failure Injection for Testing

A cleanup chain is correct only if every label is reachable. The only way to be sure is to fire each failure path on purpose and observe that the driver unloads cleanly afterwards. Waiting for real hardware failures to exercise the paths is not a strategy: most paths are never exercised in real life.

The tool for this kind of testing is deliberate failure injection. The developer adds a temporary `goto` or a temporary early return in the middle of the attach chain and confirms that the driver's resources are all released when the injected failure fires.

A minimal pattern for `myfirst`:

```c
#ifdef MYFIRST_DEBUG_INJECT_FAIL_CDEV
	error = ENOMEM;
	goto fail_cdev;
#endif
```

Compile the driver with `-DMYFIRST_DEBUG_INJECT_FAIL_CDEV` and load it. Attach returns `ENOMEM`. `kldstat` shows no residue. `dmesg` shows the attach failure and no kernel complaints about leaked locks or leaked resources. Unload the module, remove the define, recompile, and the driver is back to normal.

Do this once per label, in turn:

1. Inject a failure just after the lock is initialised. Confirm that only the lock is released.
2. Inject a failure just after the cdev is created. Confirm that the cdev and lock are released.
3. Inject a failure just after the sysctl tree is built. Confirm that the cdev and lock are released, and the sysctl OIDs disappear.
4. Inject a failure just after the log state is initialised. Confirm that every resource acquired to that point is released.

If any injection leaves a residue, the cleanup chain has a bug. Fix the bug, rerun the injection, and move on.

This is uncomfortable work the first time, and reassuring afterwards. A driver whose every failure path has been exercised once is a driver whose failure paths will keep working as the code evolves. A driver whose failure paths have never been exercised is a driver with latent bugs that will appear at the worst possible moment.

The companion example `ex05-failure-injection/` under `examples/part-05/ch25-advanced/` contains a version of `myfirst_attach` with every failure injection site marked by a commented `#define`. The lab at the end of the chapter walks through each injection in turn.

### A Complete `myfirst_attach` for Chapter 25

Putting all of Section 5 together with the Chapter 25 additions (log state, tunable fetches, capability bitmask), the final attach function looks like this:

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	struct make_dev_args args;
	int error;

	/*
	 * Stage 1: softc basics.  Cannot fail.  Recorded for consistency;
	 * no cleanup label is needed because no resource is held yet.
	 */
	sc->sc_dev = dev;

	/*
	 * Stage 2: lock.  Cannot fail on MTX_DEF, but needs a label
	 * because anything below this line can fail and must release it.
	 */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	/*
	 * Stage 3: pre-populate the softc with defaults, then allow
	 * boot-time tunables to override.  No allocations here, so no
	 * cleanup is needed.  Defaults come from the Section 3 tunable
	 * set.
	 */
	strlcpy(sc->sc_msg, "Hello from myfirst", sizeof(sc->sc_msg));
	sc->sc_msglen = strlen(sc->sc_msg);
	sc->sc_open_count = 0;
	sc->sc_total_reads = 0;
	sc->sc_total_writes = 0;
	sc->sc_debug = 0;
	sc->sc_timeout_sec = 5;
	sc->sc_max_retries = 3;
	sc->sc_log_pps = MYF_RL_DEFAULT_PPS;

	TUNABLE_INT_FETCH("hw.myfirst.debug_mask_default",
	    &sc->sc_debug);
	TUNABLE_INT_FETCH("hw.myfirst.timeout_sec",
	    &sc->sc_timeout_sec);
	TUNABLE_INT_FETCH("hw.myfirst.max_retries",
	    &sc->sc_max_retries);
	TUNABLE_INT_FETCH("hw.myfirst.log_ratelimit_pps",
	    &sc->sc_log_pps);

	/*
	 * Stage 4: cdev.  Can fail.  On failure, release the lock and
	 * return the error from make_dev_s.
	 */
	make_dev_args_init(&args);
	args.mda_devsw   = &myfirst_cdevsw;
	args.mda_uid     = UID_ROOT;
	args.mda_gid     = GID_WHEEL;
	args.mda_mode    = 0660;
	args.mda_si_drv1 = sc;
	args.mda_unit    = device_get_unit(dev);

	error = make_dev_s(&args, &sc->sc_cdev, "myfirst%d",
	    device_get_unit(dev));
	if (error != 0)
		goto fail_mtx;

	/*
	 * Stage 5: sysctl tree.  Cannot fail.  The framework owns the
	 * context, so no cleanup label is required specifically for it.
	 */
	myfirst_sysctl_attach(sc);

	/*
	 * Stage 6: rate-limit and counter state.  Can fail if memory
	 * allocation fails.  On failure, release the cdev and the lock.
	 */
	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	DPRINTF(sc, MYF_DBG_INIT,
	    "attach: version 1.8-maintenance complete\n");
	return (0);

fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

Every resource is accounted for. Every failure path is linear. The function has a single success return at the transition from acquisitions to cleanup, and a single failure return at the bottom of the cleanup chain. Adding a seventh resource next chapter is a three-line operation: one new acquisition block, one new label, one new cleanup line.

### Common Mistakes in Failure Paths

A few failure-path mistakes are worth naming once so they can be recognised when they appear in someone else's code or in a review.

The first mistake is **a missing label**. A developer adds a new resource acquisition but forgets to add its cleanup label. The compiler does not warn; the chain looks fine from the outside; but on failure after the new acquisition, the cleanup of everything below is skipped. The rule is that every acquisition has a label. Even if the acquisition cannot fail, it still needs a label so later acquisitions can reach it.

The second mistake is **freeing a resource twice**. A developer adds a local cleanup inside a helper and forgets that the caller's cleanup label also frees the resource. The helper frees once, the caller frees again, and either the kernel panics (for memory) or the witness code complains (for locks). The rule is that only one party owns each resource's cleanup. If the helper acquires the resource and stores it into the softc, the helper does not clean it up on the caller's behalf; it either succeeds or leaves the softc untouched.

The third mistake is **relying on `NULL` tests**. A developer writes a cleanup chain like this:

```c
fail_cdev:
	if (sc->sc_cdev != NULL)
		destroy_dev(sc->sc_cdev);
fail_mtx:
	if (sc->sc_mtx_initialised)
		mtx_destroy(&sc->sc_mtx);
```

The logic is: skip cleanup if the resource was not actually acquired. The intent is defensive; the effect is to hide bugs. If the `NULL` check is there because the cleanup might be reached in a state where the resource is not acquired, the chain is wrong: the goto target should be a different label. The correct behaviour is to make the cleanup label unreachable unless the resource was actually acquired. Labels that can be reached in either state are a symptom of a muddled acquisition order, and the `NULL` check only papers over it.

The fourth mistake is **using `goto` for non-error flow**. `goto` in the attach function is strictly for failure paths. A `goto` that skips a section of the acquisition chain on some non-error condition is a violation of the linear-cleanup invariant: the cleanup chain assumes that every label corresponds to a resource that has been acquired, and a `goto` that bypasses an acquisition breaks that assumption. If conditional acquisition is needed, use an `if` around the acquisition itself, not a `goto` around it.

### Wrapping Up Section 5

Attach and detach are the seams that hold a driver to the kernel. A correct attach is a linear stack of acquisitions; a correct detach is the stack popped in reverse. The labelled-goto pattern is how FreeBSD drivers encode that stack in C without buying into the kernels of other operating systems (C++ destructors, Go defer, Rust Drop). It is unglamorous and it scales: a driver with a dozen resources reads exactly like a driver with two, and the rules for adding a new resource are always the same.

The `myfirst` attach function now has four failure labels and a clean separation of acquisition, success return, and cleanup. Every new resource that Chapter 26 adds will fit into this shape.

In the next section, we step back from any single function and look at how a growing driver is spread across files. One `myfirst.c` with every function in it has carried us for eight chapters; it is time to split it into focused units so that the structure of the driver is visible at the file level.

## Section 6: Modularization and Separation of Concerns

By the end of Chapter 24 the `myfirst` driver has grown beyond what one source file can comfortably hold. The file shape was `myfirst.c` plus `myfirst_debug.c`, `myfirst_ioctl.c`, and `myfirst_sysctl.c`; `myfirst.c` still carried the cdevsw, the read/write callbacks, the open/close callbacks, the attach and detach routines, and the module glue. That was fine for teaching, because every addition landed in a file small enough for a reader to hold in their head. It is no longer fine for a driver that has an ioctl surface, a sysctl tree, a debug framework, a rate-limited logging helper, a capability bitmask, a versioning discipline, and a labelled-cleanup attach routine. A file with that much in it becomes painful to read, painful to diff, and painful to hand to a new contributor.

Section 6 is about the other direction. It does not introduce new behaviour; every function that exists at the end of Section 5 is still here at the end of Section 6. What changes is the file layout and the boundary lines between pieces. The goal is a driver whose structure you can understand from `ls`, and whose individual files each answer a single question.

### Why Split Files

The temptation with a self-contained driver is to keep everything in one file. A single `myfirst.c` is easy to locate, easy to grep, easy to copy into a tarball. Splitting feels like bureaucracy. The argument for splitting appears when the driver crosses one of three thresholds.

The first threshold is **comprehension**. A reader who opens `myfirst.c` should be able to find what they are looking for in a few seconds. A 1200-line file with eight unrelated responsibilities is hard to navigate; the reader has to scroll past the cdevsw to find the sysctl, past the sysctl to find the ioctl, past the ioctl to find the attach routine. Each time they switch subject, they have to reload their mental context. With separate files, the subject is the filename: `myfirst_ioctl.c` is about ioctls, `myfirst_sysctl.c` is about sysctls, `myfirst.c` is about lifecycle.

The second threshold is **independence**. Two unrelated changes should not modify the same file. When a developer adds a sysctl and another developer adds an ioctl, their patches should not compete for the same lines of `myfirst.c`. Small, focused files let two changes land in parallel with no merge conflict and no risk that a bug in one change accidentally touches the other.

The third threshold is **testability and reuse**. A driver's logging infrastructure, its ioctl dispatch, and its sysctl tree are often useful to more than one driver inside the same project. Keeping them in separate files with clean interfaces makes them candidates for sharing later. A driver that lives in a single file cannot easily share anything; extraction means copying and manually renaming, which is an error-prone operation.

`myfirst` at the end of Chapter 25 has crossed all three thresholds. Splitting the file is the maintenance act that keeps the driver healthy for the next ten chapters.

### A File Layout for `myfirst`

The proposed layout is the one the Makefile in the final Chapter 25 examples directory uses:

```text
myfirst.h          - public types and constants (softc, SRB, status bits).
myfirst.c          - module glue, cdevsw, devclass, module events.
myfirst_bus.c      - Newbus methods and device_identify.
myfirst_cdev.c     - open/close/read/write callbacks; no ioctl.
myfirst_ioctl.h    - ioctl command numbers and payload structures.
myfirst_ioctl.c    - myfirst_ioctl switch and helpers.
myfirst_sysctl.c   - myfirst_sysctl_attach and handlers.
myfirst_debug.h    - DPRINTF/DLOG/DLOG_RL macros and class bits.
myfirst_debug.c    - debug-class enumeration (if any out-of-line).
myfirst_log.h      - rate-limit state structure.
myfirst_log.c      - myfirst_log_attach/detach and helpers.
```

Seven `.c` files and four `.h` files. Each `.c` file has a subject matter named by its filename. The headers declare the interfaces that cross file boundaries. No file imports internals of another file; every cross-file reference goes through a header.

At first sight this looks like more files than the driver needs. It is not. Each file has a specific responsibility, and the header that goes with it is one to three dozen lines of declarations. The cumulative size is the same as the single-file version; the structure is dramatically clearer.

### The Single-Responsibility Rule

The rule that governs the split is a single-responsibility rule: each file answers one question about the driver.

- `myfirst.c` answers: how does this module attach to the kernel and wire its pieces together?
- `myfirst_bus.c` answers: how does Newbus discover and instantiate my driver?
- `myfirst_cdev.c` answers: how does the driver serve open/close/read/write?
- `myfirst_ioctl.c` answers: how does the driver handle the commands its header declares?
- `myfirst_sysctl.c` answers: how does the driver expose its state to `sysctl(8)`?
- `myfirst_debug.c` answers: how are debug messages classified and rate-limited?
- `myfirst_log.c` answers: how is rate-limit state initialised and released?

The test for whether a change belongs in a given file is the answer test. If the change does not answer the file's question, it belongs elsewhere. A new sysctl does not belong in `myfirst_ioctl.c`; a new ioctl does not belong in `myfirst_sysctl.c`; a new read callback variant does not belong in `myfirst.c`. The rule is explicit, and a reviewer applying it rejects patches that put things in the wrong file.

Applying the rule to the existing Chapter 24 shape gives the Chapter 25 shape.

### Public vs Private Headers

Headers carry the interface between files. A driver that splits its `.c` files has to decide, for each declaration, whether it belongs in a public header or a private one.

**Public headers** contain types and constants that are visible to more than one `.c` file. `myfirst.h` is the main public header for the driver. It declares:

- The `struct myfirst_softc` definition (every `.c` file needs it).
- Constants that appear in more than one file (debug-class bits, softc field sizes).
- Prototypes for functions that are called across file boundaries (`myfirst_sysctl_attach`, `myfirst_log_attach`, `myfirst_log_ratelimited_printf`, `myfirst_ioctl`).

**Private headers** carry declarations that are only needed by one `.c` file. `myfirst_ioctl.h` is the canonical example. It declares the command numbers and payload structures; they are needed by `myfirst_ioctl.c` and by user-space callers, but no other in-kernel file needs them. Putting them in `myfirst.h` would leak the wire format into every translation unit.

The distinction matters because every public declaration is a contract the driver has to honour. A type in `myfirst.h` that changes size breaks every file that includes `myfirst.h`. A type in `myfirst_ioctl.h` that changes size breaks only `myfirst_ioctl.c` and the user-space tools that compiled against it.

For `myfirst` at the end of Chapter 25, the public header `myfirst.h` looks like this (trimmed to the declarations relevant to this section):

```c
/*
 * myfirst.h - public types and constants for the myfirst driver.
 *
 * Types and prototypes declared here are visible to every .c file in
 * the driver.  Keep this header small.  Wire-format declarations live
 * in myfirst_ioctl.h.  Debug macros live in myfirst_debug.h.  Rate-
 * limit state lives in myfirst_log.h.
 */

#ifndef _MYFIRST_H_
#define _MYFIRST_H_

#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

#include "myfirst_log.h"

struct myfirst_softc {
	device_t       sc_dev;
	struct mtx     sc_mtx;
	struct cdev   *sc_cdev;

	char           sc_msg[MYFIRST_MSG_MAX];
	size_t         sc_msglen;

	u_int          sc_open_count;
	u_int          sc_total_reads;
	u_int          sc_total_writes;

	u_int          sc_debug;
	u_int          sc_timeout_sec;
	u_int          sc_max_retries;
	u_int          sc_log_pps;

	struct myfirst_ratelimit sc_rl_generic;
	struct myfirst_ratelimit sc_rl_io;
	struct myfirst_ratelimit sc_rl_intr;
};

#define MYFIRST_MSG_MAX  256

/* Sysctl tree. */
void myfirst_sysctl_attach(struct myfirst_softc *);

/* Rate-limit state. */
int  myfirst_log_attach(struct myfirst_softc *);
void myfirst_log_detach(struct myfirst_softc *);

/* Ioctl dispatch. */
struct thread;
int  myfirst_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);

#endif /* _MYFIRST_H_ */
```

Nothing in `myfirst.h` references a wire-format constant, a debug-class bit, or a rate-limit structure internals. The softc includes three rate-limit fields by value, so `myfirst.h` has to include `myfirst_log.h`, but the internals of `struct myfirst_ratelimit` live in `myfirst_log.h` and are not exposed here.

### The Anatomy of `myfirst.c` After the Split

`myfirst.c` after the split is the shortest `.c` file in the driver. It contains the cdevsw table, the module event handler, the device class declaration, and the attach/detach routines. Every other responsibility has moved elsewhere:

```c
/*
 * myfirst.c - module glue and cdev wiring for the myfirst driver.
 *
 * This file owns the cdevsw table, the devclass, the attach and
 * detach routines, and the MODULE_VERSION declaration.  The cdev
 * callbacks themselves live in myfirst_cdev.c.  The ioctl dispatch
 * lives in myfirst_ioctl.c.  The sysctl tree lives in
 * myfirst_sysctl.c.  The rate-limit infrastructure lives in
 * myfirst_log.c.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"
#include "myfirst_ioctl.h"

MODULE_VERSION(myfirst, 18);

extern d_open_t    myfirst_open;
extern d_close_t   myfirst_close;
extern d_read_t    myfirst_read;
extern d_write_t   myfirst_write;

struct cdevsw myfirst_cdevsw = {
	.d_version = D_VERSION,
	.d_name    = "myfirst",
	.d_open    = myfirst_open,
	.d_close   = myfirst_close,
	.d_read    = myfirst_read,
	.d_write   = myfirst_write,
	.d_ioctl   = myfirst_ioctl,
};

static int
myfirst_attach(device_t dev)
{
	/* Section 5's labelled-cleanup attach goes here. */
	...
}

static int
myfirst_detach(device_t dev)
{
	/* Section 5's mirror-of-attach detach goes here. */
	...
}

static device_method_t myfirst_methods[] = {
	DEVMETHOD(device_probe,   myfirst_probe),
	DEVMETHOD(device_attach,  myfirst_attach),
	DEVMETHOD(device_detach,  myfirst_detach),
	DEVMETHOD_END
};

static driver_t myfirst_driver = {
	"myfirst",
	myfirst_methods,
	sizeof(struct myfirst_softc),
};

DRIVER_MODULE(myfirst, nexus, myfirst_driver, 0, 0);
```

The file has one job: wire the driver's pieces together at the kernel level. It is a few hundred lines; every other file in the driver is smaller.

### `myfirst_cdev.c`: The Character Device Callbacks

The open, close, read, and write callbacks were the first code we wrote back in Chapter 18. They have grown since then. Extracting them to `myfirst_cdev.c` keeps them with each other and out of `myfirst.c`:

```c
/*
 * myfirst_cdev.c - character-device callbacks for the myfirst driver.
 *
 * The open/close/read/write callbacks all operate on the softc that
 * make_dev_s installed as si_drv1.  The ioctl dispatch is in
 * myfirst_ioctl.c; this file intentionally does not handle ioctls.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

int
myfirst_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;

	mtx_lock(&sc->sc_mtx);
	sc->sc_open_count++;
	mtx_unlock(&sc->sc_mtx);

	DPRINTF(sc, MYF_DBG_OPEN, "open: count %u\n", sc->sc_open_count);
	return (0);
}

/* close, read, write follow the same pattern. */
```

Each callback begins with `sc = dev->si_drv1` (the per-cdev pointer that `make_dev_args` set) and operates on the softc. No cross-file coupling beyond the public header.

### `myfirst_ioctl.c`: The Command Switch

`myfirst_ioctl.c` has been in its own file since Chapter 22. The Chapter 25 addition is the `MYFIRSTIOC_GETCAPS` handler:

```c
int
myfirst_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct myfirst_softc *sc = dev->si_drv1;
	int error = 0;

	switch (cmd) {
	case MYFIRSTIOC_GETVER:
		*(int *)data = MYFIRST_IOCTL_VERSION;
		break;
	case MYFIRSTIOC_RESET:
		mtx_lock(&sc->sc_mtx);
		sc->sc_total_reads  = 0;
		sc->sc_total_writes = 0;
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_GETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy((char *)data, sc->sc_msg, MYFIRST_MSG_MAX);
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_SETMSG:
		mtx_lock(&sc->sc_mtx);
		strlcpy(sc->sc_msg, (const char *)data, MYFIRST_MSG_MAX);
		sc->sc_msglen = strlen(sc->sc_msg);
		mtx_unlock(&sc->sc_mtx);
		break;
	case MYFIRSTIOC_GETCAPS:
		*(uint32_t *)data = MYF_CAP_RESET | MYF_CAP_GETMSG |
		                    MYF_CAP_SETMSG;
		break;
	default:
		error = ENOIOCTL;
		break;
	}
	return (error);
}
```

The switch is the entire public ioctl surface of the driver. Adding a command means adding a case; retiring one means deleting a case and deprecating the constant in `myfirst_ioctl.h`.

### `myfirst_log.h` and `myfirst_log.c`: Rate-Limited Logging

Section 1 introduced the rate-limited logging macro `DLOG_RL` and the `struct myfirst_ratelimit` state it tracks. The rate-limit state was left embedded in the softc in Section 1 because the abstraction had not yet been factored out. Section 6 is the right moment to factor it out: the rate-limit code is small enough to be worth collecting in one place and general enough that other drivers might want it.

`myfirst_log.h` contains the state definition:

```c
#ifndef _MYFIRST_LOG_H_
#define _MYFIRST_LOG_H_

#include <sys/time.h>

struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define MYF_RL_DEFAULT_PPS  10

#endif /* _MYFIRST_LOG_H_ */
```

`myfirst_log.c` contains the attach and detach helpers:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include "myfirst.h"
#include "myfirst_debug.h"

int
myfirst_log_attach(struct myfirst_softc *sc)
{
	/*
	 * The rate-limit state is embedded by value in the softc, so
	 * there is no allocation to do.  This function exists so that
	 * the attach chain has a named label for logging in case a
	 * future version needs per-class allocations.
	 */
	bzero(&sc->sc_rl_generic, sizeof(sc->sc_rl_generic));
	bzero(&sc->sc_rl_io,      sizeof(sc->sc_rl_io));
	bzero(&sc->sc_rl_intr,    sizeof(sc->sc_rl_intr));

	return (0);
}

void
myfirst_log_detach(struct myfirst_softc *sc)
{
	/* Nothing to release; the state is embedded in the softc. */
	(void)sc;
}
```

Today `myfirst_log_attach` does no allocation; it zeroes the rate-limit fields and returns. Tomorrow, if the driver needs a dynamic array of per-class counters, the allocation fits here and the attach chain does not have to change. This is the value of extracting the helper before it is strictly necessary: the shape is ready for growth.

The header file size matters here. `myfirst_log.h` is under 20 lines. A header of 20 lines is cheap to include everywhere, cheap to read, and cheap to keep in sync. If `myfirst_log.h` grew to 200 lines, the cost of including it from every `.c` file would start to show up in compile times and in review friction; at that point the next step is to split it again.

### The Updated Makefile

The Makefile for the split driver lists every `.c` file:

```makefile
# Makefile for the myfirst driver - Chapter 25 (1.8-maintenance).
#
# Chapter 25 splits the driver into subject-matter files.  Each file
# answers a single question; the Makefile lists them in alphabetical
# order after myfirst.c (which carries the module glue) so the
# reader sees the main file first.

KMOD=	myfirst
SRCS=	myfirst.c myfirst_cdev.c myfirst_debug.c myfirst_ioctl.c \
	myfirst_log.c myfirst_sysctl.c

CFLAGS+=	-I${.CURDIR}

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
```

`SRCS` lists six `.c` files, one per subject. Adding a seventh is a one-line change. The kernel build system picks up every file in `SRCS` automatically; there is no manual linking step, no makefile dependency tree to maintain.

### Where to Draw Each File Boundary

The hardest part of splitting a driver is not deciding to split; it is deciding where the boundaries go. Most splits go through three phases, and the phases apply to any driver, not just `myfirst`.

**Phase one** is the flat file. Everything is in `driver.c`. This is the right shape for the first 300 lines of a driver. Splitting sooner creates more friction than it saves.

**Phase two** is the subject split. The ioctl dispatch goes into `driver_ioctl.c`, the sysctl tree goes into `driver_sysctl.c`, the debug infrastructure goes into `driver_debug.c`. Each file is named after the subject it handles. This is where `myfirst` has been since Chapter 24.

**Phase three** is the subsystem split. As the driver grows, one subject grows larger than a single file. The ioctl file splits into `driver_ioctl.c` (the dispatch) and `driver_ioctl_rw.c` (the read/write payload helpers). The sysctl file splits similarly. This is where a full-featured driver ends up, often in its third or fourth major version.

`myfirst` at the end of Chapter 25 is solidly in phase two. Phase three is not warranted yet, and Chapter 26 will reset the clock when it splits the pseudo-driver into a USB-attached variant and leaves `myfirst_core.c` as the subject-agnostic core. There is no value in pre-emptively splitting to phase three today.

The rule of thumb for when to move from phase two to phase three is: when a single subject file crosses 1,000 lines, or when two unrelated changes to the same subject file cause a merge conflict, the subject is ready to split.

### The Include Graph and Build Ordering

Once a driver is split into several files, the include graph matters. A circular include is not a hard error in C, but it is a sign of a muddled dependency structure that will confuse readers. The right shape is a directed acyclic graph of headers, rooted at `myfirst.h`, with leaf headers like `myfirst_ioctl.h` and `myfirst_log.h`.

`myfirst.h` is the widest header. It declares the softc and prototypes that every other file uses. It includes `myfirst_log.h` because the softc has rate-limit fields by value.

`myfirst_debug.h` is a leaf. It declares the `DPRINTF` macro family and the class bits. It is included by every `.c` file, directly or indirectly. It is not included by `myfirst.h`, because `myfirst.h` should not force the debug macros into any caller that does not want them.

`myfirst_ioctl.h` is a leaf. It declares command numbers, payload structures, and the wire-format version integer. It is included by `myfirst_ioctl.c` (and its user-space counterpart `myfirstctl.c`).

No header includes a file other than public kernel headers and the driver's own headers. No `.c` file includes another `.c` file. The include graph is shallow and easy to diagram.

### The Cost of Splitting

Splitting files has a real cost. Every split adds a header file, and every header file has to be maintained. A function whose signature changes has to be updated in the `.c` and in the `.h`, and the change has to propagate to every other `.c` file that includes the header. A driver with twelve files is slightly slower to compile than a driver with one, because every `.c` has to include several headers and the preprocessor has to parse them.

These costs are real but small. They are much smaller than the cost of a monolithic file that nobody wants to touch. The rule is to split when the cost of not splitting exceeds the cost of maintaining the boundary. For `myfirst` at the end of Chapter 25, the threshold has been crossed.

### A Practical Procedure for Splitting a Real Driver

Splitting a file is a routine refactor, but a routine refactor can still introduce bugs if done carelessly. A practical procedure for splitting an in-tree driver is:

1. **Identify the subjects.** Read the monolithic file top to bottom and group its functions by subject (cdev, ioctl, sysctl, debug, lifecycle). Write the grouping on a piece of paper or in a comment block.

2. **Create the empty files.** Add the new `.c` files and their headers to the source tree. Compile once to make sure the build system sees them.

3. **Move one subject at a time.** Move the ioctl functions into `driver_ioctl.c`. Move their declarations into `driver_ioctl.h`. Update `driver.c` to `#include "driver_ioctl.h"`. Compile. Run the driver through its test matrix.

4. **Commit each subject split.** Each subject move is a single commit. The commit log reads: "myfirst: split ioctl dispatch into driver_ioctl.c". A reviewer can see the move clearly; a `git blame` shows the same line in the new file on the same commit.

5. **Verify the include graph.** After all subjects are moved, compile with `-Wunused-variable` and `-Wmissing-prototypes` to catch functions that should have had prototypes but did not. Use `nm` on the built module to confirm that no symbols that should be `static` are being exported.

6. **Retest.** Run the full driver test matrix. Splitting a file should not change behaviour; if a test starts failing, the split has introduced a bug.

The procedure for `myfirst` at the end of Chapter 25 follows exactly these steps. The final directory under `examples/part-05/ch25-advanced/` is the result.

### Common Mistakes When Splitting

A few mistakes are common the first time a driver is split. Watching for them shortens the learning curve.

The first mistake is **putting declarations in the wrong header**. If `myfirst.h` declares a function that is only called from `myfirst_ioctl.c`, every other translation unit is paying to parse a declaration it does not need. If `myfirst_ioctl.h` declares a function that is called from both `myfirst_ioctl.c` and `myfirst_cdev.c`, the two consumers are coupled through the ioctl header and any change in the ioctl header rebuilds both files. The fix is to put cross-cutting declarations in `myfirst.h` and subject-specific declarations in subject-specific headers.

The second mistake is **forgetting `static` on functions that should be file-scope**. A function that is used only inside `myfirst_sysctl.c` should be declared `static`. Without `static`, the function is exported from the object file, which means another file could accidentally call it, and any later renaming in the original file becomes an ABI change. A `static` discipline prevents this entire class of problem.

The third mistake is **circular includes**. If `myfirst_ioctl.h` includes `myfirst.h`, and `myfirst.h` includes `myfirst_ioctl.h`, the driver compiles (thanks to include guards) but the dependency graph is wrong. Every edit to either file now triggers a rebuild of everything that includes either one. The fix is to decide which header sits higher in the graph and remove the back-reference.

The fourth mistake is **re-introducing a subject into the wrong file**. Six months after the split, someone adds a new ioctl by editing `myfirst.c` because that was where ioctls used to live. The single-responsibility rule has to be enforced by reviewers. A patch that puts a new ioctl in `myfirst.c` is rejected with a comment pointing at `myfirst_ioctl.c`.

### Wrapping Up Section 6

A driver whose files each answer one question is a driver that you can hand to a new contributor on their first day. They read the filenames, they pick a subject, and they start editing exactly one file. The `myfirst` driver has crossed that threshold. Six `.c` files plus their headers hold every function the driver has grown since Chapter 17, with each file named for what it does.

In the next section, we turn from internal organisation to external preparation. A driver that is ready for production has a short list of properties that it must satisfy before it can be installed on a machine the developer does not own. Chapter 25's production-readiness checklist names those properties and walks `myfirst` through each one.

## Section 7: Preparing for Production Use

A driver that works on your workstation is not a driver that is ready for production. Production is the set of conditions your code faces when it is installed on hardware you do not own, booted by operators you will never meet, and expected to behave predictably for months or years between reboots. The distance between "it works for me" and "it is ready to ship" is measured in habits, not in features. This section names the habits.

`myfirst` in its Chapter 25 shape is as feature-complete as the pseudo-driver is going to get. The remaining work is not to add functionality, but to harden the edges so the driver survives the environments it cannot control.

### The Production Readiness Mindset

The mindset shift is this: every decision the driver makes implicitly at development time has to be made explicitly at production time. Where a tunable has a default, the default has to be the right default. Where a sysctl is writable, the consequences of a write by a scared operator at 3 a.m. have to be safe. Where a log message might fire, the message has to be useful without help from the developer. Where a module depends on another module, the dependency has to be declared so the loader does not load them in the wrong order.

Production-readiness is not a one-shot action; it is an attitude that runs through every decision. A driver that is nearly ready for production often has one or two specific gaps: a tunable with no documentation, a log message that fires every microsecond, a detach path that assumes nobody is using the device. The discipline of production-readiness is to find those specific gaps and close them, one by one, until the driver's behaviour is predictable on a machine the developer is not standing in front of.

### Declaring Module Dependencies

The first production habit is to be explicit about what the module needs. If `myfirst` calls a function that lives in another kernel module, the kernel's module loader needs to know about the dependency before the call, or the kernel loads `myfirst` and panics the first time the dependency is used.

The mechanism is `MODULE_DEPEND`. Section 4 introduced it as a compatibility tool; in production, it is also a correctness tool. A driver without `MODULE_DEPEND` on its real dependencies works by accident in most boot orderings and fails mysteriously in others. A driver with `MODULE_DEPEND` on every real dependency either loads correctly or refuses to load with a clear error message.

For the pseudo-driver `myfirst`, there are no real dependencies yet; the driver only uses symbols from the kernel core, which is always present. The Chapter 26 USB variant will add the first real `MODULE_DEPEND`:

```c
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
```

The three version numbers are the minimum, preferred, and maximum of the USB stack version that `myfirst_usb` is compatible with. At load time, the kernel checks the installed USB stack version against this range and refuses to load `myfirst_usb` if the USB stack is missing or outside the range.

The production habit is: before shipping, grep the driver for every symbol it calls and confirm that every symbol lives either in the kernel core or in a module that the driver declares a dependency on. A missing `MODULE_DEPEND` works until the boot ordering changes, and then the driver panics on production hardware.

### Publishing PNP Information

For hardware drivers, the kernel's module loader consults each module's PNP metadata to decide which driver handles which device. A USB driver that does not publish PNP information works when loaded manually and fails when the boot loader tries to auto-load the driver for a newly-plugged device. The fix is `MODULE_PNP_INFO`, which the driver uses to declare the vendor/product identifiers it handles:

```c
MODULE_PNP_INFO("U16:vendor;U16:product", uhub, myfirst_usb,
    myfirst_pnp_table, nitems(myfirst_pnp_table));
```

The first string describes the format of the PNP table entries. `uhub` is the bus name; `myfirst_usb` is the driver name; `myfirst_pnp_table` is a static array of structures, one per device the driver handles.

`myfirst` at Chapter 25 is still a pseudo-driver and has no hardware to match. `MODULE_PNP_INFO` comes into play in Chapter 26 with the first real hardware attach. For Chapter 25, the production habit is simply to know the macro exists and to plan for it when hardware arrives.

### The `MOD_QUIESCE` Event

The kernel module event handler is called with one of four events: `MOD_LOAD`, `MOD_UNLOAD`, `MOD_SHUTDOWN`, `MOD_QUIESCE`. Most drivers handle `MOD_LOAD` and `MOD_UNLOAD` explicitly, and the kernel synthesises the other two. For production drivers, `MOD_QUIESCE` deserves attention.

`MOD_QUIESCE` is the kernel's question "can you be unloaded right now?" It fires before `MOD_UNLOAD` and gives the driver a chance to refuse cleanly. A driver that is mid-operation (an outstanding DMA transfer, an open file descriptor, a pending timer) can return a non-zero errno from `MOD_QUIESCE` to refuse the unload; the kernel then does not proceed to `MOD_UNLOAD`.

For `myfirst`, the quiesce check is already built into `myfirst_detach`: if `sc_open_count > 0`, detach returns `EBUSY`. The kernel's module loader propagates that `EBUSY` back to `kldunload(8)`, and the operator sees "module myfirst is busy". The check is in the right place, but the discipline of thinking about `MOD_QUIESCE` separately from `MOD_UNLOAD` is worth naming: `MOD_QUIESCE` is the "are you safe to unload?" question, and `MOD_UNLOAD` is the "go ahead and unload" command. Some drivers have state that is safe to check in `MOD_QUIESCE` but not safe to acquire in `MOD_UNLOAD`; splitting them lets the driver answer the question without side effects.

### Emitting `devctl_notify` Events

Long-running production systems are monitored by daemons like `devd(8)`, which watch for device arrivals, departures, and state changes. The mechanism the kernel uses to notify `devd` is `devctl_notify(9)`: a driver emits a structured event, `devd` reads the event, and `devd` takes a configured action (run a script, log a message, notify an operator).

The prototype is:

```c
void devctl_notify(const char *system, const char *subsystem,
    const char *type, const char *data);
```

- `system` is a top-level category like `"DEVFS"`, `"ACPI"`, or a driver-specific tag.
- `subsystem` is the driver or subsystem name.
- `type` is a short event name.
- `data` is optional structured data (key=value pairs) for the daemon to parse.

For `myfirst`, a useful production event is "the in-driver message was rewritten":

```c
devctl_notify("myfirst", device_get_nameunit(sc->sc_dev),
    "MSG_CHANGED", NULL);
```

After the operator writes a new message through `ioctl(fd, MYFIRSTIOC_SETMSG, buf)`, the driver emits a `MSG_CHANGED` event. A `devd` rule can match the event and, for example, send a syslog entry or notify a monitoring daemon:

```text
notify 0 {
    match "system"    "myfirst";
    match "type"      "MSG_CHANGED";
    action "logger -t myfirst 'message changed on $subsystem'";
};
```

The production habit here is to ask, for each interesting event in the driver, whether an operator might want to react to it. If the answer is yes, emit a `devctl_notify` with a well-chosen name. Downstream tooling can then build on the event, and the driver does not have to know what those tools are.

### Writing a `MAINTENANCE.md`

Every production driver should have a maintenance file that describes, in plain English, what the driver does, what tunables it accepts, what sysctls it exposes, what ioctls it handles, what events it emits, and what the version history is. The file lives alongside the source code in the repository; it is read by operators, by new developers, by security reviewers, and by the author six months later.

A concrete skeleton for `MAINTENANCE.md`:

```text
# myfirst

A demonstration character driver that carries the book's running
example.  This file is the operator-facing reference.

## Overview

myfirst registers a pseudo-device at /dev/myfirst0 and serves a
read-write message buffer, a set of ioctls, a sysctl tree, and a
configurable debug-class logger.

## Tunables

- hw.myfirst.debug_mask_default (int, default 0)
    Initial value of dev.myfirst.<unit>.debug.mask.
- hw.myfirst.timeout_sec (int, default 5)
    Initial value of dev.myfirst.<unit>.timeout_sec.
- hw.myfirst.max_retries (int, default 3)
    Initial value of dev.myfirst.<unit>.max_retries.
- hw.myfirst.log_ratelimit_pps (int, default 10)
    Initial rate-limit ceiling (prints per second per class).

## Sysctls

All sysctls live under dev.myfirst.<unit>.

Read-only: version, open_count, total_reads, total_writes,
message, message_len.

Read-write: debug.mask (mirror of debug_mask_default), timeout_sec,
max_retries, log_ratelimit_pps.

## Ioctls

Defined in myfirst_ioctl.h.  Command magic 'M'.

- MYFIRSTIOC_GETVER (0): returns MYFIRST_IOCTL_VERSION.
- MYFIRSTIOC_RESET  (1): zeros read/write counters.
- MYFIRSTIOC_GETMSG (2): reads the in-driver message.
- MYFIRSTIOC_SETMSG (3): writes the in-driver message.
- MYFIRSTIOC_GETCAPS (5): returns MYF_CAP_* bitmask.

Command 4 was reserved during Chapter 23 draft work and retired
before release.  Do not reuse the number.

## Events

Emitted through devctl_notify(9).

- system=myfirst subsystem=<unit> type=MSG_CHANGED
    The operator-visible message was rewritten.

## Version History

See Change Log below.

## Change Log

### 1.8-maintenance
- Added MYFIRSTIOC_GETCAPS (command 5).
- Added tunables for timeout_sec, max_retries, log_ratelimit_pps.
- Added rate-limited logging via ppsratecheck(9).
- Added devctl_notify for MSG_CHANGED.
- No breaking changes from 1.7.

### 1.7-integration
- First end-to-end integration of ioctl, sysctl, debug.
- Introduced MYFIRSTIOC_{GETVER,RESET,GETMSG,SETMSG}.

### 1.6-debug
- Added DPRINTF framework and SDT probes.
```

The file is nothing glamorous. It is a reference that is kept up to date with every version bump and that serves as the single source of truth for operators.

The production habit is: every change to the driver's visible surface (a new tunable, a new sysctl, a new ioctl, a new event, a behaviour change) has a corresponding entry in `MAINTENANCE.md`. The file never falls behind the code. A driver whose `MAINTENANCE.md` is out of date is a driver whose users are guessing; a driver whose `MAINTENANCE.md` is current is a driver whose users can self-serve.

### A `devd` Rule Set

`devd(8)` rules tell the daemon how to react to kernel events. For a production deployment of `myfirst`, a minimal rule set would ensure that important events reach the operator:

```console
# /etc/devd/myfirst.conf
#
# devd rules for the myfirst driver.  Drop this file into
# /etc/devd/ and restart devd(8) for the rules to take effect.

notify 0 {
    match "system"    "myfirst";
    match "type"      "MSG_CHANGED";
    action "logger -t myfirst 'message changed on $subsystem'";
};

# Future: match attach/detach events once Chapter 26's USB variant
# starts emitting them.
```

The file is short. It declares one rule, matches a specific event, and takes a specific action. In production, such files grow to match more events, trigger more actions, and in some deployments notify a monitoring system that watches for driver anomalies.

Including a draft `devd.conf` in the driver's repository makes it easy for an operator to adopt. They copy the file, adjust the actions, and the driver's events are integrated into the site's monitoring on the first day.

### Logs: Friend of the Support Engineer

A production driver's log messages are read by support engineers who do not have access to the source code and cannot reproduce the problem on demand. The rules that make a log message useful to a support engineer are different from the rules that make a log message useful to a developer.

A developer reading their own log message can rely on context that a support engineer does not have. The support engineer cannot ask "which attach?" or "which device?" or "what was `error` when this fired?" The answer has to already be in the message.

The production habit is to audit every log message in the driver and ask three questions:

1. **Does the message name its device?** `device_printf(dev, ...)` prefixes the output with the device nameunit; bare `printf` does not. Every message that is not from `MOD_LOAD` (where there is no device yet) should be `device_printf`.

2. **Does the message include the relevant numeric context?** "Failed to allocate" is not useful. "Failed to allocate: error 12 (ENOMEM)" is. "Failed to allocate a timer: error 12" is better.

3. **Does the message appear at the appropriate rate?** Section 1 covered rate-limiting. The final pass is to ensure that every message that can fire in a loop is either rate-limited or is demonstrably one-shot.

A log message that satisfies these three questions reaches a support engineer with enough information to file a useful bug report. A log message that fails any of them wastes the operator's time and the developer's.

### Handling Bus Attach/Detach Gracefully

Production drivers, especially hot-pluggable ones, have to handle repeated attach and detach cycles without leaks. The discipline of Section 5's labelled-cleanup pattern is part of the answer; the other part is to confirm that repeated attach/detach actually works. The lab at the end of this chapter walks through a regression script that loads, unloads, and reloads the driver 100 times in a row and verifies that the module's memory footprint does not grow.

A driver that passes the 100-cycle test is a driver that will survive a month of hot-plug events on production hardware. A driver that fails the 100-cycle test has a leak that will manifest, over time, as slow memory growth or as the kernel running out of some bounded resource (sysctl OIDs, cdev minor numbers, devclass entries).

The test is simple to run and is disproportionately valuable. Make it part of the driver's pre-release checklist.

### Handling Unexpected Operator Actions

Operators make mistakes. They run `kldunload myfirst` while a test program is reading from `/dev/myfirst0`. They set `dev.myfirst.0.debug.mask` to a value that enables every class at once. They copy `MAINTENANCE.md` and skip the section on tunables. The production driver has to tolerate these actions without crashing, corrupting state, or leaving the system in a broken configuration.

For each exposed interface, the production habit is to ask: what is the worst sequence of operator actions I can imagine, and does the driver survive it?

- `kldunload` while a file descriptor is open: `myfirst_detach` returns `EBUSY`. Operator sees "module busy". Driver is unchanged.
- A writable sysctl being set to an out-of-range value: the sysctl handler clamps the value or returns `EINVAL`. Driver's internal state is unchanged.
- A `MYFIRSTIOC_SETMSG` with a message longer than the buffer: `strlcpy` truncates. The copy is correct; the truncation is visible in `message_len`.
- A concurrent pair of `MYFIRSTIOC_SETMSG` calls: the softc mutex serialises them. Whichever runs second wins; both succeed.

If any of these actions produces a crash, a corruption, or an inconsistent state, the driver is not production-ready. The fix is always the same: add the missing guard, restart the test, and add a comment documenting the invariant.

### A Production-Readiness Checklist

The habits in this section fit into a short checklist that a developer can walk through before shipping:

```text
myfirst production readiness
----------------------------

[  ] MODULE_DEPEND declared for every real dependency.
[  ] MODULE_PNP_INFO declared if the driver binds to hardware.
[  ] MOD_QUIESCE answers "can you unload?" without side effects.
[  ] devctl_notify emitted for operator-relevant events.
[  ] MAINTENANCE.md current: tunables, sysctls, ioctls, events.
[  ] devd.conf snippet included with the driver.
[  ] Every log message is device_printf, includes errno,
     and is rate-limited if it can fire in a loop.
[  ] attach/detach survives 100 load/unload cycles.
[  ] sysctls reject out-of-range values.
[  ] ioctl payload is bounds-checked.
[  ] Failure paths exercised via deliberate injection.
[  ] Versioning discipline: MYFIRST_VERSION, MODULE_VERSION,
     MYFIRST_IOCTL_VERSION each bumped for their own reason.
```

The list is short on purpose. Twelve items, most of them already addressed by the habits introduced in earlier sections. A driver that checks every box is ready to be installed by someone who will never meet you.

### What the `myfirst` Driver Covers

Running `myfirst` through the checklist at the end of Chapter 25 gives the following status.

`MODULE_DEPEND` is not required because the driver has no subsystem dependencies; this is noted explicitly in `MAINTENANCE.md`.

`MODULE_PNP_INFO` is not required because the driver does not bind to hardware; this is also noted in `MAINTENANCE.md`.

`MOD_QUIESCE` is answered by the `sc_open_count` check in `myfirst_detach`; a dedicated `MOD_QUIESCE` handler is not added for this version because the semantics are identical.

`devctl_notify` is emitted on `MYFIRSTIOC_SETMSG` with event type `MSG_CHANGED`.

`MAINTENANCE.md` is shipped in the examples directory and contains tunables, sysctls, ioctls, events, and a Change Log entry for 1.8-maintenance.

The `devd.conf` snippet is shipped alongside `MAINTENANCE.md` and demonstrates the single `MSG_CHANGED` rule.

Every log message is emitted through `device_printf` (or `DPRINTF`, which wraps `device_printf`); every message that fires in a hot path is wrapped in `DLOG_RL`.

The attach/detach regression script (see Labs) runs 100 cycles without growing the kernel's memory footprint.

The sysctls for `timeout_sec`, `max_retries`, and `log_ratelimit_pps` each reject out-of-range values in their handlers.

The ioctl payloads are bounds-checked at the structure level by the kernel's ioctl framework (`_IOR`, `_IOW`, `_IOWR` declare exact sizes) and inside the driver where string length matters.

Failure injection points are marked by conditional `#ifdef` in the examples; every label has been reached at least once in development.

Version identifiers each have their own rule: string bumped, module integer bumped, ioctl integer unchanged because additions are backwards compatible.

Twelve checks, twelve outcomes. The driver is ready for the next chapter.

### Wrapping Up Section 7

Production is the quiet standard that separates interesting code from shippable code. The disciplines named here are not glamorous; they are the specific things that keep a driver working when it is deployed far from the developer who wrote it. `myfirst` has grown through five chapters of instructional content and now wears the harness that lets it survive outside the book.

In the next section, we turn to the two kernel infrastructures that let a driver run code at specific lifecycle points without manual wiring: `SYSINIT(9)` for boot-time initialisation and `EVENTHANDLER(9)` for runtime notifications. These are the last two pieces of the FreeBSD toolkit the book will introduce before Chapter 26 applies everything to a real bus.

## Section 8: SYSINIT, SYSUNINIT, and EVENTHANDLER

A driver's attach and detach routines handle everything that happens between instantiation and teardown, but there are things a driver may need to do that fall outside that window. Some code has to run before any device is instantiated at all: loading boot-time tunables, initialising a subsystem-wide lock, setting up a pool that the first `attach` will consume. Other code has to run in response to system-wide events that are not device-specific: a system-wide suspend, a low-memory condition, a shutdown that is about to sync filesystems and power down.

The FreeBSD kernel provides two mechanisms for these cases. `SYSINIT(9)` registers a function to run at a specific boot stage, and its companion `SYSUNINIT(9)` registers a cleanup function to run at module unload. `EVENTHANDLER(9)` registers a callback to run whenever the kernel fires a named event.

Both mechanisms have been available since FreeBSD's earliest releases. They are boring infrastructure; that is their value. A driver that uses them correctly can react to the full kernel lifecycle without writing a single line of manual registration code. A driver that ignores them either misses its cue or reinvents a worse version of the same facility.

### Why the Kernel Needs Boot-Time Ordering

The FreeBSD kernel boots in a precise order. Memory management comes up before any allocator is usable. Tunables are parsed before drivers can read them. Locks are initialised before anything is allowed to acquire them. Filesystems are mounted only after the devices they live on are probed. Each of these dependencies has to be honoured, or the kernel panics before `init(8)` starts.

The mechanism that enforces the ordering is `SYSINIT(9)`. A `SYSINIT` macro declares that a given function should run at a given subsystem ID with a given order constant. The kernel's boot sequence gathers every `SYSINIT` in the running configuration, sorts them by (subsystem, order), and calls them in that sequence. Modules loaded after the kernel has booted still honour their `SYSINIT` declarations: the loader calls them at module attach time, in the same sorted order.

From the driver's point of view, a `SYSINIT` is a way to say "do this thing at that point in the boot sequence, and I do not care which other code is also registering at that point". The kernel handles the sorting; the driver writes the callback.

### The Subsystem ID Space

Subsystem IDs are defined in `/usr/src/sys/sys/kernel.h`. The constants have descriptive names and numeric values that reflect their ordering. A driver picks the subsystem that corresponds to its callback's purpose:

- `SI_SUB_TUNABLES` (0x0700000): evaluate boot-time tunables. This is where `TUNABLE_INT_FETCH` and its siblings run. Code that consumes tunables has to run after this point.
- `SI_SUB_KLD` (0x2000000): loadable kernel module setup. Early module infrastructure runs here.
- `SI_SUB_SMP` (0x2900000): bring up the application processors.
- `SI_SUB_DRIVERS` (0x3100000): let drivers initialise. This is the subsystem most user-land drivers register against if they need early code that runs before any device attaches.
- `SI_SUB_CONFIGURE` (0x3800000): configure devices. By the end of this subsystem, every compiled-in driver has had a chance to attach.

There are more than a hundred subsystem IDs in `kernel.h`. The ones above are the ones a character-device driver most often interacts with. The numeric values are sorted so that "smaller number" means "earlier in boot".

Within a subsystem, the order constant gives fine-grained ordering:

- `SI_ORDER_FIRST` (0x0): run before most other code in the same subsystem.
- `SI_ORDER_SECOND`, `SI_ORDER_THIRD`: explicit step-by-step ordering.
- `SI_ORDER_MIDDLE` (0x1000000): run in the middle. Most driver-level `SYSINIT`s use this or the one below.
- `SI_ORDER_ANY` (0xfffffff): run last. The kernel does not promise any specific order among `SI_ORDER_ANY` entries.

The driver author picks the lowest order that makes the callback run after its prerequisites and before its dependents. For most purposes, `SI_ORDER_MIDDLE` is right.

### When a Driver Needs `SYSINIT`

Most character-device drivers do not need `SYSINIT` at all. `DRIVER_MODULE` already registers the driver with Newbus; the driver's `device_attach` method runs when a matching device appears. That is enough for any work that is per-instance.

`SYSINIT` is for work that is not per-instance. A list of reasons a driver might register a `SYSINIT`:

- **Initialise a global pool** that every instance of the driver will draw from. The pool exists once; it does not belong to any one softc.
- **Register with a kernel subsystem** that expects callers to register before they use it. For example, a driver that wants to receive `vm_lowmem` events registers early so that the first low-memory event does not miss it.
- **Parse a complex tunable** that requires more work than a single `TUNABLE_INT_FETCH`. The tunable-parsing code runs during `SI_SUB_TUNABLES` and populates a global structure that per-instance code consults later.
- **Self-test** a cryptographic primitive or a subsystem initialiser before the first caller can use it.

For `myfirst`, none of these applies today. The driver is per-instance, its tunables are simple, and it uses no subsystem that requires pre-registration. Chapter 25 introduces `SYSINIT` not because `myfirst` needs one, but because the reader should be familiar with the macro and understand when a future change would call for it.

### The Shape of a `SYSINIT` Declaration

The macro signature is:

```c
SYSINIT(uniquifier, subsystem, order, func, ident);
```

- `uniquifier` is a C identifier that ties the `SYSINIT` symbol to this declaration. It appears nowhere else. Convention is to use a short name that matches the subsystem or function.
- `subsystem` is the `SI_SUB_*` constant.
- `order` is the `SI_ORDER_*` constant.
- `func` is a function pointer with signature `void (*)(void *)`.
- `ident` is a single argument passed to `func`. For most uses, it is `NULL`.

The matching cleanup macro is:

```c
SYSUNINIT(uniquifier, subsystem, order, func, ident);
```

`SYSUNINIT` registers a cleanup function. It runs at module unload in the reverse order of `SYSINIT` declarations. For code that is compiled into the kernel (not a module), `SYSUNINIT` never fires because the kernel never unloads; but the declaration is still useful because compiling the driver as a module exercises the cleanup path.

### A Worked `SYSINIT` Example for `myfirst`

Consider a hypothetical enhancement to `myfirst`: a global, driver-wide pool of pre-allocated log buffers that every instance can draw from. The pool is initialised once per module load and destroyed once per module unload. Per-instance attach and detach do not touch the pool directly; they only take and return buffers from it.

The `SYSINIT` declaration looks like this:

```c
#include <sys/kernel.h>

static struct myfirst_log_pool {
	struct mtx       lp_mtx;
	/* ... per-pool state ... */
} myfirst_log_pool;

static void
myfirst_log_pool_init(void *unused __unused)
{
	mtx_init(&myfirst_log_pool.lp_mtx, "myfirst log pool",
	    NULL, MTX_DEF);
	/* Allocate pool entries. */
}

static void
myfirst_log_pool_fini(void *unused __unused)
{
	/* Release pool entries. */
	mtx_destroy(&myfirst_log_pool.lp_mtx);
}

SYSINIT(myfirst_log_pool,  SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    myfirst_log_pool_init, NULL);
SYSUNINIT(myfirst_log_pool, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    myfirst_log_pool_fini, NULL);
```

When `myfirst` is loaded, the kernel sorts the `SYSINIT` entries and calls `myfirst_log_pool_init` during the `SI_SUB_DRIVERS` phase. The first `myfirst_attach` that runs afterwards finds the pool ready. When the module is unloaded, `myfirst_log_pool_fini` runs after every instance has been detached, giving the pool a chance to release its resources.

This is a sketch for instructional purposes; `myfirst` does not actually use a global pool in the shipped Chapter 25 code. The reader who eventually writes a driver that does need one will find the pattern here.

### The Ordering Between `SYSINIT` and `DRIVER_MODULE`

`DRIVER_MODULE` itself is implemented as a `SYSINIT` under the hood. It registers the driver with Newbus during a specific subsystem phase, and Newbus's own `SYSINIT`s probe and attach devices afterwards. A driver's custom `SYSINIT` can therefore be ordered relative to `DRIVER_MODULE` by choosing the right subsystem and order.

A rule of thumb:

- `SYSINIT` at `SI_SUB_DRIVERS` with `SI_ORDER_FIRST` runs before `DRIVER_MODULE`'s registration.
- `SYSINIT` at `SI_SUB_CONFIGURE` with `SI_ORDER_MIDDLE` runs after most device attaches but before the final configuration step.

For a global pool that attach depends on, `SI_SUB_DRIVERS` with `SI_ORDER_MIDDLE` is usually correct: the pool is initialised before `DRIVER_MODULE`'s devices start attaching (because `SI_SUB_DRIVERS` is earlier than `SI_SUB_CONFIGURE`), and the order constant keeps it away from the earliest hooks.

### `EVENTHANDLER`: Reacting to Runtime Events

A `SYSINIT` fires once, at a known boot phase. An `EVENTHANDLER` fires zero or more times, whenever a specific system event occurs. The mechanisms are cousins; they complement each other.

The kernel defines a number of named events. Each event has a fixed callback signature and a fixed set of circumstances in which it fires. A driver that cares about an event registers a callback; the kernel invokes the callback every time the event fires; the driver deregisters the callback at detach.

Some commonly useful events:

- `shutdown_pre_sync`: the system is about to sync filesystems. Drivers with in-memory caches flush them here.
- `shutdown_post_sync`: the system has finished syncing filesystems. Drivers that need to know "the filesystem is quiet" hook here.
- `shutdown_final`: the system is about to halt or reboot. Drivers with hardware state that must be saved do it here.
- `vm_lowmem`: the virtual memory subsystem is under pressure. Drivers with caches of their own should release some memory back.
- `power_suspend_early`, `power_suspend`, `power_resume`: suspend/resume lifecycle.
- `dev_clone`: a device cloning event, used by pseudo-devices that appear on demand.

The list is not fixed; new events are added as the kernel grows. The ones above are the ones a general driver most often considers.

### The Shape of an `EVENTHANDLER` Registration

The pattern has three parts: declare a handler function with the right signature, register it at attach time, deregister it at detach time. The registration returns an opaque tag; the deregistration needs that tag.

For `shutdown_pre_sync`, the handler signature is:

```c
void (*handler)(void *arg, int howto);
```

`arg` is whatever pointer the driver passed to the registration; typically it is the softc. `howto` is the shutdown flags (`RB_HALT`, `RB_REBOOT`, etc.).

A minimal shutdown handler for `myfirst`:

```c
#include <sys/eventhandler.h>

static eventhandler_tag myfirst_shutdown_tag;

static void
myfirst_shutdown(void *arg, int howto)
{
	struct myfirst_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	DPRINTF(sc, MYF_DBG_INIT, "shutdown: howto=0x%x\n", howto);
	/* Flush any pending state here. */
	mtx_unlock(&sc->sc_mtx);
}
```

The registration happens inside `myfirst_attach` (or in a helper called from it):

```c
myfirst_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
```

The deregistration happens inside `myfirst_detach`:

```c
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, myfirst_shutdown_tag);
```

The deregistration is mandatory. A driver that detaches without deregistering leaves a dangling callback pointer in the kernel's event list. When the kernel next fires the event, it calls into a memory region that is no longer mapped, and the system panics.

The tag stored in `myfirst_shutdown_tag` is what binds the registration to the deregistration. For a driver with a single instance, a static variable like the one above works. For a driver with multiple instances, the tag should live in the softc so that each instance's deregistration references its own tag.

### `EVENTHANDLER` in the Attach Chain

Because registration and deregistration are symmetric, they slot cleanly into the labelled-cleanup pattern from Section 5. Registration becomes an acquisition; its failure mode is "did the registration return an error?" (it can fail under low-memory conditions); its cleanup is `EVENTHANDLER_DEREGISTER`.

The updated attach fragment for an `EVENTHANDLER`-aware `myfirst`:

```c
	/* Stage 7: shutdown handler. */
	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
```

And the matching detach, with the deregistration first (reverse order of acquisition):

```c
	EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_tag);
	myfirst_log_detach(sc);
	destroy_dev(sc->sc_cdev);
	mtx_destroy(&sc->sc_mtx);
```

`sc->sc_shutdown_tag` lives in the softc. Storing it there is important: the deregistration needs to know which specific registration to remove, and per-softc storage keeps the two instances of the driver independent.

### Priority: `SHUTDOWN_PRI_*`

Within a single event, callbacks are called in priority order. The priority is the fourth argument to `EVENTHANDLER_REGISTER`. For shutdown events, the common constants are:

- `SHUTDOWN_PRI_FIRST`: run before most other handlers.
- `SHUTDOWN_PRI_DEFAULT`: run in the default order.
- `SHUTDOWN_PRI_LAST`: run after other handlers.

A driver with hardware that needs to be quiesced before filesystems flush might register with `SHUTDOWN_PRI_FIRST`. A driver whose state depends on the filesystems already being flushed (unlikely in practice) might register with `SHUTDOWN_PRI_LAST`. Most drivers use `SHUTDOWN_PRI_DEFAULT` and do not think about priority.

Similar priority constants exist for other events (`EVENTHANDLER_PRI_FIRST`, `EVENTHANDLER_PRI_ANY`, `EVENTHANDLER_PRI_LAST`).

### When to Use `vm_lowmem`

`vm_lowmem` is the event the VM subsystem fires when free memory drops below a threshold. A driver that maintains a cache of its own (a pool of pre-allocated blocks, for example) can release some of them back to the kernel in response.

The handler is called with a single argument (the subsystem ID that triggered the event). A minimal handler for a driver with a cache:

```c
static void
myfirst_lowmem(void *arg, int unused __unused)
{
	struct myfirst_softc *sc = arg;

	mtx_lock(&sc->sc_mtx);
	/* Release some entries from the cache. */
	mtx_unlock(&sc->sc_mtx);
}
```

The registration looks the same as the shutdown one but for the event name:

```c
sc->sc_lowmem_tag = EVENTHANDLER_REGISTER(vm_lowmem,
    myfirst_lowmem, sc, EVENTHANDLER_PRI_ANY);
```

A driver that does not maintain a cache should not register for `vm_lowmem`. The cost of doing so is not zero: the kernel calls every registered handler on every low-memory event, and a no-op handler adds latency to that call chain.

For `myfirst`, there is no cache, so `vm_lowmem` is not used. The pattern is introduced for the reader who is about to write a driver that needs it.

### `power_suspend_early` and `power_resume`

Suspend/resume is a sensitive lifecycle. Between `power_suspend_early` and `power_resume`, the driver's devices are expected to be quiescent: no I/O, no interrupts, no state transitions. A driver with hardware state that must be saved before suspend and restored after resume registers handlers for both events.

For character-device drivers that do not manage hardware, these events usually do not apply. For bus-attached drivers (PCI, USB, SPI), the bus layer handles most of the suspend/resume bookkeeping, and the driver just has to provide `device_suspend` and `device_resume` methods in its `device_method_t` table. The `EVENTHANDLER` approach is for drivers that want to react to a system-wide suspend without being bus-attached.

Chapter 26 will revisit suspend/resume when `myfirst` becomes a USB driver; at that point the bus layer's mechanism is the preferred one.

### The Module Event Handler

Related to `SYSINIT` and `EVENTHANDLER` is the module event handler: the callback the kernel invokes for `MOD_LOAD`, `MOD_UNLOAD`, `MOD_QUIESCE`, and `MOD_SHUTDOWN`. Most drivers do not override it; `DRIVER_MODULE` supplies a default implementation that calls `device_probe` and `device_attach` appropriately.

A driver that needs custom behaviour at module load (beyond what `SYSINIT` can do) can supply its own handler:

```c
static int
myfirst_modevent(module_t mod, int what, void *arg)
{
	switch (what) {
	case MOD_LOAD:
		/* Custom load behaviour. */
		return (0);
	case MOD_UNLOAD:
		/* Custom unload behaviour. */
		return (0);
	case MOD_QUIESCE:
		/* Can we be unloaded?  Return errno if not. */
		return (0);
	case MOD_SHUTDOWN:
		/* Shutdown notification; usually no-op. */
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}
```

The handler is wired up through a `moduledata_t` structure rather than through `DRIVER_MODULE`. The two approaches are mutually exclusive for a given module name; a driver picks one or the other.

For most drivers, `DRIVER_MODULE`'s default is correct, and the module-event handler is not customised. `myfirst` uses `DRIVER_MODULE` throughout.

### Deregistration Discipline

The single most important rule when using `EVENTHANDLER` is: register once, deregister once, in attach and detach respectively. Two failure modes show up when the rule is broken.

The first failure mode is the **missed deregistration**. Detach runs, the tag is not deregistered, the kernel's event list still points at the softc's handler, and the next event fires into freed memory. The panic happens far from the cause, because the next event might fire minutes or hours after detach.

The fix is mechanical: every `EVENTHANDLER_REGISTER` in attach gets a matching `EVENTHANDLER_DEREGISTER` in detach. The labelled-cleanup pattern from Section 5 makes this easy: registration is an acquisition with a label, and the cleanup chain deregisters in reverse order.

The second failure mode is the **double registration**. A driver that registers the same handler twice has two entries in the kernel's event list; detaching once removes only one of them. The kernel then has a stale entry pointing at the softc that just went away.

The fix is also mechanical: register exactly once per attach. Do not register in a helper that is called from multiple places; do not register lazily in response to a first event.

### A Complete Lifecycle Example

Putting `SYSINIT`, `EVENTHANDLER`, and the labelled-cleanup attach together, the full lifecycle of a `myfirst` driver with a global pool and a shutdown handler runs as follows:

At kernel boot or module load:
- `SI_SUB_TUNABLES` fires. `TUNABLE_*_FETCH` calls in attach will see their values.
- `SI_SUB_DRIVERS` fires. `myfirst_log_pool_init` runs (via `SYSINIT`). The global pool is ready.
- `SI_SUB_CONFIGURE` fires. `DRIVER_MODULE` registers the driver. Newbus probes; `myfirst_probe` and `myfirst_attach` run for each instance.
- Inside `myfirst_attach`: lock, cdev, sysctl, log state, shutdown handler registered.

At runtime:
- `ioctl(fd, MYFIRSTIOC_SETMSG, buf)` updates the message.
- `devctl_notify` emits `MSG_CHANGED`; `devd` logs it.

At shutdown:
- The kernel fires `shutdown_pre_sync`. `myfirst_shutdown` runs for each registered handler.
- Filesystems sync.
- `shutdown_final` fires. The machine halts.

At module unload (before shutdown):
- `MOD_QUIESCE` fires. `myfirst_detach` returns `EBUSY` if any device is in use.
- `MOD_UNLOAD` fires. `myfirst_detach` runs for each instance: deregister handler, release log state, destroy cdev, destroy lock.
- `SYSUNINIT` fires. `myfirst_log_pool_fini` runs. The global pool is released.
- The module unmaps.

Every step is in a well-defined place. Every acquisition has a matching release. A driver that follows the pattern closely is a driver that the FreeBSD kernel can load, run, and unload any number of times without accumulating state.

### Deciding What to Register For

A driver author deciding whether to register for an event should ask three questions.

First, **does the event actually matter to this driver?** `vm_lowmem` matters to a driver with a cache; it is noise to a driver without one. `shutdown_pre_sync` matters to a driver whose hardware needs to be quiesced; it is noise to a pseudo-driver. A handler that does nothing useful is still called on every event, slowing the system down slightly on every trigger.

Second, **is the event the right one?** FreeBSD has several shutdown events. `shutdown_pre_sync` fires before filesystem syncs; `shutdown_post_sync` fires after; `shutdown_final` fires just before halt. A driver registering for the wrong phase might flush its cache too early (before data that should be flushed) or too late (after filesystems are already dying).

Third, **has the event been stable across kernel versions?** `shutdown_pre_sync` has been stable for a long time and is safe to use. Newer or more specialised events may change signatures between releases. A driver that targets a specific FreeBSD release (this book is aligned with 14.3) can rely on the events in that release; a driver that targets a range of releases has to be more careful.

For `myfirst`, the shipped Chapter 25 registers `shutdown_pre_sync` as a demonstration. The handler is a no-op: it just logs that shutdown is starting. The registration, deregistration, and labelled cleanup are the point of the example, not the handler body.

### Common Mistakes with `SYSINIT` and `EVENTHANDLER`

A handful of mistakes recur when these mechanisms are used for the first time.

The first mistake is **running heavy code in a `SYSINIT`**. Boot-time code runs in a context where many kernel subsystems are still initialising. A `SYSINIT` that calls into a complex subsystem may race with that subsystem's own initialisation. The rule is: `SYSINIT` code should be minimal and self-contained. Complex initialisation belongs in the driver's attach routine, which runs after every subsystem has come up.

The second mistake is **using `SYSINIT` instead of `device_attach`**. A `SYSINIT` runs once per module load, but `device_attach` runs once per device. A driver that initialises per-device state in a `SYSINIT` is making a category error; the per-device state does not exist yet at `SYSINIT` time.

The third mistake is **forgetting the priority argument on `EVENTHANDLER_REGISTER`**. The function takes four arguments: event name, callback, argument, priority. Some drivers forget the priority and pass the wrong number of arguments; the compiler catches this with an error, but a driver that happens to pass `0` by accident registers with the lowest possible priority, which may be wrong.

The fourth mistake is **not zeroing the tag field**. If `sc->sc_shutdown_tag` is uninitialised when `EVENTHANDLER_DEREGISTER` is called on a failure path, the deregistration tries to remove a tag that was never registered. The kernel detects this (the tag does not exist in its event list) and the deregistration is a no-op, but the pattern is fragile. The cleaner discipline is to zero the softc at allocation time (Newbus does this automatically via `device_get_softc`, but drivers that allocate their own softcs have to do it manually) and to never reach a deregistration for a tag that was not registered.

### Wrapping Up Section 8

`SYSINIT` and `EVENTHANDLER` are the kernel's way of letting a driver participate in lifecycles beyond its own attach/detach window. `SYSINIT` runs code at a specific boot phase; `EVENTHANDLER` runs code in response to a named kernel event. Together they cover the cases where per-device code is not enough and the driver has to engage with the system as a whole.

`myfirst` at the end of Chapter 25 uses `EVENTHANDLER_REGISTER` for a demonstration `shutdown_pre_sync` handler; the registration, deregistration, and labelled-cleanup shape are all in place. `SYSINIT` is introduced but not used, because `myfirst` has no global pool today. The patterns are planted; when a future chapter's driver does need them, the reader will recognise them immediately.

With Section 8 complete, every mechanism the chapter set out to teach is in the driver. The remaining material in the chapter applies the mechanisms through hands-on labs, challenge exercises, and a troubleshooting reference for when things go wrong.

## Hands-on Labs

The labs in this section exercise the chapter's mechanisms on a real FreeBSD 14.3 system. Each lab has a specific measurable outcome; after running the lab, you should be able to state what you saw and what it means. The labs assume you have the `examples/part-05/ch25-advanced/` companion directory at hand.

Before starting, build the driver as shipped at the top of `ch25-advanced/`:

```console
# cd examples/part-05/ch25-advanced
# make clean
# make
# kldload ./myfirst.ko
# ls /dev/myfirst*
/dev/myfirst0
```

If any of these steps fails, fix the toolchain or the source before continuing. The labs assume a working baseline.

### Lab 1: Reproducing a Log Flood

Purpose: see the difference between an unrated `device_printf` and the rate-limited `DLOG_RL` when fired in a hot loop.

Source: `examples/part-05/ch25-advanced/lab01-log-flood/` contains a small user-space program that calls `read()` on `/dev/myfirst0` 10,000 times as fast as the kernel will let it.

Step 1. Temporarily set the debug mask to enable the I/O class and the printf on the read path:

```console
# sysctl dev.myfirst.0.debug.mask=0x4
```

The mask bit `0x4` enables `MYF_DBG_IO`, which the read callback uses.

Step 2. Run the flood with the naive `DPRINTF` version of the driver first. Build and load `myfirst-flood-unlimited.ko` from `lab01-log-flood/unlimited/`:

```console
# make -C lab01-log-flood/unlimited
# kldunload myfirst
# kldload lab01-log-flood/unlimited/myfirst.ko
# dmesg -c > /dev/null
# ./lab01-log-flood/flood 10000
# dmesg | wc -l
```

Expected outcome: approximately 10,000 lines in `dmesg`. The console may also be filled; the system's log buffer wraps, and earlier messages are lost.

Step 3. Unload and reload the rate-limited version from `lab01-log-flood/limited/`, which uses `DLOG_RL` with a 10 pps cap:

```console
# kldunload myfirst
# kldload lab01-log-flood/limited/myfirst.ko
# dmesg -c > /dev/null
# ./lab01-log-flood/flood 10000
# sleep 5
# dmesg | wc -l
```

Expected outcome: approximately 50 lines in `dmesg`. The flood now emits at most 10 messages per second; the 10-second test window produces roughly 50 messages (the first second's burst token plus subsequent seconds' allowances, so count exactly may vary but should be within ten).

Step 4. Compare the two outputs side by side. The rate-limited version is readable; the unlimited version is not. Both drivers had identical read behaviour; only the logging discipline differs.

Record: the wall-clock time it took the flood to complete in both cases. The unlimited version is noticeably slower because the console output itself is a bottleneck. Rate-limiting has a visible performance benefit as well as a clarity benefit.

### Lab 2: errno Audit with `truss`

Purpose: see what `truss(1)` reports when the driver returns different errno values, and calibrate your intuition about which errno to return from which code path.

Source: `examples/part-05/ch25-advanced/lab02-errno-audit/` contains a user program that makes a series of deliberately invalid calls and a script that runs it under `truss`.

Step 1. Load the stock `myfirst.ko` if not already loaded:

```console
# kldload ./myfirst.ko
```

Step 2. Run the audit program under `truss`:

```console
# truss -f -o /tmp/audit.truss ./lab02-errno-audit/audit
# less /tmp/audit.truss
```

The program performs these operations in sequence:
1. Opens `/dev/myfirst0`.
2. Issues an unknown ioctl command (command number 99).
3. Issues `MYFIRSTIOC_SETMSG` with a NULL argument.
4. Writes a zero-length buffer.
5. Writes a buffer larger than the driver accepts.
6. Sets `dev.myfirst.0.timeout_sec` to a value larger than allowed.
7. Closes.

Step 3. In the `truss` output, find each operation and note its errno. The expected outcomes:

1. `open`: returns a file descriptor. No errno.
2. `ioctl(_IOC=0x99)`: returns `ENOTTY` (the kernel's translation of the driver's `ENOIOCTL`).
3. `ioctl(MYFIRSTIOC_SETMSG, NULL)`: returns `EFAULT` (the kernel catches the NULL before the handler runs).
4. `write(0 bytes)`: returns `0` (no error, just no bytes written).
5. `write(oversize)`: returns `EINVAL` (the driver rejects lengths above its buffer size).
6. `sysctl write out-of-range`: returns `EINVAL` (the sysctl handler rejects the value).
7. `close`: returns 0. No errno.

Step 4. For each observed errno, locate the driver code that returned it. Walk the call chain from `truss` to the kernel source and confirm that the errno you see in `truss` is the one the driver returned. This exercise calibrates your mental map between "what the user sees" and "what the driver says".

### Lab 3: Tunable Reboot Behaviour

Purpose: verify that a loader tunable actually changes the driver's initial state when the module is first loaded.

Source: `examples/part-05/ch25-advanced/lab03-tunable-reboot/` contains a helper script `apply_tunable.sh`.

Step 1. With the stock module loaded and no tunable set, confirm the initial value of the timeout:

```console
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 5
```

The default is 5, set in the attach routine.

Step 2. Unload the module, set the loader tunable, reload, and confirm the new initial value:

```console
# kldunload myfirst
# kenv hw.myfirst.timeout_sec=12
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 12
```

The tunable set via `kenv(1)` took effect because `TUNABLE_INT_FETCH` in attach read it before the sysctl was published.

Step 3. Change the sysctl at runtime and confirm that the change is accepted but does not propagate back to the tunable:

```console
# sysctl dev.myfirst.0.timeout_sec=25
dev.myfirst.0.timeout_sec: 12 -> 25
# kenv hw.myfirst.timeout_sec
hw.myfirst.timeout_sec="12"
```

The tunable still reads 12; the sysctl reads 25. The tunable is the initial value; the sysctl is the runtime value. They diverge the moment the sysctl is written.

Step 4. Unload and reload. The tunable value is still 12 (because it is in the kernel environment), so the new sysctl starts at 12, not 25. This is the lifecycle: tunable sets initial, sysctl sets runtime, unload loses runtime, tunable survives.

Step 5. Clear the tunable and reload:

```console
# kldunload myfirst
# kenv -u hw.myfirst.timeout_sec
# kldload ./myfirst.ko
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 5
```

Back to the attach-time default. The lifecycle is consistent end to end.

### Lab 4: Deliberate Attach Failure Injection

Purpose: verify that every label in the cleanup chain is reached and that no resources leak when a failure is injected in the middle of attach.

Source: `examples/part-05/ch25-advanced/lab04-failure-injection/` contains four build variants of the module, each compiling in a different failure injection point:

- `inject-mtx/`: fails just after the lock is initialised.
- `inject-cdev/`: fails just after the cdev is created.
- `inject-sysctl/`: fails just after the sysctl tree is built.
- `inject-log/`: fails just after the log state is initialised.

Each variant defines exactly one of the `MYFIRST_DEBUG_INJECT_FAIL_*` macros from Section 5.

Step 1. Build and load the first variant. The load should fail:

```console
# make -C lab04-failure-injection/inject-mtx
# kldload lab04-failure-injection/inject-mtx/myfirst.ko
kldload: an error occurred while loading module myfirst. Please check dmesg(8) for more details.
# dmesg | tail -3
myfirst0: attach: stage 1 complete
myfirst0: attach: injected failure after mtx_init
device_attach: myfirst0 attach returned 12
```

The attach function returned `ENOMEM` (errno 12) at the injected failure point. The module is not loaded:

```console
# kldstat -n myfirst
kldstat: can't find file: myfirst
```

Step 2. Repeat for the other three variants. Each should fail at the specific stage the name suggests, and each should leave the kernel in a clean state. To confirm the clean state, check for leftover sysctl OIDs, leftover cdevs, and leftover locks:

```console
# sysctl dev.myfirst 2>&1 | head
sysctl: unknown oid 'dev.myfirst'
# ls /dev/myfirst* 2>&1
ls: No match.
# dmesg | grep -i "witness\|leak"
```

Expected: no matches. No sysctl, no cdev, no witness complaints. The cleanup chain is working.

Step 3. Run the combined regression script that builds every variant and checks the results automatically:

```console
# ./lab04-failure-injection/run.sh
```

The script builds each variant, loads it, confirms that the load fails, confirms that the state is clean, and reports a one-line summary per variant. A pass on all four variants means every label in the cleanup chain has been exercised on a real kernel and released every resource that was held at that label.

### Lab 5: `shutdown_pre_sync` Handler

Purpose: confirm that the registered shutdown handler actually fires during a real shutdown, and observe its ordering relative to filesystem sync.

Source: `examples/part-05/ch25-advanced/lab05-shutdown-handler/` contains a version of `myfirst.ko` whose `shutdown_pre_sync` handler prints a distinctive message to the console.

Step 1. Load the module and verify the handler is registered by reading the log on attach:

```console
# kldload lab05-shutdown-handler/myfirst.ko
# dmesg | tail -1
myfirst0: attach: shutdown_pre_sync handler registered
```

Step 2. Issue a reboot. On a test machine (not a production machine), the simplest way is:

```console
# shutdown -r +1 "testing myfirst shutdown handler"
```

Watch the console as the machine shuts down. The expected sequence:

```text
myfirst0: shutdown: howto=0x4
Syncing disks, buffers remaining... 0 0 0
Uptime: ...
```

The `myfirst0: shutdown: howto=0x4` line appears **before** "Syncing disks", because `shutdown_pre_sync` fires before filesystems sync. If the handler message appears after the sync message, the registration was at the wrong event (`shutdown_post_sync` or `shutdown_final`). If the message never appears, the handler was never registered or never deregistered (double-free would cause a panic, but silent missing would suggest a registration bug).

Step 3. After the machine reboots, confirm that unloading before shutdown still removes the handler cleanly:

```console
# kldload lab05-shutdown-handler/myfirst.ko
# kldunload myfirst
# dmesg | tail -2
myfirst0: detach: shutdown handler deregistered
myfirst0: detach: complete
```

The deregistration message confirms the cleanup path in detach ran. The attach/detach pair is symmetric; no event list entries leak.

### Lab 6: The 100-Cycle Regression Script

Purpose: run a sustained load/unload cycle to catch leaks that only appear under repeated attach/detach. This is the test from Section 7's production checklist.

Source: `examples/part-05/ch25-advanced/lab06-100-cycles/` contains `run.sh`, which performs 100 cycles of kldload / sleep / kldunload and records the kernel's memory footprint before and after.

Step 1. Record the kernel's initial memory footprint:

```console
# vmstat -m | awk '$1=="Solaris" || $1=="kernel"' > /tmp/before.txt
# cat /tmp/before.txt
```

Step 2. Run the cycle script:

```console
# ./lab06-100-cycles/run.sh
cycle 1/100: ok
cycle 2/100: ok
...
cycle 100/100: ok
done: 100 cycles, 0 failures, 0 leaks detected.
```

Step 3. Record the final memory footprint:

```console
# vmstat -m | awk '$1=="Solaris" || $1=="kernel"' > /tmp/after.txt
# diff /tmp/before.txt /tmp/after.txt
```

Expected: no significant difference. If there is a difference of more than a few kilobytes (the kernel's own bookkeeping fluctuates), the driver has a leak.

Step 4. If the script reports any failure, examine `/tmp/myfirst-cycles.log` (which `run.sh` populates) for the first failing cycle. The failure is usually at the deregistration step: a missing `EVENTHANDLER_DEREGISTER` or a missing `mtx_destroy`.

A clean 100-cycle run is one of the simplest ways to gain confidence in a driver's lifecycle discipline. Repeat it after every material change to the attach or detach chain.

### Lab 7: Capability Discovery in User Space

Purpose: confirm that a user-space program can discover the driver's capabilities at run time and behave accordingly, as designed in Section 4.

Source: `examples/part-05/ch25-advanced/lab07-getcaps/` contains `mfctl25.c`, an updated version of `myfirstctl` that issues `MYFIRSTIOC_GETCAPS` before each operation and skips unsupported ones.

Step 1. Build `mfctl25`:

```console
# make -C lab07-getcaps
```

Step 2. Run against the stock Chapter 25 driver and observe the capability report:

```console
# ./lab07-getcaps/mfctl25 caps
Driver reports capabilities:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

The driver reports three capabilities. The `MYF_CAP_TIMEOUT` bit is defined but not set because the timeout behaviour is a sysctl, not an ioctl.

Step 3. Run each operation and confirm that the program only attempts supported ones:

```console
# ./lab07-getcaps/mfctl25 reset
# ./lab07-getcaps/mfctl25 getmsg
Current message: Hello from myfirst
# ./lab07-getcaps/mfctl25 setmsg "new message"
# ./lab07-getcaps/mfctl25 timeout
Timeout ioctl not supported; use sysctl dev.myfirst.0.timeout_sec instead.
```

The last line is the capability check firing: the program asked for `MYF_CAP_TIMEOUT`, the driver did not advertise it, and the program printed a helpful message instead of issuing an ioctl that would return `ENOTTY`.

Step 4. Load an older build (Chapter 24's `myfirst.ko` in `lab07-getcaps/ch24/`) and rerun:

```console
# kldunload myfirst
# kldload lab07-getcaps/ch24/myfirst.ko
# ./lab07-getcaps/mfctl25 caps
GETCAPS ioctl not supported.  Falling back to default feature set:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

When `GETCAPS` itself returns `ENOTTY`, the program falls back to a safe default set that matches Chapter 24's known behaviour. This is the forward-compatibility pattern in action.

Step 5. Reload the Chapter 25 driver to restore the test state:

```console
# kldunload myfirst
# kldload ./myfirst.ko
```

The exercise demonstrates that capability discovery lets one user-space program work correctly across two driver versions, which is the whole point of the pattern.

### Lab 8: Sysctl Range Validation

Purpose: confirm that every writable sysctl the driver exposes rejects out-of-range values and leaves the internal state untouched on rejection.

Source: `examples/part-05/ch25-advanced/lab08-sysctl-validation/` contains the driver built with range checks and a test script `run.sh` that drives every sysctl to its limits.

Step 1. Load the driver and list its writable sysctls:

```console
# kldload ./myfirst.ko
# sysctl -W dev.myfirst.0 | grep -v "^dev.myfirst.0.debug.classes"
dev.myfirst.0.timeout_sec: 5
dev.myfirst.0.max_retries: 3
dev.myfirst.0.log_ratelimit_pps: 10
dev.myfirst.0.debug.mask: 0
```

Four writable sysctls. Each has a specific valid range.

Step 2. Try to set each sysctl to zero, to its maximum allowed, and to one above its maximum:

```console
# sysctl dev.myfirst.0.timeout_sec=0
sysctl: dev.myfirst.0.timeout_sec: Invalid argument
# sysctl dev.myfirst.0.timeout_sec=60
dev.myfirst.0.timeout_sec: 5 -> 60
# sysctl dev.myfirst.0.timeout_sec=61
sysctl: dev.myfirst.0.timeout_sec: Invalid argument
# sysctl dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: 60
```

The out-of-range attempts are rejected with `EINVAL`; the internal value is unchanged. The valid assignment to 60 succeeds.

Step 3. Repeat for the other sysctls:

- `max_retries`: valid range 1-100. Try 0, 100, 101.
- `log_ratelimit_pps`: valid range 1-10000. Try 0, 10000, 10001.
- `debug.mask`: valid range 0-0xff (the defined bits). Try 0, 0xff, 0x100.

For each, the script reports pass or fail. A driver that passes every case has correct handler-level validation.

Step 4. Examine the sysctl handlers in `examples/part-05/ch25-advanced/myfirst_sysctl.c` and note the pattern:

```c
static int
myfirst_sysctl_timeout_sec(SYSCTL_HANDLER_ARGS)
{
	struct myfirst_softc *sc = arg1;
	u_int new_val;
	int error;

	mtx_lock(&sc->sc_mtx);
	new_val = sc->sc_timeout_sec;
	mtx_unlock(&sc->sc_mtx);

	error = sysctl_handle_int(oidp, &new_val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (new_val < 1 || new_val > 60)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	sc->sc_timeout_sec = new_val;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}
```

Note the order of operations: copy the current value out for the read side, call `sysctl_handle_int` to handle the copy, validate on write, commit under the lock only after validation succeeds. A handler that commits before validation exposes inconsistent state to concurrent readers.

Step 5. Confirm that the sysctl description is helpful (`sysctl -d`):

```console
# sysctl -d dev.myfirst.0.timeout_sec
dev.myfirst.0.timeout_sec: Operation timeout in seconds (range 1-60)
```

The description states the unit and the range. A user reading the sysctl without consulting any documentation can still set it correctly.

### Lab 9: Log Message Audit Across the Driver

Purpose: inventory every log message in the driver and confirm that each one follows the Section 1 and Section 2 disciplines (`device_printf`, includes errno where relevant, rate-limited when on a hot path).

Source: `examples/part-05/ch25-advanced/lab09-log-audit/` contains an audit script `audit.sh` and a grep-based checker.

Step 1. Run the audit script against the driver's source:

```console
# cd examples/part-05/ch25-advanced
# ./lab09-log-audit/audit.sh
```

The script greps for every `printf`, `device_printf`, `log`, `DPRINTF`, and `DLOG_RL` call in the source tree and categorises each into:

- PASS: uses `device_printf` or `DPRINTF`, device name is implicit.
- PASS: uses `DLOG_RL` on a hot path.
- WARN: uses `printf` without device context (may be legitimate at `MOD_LOAD`).
- FAIL: uses `device_printf` on a hot path without rate limiting.

Expected output (for the Chapter 25 stock driver):

```text
myfirst.c:    15 log messages - 15 PASS
myfirst_cdev.c:  6 log messages - 6 PASS
myfirst_ioctl.c: 4 log messages - 4 PASS
myfirst_sysctl.c: 0 log messages
myfirst_log.c:   2 log messages - 2 PASS
Total: 27 log messages - 0 WARN, 0 FAIL
```

Step 2. Intentionally break one message (for example, change a `DPRINTF(sc, MYF_DBG_IO, ...)` in the read callback to a bare `device_printf(sc->sc_dev, ...)`) and rerun:

```text
myfirst_cdev.c: 6 log messages - 5 PASS, 1 FAIL
  myfirst_cdev.c:83: device_printf on hot path not rate-limited
Total: 27 log messages - 0 WARN, 1 FAIL
```

The audit caught the regression. Revert the change and rerun to confirm the count returns to zero failures.

Step 3. Add a new log message to a non-hot path (for example, a one-time initialisation message at attach). Confirm the audit accepts it as a PASS:

```c
device_printf(dev, "initialised with timeout %u\n",
    sc->sc_timeout_sec);
```

One-shot messages at attach do not need rate limiting because they fire exactly once per instance per load.

Step 4. For each message the audit categorises as a PASS, confirm that the message includes meaningful context. A message like "error" is a PASS for the audit tool but a FAIL for the human reader. A second manual pass over the grep output is required to confirm the messages are actually useful.

The lab demonstrates two points. First, a mechanical audit catches the categorical rules (rate limiting on hot paths, `device_printf` over bare `printf`) but cannot judge message quality. Second, the human pass is what confirms that the messages contain enough context to be diagnosed from. Both passes together give the driver a log surface that will actually help a future support engineer.

### Lab 10: Multi-Version Compatibility Matrix

Purpose: confirm that the capability-discovery pattern introduced in Section 4 actually allows a single user-space program to work with three different versions of the driver.

Source: `examples/part-05/ch25-advanced/lab10-compat-matrix/` contains three pre-built `.ko` files corresponding to driver versions 1.6-debug, 1.7-integration, and 1.8-maintenance, plus a single user-space program `mfctl-universal` that uses `MYFIRSTIOC_GETCAPS` (or a fallback) to decide which operations to attempt.

Step 1. Load each driver version in turn and run `mfctl-universal --caps` against it:

```console
# kldload lab10-compat-matrix/v1.6/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.6-debug
GETCAPS ioctl: not supported
Using fallback capability set:
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG

# kldunload myfirst
# kldload lab10-compat-matrix/v1.7/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.7-integration
GETCAPS ioctl: not supported
Using fallback capability set:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG

# kldunload myfirst
# kldload lab10-compat-matrix/v1.8/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal --caps
Driver: version 1.8-maintenance
GETCAPS ioctl: supported
Driver reports capabilities:
  MYF_CAP_RESET
  MYF_CAP_GETMSG
  MYF_CAP_SETMSG
```

Three driver versions, one user-space program, three different capability decisions. The program works with each version.

Step 2. Exercise each capability in turn and confirm the program skips unsupported ones:

```console
# kldunload myfirst
# kldload lab10-compat-matrix/v1.6/myfirst.ko
# ./lab10-compat-matrix/mfctl-universal reset
reset: not supported on this driver version (1.6-debug)
# ./lab10-compat-matrix/mfctl-universal getmsg
Current message: Hello from myfirst
```

The reset operation, which was added in 1.7, is skipped cleanly on 1.6. The program prints a helpful message rather than issuing an ioctl that would return `ENOTTY`.

Step 3. Read the `mfctl-universal` source and note the three-tier fallback:

```c
uint32_t
driver_caps(int fd, const char *version)
{
	uint32_t caps;

	if (ioctl(fd, MYFIRSTIOC_GETCAPS, &caps) == 0)
		return (caps);
	if (errno != ENOTTY)
		err(1, "GETCAPS ioctl");

	/* Fallback by version string. */
	if (strstr(version, "1.8-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.7-") != NULL)
		return (MYF_CAP_RESET | MYF_CAP_GETMSG |
		    MYF_CAP_SETMSG);
	if (strstr(version, "1.6-") != NULL)
		return (MYF_CAP_GETMSG | MYF_CAP_SETMSG);

	/* Unknown version: use the minimal safe set. */
	return (MYF_CAP_GETMSG);
}
```

The first tier asks the driver directly. The second tier matches known version strings. The third tier falls back to a minimal set that every version of the driver has supported.

Step 4. Reflect on what happens when version 1.9 ships with a new capability bit. The program does not need to be updated: `MYFIRSTIOC_GETCAPS` on 1.9 will report the new bit, the program will see it, and if the program knows the corresponding operation it will use it. If the program does not know the operation, the bit is ignored. Either way, the program continues to work.

The lab demonstrates that capability discovery is not an abstract pattern; it is the specific mechanism that lets one user-space program span three driver versions without modification.

## Challenge Exercises

The challenges in this section go beyond the labs. Each one asks you to extend the driver in a direction the chapter pointed at but did not complete. Work through them when you are ready; none of them requires new kernel knowledge beyond what the chapter covered, but each one requires a careful walk through the existing code.

### Challenge 1: Per-Class Rate Limits

Section 1 sketched three rate-limit slots in the softc (`sc_rl_generic`, `sc_rl_io`, `sc_rl_intr`), but the `DLOG_RL` macro uses a single pps value (`sc_log_pps`). Extend the driver so that each class has its own sysctl-configurable pps cap:

- Add `sc_log_pps_io` and `sc_log_pps_intr` fields to the softc alongside `sc_log_pps` (which stays as the generic cap).
- Add matching sysctls under `dev.myfirst.<unit>.log.pps_*` and matching tunables under `hw.myfirst.log_pps_*`.
- Update `DLOG_RL_IO` and `DLOG_RL_INTR` helpers (or a generic helper that takes both the class and the pps value) to honour the per-class cap.

Write a short test program that fires a burst of messages in each class and confirm from `dmesg` that each class is rate-limited independently. The generic bucket should not starve the I/O bucket, and vice versa.

Hint: the most reusable shape is a helper function `myfirst_log_ratelimited(sc, class, fmt, ...)` that looks up the right rate-limit state and the right pps cap given the class bit. The `DLOG_RL_*` macros become thin wrappers over the helper.

### Challenge 2: A Writable String sysctl

Section 3 warned about the complication of writable string sysctls. Implement one correctly. The sysctl should be `dev.myfirst.<unit>.message`, with `CTLFLAG_RW`, and should let an operator rewrite the in-driver message with a single `sysctl(8)` call.

Requirements:

1. The handler must take the softc mutex around the update.
2. The handler must validate the length against `sizeof(sc->sc_msg)` and reject oversize strings with `EINVAL`.
3. The handler must use `sysctl_handle_string` for the copy; do not reimplement user-space access.
4. On successful update, the handler must emit a `devctl_notify` for `MSG_CHANGED`, just like the ioctl does.

Test with:

```console
# sysctl dev.myfirst.0.message="hello from sysctl"
# sysctl dev.myfirst.0.message
dev.myfirst.0.message: hello from sysctl
# sysctl dev.myfirst.0.message="$(printf 'A%.0s' {1..1000})"
sysctl: dev.myfirst.0.message: Invalid argument
```

The second `sysctl` should fail (oversize), and the driver's message should be unchanged.

Consider: should the ioctl and the sysctl emit the same `MSG_CHANGED` event, or different ones? Both sides are updating the same underlying state; a single event type is probably correct. Document your decision in `MAINTENANCE.md`.

### Challenge 3: A `MOD_QUIESCE` Handler Separate from Detach

Section 7 noted that `MOD_QUIESCE` and `MOD_UNLOAD` are conceptually distinct but that `myfirst` handles both through `myfirst_detach`. Split them so the quiesce question can be answered without side effects.

Requirements:

1. Add an explicit `MOD_QUIESCE` check to a module event handler. The handler returns `EBUSY` if any device is open, `0` otherwise.
2. The handler does not call `destroy_dev`, does not destroy locks, does not alter state. It only reads `sc_open_count`.
3. For each attached instance, iterate via `devclass` and check every softc. Use the `myfirst_devclass` symbol that `DRIVER_MODULE` exports.

Hint: look at `/usr/src/sys/kern/subr_bus.c` for `devclass_get_softc` and related helpers. They are the way to enumerate softcs from a module-level function that does not have a `device_t`.

Test: open `/dev/myfirst0`, try `kldunload myfirst`, confirm that it reports "module busy" and that the driver is unchanged. Close the fd, retry the unload, confirm that it succeeds.

### Challenge 4: A Drain-Based Detach Instead of `EBUSY`

The chapter's detach pattern refuses to unload if the driver is in use. A more elaborate pattern drains in-flight references rather than refusing. Implement it.

Requirements:

1. Add an `is_dying` boolean to the softc, protected by `sc_mtx`.
2. In `myfirst_open`, check `is_dying` under the lock and return `ENXIO` if true.
3. In `myfirst_detach`, set `is_dying` under the lock. Wait for `sc_open_count` to reach zero, using `mtx_sleep` with a condition variable or a simple polling loop with a timeout.
4. After `sc_open_count` reaches zero, proceed with destroy_dev and the rest of the detach chain.

Add a timeout: if `sc_open_count` does not reach zero within (say) 30 seconds, return `EBUSY` from detach. The operator gets a clear signal that the driver is not draining; they can kill the offending process and retry.

Test: open `/dev/myfirst0` in a loop from one shell, call `kldunload myfirst` from another shell, and observe the drain behaviour.

### Challenge 5: A Sysctl-Driven Version Bump Check

Write a small user-space program that reads `dev.myfirst.<unit>.version`, parses the version string, and compares it against a minimum version the program requires. The program should print "ok" if the driver is new enough and "driver too old, please update" if not.

Requirements:

1. Parse the string `X.Y-tag` into integers. Reject malformed strings with a clear error.
2. Compare against a minimum of `"1.8"`. A driver reporting `"1.7-integration"` should fail the check; a driver reporting `"1.8-maintenance"` should pass; a driver reporting `"2.0-something"` should pass.
3. Exit with status `0` on success, non-zero on failure, so the check can be used in shell scripts.

Reflect: could a well-designed program rely on the version string for compatibility checking, or is the capability bitmask from Section 4 the better signal? There is no single right answer; the exercise is to think about the trade-off.

### Challenge 6: Add a Sysctl That Enumerates Open File Descriptors

Add a new sysctl `dev.myfirst.<unit>.open_fds` that returns, as a string, the PIDs of the processes that currently have the device open. This is harder than it sounds: the driver does not normally track which process opened each fd.

Hint: in `myfirst_open`, store the calling thread's PID in a linked list under the softc. In `myfirst_close`, remove the corresponding entry. In the sysctl handler, walk the list under the softc mutex and build a comma-separated string of PIDs.

Edge cases:

1. A process that is open multiple times (multiple fds, forked children) should appear once or multiple times? Decide and document.
2. The list must be bounded in length (attackers could open the device millions of times).
3. The sysctl value is read-only; the handler must not modify the list.

Reflect: is this information actually useful, or is `fstat(1)` a better tool for the same job? The answer depends on whether the driver can provide information that user-space tools cannot derive themselves.

### Challenge 7: A Second `EVENTHANDLER` for `vm_lowmem`

`myfirst` does not have a cache today, but imagine it did: a pool of pre-allocated 4 KB buffers used for read/write operations. Under low memory, the driver should release some of the buffers back.

Implement a synthetic cache: allocate an array of 64 `malloc(M_TEMP, 4096)` pointers at attach. Register a `vm_lowmem` handler that, when fired, releases half of the cached buffers. Reattach reallocates them.

Requirements:

1. The cache allocations happen under the softc mutex.
2. The `vm_lowmem` handler takes the mutex, scans the array, and `free()`s the first 32 buffers.
3. A sysctl `dev.myfirst.<unit>.cache_free` reports the current number of free (NULL) slots; an operator can confirm that the handler fired.

Test: use a `stress -m 10 --vm-bytes 512M` loop to drive the system into low-memory pressure, and watch the `cache_free` sysctl. Over time, it should grow as `vm_lowmem` fires repeatedly.

Reflect: is this what the event is meant to be used for? Many drivers that register for `vm_lowmem` have caches that are much larger than 64 buffers; the cost/benefit is different. This is a teaching exercise; a real driver would think harder about whether its cache is worth the complexity.

### Challenge 8: A `MYFIRSTIOC_GETSTATS` Ioctl Returning a Structured Payload

So far every ioctl the driver handles returns a scalar: an integer, a uint32, or a fixed-size string. Add a `MYFIRSTIOC_GETSTATS` ioctl that returns a structured payload with every counter the driver maintains.

Requirements:

1. Define `struct myfirst_stats` in `myfirst_ioctl.h` with fields for `open_count`, `total_reads`, `total_writes`, `log_drops` (a new counter you add), and `last_error_errno` (another new counter).
2. Add `MYFIRSTIOC_GETSTATS` with command number 6, declared as `_IOR('M', 6, struct myfirst_stats)`.
3. The handler copies the softc's counters into the payload under `sc_mtx` and returns.
4. Advertise a new capability bit `MYF_CAP_STATS` in the `GETCAPS` response.
5. Update `MAINTENANCE.md` to document the new ioctl and the new capability.

Edge cases:

1. What happens if the struct size changes later? The `_IOR` macro bakes the size into the command number. Adding a field bumps the command number, which breaks old callers. The fix is to include a `version` and a `reserved` space in the struct from day one; any future addition reuses the reserved space.

2. Is it safe to return all counters atomically, or do they need separate locking? Holding `sc_mtx` for the duration of the copy is the simplest discipline.

Reflect: this is where ioctl design starts to feel complex. For a simple counter snapshot, a sysctl with a string-format output might be easier than an ioctl with a versioned struct. Which would you pick, and why?

### Challenge 9: Devctl-Based Live Monitoring

Add a second `devctl_notify` event that fires every time the rate-limit bucket drops a message. The event should include the class name and the current bucket state as key=value data.

Requirements:

1. When `ppsratecheck` returns zero (message dropped), increment a per-class drop counter and emit a `devctl_notify` with `system="myfirst"`, `type="LOG_DROPPED"`, and data `"class=io drops=42"`.
2. The devctl event itself must be rate-limited; otherwise the act of reporting drops becomes another flood. Use a second `ppsratecheck` with a slow cap (e.g. 1 pps) for the devctl emissions.
3. Write a devd rule that matches the event and logs a summary every time it fires.

Test: run Lab 1's flood program and confirm that `devctl` emits the drop report without flooding itself.

## Troubleshooting Guide

When the mechanisms in this chapter misbehave, the symptoms are often indirect: a silent missing log line, a driver that refuses to load for the wrong reason, a sysctl that does not appear, a reboot that does not call your handler. This reference maps common symptoms to the mechanism that is most likely responsible, with the first places to look in the driver source.

### Symptom: `kldload` returns "Exec format error"

The module was built against a kernel ABI that does not match the running kernel. The typical cause is a mismatch between the running kernel version and the `SYSDIR` used at compile time.

Check: `uname -r` and the value of `SYSDIR` in the Makefile. If the kernel is 14.3-RELEASE but the build picked up headers from a newer 15.0-CURRENT tree, the ABI is different.

Fix: point `SYSDIR` at the source tree that matches the running kernel. The Chapter 25 Makefile uses `/usr/src/sys` by default; on a 14.3 system with matching `/usr/src`, this is correct.

### Symptom: `kldload` returns "No such file or directory" for an obvious-looking file

The file is present but the kernel's module loader cannot parse it. Common causes: the file is a stale build artefact from a different machine, or the file is corrupt.

Check: `file myfirst.ko` should report it as an ELF 64-bit LSB shared object. If it reports anything else, rebuild from source.

### Symptom: `kldload` succeeds but `kldstat` does not show the module

The loader decided to auto-unload the module. This happens when `MOD_LOAD` returned zero but `DRIVER_MODULE`'s `device_identify` did not find any device. For `myfirst`, which uses `nexus` as its parent, this should not happen; the pseudo-driver always finds `nexus`.

Check: `dmesg | tail -20` for any line like `module "myfirst" failed to register`. The message points at what went wrong.

### Symptom: `kldload` reports "module busy"

A previous instance of the driver is still loaded and has an open file descriptor somewhere. The `MOD_QUIESCE` path in the old instance returned `EBUSY`.

Check: `fstat | grep myfirst` should show the process holding the fd. Kill the process or close the fd, then retry `kldunload`.

### Symptom: `sysctl dev.myfirst.0.debug.mask=0x4` returns "Operation not permitted"

The caller is not root. sysctls with `CTLFLAG_RW` typically require root privilege unless they are explicitly marked otherwise.

Check: are you running as root? `sudo sysctl ...` or `su -` first.

### Symptom: A new sysctl does not appear in the tree

`SYSCTL_ADD_*` either was not called, or was called with the wrong context/tree pointer. The most common bug is to use `SYSCTL_STATIC_CHILDREN` for a per-device OID instead of `device_get_sysctl_tree`.

Check: inside `myfirst_sysctl_attach`, confirm that `ctx = device_get_sysctl_ctx(dev)` and `tree = device_get_sysctl_tree(dev)` are used, and that every `SYSCTL_ADD_*` call passes `ctx` as the first argument.

### Symptom: A tunable seems to be ignored

`TUNABLE_*_FETCH` runs at attach time, but only if the tunable is in the kernel environment at that moment. The common mistakes are (a) setting the tunable after the module is loaded, (b) typing the name wrong, (c) forgetting that `kenv` is not persistent.

Check:
- `kenv hw.myfirst.timeout_sec` before reloading the module. The value should be what you expect.
- The string passed to `TUNABLE_INT_FETCH` must match `kenv` exactly. A typo in one side is silent.
- Setting via `/boot/loader.conf` requires a reboot (or a `kldunload` followed by `kldload`, which rereads `loader.conf` for the specific module's tunables).

### Symptom: A log message should fire but does not appear in `dmesg`

Three common causes:

1. The debug-class bit is not set. Check `sysctl dev.myfirst.0.debug.mask`; the class bit must be enabled.
2. The rate-limit bucket is empty. If the message is emitted through `DLOG_RL`, the first few fire and the rest are suppressed silently. Set the pps cap higher via the sysctl or wait a second for the bucket to refill.
3. The message is emitted but filtered by the system's `sysctl kern.msgbuf_show_timestamp` setting or the `dmesg` buffer size (`sysctl kern.msgbuf_size`).

Check: `dmesg -c > /dev/null` to clear the buffer, reproduce the action, and re-read the buffer. The emptied buffer should contain only the driver's output.

### Symptom: A log message appears once and is then silent forever

The rate-limit bucket permanently denies the message. This happens if `rl_curpps` becomes very large and the pps cap is very low. Check that `ppsratecheck` is being called with a stable `struct timeval` and a stable `int *` (both members of the softc); a per-call stack variable would be reset to zero every call and the algorithm would fire every time.

Check: the rate-limit state must be in the softc or another persistent location, not in local variables.

### Symptom: Attach fails and the cleanup chain does not release a resource

The labelled-goto is missing a step or has a stray `return` that short-circuits the chain. Add a `device_printf(dev, "reached label fail_X\n")` at the top of each label and rerun the failure-injection lab. The label that does not print is the label that was skipped.

Common cause: an intermediate `return (error)` inserted for debugging and never removed. The compiler does not warn because the chain is still syntactically valid; the behaviour is wrong.

### Symptom: Detach panics with a "witness" warning

A lock was destroyed while still held, or a lock was acquired after its owner was destroyed. The witness subsystem catches both. The backtrace points at the lock name, which maps to the softc field.

Check: the detach chain should be the exact reverse of attach. A common mistake is `mtx_destroy(&sc->sc_mtx)` called before `destroy_dev(sc->sc_cdev)`: the cdev's callbacks can still be running, they try to take the lock, and the lock is gone. Fix: destroy the cdev first, then the lock.

### Symptom: The driver panics at module unload with a dangling pointer

The `EVENTHANDLER_DEREGISTER` was not called, the kernel fired the event, and the callback pointer pointed at freed memory.

Check: for every `EVENTHANDLER_REGISTER` in attach, search detach for `EVENTHANDLER_DEREGISTER`. The count must match. If the count matches but the panic still happens, the tag stored in the softc was corrupted; audit the code path between registration and deregistration for memory scribbles.

### Symptom: `MYFIRSTIOC_GETVER` returns an unexpected value

The ioctl version integer in `myfirst_ioctl.h` does not match what `myfirst_ioctl.c` writes into the buffer. This happens if the header is updated but the handler still returns a hard-coded constant.

Check: the handler should write `MYFIRST_IOCTL_VERSION` (the constant from the header), not a literal integer.

### Symptom: `devctl_notify` events never appear in `devd.log`

`devd(8)` is not running, or its configuration does not match the event.

Check:
- `service devd status` confirms the daemon is running.
- `grep myfirst /etc/devd/*.conf` should find the rule.
- `devd -Df` in the foreground prints every event as it arrives; reproduce the action and watch the output.

### Symptom: The 100-cycle regression script grows the kernel's memory footprint

A resource is leaking per load/unload cycle. Common culprits: a `malloc` without a matching `free`, an `EVENTHANDLER_REGISTER` without a matching deregister, a `sysctl_ctx_init` that the driver calls manually without calling `sysctl_ctx_free` at detach (Chapter 25 uses `device_get_sysctl_ctx`, which is managed by Newbus; a driver that allocates its own context must free it).

Check: `vmstat -m | grep myfirst` before and after to see the driver's own memory consumption, and `vmstat -m | grep solaris` for kernel-level structures that the driver might be allocating indirectly.

### Symptom: Two concurrent `MYFIRSTIOC_SETMSG` calls interleave their writes

The softc mutex is not being held around the update. The two threads are writing into `sc->sc_msg` simultaneously, producing a corrupt result.

Check: every access to `sc->sc_msg` and `sc->sc_msglen` in the ioctl handler must be inside `mtx_lock(&sc->sc_mtx) ... mtx_unlock(&sc->sc_mtx)`.

### Symptom: A sysctl value resets on every module load

This is the intended behaviour, not a bug. The attach-time default is whatever the tunable evaluates to, which is either the `TUNABLE_INT_FETCH` default or the value set via `kenv`. The runtime sysctl write is lost at unload. If you want the value to persist, set it via `kenv` or `/boot/loader.conf`.

### Symptom: `MYFIRSTIOC_GETCAPS` returns a value without the bits you just added

The `myfirst_ioctl.c` file was updated but not recompiled, or the wrong build was loaded. Also check that the handler in the switch statement uses the `|=` operator or a single assignment that includes every bit.

Check: `make clean && make` from the example directory. `kldstat -v | grep myfirst` to confirm the loaded module's path matches what you built.

### Symptom: A SYSINIT fires before the kernel's allocator is ready

The SYSINIT is registered at too early a subsystem ID. Many subsystems (tunables, locks, early initcalls) are not allowed to call `malloc` with `M_NOWAIT` let alone `M_WAITOK`. If your callback calls `malloc` and the kernel panics at boot, look at the subsystem ID.

Check: the subsystem ID in `SYSINIT(...)`. For a callback that allocates memory, use `SI_SUB_DRIVERS` or later; do not use `SI_SUB_TUNABLES` or earlier.

### Symptom: A handler registered with `EVENTHANDLER_PRI_FIRST` still runs late

`EVENTHANDLER_PRI_FIRST` is not a hard guarantee; it is a priority in a sorted queue. If another handler is also registered with `EVENTHANDLER_PRI_FIRST`, the order between them is undefined. The documented priorities are coarse; fine-grained ordering is not supported.

Check: accept that the priority is a hint, not a contract. If the driver absolutely requires running before or after a specific other handler, the design is wrong; restructure the driver so the ordering does not matter.

### Symptom: `dmesg` does not show any output from the driver

The driver is using `printf` (the libc-like one) rather than `device_printf` or `DPRINTF`. Kernel `printf` still works but does not carry the device name, which makes messages hard to filter.

Check: every message in the driver should go through `device_printf(dev, ...)` or `DPRINTF(sc, class, fmt, ...)`. Bare `printf` is usually a mistake.

## Quick Reference

The Quick Reference is a single-page summary of the macros, flags, and functions Chapter 25 introduced. Use it at the keyboard once the material is familiar.

### Rate-limited logging

```c
struct myfirst_ratelimit {
	struct timeval rl_lasttime;
	int            rl_curpps;
};

#define DLOG_RL(sc, rlp, pps, fmt, ...) do {                            \
	if (ppsratecheck(&(rlp)->rl_lasttime, &(rlp)->rl_curpps, pps)) \
		device_printf((sc)->sc_dev, fmt, ##__VA_ARGS__);        \
} while (0)
```

Use `DLOG_RL` for any message that can fire in a loop. Place the `struct myfirst_ratelimit` in the softc (not on the stack).

### Errno vocabulary

| Errno | Value | Use |
|-------|-------|-----|
| `0` | 0 | success |
| `EPERM` | 1 | operation not permitted (root-only) |
| `ENOENT` | 2 | no such file |
| `EBADF` | 9 | bad file descriptor |
| `ENOMEM` | 12 | cannot allocate memory |
| `EACCES` | 13 | permission denied |
| `EFAULT` | 14 | bad address (user pointer) |
| `EBUSY` | 16 | resource busy |
| `ENODEV` | 19 | no such device |
| `EINVAL` | 22 | invalid argument |
| `ENOTTY` | 25 | inappropriate ioctl for device |
| `ENOTSUP` / `EOPNOTSUPP` | 45 | operation not supported |
| `ENOIOCTL` | -3 | ioctl not handled by this driver (internal; kernel maps to `ENOTTY`) |

### Tunable families

```c
TUNABLE_INT_FETCH("hw.myfirst.name",    &sc->sc_int_var);
TUNABLE_LONG_FETCH("hw.myfirst.name",   &sc->sc_long_var);
TUNABLE_BOOL_FETCH("hw.myfirst.name",   &sc->sc_bool_var);
TUNABLE_STR_FETCH("hw.myfirst.name",     sc->sc_str_var,
                                          sizeof(sc->sc_str_var));
```

Call each fetch once in attach after the default has been populated. The fetch updates the variable only if the tunable is present.

### Sysctl flag summary

| Flag | Meaning |
|------|---------|
| `CTLFLAG_RD` | read-only |
| `CTLFLAG_RW` | read-write |
| `CTLFLAG_TUN` | cooperate with a loader tunable at attach time |
| `CTLFLAG_RDTUN` | shorthand for read-only + tunable |
| `CTLFLAG_RWTUN` | shorthand for read-write + tunable |
| `CTLFLAG_MPSAFE` | handler is MPSAFE |
| `CTLFLAG_SKIP` | hide the OID from default `sysctl(8)` listings |

### Version identifiers

- `MYFIRST_VERSION`: human-readable release string, e.g. `"1.8-maintenance"`.
- `MODULE_VERSION(myfirst, N)`: integer used by `MODULE_DEPEND`.
- `MYFIRST_IOCTL_VERSION`: integer returned by `MYFIRSTIOC_GETVER`; bumped only for wire-format breaks.

### Capability bits

```c
#define MYF_CAP_RESET    (1U << 0)
#define MYF_CAP_GETMSG   (1U << 1)
#define MYF_CAP_SETMSG   (1U << 2)
#define MYF_CAP_TIMEOUT  (1U << 3)

#define MYFIRSTIOC_GETCAPS  _IOR('M', 5, uint32_t)
```

### Labelled-cleanup skeleton

```c
static int
myfirst_attach(device_t dev)
{
	struct myfirst_softc *sc = device_get_softc(dev);
	int error;

	/* acquire resources in order */
	mtx_init(&sc->sc_mtx, "myfirst", NULL, MTX_DEF);

	error = make_dev_s(...);
	if (error != 0)
		goto fail_mtx;

	myfirst_sysctl_attach(sc);

	error = myfirst_log_attach(sc);
	if (error != 0)
		goto fail_cdev;

	sc->sc_shutdown_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    myfirst_shutdown, sc, SHUTDOWN_PRI_DEFAULT);
	if (sc->sc_shutdown_tag == NULL) {
		error = ENOMEM;
		goto fail_log;
	}

	return (0);

fail_log:
	myfirst_log_detach(sc);
fail_cdev:
	destroy_dev(sc->sc_cdev);
fail_mtx:
	mtx_destroy(&sc->sc_mtx);
	return (error);
}
```

### File layout for a modular driver

```text
driver.h           public types
driver.c           module glue, cdevsw, attach/detach
driver_cdev.c      open/close/read/write
driver_ioctl.h     ioctl command numbers
driver_ioctl.c     ioctl dispatch
driver_sysctl.c    sysctl tree
driver_debug.h     DPRINTF macros
driver_log.h       rate-limit structures
driver_log.c       rate-limit helpers
```

### Production checklist

```text
[  ] MODULE_DEPEND declared for every real dependency.
[  ] MODULE_PNP_INFO declared if the driver binds to hardware.
[  ] MOD_QUIESCE answers "can you unload?" without side effects.
[  ] devctl_notify emitted for operator-relevant events.
[  ] MAINTENANCE.md current.
[  ] devd.conf snippet included.
[  ] Every log message is device_printf, includes errno,
     and is rate-limited if it can fire in a loop.
[  ] attach/detach survives 100 load/unload cycles.
[  ] sysctls reject out-of-range values.
[  ] ioctl payload is bounds-checked.
[  ] Failure paths exercised via deliberate injection.
[  ] Versioning discipline: three independent version
     identifiers, each bumped for its own reason.
```

### SYSINIT subsystem IDs

| Constant | Value | Use |
|----------|-------|-----|
| `SI_SUB_TUNABLES` | 0x0700000 | establish tunable values |
| `SI_SUB_KLD` | 0x2000000 | KLD and module setup |
| `SI_SUB_SMP` | 0x2900000 | start the APs |
| `SI_SUB_DRIVERS` | 0x3100000 | let drivers initialise |
| `SI_SUB_CONFIGURE` | 0x3800000 | configure devices |

Within a subsystem:
- `SI_ORDER_FIRST` = 0x0
- `SI_ORDER_SECOND` = 0x1
- `SI_ORDER_MIDDLE` = 0x1000000
- `SI_ORDER_ANY` = 0xfffffff

### Shutdown event priorities

- `SHUTDOWN_PRI_FIRST`: run early.
- `SHUTDOWN_PRI_DEFAULT`: default.
- `SHUTDOWN_PRI_LAST`: run late.

### EVENTHANDLER skeleton

```c
sc->sc_tag = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    my_handler, sc, SHUTDOWN_PRI_DEFAULT);
/* ... in detach ... */
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_tag);
```

### Tunable name ladders

Tunables and sysctls follow a hierarchical naming convention. The table below lists the nodes this chapter introduces:

| Name | Kind | Purpose |
|------|------|---------|
| `hw.myfirst.debug_mask_default` | tunable | initial debug mask for every instance |
| `hw.myfirst.timeout_sec` | tunable | initial operation timeout in seconds |
| `hw.myfirst.max_retries` | tunable | initial retry count |
| `hw.myfirst.log_ratelimit_pps` | tunable | initial messages-per-second cap |
| `dev.myfirst.<unit>.version` | sysctl (RD) | release string |
| `dev.myfirst.<unit>.open_count` | sysctl (RD) | active fd count |
| `dev.myfirst.<unit>.total_reads` | sysctl (RD) | lifetime read calls |
| `dev.myfirst.<unit>.total_writes` | sysctl (RD) | lifetime write calls |
| `dev.myfirst.<unit>.message` | sysctl (RD) | current buffer contents |
| `dev.myfirst.<unit>.message_len` | sysctl (RD) | current buffer length |
| `dev.myfirst.<unit>.timeout_sec` | sysctl (RWTUN) | runtime timeout |
| `dev.myfirst.<unit>.max_retries` | sysctl (RWTUN) | runtime retry count |
| `dev.myfirst.<unit>.log_ratelimit_pps` | sysctl (RWTUN) | runtime pps cap |
| `dev.myfirst.<unit>.debug.mask` | sysctl (RWTUN) | runtime debug mask |
| `dev.myfirst.<unit>.debug.classes` | sysctl (RD) | class names and bit values |

Read the table as the interface contract. The `hw.myfirst.*` family is set at boot; the `dev.myfirst.*` family is adjusted at run time. Every writable entry has a matching read-only counterpart the operator can use to confirm the current value.

### Ioctl command ladder

Chapter 25's ioctl header defines these commands under magic `'M'`:

| Command | Number | Direction | Purpose |
|---------|--------|-----------|---------|
| `MYFIRSTIOC_GETVER` | 0 | read | returns `MYFIRST_IOCTL_VERSION` |
| `MYFIRSTIOC_RESET` | 1 | no-data | zero read/write counters |
| `MYFIRSTIOC_GETMSG` | 2 | read | copy current message out |
| `MYFIRSTIOC_SETMSG` | 3 | write | copy new message in |
| (retired) | 4 | n/a | reserved; do not reuse |
| `MYFIRSTIOC_GETCAPS` | 5 | read | capability bitmask |

Adding a new command means picking the next unused number. Retiring a command means leaving its number in the ladder with `(retired)` next to it, not reusing the number.

## Real-World Driver Walkthroughs

The chapter so far has built its discipline on the `myfirst` pseudo-driver. This section turns the lens around and looks at how the same disciplines appear in drivers that ship as part of FreeBSD 14.3. Each walkthrough starts from a real source file in `/usr/src`, names the Chapter 25 pattern that is in play, and points at the lines where the pattern is visible. The goal is not to document the specific drivers (their own documentation does that), but to show that Chapter 25's habits are not invented: they are the habits that working in-tree drivers already practise.

Reading real drivers with a pattern vocabulary is the single fastest way to accelerate your own judgement. Once you recognise `ppsratecheck` on sight, every driver that uses it becomes faster to read.

### `mmcsd(4)`: Rate-Limited Error Logging on a Hot Path

The `mmcsd` driver at `/usr/src/sys/dev/mmc/mmcsd.c` serves MMC and SD card storage. A filesystem above it generates a continuous stream of block I/O, and each block that fails the underlying MMC request produces a potential error log line. Without rate limiting, a slow or flaky card would flood `dmesg` in seconds.

The driver declares its rate-limit state per-softc, as the chapter recommends:

```c
struct mmcsd_softc {
	...
	struct timeval log_time;
	int            log_count;
	...
};
```

`log_time` and `log_count` are the `ppsratecheck` state. Every hot path that emits a log message wraps the `device_printf` the same way:

```c
#define LOG_PPS  5 /* Log no more than 5 errors per second. */

...

if (req.cmd->error != MMC_ERR_NONE) {
	if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
		device_printf(dev, "Error indicated: %d %s\n",
		    req.cmd->error,
		    mmcsd_errmsg(req.cmd->error));
	...
}
```

The pattern is exactly the `DLOG_RL` shape the chapter introduced, with the macro expanded in place. `LOG_PPS` is set to 5 messages per second, and the state lives in the softc so repeated calls into the hot path share the same bucket.

Three observations worth taking away. First, this pattern is not theoretical: a shipping FreeBSD driver uses it on a hot path that can fire thousands of times per second. Second, the macro-versus-inline choice is a matter of taste; `mmcsd.c` open-codes the call, and the pattern is just as readable. Third, the `LOG_PPS` constant is conservative (5 per second); the author preferred fewer messages over more. A driver author can tune the pps cap to match the expected error rate and the operator's tolerance.

### `uftdi(4)`: Module Dependencies and PNP Metadata

The `uftdi` driver at `/usr/src/sys/dev/usb/serial/uftdi.c` attaches to USB-serial adapters based on FTDI chips. It is a textbook example of a driver that depends on another kernel module: it cannot work without the USB stack.

Near the bottom of the file:

```c
MODULE_DEPEND(uftdi, ucom, 1, 1, 1);
MODULE_DEPEND(uftdi, usb, 1, 1, 1);
MODULE_VERSION(uftdi, 1);
```

Two dependencies are declared. The first is on `ucom`, the generic USB-serial framework that `uftdi` builds on. The second is on `usb`, the USB core. Both are bounded to version 1 exactly. Loading `uftdi.ko` on a kernel without `usb` or `ucom` fails with a clear error; loading it on a kernel where the subsystem version has bumped past 1 also fails until `uftdi`'s own declaration is updated.

The PNP metadata is published through a macro that expands to `MODULE_PNP_INFO`:

```c
USB_PNP_HOST_INFO(uftdi_devs);
```

`USB_PNP_HOST_INFO` is the USB-specific helper defined in `/usr/src/sys/dev/usb/usbdi.h`. It expands to `MODULE_PNP_INFO` with the correct format string for USB vendor/product tuples. `uftdi_devs` is a static array of `struct usb_device_id` entries, one per (vendor, product, interface) triple the driver handles.

This is the production-readiness pattern from Chapter 25 Section 7 applied to a real hardware driver: dependencies declared, metadata published, version integer present. A new USB serial adapter appearing on a system causes `devd(8)` to consult the PNP metadata, identify `uftdi` as the driver, and load it if it is not already loaded. The mechanism is fully automatic once the metadata is right.

Chapter 26's version of `myfirst` will use the same pattern and the same helper.

### `iscsi(4)`: Shutdown Handler Registered in `attach`

The iSCSI initiator at `/usr/src/sys/dev/iscsi/iscsi.c` holds open connections to remote storage targets. When the system shuts down, the initiator must close those connections gracefully before the network layer is torn down; otherwise the remote end is left with stale sessions.

The shutdown handler is registered at attach:

```c
sc->sc_shutdown_pre_eh = EVENTHANDLER_REGISTER(shutdown_pre_sync,
    iscsi_shutdown_pre, sc, SHUTDOWN_PRI_FIRST);
```

Two details are important. First, the registration's tag is stored in the softc (`sc->sc_shutdown_pre_eh`), so the later deregistration can reference it. Second, the priority is `SHUTDOWN_PRI_FIRST`, not `SHUTDOWN_PRI_DEFAULT`: the iSCSI driver wants to close connections before anyone else begins shutdown work, because storage connections take time to close cleanly.

The deregistration happens in the detach path:

```c
EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->sc_shutdown_pre_eh);
```

One registration, one deregistration. The tag in the softc keeps them bound.

For `myfirst`, the chapter's demonstration used `SHUTDOWN_PRI_DEFAULT` because there is no good reason for the pseudo-driver to run early. Real drivers pick a priority based on what depends on what: drivers that must be quiet before other drivers pick `SHUTDOWN_PRI_FIRST`; drivers that depend on filesystems being intact pick `SHUTDOWN_PRI_LAST`. The priority is a design decision, and `iscsi` shows one way to make it.

### `ufs_dirhash`: Cache Eviction on `vm_lowmem`

The UFS directory hash cache at `/usr/src/sys/ufs/ufs/ufs_dirhash.c` is a per-filesystem in-memory accelerator for directory lookups. Under normal operation the cache is beneficial; under memory pressure it becomes a liability, so the subsystem registers a `vm_lowmem` handler that discards cache entries:

```c
EVENTHANDLER_REGISTER(vm_lowmem, ufsdirhash_lowmem, NULL,
    EVENTHANDLER_PRI_FIRST);
```

The fourth argument is `EVENTHANDLER_PRI_FIRST`, which asks to run early in the list of registered `vm_lowmem` handlers. The dirhash author picked early execution because the cache is pure reclaimable memory: freeing it promptly gives other handlers (which may hold dirtier or less easily released state) a better chance of succeeding.

The callback itself does the real work: walk the hash tables, release entries that can be released, account for the memory freed. The essential design point is that the callback does not panic if there is nothing to release; it simply returns having done nothing.

This is the Chapter 25 `vm_lowmem` pattern in a subsystem that is not a device driver but that shares the discipline. The lessons transfer: if `myfirst` ever gets a cache, the shape is already here.

### `tcp_subr`: `vm_lowmem` Without a Softc

The TCP subsystem at `/usr/src/sys/netinet/tcp_subr.c` also registers for `vm_lowmem`, but its registration is different in an instructive way:

```c
EVENTHANDLER_REGISTER(vm_lowmem, tcp_drain, NULL, LOWMEM_PRI_DEFAULT);
```

The third argument (the callback data) is `NULL`, not a softc pointer. The TCP subsystem does not have a single softc; its state is scattered across many structures. The callback has to find its state by other means (globals, per-CPU variables, hash table lookups).

This raises a question the chapter hinted at: when is it acceptable to pass `NULL` as the callback argument? The answer is: when the callback has another way to find its state. For a driver with a per-device softc, passing `sc` is almost always the right choice. For a subsystem with global state, passing `NULL` and letting the callback use its known globals is fine.

`myfirst` will always pass `sc` because `myfirst` is a per-device driver. A reader who finds themselves writing a subsystem-level callback should recognise that the pattern changes in subtle ways when the subject is global.

### `if_vtnet`: Rate-Limited Logging Around a Specific Fault

The VirtIO network driver at `/usr/src/sys/dev/virtio/network/if_vtnet.c` provides a narrower but instructive counterpoint to `mmcsd`. Where `mmcsd` wraps every hot-path error in rate-limited logging, `if_vtnet` reaches for `ppsratecheck` only around a specific misbehaviour: a TSO packet with ECN bits set that the VirtIO host has not negotiated. The call site is small and self-contained:

```c
static struct timeval lastecn;
static int curecn;
...
if (ppsratecheck(&lastecn, &curecn, 1))
        if_printf(sc->vtnet_ifp,
            "TSO with ECN not negotiated with host\n");
```

Two details are worth pointing out. First, the rate-limit state is declared `static` at file scope, not per-softc. The author decided that "the VirtIO host disagrees with the guest about ECN" is a system-level misconfiguration rather than a per-interface fault, so one shared bucket is enough; per-softc state would let a single virtual interface's flood starve another's. Second, the cap is 1 pps, which is deliberately aggressive: the warning is informational and should appear no more than once per second across the entire system. A driver designer who expected the warning to fire often could raise the cap.

FreeBSD also provides `ratecheck(9)`, the event-count sibling of `ppsratecheck`. `ratecheck` fires when the time since the last allowed event exceeds a threshold; `ppsratecheck` fires when the recent burst rate is below a cap. They are complementary: `ratecheck` is better when you want a minimum interval between messages, while `ppsratecheck` is better when you want a maximum burst rate.

The takeaway from `if_vtnet` is that rate-limit state can be global or per-instance, and the choice follows the expected shape of the failure. Chapter 25 put the state in the softc because `myfirst`'s errors are per-instance; a different driver might reasonably make a different choice.

### `vt_core`: Multiple `EVENTHANDLER` Registrations in Sequence

The virtual terminal subsystem at `/usr/src/sys/dev/vt/vt_core.c` registers multiple event handlers at different points in its lifecycle. The shutdown handler for the console window is one:

```c
EVENTHANDLER_REGISTER(shutdown_pre_sync, vt_window_switch,
    vw, SHUTDOWN_PRI_DEFAULT);
```

`SHUTDOWN_PRI_DEFAULT` is the neutral priority: the VT switch runs after anything that asked for `SHUTDOWN_PRI_FIRST` and before anything that asked for `SHUTDOWN_PRI_LAST`. The choice is deliberate: the terminal switch has no ordering requirement against other subsystems, so the author picked the default rather than claiming a priority the driver did not need.

The point for our purposes is that `vt_core` registers this handler once, at boot time, and never deregisters it. A driver whose lifetime is the kernel's lifetime does not need to deregister; the kernel never calls the handler after it is gone. A driver like `myfirst` that can be loaded and unloaded as a module does need to deregister, because unloading the module destroys the handler's code. The rule is: deregister if your code can disappear while the kernel keeps running.

This distinction is important for module authors. Built-in code paths often look like they are missing their deregistration, but they are not; they just do not need it. A module that follows the built-in pattern verbatim will panic on unload.

### FFS (`ffs_alloc.c`): Per-Condition Rate-Limit Buckets

The FFS allocator at `/usr/src/sys/ufs/ffs/ffs_alloc.c` is a filesystem rather than a device driver, but it faces exactly the log-flood problem Chapter 25 is about. A disk that repeatedly runs out of blocks or inodes can emit one error per failed `write(2)` call, which is unbounded in practice. The allocator reaches for `ppsratecheck` at four distinct sites, and the way it scopes the rate-limit state is a good lesson in bucket design.

Each mounted filesystem carries rate-limit state in its mount structure. Two separate buckets handle two separate kinds of error:

```c
/* "Filesystem full" reports: blocks or inodes exhausted. */
um->um_last_fullmsg
um->um_secs_fullmsg

/* Cylinder-group integrity reports. */
um->um_last_integritymsg
um->um_secs_integritymsg
```

The "filesystem full" bucket is shared across two code paths (`ffs_alloc` and `ffs_realloccg`, both writing a message like "write failed, filesystem is full") plus inode exhaustion. The integrity bucket is shared across two different integrity failures (cylinder checkhash mismatch and magic-number mismatch). At each call site the shape is identical:

```c
if (ppsratecheck(&ump->um_last_fullmsg,
    &ump->um_secs_fullmsg, 1)) {
        UFS_UNLOCK(ump);
        ffs_fserr(fs, ip->i_number, "filesystem full");
        uprintf("\n%s: write failed, filesystem is full\n",
            fs->fs_fsmnt);
        ...
}
```

Three design decisions are visible. First, the rate-limit state is per mount point, not global. A heavy-write filesystem should not suppress messages from a different filesystem that is also filling up. Second, the cap is 1 pps: at most one message per second per mount per bucket. Third, related messages share a bucket (all "fullness" messages; all "integrity" messages), while unrelated ones have their own. The author of `ffs_alloc.c` decided that an operator who is flooded with "filesystem full" should not also be flooded with "out of inodes" for the same mount point; the two are symptoms of the same condition and one message per second is enough.

A pseudo-driver like `myfirst` can borrow the pattern directly. If `myfirst` someday grows a class of errors related to capacity (say, "buffer full" for the write path and "no free slots" for the ioctl path), those belong in the same bucket. A completely different failure (say, "command version mismatch") deserves its own. The discipline that `ffs_alloc.c` applies to a filesystem carries over unchanged to a device driver.

### Reading More Drivers

Every FreeBSD driver is a case study in how a specific team solved the problems Chapter 25 names. A few areas of `/usr/src/sys` are particularly rich for pattern-hunting:

- `/usr/src/sys/dev/usb/` for USB drivers: `MODULE_DEPEND` and `MODULE_PNP_INFO` everywhere.
- `/usr/src/sys/dev/pci/` for PCI drivers: labelled-cleanup attach routines at industrial scale.
- `/usr/src/sys/dev/cxgbe/` for a complex modern driver: rate-limited logging, sysctl trees with hundreds of OIDs, versioning via module ABI.
- `/usr/src/sys/netinet/` for subsystem-level `EVENTHANDLER` usage.
- `/usr/src/sys/kern/subr_*.c` for examples of `SYSINIT` at many different subsystem IDs.

When you read a new driver, start by finding its attach function. Count the acquisitions and the labels; they should match. Find the detach function. Confirm it releases the resources in reverse order. Find the error paths and see which errno each one returns. Look for `ppsratecheck` or `ratecheck` near any log message that fires on a hot path. Look for `MODULE_DEPEND` declarations. Look for `EVENTHANDLER_REGISTER` and confirm there is a matching deregister.

Every one of these inspections takes seconds once the patterns are familiar. Each one strengthens your own instinct for when a pattern is or is not being applied correctly.

### What You Will Not See

A few patterns that Chapter 25 recommends do not appear in every driver, and their absence is not always a bug. Knowing which patterns are optional keeps you from expecting them everywhere.

`MAINTENANCE.md` is a habit recommended by this book, not a FreeBSD requirement. Most in-tree drivers do not ship a per-driver maintenance file; instead, the manual page is the operator-facing reference, and the release notes carry the change log. Both solutions work; the choice between them is a project convention.

`devctl_notify` is optional. Many drivers do not emit any events, and there is no rule that they must. The pattern is valuable when there are events that operators would actually want to react to; for drivers with quiet behaviour and no operator-visible state changes, emitting events is unnecessary.

`SYSINIT` outside of `DRIVER_MODULE` is uncommon in modern drivers. Most driver-level work happens in `device_attach`, which runs per-instance. Explicit `SYSINIT` registrations are most common in subsystems and core kernel code; individual drivers rarely need one. Chapter 25 introduced `SYSINIT` because the reader will meet it eventually, not because most drivers use it.

The explicit module event handler (a driver-supplied `MOD_LOAD`/`MOD_UNLOAD` function instead of `DRIVER_MODULE`) is also uncommon. It is there when a driver needs custom behaviour at load time that does not fit the Newbus model, but most drivers happily use the default.

When you read a driver that omits one of these patterns, the absence usually reflects a design decision specific to that driver, not a failure of discipline. The patterns are tools; not every tool is needed on every job.

### Wrapping Up the Walkthroughs

Every pattern the chapter introduced is visible somewhere in the FreeBSD source tree. The `mmcsd` driver rate-limits its hot-path logs. The `uftdi` driver declares its module dependencies and PNP metadata. The `iscsi` driver registers a prioritised `shutdown_pre_sync` handler. The UFS dirhash cache releases memory on `vm_lowmem`. Each of these is a real, shipping, tested application of a Chapter 25 discipline.

Reading these drivers with a pattern vocabulary accelerates your own intuition faster than any single-author textbook can. The patterns repeat; the drivers differ. Once you can name the pattern, every new driver you read reinforces it.

The `myfirst` driver at the end of Chapter 25 wears every discipline. You have just seen the same disciplines worn by eight other drivers. The next step is to wear them yourself, on a driver that is not in this book. That is the work of a career, and the foundations are now in your hands.

## Wrapping Up

Chapter 25 is the longest chapter in Part 5, and its length is deliberate. Every section introduced a discipline that turns a working driver into a maintainable one. None of the disciplines are glamorous; each one is the specific habit that keeps a driver working when it is used by people other than its author.

The chapter began with rate-limited logging. A driver that cannot spam the console is a driver whose log messages are worth reading. `ppsratecheck(9)` and the `DLOG_RL` macro make the discipline mechanical: place the rate-limit state in the softc, wrap hot-path messages with the macro, and let the kernel do the per-second bookkeeping. Everything downstream of logging benefits, because a log you can read is a log you can debug from.

The second section named errno values and told them apart. `ENOTTY` is not `EINVAL`; `EPERM` is not `EACCES`; `ENOIOCTL` is the kernel's special signal that a driver did not recognise an ioctl command and wants the kernel to try another handler before giving up. Knowing the vocabulary turns vague bug reports into precise ones, and precise bug reports reach root causes faster.

Section 3 treated configuration as a first-class concern. A tunable is a boot-time initial value; a sysctl is a runtime handle. The two cooperate through `TUNABLE_*_FETCH` and `CTLFLAG_TUN`. A driver that exposes exactly the right tunables and sysctls gives operators enough control to solve their own problems without having to modify the source. The `hw.myfirst.timeout_sec` and `dev.myfirst.0.timeout_sec` pair is now the template every future tunable will follow.

Section 4 named the three version identifiers a driver needs (release string, module integer, ioctl wire integer) and pointed out that they change for different reasons. The `GETCAPS` ioctl gives user-space a run-time discovery mechanism that survives the addition and removal of capabilities over time, and the `MODULE_DEPEND` macro makes inter-module compatibility explicit. The `myfirst` driver's own jump from 1.7 to 1.8 is a small but complete case study.

Section 5 named the labelled-goto pattern and made it the unconditional discipline for attach and detach. Every resource gets a label; every label falls through to the next; the single `return (0)` separates the acquisition chain from the cleanup chain. The pattern scales from two resources to a dozen without changing shape. Deliberate failure injection confirms that every label is reachable.

Section 6 split `myfirst` into subject-matter files. One file per concern, a single responsibility rule for deciding where to put new code, and a small public header that carries only the declarations every file needs. The driver is now at phase two of the typical multi-file split, and the rules for reaching phase three are named explicitly.

Section 7 turned the focus outward. The production-readiness checklist covered module dependencies, PNP metadata, `MOD_QUIESCE`, `devctl_notify`, `MAINTENANCE.md`, `devd` rules, log message quality, the 100-cycle regression test, input validation, failure-path exercise, and versioning discipline. Every item on the checklist is a specific habit that catches a specific failure mode. `myfirst` at the end of Chapter 25 passes every item.

Section 8 closed the loop with `SYSINIT(9)` and `EVENTHANDLER(9)`. A driver that needs to participate in the kernel lifecycle beyond its own attach/detach window has a clean mechanism for doing so. `myfirst` registers for `shutdown_pre_sync` as a demonstration; other drivers register for `vm_lowmem`, suspend/resume, or custom events.

The driver has grown from a single file with one message buffer into a modular, observable, version-disciplined, rate-limited, and production-ready pseudo-device. The mechanics of FreeBSD character-driver authorship are now in the reader's hands.

What has not been covered yet is the world of hardware. Every chapter so far has used a pseudo-driver; the character device backs a software buffer and a set of counters. A driver that actually talks to hardware has to allocate bus resources, map I/O windows, attach interrupt handlers, program DMA engines, and survive the specific failure modes of real devices. Chapter 26 starts that work.

## Part 5 Checkpoint

Part 5 covered debugging, tracing, and the engineering practices that separate a driver that merely compiles from a driver that a team can maintain. Before Part 6 changes the terrain by attaching `myfirst` to real transports, confirm that the Part 5 habits are in your fingers and not just in your notes.

By the end of Part 5 you should be comfortable doing each of the following:

- Investigating driver behaviour with the right tool for the question: `ktrace` and `kdump` for per-process system calls, `ddb` for live or post-mortem kernel inspection, `kgdb` with a core dump for crashed kernels, `dtrace` and SDT probes for in-kernel tracing without source changes, and `procstat` together with `lockstat` for state and contention views.
- Reading a panic message and extracting the right breadcrumbs, turning a stack trace into a reproducible test case rather than a guessing game.
- Building and running the Chapter 24 integration stack `myfirst`: a driver that exposes its surfaces through ioctl, sysctl, DTrace probes, and `devctl_notify` while exercising a full lifecycle injection path.
- Applying the Chapter 25 production-readiness checklist item by item: `MODULE_DEPEND`, PNP metadata, `MOD_QUIESCE` as a separate handler, `devctl_notify`, `MAINTENANCE.md`, `devd` rules, log message quality, the 100-cycle regression test, input validation, deliberate failure-path exercise, and version discipline.
- Using `SYSINIT(9)` and `EVENTHANDLER(9)` when a driver must participate in the kernel lifecycle beyond its own attach and detach window, including graceful registration and deregistration.

If any of these still feels soft, the labs to revisit are:

- Debugging tools in practice: Lab 23.1 (A First DDB Session), Lab 23.2 (Measuring the Driver with DTrace), Lab 23.4 (Finding a Memory Leak with vmstat -m), and Lab 23.5 (Installing the 1.6-debug Refactor).
- Integration and observability: Lab 1 (Build and Load the Stage 1 Driver), Lab 4 (Walk the Lifecycle by Injecting a Failure), Lab 5 (Trace the Integration Surfaces with DTrace), and Lab 6 (Integration Smoke Test) in Chapter 24.
- Production discipline: Lab 3 (Tunable Reboot Behaviour), Lab 4 (Deliberate Attach Failure Injection), Lab 6 (The 100-Cycle Regression Script), and Lab 10 (Multi-Version Compatibility Matrix) in Chapter 25.

Part 6 expects the following as a baseline:

- Confidence reading a panic and following it to a root cause, since three new transports will introduce three new ways for things to go wrong.
- A `myfirst` that passes the Chapter 25 production checklist, so that Chapter 26 can add USB without the earlier discipline wobbling under new load.
- Awareness that Part 6 changes the running-example pattern: the `myfirst` thread will extend through Chapter 26 as `myfirst_usb`, then pause while Chapters 27 and 28 use fresh, transport-shaped demos. The discipline continues; only the code artefact changes. Chapter 29 returns to the cumulative shape.

If those hold, you are ready for Part 6. If one still looks unsteady, the fix is a single well-chosen lab, not a rush forward.

## Bridge to Chapter 26

Chapter 26 turns the `myfirst` driver outward. Instead of serving a buffer in RAM, the driver will attach to a real bus and service a real device. The book's first hardware target is the USB subsystem: USB is ubiquitous, well-documented, and has a clean kernel interface that is easier to start with than PCI or ISA.

The habits you have built in Chapter 25 carry over unchanged. The labelled-goto pattern in attach scales to bus resource allocations: `bus_alloc_resource`, `bus_setup_intr`, `bus_dma_tag_create`, each becomes another acquisition in the chain, each with its own label. The modular file layout extends naturally: `myfirst_usb.c` joins `myfirst.c`, `myfirst_cdev.c`, and the rest. The production checklist adds two items: `MODULE_DEPEND(myfirst_usb, usb, ...)` and `MODULE_PNP_INFO` for the vendor/product identifiers the driver handles. The versioning discipline continues; the Chapter 26 driver will be 1.9-usb.

What is genuinely new in Chapter 26 is not the driver's structure; it is the interfaces between the driver and the USB subsystem. You will meet `usb_request_methods`, transfer setup callbacks, the separation between control, bulk, interrupt, and isochronous transfers, and the USB-specific lifecycle (attach is per-device, just like Newbus; detach is per-device, just like Newbus; hot-plug is the normal operating condition, unlike most other buses).

Before you start Chapter 26, pause and confirm that the Chapter 25 material is solid. The labs are meant to be run; running them is the single best way to find the parts of the chapter that did not land. If you have run Labs 1 through 7 and the challenge exercises are clear, you are ready for hardware.

Chapter 26 begins with a simple USB device (a FTDI serial adapter, most likely) and walks through probe, attach, bulk transfer setup, read/write dispatch, and the detach/hot-unplug paths. By the end of Chapter 26, `myfirst_usb.ko` will serve real bytes from a real cable, and the discipline from Chapter 25 will keep it maintainable.

## Glossary

The terms in this glossary all appear in Chapter 25 and are worth knowing by name. Definitions are short on purpose; the chapter body is where each concept is developed.

**Attach chain.** The ordered sequence of resource acquisitions in a driver's `device_attach` method. Each acquisition that can fail is followed by a goto to the label that undoes the previously acquired resources.

**Capability bitmask.** A 32-bit (or 64-bit) integer returned by `MYFIRSTIOC_GETCAPS`, with one bit per optional feature. Lets user-space query the driver at run time for which features it supports.

**Cleanup chain.** The ordered sequence of labels at the bottom of a driver's `device_attach` method. Each label releases one resource and falls through to the next. Reverse of the acquisition order.

**`CTLFLAG_SKIP`.** A sysctl flag that hides an OID from default `sysctl(8)` listings. The OID is still readable when its full name is given explicitly. Defined in `/usr/src/sys/sys/sysctl.h`.

**`CTLFLAG_RDTUN` / `CTLFLAG_RWTUN`.** Shorthand for `CTLFLAG_RD | CTLFLAG_TUN` and `CTLFLAG_RW | CTLFLAG_TUN` respectively. Declares a sysctl that cooperates with a loader tunable.

**`devctl_notify`.** Kernel function that emits a structured event readable by `devd(8)`. Lets a driver notify user-space daemons of interesting state changes.

**`DLOG_RL`.** Macro wrapper over `ppsratecheck` and `device_printf` that caps a log message at a given messages-per-second rate.

**Errno.** A small positive integer representing a specific failure mode. FreeBSD's errno table is in `/usr/src/sys/sys/errno.h`.

**Event handler.** A callback registered with `EVENTHANDLER_REGISTER` that runs whenever the kernel fires a named event. Deregistered with `EVENTHANDLER_DEREGISTER`.

**`EVENTHANDLER(9)`.** FreeBSD's generic event-notification framework. Defines events, lets subsystems publish them, lets drivers subscribe.

**Failure injection.** A deliberate test technique that causes a code path to fail in order to exercise its cleanup. Usually implemented as a conditional `return` guarded by an `#ifdef`.

**Labelled-goto pattern.** See "attach chain" and "cleanup chain". The idiomatic FreeBSD shape for attach and detach that uses `goto label;` for linear unwinding rather than nested `if`.

**`MOD_QUIESCE`.** The module event that asks "can you be unloaded right now?". A driver returns `EBUSY` to refuse; `0` to accept.

**`MODULE_DEPEND`.** Macro that declares a dependency on another kernel module. The kernel enforces load order and version compatibility.

**`MODULE_PNP_INFO`.** Macro that publishes the vendor/product identifiers a driver handles. The kernel uses the metadata to auto-load drivers when matching hardware appears.

**`MODULE_VERSION`.** Macro that declares a module's version integer. Used by `MODULE_DEPEND` for compatibility checks.

**`myfirst_ratelimit`.** The struct holding the per-class rate-limit state (`lasttime` and `curpps`). Must live in the softc, not on the stack.

**`MYFIRST_VERSION`.** The driver's human-readable release string, e.g. `"1.8-maintenance"`. Exposed via `dev.myfirst.<unit>.version`.

**`MYFIRST_IOCTL_VERSION`.** The driver's ioctl wire-format version integer. Returned by `MYFIRSTIOC_GETVER`. Bumped only for breaking changes.

**Pps.** Events per second. A rate-limit cap expressed in pps (e.g. 10 pps = 10 messages per second).

**`ppsratecheck(9)`.** The FreeBSD rate-limit primitive. Takes a `struct timeval`, an `int *`, and a pps cap; returns nonzero to allow the event.

**Production-ready.** A driver that satisfies the Section 7 checklist: declared dependencies, documented surface, rate-limited logging, exercised failure paths, passed the 100-cycle test.

**`SHUTDOWN_PRI_*`.** Priority constants passed to `EVENTHANDLER_REGISTER` for shutdown events. `FIRST` runs early; `LAST` runs late; `DEFAULT` runs in the middle.

**`SI_SUB_*`.** Subsystem identifiers for `SYSINIT`. Numeric values sort by boot order. Common constants: `SI_SUB_TUNABLES`, `SI_SUB_DRIVERS`, `SI_SUB_CONFIGURE`.

**`SI_ORDER_*`.** Order constants for `SYSINIT` within a subsystem. `FIRST` runs first; `MIDDLE` runs in the middle; `ANY` runs last (no guaranteed order among `ANY` entries).

**Single-responsibility rule.** Each source file answers one question about the driver. Violations mean new ioctls creeping into `myfirst_sysctl.c` or new sysctls creeping into `myfirst_ioctl.c`.

**Softc.** The per-device state structure. Newbus allocates it, zeroes it, and hands it to `device_attach` via `device_get_softc`.

**`sysctl(8)`.** The user-space command and kernel interface for runtime parameters. Node names live under a fixed hierarchy (`kern.*`, `hw.*`, `net.*`, `dev.*`, `vm.*`, `debug.*`, etc.).

**`SYSINIT(9)`.** FreeBSD's boot-time initialisation macro. Registers a function to run at a specific subsystem and order during kernel startup or module load.

**`SYSUNINIT(9)`.** Companion to `SYSINIT`. Registers a cleanup function to run at module unload in the reverse of the `SYSINIT` order.

**Tag (event handler).** The opaque value returned by `EVENTHANDLER_REGISTER` and consumed by `EVENTHANDLER_DEREGISTER`. Must be stored (usually in the softc) so the deregistration can locate the registration.

**Tunable.** A value parsed from the kernel environment at boot or module load and consumed via `TUNABLE_*_FETCH`. Sets initial values; lives at the `hw.`, `kern.`, or `debug.` level.

**`TUNABLE_INT_FETCH`, `_LONG_FETCH`, `_BOOL_FETCH`, `_STR_FETCH`.** The family of macros that read a tunable from the kernel environment and populate a variable. Silent if the tunable is absent.

**Version split.** The practice of using three independent version identifiers (release string, module integer, ioctl integer) that change for different reasons.

**`vm_lowmem` event.** An `EVENTHANDLER` event fired when the virtual memory subsystem is under pressure. Drivers with caches can release some memory back.

**Wire format.** The layout of data that crosses a user/kernel boundary. The ioctl wire format is determined by the `_IOR`, `_IOW`, `_IOWR` declarations and the payload structure. A wire-format change is a breaking change and requires a `MYFIRST_IOCTL_VERSION` bump.

