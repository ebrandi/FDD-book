---
title: "Debugging and Tracing"
description: "Chapter 23 opens Part 5 by teaching how to debug and trace FreeBSD device drivers in a disciplined, reproducible way. It explains why kernel debugging differs from userland debugging and what that difference asks of the reader; how to use printf() and device_printf() effectively and where the output actually ends up; how dmesg, the kernel message buffer, /var/log/messages, and syslog compose to form the logging pipeline the driver relies on; how to build and run a debug kernel with DDB, KDB, INVARIANTS, WITNESS, and related options, and what each option actually checks; how DTrace exposes a live view of kernel activity through providers, probes, and short scripts; how ktrace and kdump reveal the user-kernel boundary from the user process's point of view; how to diagnose the bugs drivers keep producing, including memory leaks, race conditions, bus_space and bus_dma misuse, and post-detach access; and how to refactor logging and tracing into a clean, toggleable, maintainable subsystem. The myfirst driver grows from 1.5-power to 1.6-debug, gains myfirst_debug.c and myfirst_debug.h, gains SDT probes the reader can inspect with dtrace, gains a DEBUG.md document, and leaves Chapter 23 with a driver that tells you what it is doing whenever you ask it to."
partNumber: 5
partName: "Debugging, Tools, and Real-World Practices"
chapter: 23
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 210
---

# Debugging and Tracing

## Reader Guidance & Outcomes

Chapter 22 closed Part 4 with a driver that survives suspend, resume, and shutdown. The `myfirst` driver at version `1.5-power` attaches to a PCI device, allocates MSI-X vectors, runs a DMA pipeline, handles the full kobj power-management contract, exposes counters through sysctls, and passes a regression script that exercises attach, detach, suspend, resume, and runtime PM. For a driver that boots, runs, suspends, wakes, and eventually unloads, those mechanics are complete. What the driver does not yet have is the ability to tell its developer what is going on inside it when something goes wrong.

Chapter 23 adds that ability. Part 5 is titled *Debugging, Tools, and Real-World Practices*, and Chapter 23 opens it by teaching the observability and diagnosis discipline that turns a driver you just wrote into a driver you can maintain for years. The reader will spend this chapter learning how kernel-space debugging differs from userland debugging, how to use `printf()` and `device_printf()` with intent rather than by habit, how to follow log messages from the moment they are written to the moment a user reads them with `dmesg`, how to build a debug kernel that catches mistakes the normal kernel misses, how to ask DTrace to show the driver's live behavior without rebuilding anything, how to use `ktrace` and `kdump` to follow a user program as it crosses into the kernel through the driver's interface, how to read the characteristic patterns of common driver bugs the first time a symptom appears, and how to refactor the driver's debugging support so that it remains useful without turning the source into a pile of scattered `printf` calls.

Chapter 23's scope is precisely this. It teaches the everyday kernel-debugging toolkit a FreeBSD driver author uses in lab work, in regression testing, and in responding to bug reports. It teaches enough DTrace to instrument a driver and measure its behavior. It teaches the mental model behind the kernel's debug options so the reader can choose the right set for the problem in front of them. It shows where in the source tree these facilities live and how to read their documentation directly. It does not teach the deeper mechanics of post-mortem kernel crash analysis, because that material belongs further along in this book as a specialised topic. It does not teach driver architecture or integration with specific subsystems, because Chapter 24 is about that. It does not teach advanced performance tuning or large-scale profiling, because Part 6 covers hardware-aware tuning in depth. The discipline Chapter 23 introduces is the foundation the later chapters assume: you cannot tune what you cannot observe, and you cannot fix what you cannot reproduce.

Part 5 is where the driver earns the qualities that separate a working prototype from production software. Chapter 22 made the driver survive a change of power state. Chapter 23 makes the driver tell you what it is doing. Chapter 24 will make the driver integrate cleanly with the kernel subsystems users actually reach from their programs. Chapter 25 will make the driver resilient under stress. Each chapter adds one more quality. Chapter 23 adds observability, which turns out to be the quality every later topic depends on.

### Why Kernel Debugging Earns a Chapter of Its Own

Before the first line of code, it helps to understand why DTrace, `dmesg`, and the debug-kernel options get a whole chapter. You may already be wondering why this is any harder than debugging a user program: add some `printf` statements, run under `gdb`, step through the code, done. That mental model is reasonable in user space and catastrophic in kernel space, for three linked reasons.

The first is that **a driver bug is a kernel bug**. When a userland program has a bug, the operating system protects the rest of the machine from it: the kernel kills the process, the shell prints a message, the user tries again. None of that protection applies inside the kernel. A single bad pointer, a single missed lock, a single unfreed allocation, a single race between an interrupt handler and a task, can panic the whole system. A driver that crashes in testing drops the running VM. A driver that leaks memory eventually starves the machine. A driver that corrupts memory destroys whatever data was unlucky enough to sit next to the bug. Chapter 23 exists because these failures do not look or behave like userland failures, and the tools to find them do not look or behave like userland tools.

The second is that **visibility is limited and costly**. In a userland debugger the program is stopped for you. You can step, you can print variables, you can set conditional breakpoints, you can rewind the clock with `rr` or `gdb --record`. In the kernel, none of that is generally available at runtime without special preparation. You cannot stop a running kernel the way you stop a process, because the kernel is also what runs everything else, including the keyboard, the terminal, the network, and the disk. Every piece of visibility you add has a cost: a `printf` is cheap but slow; an SDT probe is fast but has to be compiled in and enabled; a DTrace script is flexible but runs in the kernel itself and touches every probe it fires. Chapter 23 exists because you have to spend visibility budget deliberately, and knowing what each tool costs is part of knowing how to use it.

The third is that **feedback loops are longer**. In userland the write-compile-run cycle is seconds. In kernel work, the cycle is the same under best conditions, but the consequence of a mistake is a reboot or a rollback of the VM snapshot. A bug that takes one try to reproduce in userland may take ten suspends and resumes in kernel land before the driver's internal counter wraps and the symptom appears. A bug that reproduces in every run under `gdb` may reproduce only under an unloaded kernel, at a specific load, or on a specific CPU. Chapter 23 exists because kernel debugging is only tolerable if the feedback loop is as short as you can make it, and short loops require the right combination of tools for each class of bug.

Chapter 23 earns its place by teaching those three realities together, concretely, with the `myfirst` driver as the running example. A reader who finishes Chapter 23 can add disciplined, toggleable debug output to any FreeBSD driver; read and interpret `dmesg`, `/var/log/messages`, and DTrace output without help; build a debug kernel and know what each option buys; find and fix the common driver bug classes the first time a symptom surfaces; and turn a vague report ("my machine sometimes hangs when the driver loads") into a reproducible test case, a hypothesis, a fix, and a regression.

### Where Chapter 22 Left the Driver

A few prerequisites to verify before starting. Chapter 23 extends the driver produced at the end of Chapter 22 Stage 4, tagged as version `1.5-power`. If any of the items below is uncertain, return to Chapter 22 and fix it before starting this chapter, because the debugging topics assume the driver has a stable baseline to debug.

- Your driver compiles cleanly and identifies itself as `1.5-power` in `kldstat -v`.
- The driver allocates one or three MSI-X vectors, registers a filter-plus-task interrupt pipeline, binds each vector to a CPU, and prints an interrupt banner during attach.
- The driver allocates a `bus_dma` tag and a 4 KB DMA buffer, exposes the bus address through `dev.myfirst.N.dma_bus_addr`, and cleans up the DMA resources on detach.
- The driver implements `DEVICE_SUSPEND`, `DEVICE_RESUME`, and `DEVICE_SHUTDOWN`. A `devctl suspend myfirst0` followed by `devctl resume myfirst0` succeeds cleanly, and a subsequent DMA transfer works without error.
- Counters for suspend, resume, shutdown, and runtime-PM transitions appear as sysctls under `dev.myfirst.N.`.
- `HARDWARE.md`, `LOCKING.md`, `SIMULATION.md`, `PCI.md`, `INTERRUPTS.md`, `MSIX.md`, `DMA.md`, and `POWER.md` are current in your working tree.
- `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, and `KDB_UNATTENDED` are enabled in your test kernel.

That driver is what Chapter 23 extends. The additions are modest in source lines but significant in maintainability: a new `myfirst_debug.c` file, a matching `myfirst_debug.h` header, a bank of logging macros with compile-time and runtime toggles, a set of statically defined DTrace probe points, a verbose-mode sysctl knob, a set of debug counters that distinguish normal operation from abnormal events, a version bump to `1.6-debug`, a `DEBUG.md` document that explains the subsystem, and a small collection of helper scripts that turn raw log and trace output into something a human can read.

### What You Will Learn

By the end of this chapter you will be able to:

- Describe what makes kernel debugging different from userland debugging, in concrete terms, and explain how the difference shapes the rest of the chapter's toolkit.
- Use `printf()`, `device_printf()`, `device_log()`, and `log()` correctly in driver code, with an understanding of when each is appropriate and what log priority levels mean in FreeBSD.
- Read and filter `dmesg`, follow the kernel message buffer through its lifecycle, correlate boot-time and runtime messages, and know when the messages you want are in `dmesg`, when they are in `/var/log/messages`, and when they are in both.
- Build a custom debug kernel with `DDB`, `KDB`, `KDB_UNATTENDED`, `INVARIANTS`, `WITNESS`, and related options, and know what each option actually does at runtime, what it costs, and when you want it.
- Enter `ddb` deliberately during a test, run the basic inspection commands (`show`, `ps`, `bt`, `trace`, `show alllocks`, `show pcpu`, `show mbufs`), leave `ddb` cleanly, and understand what you are looking at in each case.
- Understand what a kernel crash dump is, how `dumpdev` and `savecore(8)` produce one, and how `kgdb` opens a dump for post-mortem analysis at a level sufficient to open one and inspect a few frames.
- Explain what DTrace is, load the `dtraceall` module, list the providers the kernel exposes, write a short script that traces a kernel function, and use the `fbt`, `syscall`, `sched`, `io`, `vfs`, and `sdt` providers for common driver questions.
- Add SDT (Statically Defined Tracing) probes to a driver using `SDT_PROVIDER_DEFINE`, `SDT_PROBE_DEFINE`, and the `SDT_PROBE` macros, and use `dtrace -n 'myfirst:::entry'` to observe them.
- Run `ktrace` against a user program that talks to the driver, read the output with `kdump`, and map syscalls like `open`, `ioctl`, `read`, and `write` back to the driver's entry points.
- Recognise the characteristic patterns of driver bugs: unreleased allocations visible in `vmstat -m`, lock order violations reported by `WITNESS`, `bus_space` access past the end of a BAR, `bus_dmamap_sync` calls missed before or after a transfer, access to a device after its `DEVICE_DETACH` has run, and the symptoms each produces.
- Refactor the driver's debugging support into a dedicated `myfirst_debug.c` and `myfirst_debug.h` pair with runtime-toggleable verbose output, ENTRY and EXIT macros for function-level tracing, ERROR and WARN macros for structured problem reporting, SDT probes for external tracing, and a sysctl subtree for the debug controls.
- Read real driver debug code in the FreeBSD source tree, such as the `DPRINTF` family in `/usr/src/sys/dev/ath/if_ath_debug.h`, the `bootverbose` usage in `/usr/src/sys/dev/virtio/block/virtio_blk.c`, the SDT probes in `/usr/src/sys/dev/virtio/virtqueue.c`, and the debug sysctls in drivers like `/usr/src/sys/dev/iwm/if_iwm.c`.

The list is long because debugging is itself a family of skills. Each item is narrow and teachable. The chapter's work is the composition.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred so Chapter 23 stays focused on everyday debugging discipline.

- **Deep `kgdb` scripting and Python extensions**. The chapter shows how to open a crash dump and inspect a few frames. Building Python helpers on top of `kgdb`, walking kernel data structures with user-defined commands, and scripting dump analysis belong in a later advanced chapter.
- **`pmcstat` and hardware performance counters**. These are performance-tuning tools, not debugging tools in the narrow sense. Part 6 returns to them.
- **Lock profiling beyond `WITNESS`**. The `options LOCK_PROFILING` path and the `lockstat` DTrace provider are mentioned in context, but the full tuning workflow for contention analysis belongs with Part 6's performance chapters.
- **Detailed `KTR` event tracing**. The chapter mentions `KTR` in contrast to `ktrace` so the reader does not confuse the two, but the heavyweight in-kernel event buffer and its circular-log workflow are advanced material that rarely needs a driver's attention.
- **Rate-limited logging frameworks**. Chapter 25 covers logging etiquette, static counters, and time-based rate limiting in depth. Chapter 23 introduces the need for rate limiting informally and hands off.
- **The `bhyve debug` backend, GDB server mode, and live kernel-level breakpoints**. These are possible and occasionally useful, but they are an advanced niche and do not belong in the first debugging chapter.
- **Error injection and fault testing frameworks**. `fail(9)`, `fail_point(9)`, and related facilities are briefly introduced in context but left to Chapter 25 and the testing material that follows.

Staying inside those lines keeps Chapter 23 a chapter about how to find bugs, not a chapter about all the places the kernel has ever tried to help you find one.

### Estimated Time Investment

- **Reading only**: four to five hours. Chapter 23's ideas are conceptually lighter than Chapter 21 or Chapter 22, but they connect to a lot of tools, and a first pass through the tool survey takes time.
- **Reading plus typing the worked examples**: ten to twelve hours over two or three sessions. The driver evolves in three stages: first the macro-based logging and verbose mode, then the SDT probes, and finally the refactor into its own file pair. Each stage is short, and each stage comes with a short lab that confirms it works.
- **Reading plus all labs and challenges**: fifteen to eighteen hours over three or four sessions. The labs include an intentional `ddb` session, a DTrace exercise that measures driver latency, a `ktrace`/`kdump` walkthrough, a deliberate memory leak the reader has to find with `vmstat -m`, and an intentional lock order violation the reader has to reproduce under `WITNESS`.

Sections 4 and 5 are the densest in terms of new vocabulary. If the `ddb` commands or DTrace syntax feel opaque on first pass, that is normal. Stop, run the matching exercise on the simulated driver, and come back when the shape has settled. Debugging is one of those skills where concrete practice settles the ideas faster than further reading.

### Prerequisites

Before starting this chapter, confirm:

- Your driver source matches Chapter 22 Stage 4 (`1.5-power`). The starting point assumes every Chapter 22 primitive: the `device_suspend`, `device_resume`, and `device_shutdown` methods; the softc's `suspended` flag and power counters; the POWER.md document; and the regression test script. Chapter 23 builds on top of all of that.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk and matching the running kernel.
- A debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS`, and `DDB_CTF` is built, installed, and booting cleanly. These are the exact options Chapter 22 already asked for; Section 4 of this chapter explains each one in detail so you know what they do.
- `bhyve(8)` or `qemu-system-x86_64` is available. Chapter 23's labs deliberately run in a VM. Several of the labs can panic the kernel (that is the lesson), and a VM snapshot is the right tool to keep the feedback loop short.
- The following user-space commands are in your path: `dmesg`, `sysctl`, `kldstat`, `kldload`, `kldunload`, `devctl`, `vmstat`, `ktrace`, `kdump`, `dtrace`, `procstat`, `savecore`, `kgdb`.
- You have snapshotted your VM at the `1.5-power` state and named the snapshot clearly. Several labs in this chapter intentionally panic the kernel; you want to be able to roll back to a known good state in under a minute.

If any item above is shaky, fix it now. Debugging, unlike many topics in this book, is a skill that rewards preparation and punishes shortcuts. A lab session that starts without a snapshot is a lab session that ends with an hour of recovery; a DTrace exercise that starts without `KDTRACE_HOOKS` is a DTrace exercise that fails silently.

### How to Get the Most Out of This Chapter

Five habits pay off in this chapter more than in any previous one.

First, keep `/usr/src/sys/kern/subr_prf.c`, `/usr/src/sys/kern/kern_ktrace.c`, `/usr/src/sys/kern/subr_witness.c`, and `/usr/src/sys/sys/sdt.h` bookmarked. The first defines the kernel's `printf`, `log`, `vprintf`, and `tprintf` implementations and is the canonical answer to any question about what happens when you call them. The second is the `ktrace` implementation. The third is `WITNESS`, the lock-order verifier that Chapter 23 leans on heavily. The fourth is the SDT probe macros. Reading each once at the start of the corresponding section is the single most effective thing you can do for fluency. None of these files are long; the longest is a few thousand lines and most of that is comment.

Second, keep three real driver examples close to hand: `/usr/src/sys/dev/ath/if_ath_debug.h`, `/usr/src/sys/dev/virtio/virtqueue.c`, and `/usr/src/sys/dev/iwm/if_iwm.c`. The first illustrates the classic kernel `DPRINTF` macro pattern with compile-time and runtime toggles. The second contains real SDT probes. The third shows how a driver wires a debug sysctl through `device_get_sysctl_tree()`. Chapter 23 points back to each at the right moment. Reading them once now, without trying to memorise, gives the rest of the chapter concrete anchors to hang its ideas on.

> **A note on line numbers.** When these real drivers are referenced later, the reference is always anchored on a named symbol: a `DPRINTF` definition, an SDT probe macro, a specific sysctl callback. Those names will carry across future FreeBSD 14.x point releases; the lines where they sit will not. Jump to the symbol in your editor and trust what it reports; the example `WITNESS` output later in the chapter quotes source locations for the same reason, as pointers rather than fixed addresses.

Third, type every code change into the `myfirst` driver by hand. Debugging support is the kind of code that is easy to paste and hard to remember later. Typing out the macros, the SDT definitions, the verbose-mode sysctl, and the `myfirst_debug.c` refactor builds familiarity that a copy-paste pass does not. The goal is not to have the code; the goal is to be the person who could write it again from scratch in twenty minutes.

Fourth, work inside a VM, keep a snapshot, and do not be afraid to panic the kernel on purpose. Several of the chapter's labs ask you to enter `ddb` on a running system. Several more ask you to build a driver that intentionally misuses a lock, then to watch `WITNESS` catch it. A real panic in a real VM is, for debugging practice, cheap and safe. A real panic on the reader's development laptop is expensive and annoying. The snapshot is the cheap path.

Fifth, after finishing Section 4, re-read Chapter 22's debugging notes on suspend and resume. The suspend and resume paths are classic targets for the Chapter 23 toolkit: lock issues show up in `WITNESS`, DMA state mistakes show up as `KASSERT` failures under `INVARIANTS`, race conditions show up as intermittent `devctl suspend` failures. Seeing the Chapter 22 material in the light of Chapter 23's tools reinforces both chapters.

### Roadmap Through the Chapter

The sections in order are:

1. **Why Kernel Debugging Is Challenging.** The mental model: what makes kernel-space bugs different; why the tools from userland do not carry over; why controlled, incremental changes are the right first response; why you always debug from a position of reproducibility.
2. **Logging With `printf()` and `device_printf()`.** The everyday workhorse. Kernel printf; `device_printf` as the preferred form for device drivers; the `log(9)` family with priority levels; formatting conventions; where the output actually goes; what to log and what to leave out. Stage 1 of the Chapter 23 driver extends the `myfirst` log lines to use `device_printf` consistently and prepares the ground for the debug macros in Section 8.
3. **Using `dmesg` and Syslog for Diagnostics.** Following the output. The kernel message buffer; `dmesg` and its filters; `/var/log/messages` and `newsyslog`; the relationship between console output, `dmesg`, and syslog; what it means for a message to be "lost"; how to correlate messages across a boot cycle and across a module reload.
4. **Kernel Debug Builds and Options.** The invariants your kernel enforces. `options KDB`, `DDB`, `KDB_UNATTENDED`, `KDB_TRACE`; `INVARIANTS` and `INVARIANT_SUPPORT`; `WITNESS` and its variants; `KDTRACE_HOOKS` and `DDB_CTF`; a survey of the DDB commands a driver author uses most; a brief introduction to `kgdb` on a crash dump. This is the longest section of the chapter because the debug kernel is the ground the rest stands on.
5. **Using DTrace for Live Kernel Inspection.** The scalpel. What DTrace is; providers and probes; how to list them with `dtrace -l`; short scripts using `fbt`, `syscall`, `sched`, `io`, and `sdt`; how to add SDT probes to the `myfirst` driver. Stage 2 of the chapter driver adds SDT probes.
6. **Tracing Kernel Activity With `ktrace` and `kdump`.** The user-side viewport. What `ktrace` records, what it does not, how to invoke it, how to interpret `kdump` output, how to follow a user program as it crosses into the driver through `open`, `ioctl`, `read`, and `write`.
7. **Diagnosing Common Driver Bugs.** The field guide. Memory leaks and `vmstat -m`; lock order violations and `WITNESS`; `bus_space` and `bus_dma` misuse patterns; post-detach access; interrupt handler mistakes; lifecycle errors; the debug checklist to run before filing a bug.
8. **Refactoring and Versioning With Trace Points.** The clean house. The final split into `myfirst_debug.c` and `myfirst_debug.h`; runtime-toggleable verbose mode through sysctl; ENTRY/EXIT/ERROR macros; a small SDT probe bank; the `DEBUG.md` document; the version bump to `1.6-debug`.

After the eight sections come an extended look at real-world driver debug patterns in `if_ath_debug.h`, `virtqueue.c`, and `if_re.c`, a set of hands-on labs, a set of challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 23's story and opens Chapter 24's, a bridge, a quick-reference card, and the usual glossary of new terms.

If this is your first pass, read linearly and do the labs in order. If you are revisiting, Sections 4 and 7 stand alone and make good single-sitting reads. Section 5 assumes Sections 1 through 4; do not read DTrace in isolation on the first pass.



## Section 1: Why Kernel Debugging Is Challenging

Before the tools, the mindset. Section 1 spells out what makes kernel debugging different from what a reader may already know. A reader who has written shell scripts, small C programs, or even a multi-threaded application server has probably used `printf`, `gdb`, `strace`, `valgrind`, `lldb`, or the browser's devtools. Those tools are excellent in user space, and the mental model they encourage is reasonable in user space. In kernel space, the mental model needs to shift. Section 1 describes that shift, names the specific difficulties driver authors keep running into, and sets the stage for the tool-by-tool work that follows.

### The Nature of Kernel Code

A driver is, in the simplest possible statement, C code that runs in kernel address space. That one fact drives most of the differences between kernel debugging and userland debugging.

Kernel address space contains everything: the code the kernel itself runs, the data structures the kernel uses to manage processes and files and memory and devices, the page tables for every process on the system, the buffers of every pending network packet, the queues of every disk request, and the memory behind every device's mapped registers. Every pointer the driver holds potentially reaches every other part of the kernel. There is no separation like the one user-space programs enjoy, where a process can only touch its own address space and the operating system enforces the boundary.

The consequence is sharp: a single wrong pointer in a driver can corrupt any kernel data structure, not just the driver's own. A driver that writes one byte past the end of its softc may happen to write into the kernel's process table, or into the filesystem's free-block bitmap, or into another driver's interrupt vector table. The corruption often does not produce an immediate panic; it produces a panic five minutes later when some other code reads the structure the driver silently damaged. That delay between cause and symptom is the single most common reason kernel bugs feel harder than userland bugs. You do not catch the wrong pointer at the moment it happens; you catch a different piece of code stumbling over the mess.

Kernel code also runs without the usual safety nets. In user space, dereferencing a null pointer produces a segmentation fault, the kernel kills the process, and the user sees a message. In kernel space, dereferencing a null pointer produces a page fault, the kernel itself faults, and the fault handler decides whether the system can continue. If the page fault happens in a place where it would leave the kernel in an inconsistent state (inside an interrupt handler, inside a critical section, inside `malloc`), the kernel panics to prevent further damage. On a development kernel with `INVARIANTS` enabled, the kernel panics more aggressively than on a release kernel, because the trade-off is exactly the point: catch the bug loudly in development, rather than let it silently propagate.

There is also no standard output stream the way user programs have one. A user-space C program calls `printf` and the output appears on the terminal because the C runtime links the call to a chain that eventually writes to a file descriptor the shell connected to the terminal. In the kernel, there is no shell, no terminal in the user sense, and no C runtime. When a driver calls `printf`, it reaches the kernel's own implementation of `printf`, which writes to the kernel message buffer and, depending on settings, to the system console. Section 2 covers this in detail; the point for now is that the kernel's logging pipeline is not the same pipeline user programs use, and assuming familiarity leads to confusion later. A driver that "just calls `printf`" is calling a different function from the one a userland program calls, with different semantics and a different output path.

### Risks of Debugging in Kernel Space

The consequences of a wrong debug change in user space are usually small. A `printf` that prints the wrong variable is an inconvenience. A `printf` inside a loop that runs a billion times slows the program down but does not damage anything. A misuse of `malloc` in a debug statement leaks a few bytes, and the process eventually exits, and the leak disappears with it.

The consequences of a wrong debug change in kernel space can be much larger. A `printf` on the wrong path can deadlock the kernel, if the printf's path needs a lock the calling context has already taken. A `printf` in an interrupt handler that calls into code which may sleep can panic under `INVARIANTS`. A debug message that calls a sleeping allocator (`malloc(..., M_WAITOK)` instead of `M_NOWAIT`) while holding a spin lock will make `WITNESS` very unhappy, and if `WITNESS_KDB` is enabled, will drop the machine into the kernel debugger. A global counter used for debug purposes that is incremented without proper synchronisation is itself a source of bugs. Each of these is a real mistake driver authors make; each is a lesson this chapter will teach.

The nuance is that debugging code is, strictly, code, and it has to be as correct as any other code in the driver. "I'll clean it up later" is not safe in kernel context. A hasty debug change can introduce a new bug that is harder to find than the original bug. Chapter 23 teaches habits that avoid that trap: use macros that compile out when disabled; use the kernel's own log facility instead of inventing one; wrap conditional blocks so verbose-mode code is paid for only when verbose mode is on; and test debug code at the same level of care as production code.

Another risk is observational: adding debug output can change the behavior you are trying to observe. A race condition that reproduces reliably without debug output may disappear entirely when you add `printf` calls, because the printfs serialise through a lock and happen to hide the race window. A performance bug may look different after instrumentation, because the instrumentation itself adds cost. Kernel developers call this the Heisenberg problem of kernel debugging. The chapter's tools are chosen in part because they minimise the problem: DTrace, SDT probes, and KTR add very little overhead when inactive; `device_printf` is chatty but consistent. Sprinkling `printf("HERE\n")` into code is always available, but as a habit it costs more than it helps.

### Differences From Userland Debugging

The difference between kernel-space and user-space debugging is worth spelling out tool by tool.

A user can run a program under `gdb`, set a breakpoint, and watch the program stop. You cannot do this directly with a running kernel, because a paused kernel means a paused system. The rough equivalent is `kgdb` against a crash dump, which lets you inspect the kernel's state at the moment of the panic, but which cannot single-step forward. The other rough equivalent is `ddb`, the in-kernel debugger, which lets you pause a live kernel but runs as part of the kernel itself and has its own limited command set. Both are useful; neither behaves the way `gdb` on a live process behaves.

A user can use `strace` on Linux or `truss` on FreeBSD to watch a program's system calls. FreeBSD's `ktrace` is the closest equivalent; it records a trace of a process's syscalls, signals, namei events, I/O, and context switches into a file that `kdump` can read. But `ktrace` sees the user-side of the driver's interface, not the driver's internals. It can show you that the user process called `ioctl(fd, CMD_FOO, ...)`, but not what happened inside the `ioctl` implementation in the driver. Section 6 spends time on `ktrace` precisely because it is the tool that shows the boundary most cleanly.

A user can use `valgrind` to watch for memory errors and leaks in their program. No direct equivalent exists for the kernel. The closest analogs are `vmstat -m` for summarising kernel allocations by type, the `WITNESS` option for lock order errors, `INVARIANTS` for consistency checks the developer wrote into the code, and `MEMGUARD` for a heavier memory-debug allocator. Section 7 walks through the practical patterns.

A user can profile a program with `perf` on Linux or `gprof` on FreeBSD. FreeBSD provides `pmcstat` and DTrace for profiling. DTrace is the more flexible tool for the kind of questions a driver developer asks, and Chapter 23 focuses on DTrace rather than `pmcstat`.

A user can often just re-run a program to see if the bug happens again. Kernel developers often cannot; a kernel that panicked has to be rebooted, and bugs that only appear under load, or that depend on timing, may take hours to reproduce. The response to this difficulty is to build reproducibility into the process: snapshot the VM, script the test case, and log everything. Every tool in this chapter exists to support that response.

One final difference is cultural. The kernel-development community has built a specific vocabulary around debugging that is worth learning on its own. Terms like "lock order", "critical section", "preemption disabled", "epoch", "giant lock", "sleepable context", "non-sleepable context", and "interrupt context" all have precise meanings in FreeBSD, and using them correctly is part of being fluent in kernel debugging. Chapter 23 uses them as it goes; the glossary at the end collects the ones that recur.

### Importance of Controlled, Incremental Changes

In user space, when a program misbehaves, the first instinct is often to change several things at once and see if the behavior changes. In kernel space this is a losing strategy. Each change is potentially a new bug. Each rebuild costs a VM reboot. Each test cycle eats time. A driver developer who changes five things between compiles spends hours puzzling over which change caused the new symptom, because no single change is isolated.

The right pattern is **controlled, incremental change**. Make one change. Rebuild. Test. Observe the effect. Note what happened. Decide the next change based on the evidence. Move on. This is slower per change and faster per working fix, because you never have to unwind a combined change when one of the five fixes actually introduced a new bug. The discipline feels artificial the first time. After a dozen sessions, it stops feeling artificial and starts feeling like the only way to make progress.

A related discipline is **always start from a known-good state**. Do not debug on a driver that is already in a state you have not verified. Do not start a session by applying six local changes and then running a test. Commit your baseline, verify it works, and only then start the new work. If the new work goes wrong, `git checkout .` returns you to known-good, and you can start again. A driver author without this habit spends half of every debugging session wondering whether the baseline or the new change is at fault.

A third discipline is **reproduce before fixing**. A bug that cannot be reproduced cannot be proved fixed. The first job of a debugging session is not to fix the bug; it is to produce a short, reliable way to trigger the bug, ideally in a script. Once the trigger is reliable, the fix is straightforward: apply the candidate, re-run the trigger, observe whether the bug is gone. Without a reliable trigger, you are guessing.

A fourth discipline is **small, scripted reproduction cases**. The difference between a bug that takes an hour to fix and a bug that takes a week is often the size of the reproduction script. A bug you can reproduce with five lines of shell is a bug you can bisect with `git bisect`. A bug you can only reproduce after three hours of complex usage is a bug you will be afraid to even start debugging. Time spent shrinking a reproduction is time well spent.

Chapter 23's labs are deliberately small. The panics are triggered with three lines. The DTrace scripts are five lines. The `ktrace` demonstrations use a one-line C program. That is not accidental: it is the same pattern good driver debugging uses at any scale. Minimal reproductions and minimal changes scale to real bugs.

### The Mental Shift

What changes when a developer moves from user space to kernel space is not just the tools; it is the assumptions. In user space, it is reasonable to assume the kernel will catch your mistakes, the runtime will clean up after you, and the worst case is usually a crashed program you can restart. In kernel space, those assumptions are wrong. The kernel catches nothing; the runtime is the thing you are writing; the worst case is a panic that brings down everything.

This is not a reason to be afraid. It is a reason to be careful. Careful, here, has a specific meaning: a driver author writes code that could be debugged by someone else, in six months, from the logs and the source alone. Careful code uses clear function names. Careful code logs what it does at the levels of detail that are useful. Careful code checks its own invariants. Careful code is structured so that when it breaks, the breakage is obvious. Careful code does not try to be clever; clever code is code you cannot debug under stress.

Chapter 23 teaches the tools of kernel debugging, but the habit it actually teaches is carefulness. Every tool in the chapter is most useful when applied by a careful developer on careful code. The same tool applied to reckless code produces information you cannot interpret. The point is not to memorise DTrace syntax or `ddb` commands; the point is to develop the habit of debugging from evidence, in small steps, against a known baseline, with good logs, in a way that any collaborator can follow.

### Wrapping Up Section 1

Section 1 established the shape of the problem. Kernel code runs in a single shared address space, without the safety nets userland relies on, with failure modes that damage the whole system. Kernel debugging tools are different, more costly, and less interactive than their userland cousins. The discipline of controlled, incremental, reproducible change is not optional; it is the only strategy that works. The mental shift required is real, but it is learnable, and it is the foundation the rest of the chapter's tools stand on.

Section 2 starts the tool survey with the simplest and most universal tool every driver ends up using: the kernel's `printf` family.



## Section 2: Logging With `printf()` and `device_printf()`

The kernel's `printf` family is the humblest debugging tool in the toolkit. It is also the most used, by a wide margin. Almost every FreeBSD driver in `/usr/src/sys/dev` calls `device_printf` somewhere. Almost every boot message a user ever sees was written by a kernel `printf`. Before DTrace, before `ddb`, before SDT probes, before debug kernels, a driver author reaches for `device_printf` because it is always available, it is always safe (subject to the qualifications this section will spell out), and it is understood by every other driver author on the planet.

This section teaches how to use that family of functions deliberately. The goal is not just to show how to call `device_printf`, because that part is trivial. The goal is to build the habit of using logging as a designed feature of the driver, not as a scattered set of debugging notes that accumulate over time and never get cleaned up.

### Basics of `printf()` in Kernel Context

FreeBSD's kernel `printf` lives in `/usr/src/sys/kern/subr_prf.c`. Its signature is familiar:

```c
int printf(const char *fmt, ...);
```

It takes a format string and a variable argument list and writes the formatted text into the kernel message buffer. The buffer is a circular area of memory, usually around 192 KB in size on a modern FreeBSD system, into which every kernel message is written. The kernel does not keep a permanent log on its own; the user-space `syslogd(8)` daemon is what reads the buffer and writes selected lines to `/var/log/messages`. Section 3 covers the full pipeline.

The format string understands most of the same conversions userland's `printf` understands: `%d`, `%u`, `%x`, `%s`, `%c`, `%p`, and so on. It understands a few kernel-specific extras, such as `%D` for printing a hex dump of a byte buffer (with a separator argument). It does not understand floating-point conversions like `%f` and `%g`, because the kernel does not use floating-point math by default and the conversion code is intentionally absent. Trying to print a `double` or `float` inside the kernel is almost always a mistake, and the compiler will usually warn about it.

The kernel's `printf` has different safety properties from the userland one. Userland `printf` can allocate, can take locks inside the C runtime, and can block on I/O. The kernel's `printf` must not allocate; it must not block; it must be safe to call from almost any context. The implementation in `subr_prf.c` is deliberately simple: it formats into a small stack buffer, copies the result into the message buffer, wakes up `syslogd` if appropriate, and returns. That simplicity is part of why `printf` is the universal fallback tool.

There are a few contexts where even the kernel's `printf` is not safe. Fast interrupt handlers, for example, run before the kernel has stabilised the console state, and a `printf` from inside a fast interrupt can produce a hang or a deadlock on some platforms. Paths that hold a lock the `printf` path also tries to take can deadlock too. In practice, driver authors avoid the problem by using `printf` only from the ordinary driver entry points (attach, detach, ioctl, read, write, suspend, resume) and from interrupt tasks (the deferred half of a filter-plus-task interrupt pipeline, as Chapter 19 introduced), not from the filter itself. The `myfirst` driver already follows this pattern; Chapter 23 preserves it.

### Preferred Use: `device_printf()` for Structured, Device-Aware Logging

Raw `printf` is generic. A driver author can do better. The kernel provides `device_printf`, declared in `/usr/src/sys/sys/bus.h`:

```c
int device_printf(device_t dev, const char *fmt, ...);
```

The function prefixes every message with the device's name, typically the driver's name concatenated with the unit number: `myfirst0: `, `em1: `, `virtio_blk0: `. That prefix is not cosmetic. It is the single most important piece of information in the kernel log for a multi-device system. Without it, a message like "interrupt received" is useless in a machine with eight network cards and three storage controllers. With it, "myfirst0: interrupt received" is immediately locatable in any tool that reads logs.

The rule is simple: **in driver code, prefer `device_printf` over `printf`**. Every place the driver has a `device_t` in scope, use `device_printf`. Every place the driver does not have a `device_t` (a global init function, for example), use `printf` with an explicit prefix:

```c
printf("myfirst: module loaded\n");
```

This is the pattern that makes the kernel log readable across a system. It is also the pattern almost every FreeBSD driver follows. Open any file under `/usr/src/sys/dev/` and the pattern is visible within the first attach function.

The `myfirst` driver already uses `device_printf` in most places, because Chapter 18 introduced the convention when the driver first became a PCI device. Section 8 of this chapter wraps `device_printf` in macros that add severity, category, and a verbose-mode toggle, but the underlying function remains the same. Nothing in Chapter 23 replaces `device_printf`; the chapter elaborates on it.

### `device_log()` and Log Priority Levels

`device_printf` is plain: it writes the message at the kernel's default log priority (LOG_INFO, in syslog terms). Sometimes a driver wants to write at a higher priority, to flag a serious problem, or at a lower priority, to tag a routine informational message that should usually be filtered out.

FreeBSD 14 exposes `device_log` for that purpose, also in `/usr/src/sys/sys/bus.h`:

```c
int device_log(device_t dev, int pri, const char *fmt, ...);
```

The `pri` argument is one of the syslog priority constants from `/usr/src/sys/sys/syslog.h`:

- `LOG_EMERG` (0): system is unusable
- `LOG_ALERT` (1): action must be taken immediately
- `LOG_CRIT` (2): critical conditions
- `LOG_ERR` (3): error conditions
- `LOG_WARNING` (4): warning conditions
- `LOG_NOTICE` (5): normal but significant condition
- `LOG_INFO` (6): informational
- `LOG_DEBUG` (7): debug-level messages

For driver work, the four priorities that matter in practice are `LOG_ERR`, `LOG_WARNING`, `LOG_NOTICE`, and `LOG_INFO`. A driver rarely needs `LOG_EMERG`, `LOG_ALERT`, or `LOG_CRIT`, because most driver failures are local to the driver, not to the whole system. `LOG_DEBUG` is useful during active debugging; Chapter 23 uses it for verbose-mode output.

The underlying `log(9)` function, also in `subr_prf.c`, is simpler: `void log(int pri, const char *fmt, ...)`. `device_log` wraps `log` and adds the device-name prefix.

The practical difference between `printf` and `log` is where the output goes when the kernel is configured conservatively. By default, FreeBSD sends everything at or above `LOG_NOTICE` to the console; `LOG_INFO` and below go to the kernel message buffer but not to the console. That matters in two places. First, during boot: a high-priority message is visible on the screen during boot; a `LOG_INFO` message is not. Second, during operation: a serious error on a production system should appear on the console (so a sysadmin watching tail on `messages` sees it immediately), while a routine probe or attach message should not clutter the screen.

The `myfirst` driver in Chapter 23 uses this convention:

- Probe and attach success: `device_printf` (which is `LOG_INFO`).
- Suspend, resume, and shutdown transitions: `device_printf` (routine).
- Errors from hardware, unexpected interrupt counts, DMA failures, and failed device state: `device_log(dev, LOG_ERR, ...)` or `device_log(dev, LOG_WARNING, ...)`.
- Verbose-mode output (enabled by a sysctl): `device_log(dev, LOG_DEBUG, ...)` inside the debug macros.

### What to Log and What to Leave Out

Knowing what not to log matters as much as knowing what to log. A driver with too much output clutters `dmesg`, wastes message-buffer space, and pushes older informative messages out of the circular buffer before anyone reads them. A driver with too little output is silent when something goes wrong and gives the reader nothing to work with.

The rule of thumb for the `myfirst` driver in Chapter 23 is:

**Always log, at `device_printf` level (LOG_INFO):**

- A one-line banner at attach, stating the driver name, the version, and any key resources allocated (interrupt type, MSI-X vector count, DMA buffer size). The reader should be able to see at a glance what the driver did.
- A one-line note at detach. Detach is usually silent, but logging its completion is useful when a driver hangs on unload and the reader needs to know the detach was at least started.
- A one-line note at each suspend, resume, and shutdown. Power transitions matter, and the reader often wants to correlate a bug with a transition that preceded it.

**Always log, at `device_log` with `LOG_ERR` or `LOG_WARNING`:**

- Any failure to allocate a resource (interrupt, DMA tag, DMA buffer, mutex). The error path should always state what failed.
- Any hardware error the driver detects: a response the device did not expect, a timeout, a register value that indicates the hardware has faulted.
- Any state inconsistency the driver catches: a DMA completion for a transfer that was not started, a suspend that finds the device already suspended, a detach that finds the device still holding a client reference.

**Never log in the normal steady-state path:**

- Every interrupt. Even if the device is quiet, a driver that logs every interrupt floods the log within seconds under real load. Use DTrace or counters for per-interrupt observability.
- Every DMA transfer. Same reason.
- Every `read` or `write` call. Same reason.
- Internal function entry and exit in production. Use the verbose-mode macros (Section 8) so the developer can turn entry/exit tracing on or off.

**Log under verbose mode (enabled by the Section 8 macros and the debug sysctl):**

- Function entry and exit for key functions, with a short argument summary.
- Individual interrupt events with the interrupt status register value.
- DMA transfer submissions and completions.
- sysctl and ioctl operations and the values they returned.

The line between "always" and "verbose" is a judgement call, and different drivers draw it differently. `/usr/src/sys/dev/re/if_re.c` logs very little in production and uses a `re_debug` sysctl to enable detailed output. `/usr/src/sys/dev/virtio/block/virtio_blk.c` uses `bootverbose` to gate extra attach-time detail. `/usr/src/sys/dev/ath/if_ath_debug.h` defines a sophisticated per-subsystem mask that selectively enables different categories of output. Chapter 23's `myfirst` driver adopts a simpler pattern that is enough for teaching purposes and can be extended when the reader starts working on a real driver.

### Tracing Function Flow Without Spamming

One of the debugging idioms that is tempting but produces more harm than good is writing a `device_printf` at the start and end of every function. The idea is sound: if you can see the call sequence, you can often deduce what is happening. The practice is wrong, because the sequence produces an overwhelming amount of output, and the output serialises through the printf path, which changes the timing of the code under test.

The right pattern is to use a **verbose mode controlled by a sysctl**. Define a pair of macros:

```c
#define MYF_LOG_ENTRY(sc)       \
    do {                        \
        if ((sc)->debug_verbose) \
            device_printf((sc)->dev, "ENTRY: %s\n", __func__); \
    } while (0)

#define MYF_LOG_EXIT(sc, err)   \
    do {                        \
        if ((sc)->debug_verbose) \
            device_printf((sc)->dev, "EXIT: %s err=%d\n", __func__, (err)); \
    } while (0)
```

Then, at the top of every function the driver wants to trace, call `MYF_LOG_ENTRY(sc);`. At every return, call `MYF_LOG_EXIT(sc, err);`. When the `debug_verbose` flag is zero (the default), the macros compile down to a branch that is never taken and have essentially zero cost. When the flag is one (enabled at runtime through `sysctl dev.myfirst.0.debug_verbose=1`), the macros produce a trace that any driver developer can read.

The boolean flag shown above is deliberately simple. It introduces the idea of a runtime-controlled knob without requiring the reader to learn a new layout at the same time. Section 8 of this chapter fleshes this idiom out into a full macro bank, replaces the single boolean with a 32-bit bitmask (so the operator can enable just the interrupt trace, or just the I/O trace, rather than everything at once), moves the whole apparatus into `myfirst_debug.h`, and adds SDT probes alongside. For now, the pattern to internalise is that trace-level output should be **off by default** and **runtime toggleable**. Compile-time debug flags (the old `#ifdef DEBUG` style) are a poor second: you have to rebuild the module to change them. Runtime toggles let a developer enable verbose mode on a running system the moment they need it and disable it the moment the evidence is gathered.

### Redirecting Logs to `dmesg` and `/var/log/messages`

The kernel's `printf` writes to the kernel message buffer. That buffer is what `dmesg(8)` reads when the user runs it. Section 3 covers `dmesg` in detail; the short version is that the buffer is circular, so old messages eventually get overwritten by new ones as the buffer fills.

The `syslogd(8)` daemon reads new messages out of the buffer and writes them to the appropriate syslog file, usually `/var/log/messages`. The mapping between priority and file is configured in `/etc/syslog.conf`. By default, messages at `LOG_INFO` or higher go to `/var/log/messages`; `LOG_DEBUG` messages are dropped unless explicitly enabled. `/var/log/messages` is rotated by `newsyslog(8)` according to rules in `/etc/newsyslog.conf`; that rotation is what produces the `messages.0.gz`, `messages.1.gz`, and so on that the reader will find in a long-running system's log directory.

The practical consequence is that a driver's log output lives in two places at once. A recent message is in the kernel buffer (accessible through `dmesg`) and in `/var/log/messages` (accessible through any file-reading tool). An old message may have aged out of the buffer but still be in `/var/log/messages` or its archives. A very old message may have rolled out of `/var/log/messages` into a `messages.N.gz` archive, and can be read with `zcat`, `zgrep`, or `zless`. Section 3 walks through these tools.

There is one subtlety worth mentioning here because it trips up new driver authors: **a `LOG_DEBUG` message produced by the kernel is not written to `/var/log/messages` by default**. If the reader's debug output is at `LOG_DEBUG` and they cannot find it in the log file, the cause is usually `/etc/syslog.conf`, not the driver. A quick test: run the driver, and then run `dmesg | grep myfirst`. If the message appears in `dmesg` but not in `/var/log/messages`, the message was produced but was filtered out by syslog. Enabling `LOG_DEBUG` in `/etc/syslog.conf` (if syslog is configured to care about kernel debug messages) is one fix; using `device_printf` or `device_log(dev, LOG_INFO, ...)` is a more common fix, because it bypasses the problem by using a higher priority.

### A Concrete Walkthrough: Logging in the `myfirst` Driver

The `myfirst` driver at the start of Chapter 23 already has log lines in the obvious places: attach prints a banner, detach confirms completion, suspend and resume log their transitions, and DMA errors log a warning. Chapter 23's Stage 1 upgrades this in three targeted ways.

First, every `printf` inside the driver that has a `device_t` in scope becomes a `device_printf`. A quick grep turns up about eight lines that still use plain `printf` from the module's `MOD_LOAD` and `MOD_UNLOAD` handlers. The module-level ones stay as `printf` (they do not have a `device_t`), but they use the literal `"myfirst: "` prefix so the reader can still find them in the log. A driver that calls `printf` without a prefix in any place is a driver that produces unfindable log lines.

Second, every error path becomes a `device_log` at an explicit priority:

```c
/* Before */
if (error) {
    device_printf(dev, "bus_dma_tag_create failed: %d\n", error);
    return (error);
}

/* After */
if (error) {
    device_log(dev, LOG_ERR, "bus_dma_tag_create failed: %d\n", error);
    return (error);
}
```

The change is small, and the behavior is the same in most test kernels. The value is in the priority. A production system watching for `LOG_ERR` messages will now see the driver's errors; a development environment filtering at `LOG_INFO` will see everything.

Third, a new pattern of logging for "rare but important" state changes is introduced. Specifically, the Chapter 23 driver logs at `LOG_NOTICE` on three events the reader may want to know about but does not want every interrupt for: the first successful DMA transfer after attach, a transition into runtime suspend from idle detection, and the first successful resume after a suspend. These are not errors, but they are not routine either; they are the events a developer reading the log wants to see highlighted.

The exact code changes for Stage 1 are laid out in Section 2's hands-on exercise at the end of this section, and the corresponding files are under `examples/part-05/ch23-debug/stage1-logging/`.

### A Note on `uprintf` and `tprintf`

Two sibling functions appear in `subr_prf.c` that driver authors occasionally see but rarely use.

`uprintf(const char *fmt, ...)` writes a message to the current user process's controlling terminal, if there is one, *instead of* to the kernel message buffer. It is useful in rare cases where a driver's `ioctl` handler wants to produce an error message that only the user running the command sees, without polluting the system log. In practice, most drivers return an error code through the usual `errno` mechanism and do not use `uprintf`. The `/usr/src/sys/kern/` tree has a handful of callers, mostly in places where the kernel wants to warn a user about their own action (a security-relevant event, for example).

`tprintf(struct proc *p, int pri, const char *fmt, ...)` writes to a specific process's terminal. It is rarer still. Chapter 23 does not use either function directly. The Chapter 25 logging material returns to them in the context of user-facing diagnostic messages.

### Formatting Conventions

A few small formatting habits make log output much easier to read:

1. **End every line with `\n`**. The kernel's `printf` does not add a newline. A message without a newline will run into the next message in `dmesg`, producing output like `myfirst0: interrupt receivedmyfirst0: interrupt received` that is nearly impossible to parse visually.

2. **Use hex for register values, decimal for counters**. `status=0x80a1` is a device register; `count=27` is a counter. Mixing the two conventions in a single driver produces output that is hard to read at a glance. The `%x` and `%d` conversions are the cheap way to keep them visually distinct.

3. **Put the variable values on the right, not the left**. `myfirst0: dma transfer failed (err=%d)` scans better than `myfirst0: %d was the err from dma transfer failed`. Human readers parse English left-to-right, and putting the variable data at the end of the line matches that parse.

4. **Use short tags for repeated message categories**. `INFO:`, `WARN:`, `ERR:`, `DEBUG:` at the start of the content after the device prefix turn a log into something a grep can filter. Section 8 uses this convention in its macros.

5. **Do not include timestamps in the message**. The kernel and syslog add their own timestamps; a manual timestamp produces duplicate information and visual noise.

6. **Do not include the function name in production messages**. Function names are useful in verbose-mode output (where the entry/exit macros print them automatically) but wasteful in every production message. A message like `myfirst0: DMA completed` is more readable than `myfirst0: myfirst_dma_completion_handler: DMA completed`. If you want function names, turn on verbose mode.

7. **Match the driver's style**. If the rest of the driver uses "DMA" in uppercase, use "DMA" in every log line. If the rest uses "dma" in lowercase, match that. Consistency is easier to grep than mixed case.

These are small habits, but they pay off the first time a log becomes the only evidence of a bug that happened on a system the reader cannot access interactively.

### Exercise: Add `device_printf()` at Key Points in Your Driver

The hands-on companion for Section 2 is a short exercise. The reader should:

1. Open `myfirst_pci.c`, `myfirst_sim.c`, `myfirst_intr.c`, `myfirst_dma.c`, and `myfirst_power.c` in turn.

2. For every `printf` call that has a `device_t` or a softc (which holds a `device_t`) in scope, replace it with `device_printf(dev, ...)`.

3. For every error-path message, change the call to `device_log(dev, LOG_ERR, ...)` if the condition is a real error, or `device_log(dev, LOG_WARNING, ...)` if the condition is recoverable. Keep `device_printf` for routine informational messages.

4. For the attach banner, add a single line that names the driver version: `device_printf(dev, "myfirst PCI driver %s attached\n", MYFIRST_VERSION);`. Define `MYFIRST_VERSION` in the top of `myfirst_pci.c` as a string literal. Chapter 23 will bump it from `"1.5-power"` to `"1.6-debug"` at the final refactor in Section 8; for now, keep it at `"1.5-power-stage1"`.

5. Rebuild the module, load it, and verify that `dmesg | grep myfirst0` shows the attach banner. Generate a contrived DMA failure (the simulator's `sim_force_dma_error` sysctl is one way; Chapter 17 introduced the hook) and verify that the error message appears with the `LOG_ERR` priority.

6. Commit the changes with a message like "Chapter 23 Section 2: use device_printf consistently".

The reader should feel free to check their work against `examples/part-05/ch23-debug/stage1-logging/myfirst_pci.c` and the other stage1 files, but the learning happens in the typing, not in the checking.

### Wrapping Up Section 2

Section 2 established the ground rules for logging. Use `device_printf` when you have a `device_t`. Use `device_log` with an explicit priority when the priority matters. Log enough to make the log readable, and not so much that the log becomes noise. Route routine information at `LOG_INFO`, real errors at `LOG_ERR`, and verbose tracing at `LOG_DEBUG` gated behind a sysctl.

Section 3 now follows the log output from the moment the driver writes it to the moment a user reads it, through `dmesg`, the kernel message buffer, `/var/log/messages`, and the syslog pipeline. The driver's job ends when it calls `device_printf`; the tools the reader uses to see what the driver said begin from there.



## Section 3: Using `dmesg` and Syslog for Diagnostics

Section 2 ended with the driver's `device_printf` call. Section 3 picks up from the other end: the moment a user runs `dmesg`, opens `/var/log/messages`, or greps through syslog archives. In between sits the kernel's message buffer, the `syslogd` daemon, the `newsyslog` rotator, and a small set of configuration files that quietly determine which messages appear where. Understanding the pipeline is how a driver author stops worrying about "why don't my messages appear?" and starts debugging the actual driver.

### The Kernel Message Buffer

Every kernel message the driver produces goes first into the **kernel message buffer**. This is a single circular region of memory the kernel allocates at boot, sized by default to about 192 KB on amd64 FreeBSD 14. The size is set by the `msgbufsize` tunable; the current size is visible as:

```sh
sysctl kern.msgbufsize
```

The buffer is "circular" in the sense that when it fills, new messages overwrite the oldest. This matters for a reader hunting an old bug: if you rebooted five hours ago, produced a bunch of driver log output, and only now went looking for it, the output may have rolled out of the buffer already. The safe assumption is that the buffer holds several days of routine messages on a quiet system and a few hours of messages on a busy one.

Because the buffer is a single block of memory, reading it is inexpensive. The `dmesg(8)` command works by calling the `sysctl(8)` function to read `kern.msgbuf` and printing the contents. The `dmesg` binary is small; its source is at `/usr/src/sbin/dmesg/dmesg.c` and worth a quick look if the reader is curious.

The boot-time portion of the buffer is preserved more carefully than the runtime portion. During boot, the kernel writes every autoconf message, every driver's attach banner, every hardware detection note, into the buffer. The `/var/run/dmesg.boot` file, produced by the `/etc/rc.d/dmesg` startup script, captures the buffer's contents at the moment the boot sequence finishes and before runtime output starts overwriting it. Reading `dmesg.boot` is the way to see boot messages after the running system has long since pushed them out of the live buffer:

```sh
cat /var/run/dmesg.boot | grep myfirst
```

For a driver author, `dmesg.boot` is the answer to "did my driver attach on boot?" when the answer is needed hours later. The live `dmesg` is the answer to "what has my driver said since then?".

### `dmesg` in Practice

The common invocations are worth committing to memory.

`dmesg` with no arguments prints the current kernel buffer contents in order from oldest to newest. On a system that has been running for a while, this is a lot of output:

```sh
dmesg
```

Pipe through `less` to scroll, or through `grep` to filter:

```sh
dmesg | less
dmesg | grep myfirst
dmesg | grep -i error
dmesg | tail -50
```

`dmesg -a` prints all messages including those at priorities the kernel is configured to suppress from the normal output. On some configurations this surfaces additional low-priority detail. On most configurations the output is the same as plain `dmesg`.

`dmesg -M /var/crash/vmcore.0 -N /var/crash/kernel.0` reads the message buffer out of a saved crash dump, rather than from the live kernel. This is how a reader looks at the last few messages a kernel produced before it panicked, when the panic itself prevented the normal logging path from running. Section 4's `kgdb` material returns to this.

The `dmesg -c` form (which on some systems clears the buffer after reading) is not available in the FreeBSD kernel's way of exposing the buffer, because the buffer is not drained by reading it; reading produces a copy, and the live buffer continues to receive new messages. Users coming from Linux sometimes expect `dmesg -c` and are surprised.

A useful habit during driver development is to run `dmesg | grep myfirst | tail` after every module load, every test, and every interesting event. The repetition builds a reflex of "what did the driver just say?" that pays off the first time something unexpected happens.

### Permanent Logs in `/var/log/messages`

The kernel message buffer is volatile; a reboot empties it. For anything longer-term, FreeBSD uses `syslogd(8)`, the standard UNIX syslog daemon, which reads new messages as the kernel produces them and writes selected ones to files on disk.

The canonical file, on almost every FreeBSD system, is `/var/log/messages`. This file receives most kernel messages at priority `LOG_INFO` or higher, and most userland daemon messages too. The mapping between priority and destination file is in `/etc/syslog.conf`. A relevant excerpt looks like:

```text
*.notice;kern.debug;lpr.info;mail.crit;news.err		/var/log/messages
```

The syntax is "facility.priority" pairs. `kern.debug` means "all kernel messages at priority `LOG_DEBUG` or higher". `*.notice` means "all facilities at priority `LOG_NOTICE` or higher". The reader should know this syntax exists; understanding it is useful when a specific kind of message needs to go to a specific file.

Because `/var/log/messages` is an ordinary text file, every tool in the reader's toolkit works on it:

```sh
tail -f /var/log/messages
tail -50 /var/log/messages
grep myfirst /var/log/messages
less /var/log/messages
wc -l /var/log/messages
```

Particularly useful during driver development:

```sh
tail -f /var/log/messages | grep myfirst
```

This gives a live stream of only the driver's output, which is what most debugging sessions actually need.

When messages age out, they move to `/var/log/messages.0`, then `.1`, then `.N.gz` as rotation happens. The rotation is done by `newsyslog(8)`, which is invoked by a periodic job from `cron` and reads its rules from `/etc/newsyslog.conf`. A typical rule for `/var/log/messages` rotates the file when it reaches a size threshold, keeps some number of compressed archives, and restarts `syslogd` to reopen the new file.

To search through the archives, `zgrep` is the tool:

```sh
zgrep myfirst /var/log/messages.*.gz
```

Or, for the whole history including the current file:

```sh
grep myfirst /var/log/messages && zgrep myfirst /var/log/messages.*.gz
```

Readers who are hunting a bug that happened days ago reach for these commands routinely.

### Console Output Versus Buffered Output

One nuance confuses many driver authors the first time they encounter it. The kernel has two audiences for its messages: the in-memory buffer, and the system console. The console is the physical terminal on a laptop (or the VGA console in a VM, or the serial console in a headless box). A message can go to one, the other, or both, depending on its priority and the current console log level.

The console log level is controlled by the `kern.consmsgbuf_size` (how much memory the console uses) and more importantly `kern.msgbuflock` and the console priority filter. The simplest knob is:

```sh
sysctl -d kern.log_console_level
```

On FreeBSD, high-priority messages (LOG_WARNING and above) are always displayed on the console. Lower-priority messages are buffered but not displayed. This is why a panic message appears on the console regardless of runlevel: it has priority `LOG_CRIT` or higher, and the kernel's console output path bypasses the usual buffering.

For a driver author, the practical consequence is that `device_log(dev, LOG_ERR, ...)` is more likely to be noticed by a human than `device_printf(dev, ...)`, because the former reaches the console and the latter goes only to the buffer. Chapter 23's Section 2 pointed this out; Section 3 reinforces it with the mechanics.

A driver that produces a torrent of LOG_ERR messages floods the console and, in extreme cases, slows the system down because the console output path is not cheap. Rate limiting is the cure; Chapter 25 teaches it properly. For now, the lesson is that priority matters.

### Reviewing Logs in Practice

The common pattern for reading driver logs during development looks like this:

1. Load the module.
2. Run the test.
3. Unload the module.
4. Copy `dmesg` output to a file for analysis.

The sequence in shell form:

```sh
# Clean slate
sudo kldunload myfirst 2>/dev/null
sudo kldload ./myfirst.ko

# Test
dev_major=$(stat -f '%Hr' /dev/myfirst0)
echo "Running driver test..."
some_test_program

# Capture
dmesg | tail -200 > /tmp/myfirst-test-$(date +%Y%m%d-%H%M%S).log

# Clean slate again
sudo kldunload myfirst

# Now inspect the log at leisure
less /tmp/myfirst-test-*.log
```

Several habits that this pattern encourages are worth stating explicitly:

**Clean slate first, every time.** Unload any existing instance of the module before loading the one you want to test. Otherwise you may accidentally be running an older compile and wondering why your new code doesn't have the expected effect. An unload also leaves the `dmesg` buffer more readable because your new session starts from a known state.

**Capture every interesting session.** A log you saved to a file is a log you can diff, grep, share, and return to next week. A log you left in the live buffer is a log that may get pushed out by the time you come back to it.

**Use timestamps in filenames.** The `date +%Y%m%d-%H%M%S` convention in the snippet produces filenames like `myfirst-test-20260419-143027.log`. When you accumulate a dozen log files from a day of testing, this is the only way to find the one you want.

**Narrow the `tail -N` to a reasonable window.** The default is 10 lines, which is almost never enough for a driver test. 200 to 500 is usually the right size: long enough to capture a full test cycle, short enough to read in one pass. If your test produces more output than that, either your test is too large or your logging is too chatty.

### Filtering Output

The `grep` toolchain is your friend. A few patterns recur often enough to be worth building into muscle memory.

**Show only the driver's output:**

```sh
dmesg | grep '^myfirst'
```

The `^myfirst` anchor matches only lines that start with the device name. Without the anchor, you would also match lines that happen to contain "myfirst" in their text (for example, kernel messages about the module's file descriptor).

**Show only errors:**

```sh
dmesg | grep -iE 'error|fail|warn|fault'
```

Case-insensitive, with an extended regex that matches several common error words.

**Show a specific interval:**

```sh
awk '/START OF TEST/,/END OF TEST/' /var/log/messages
```

This relies on the reader's test harness printing "START OF TEST" and "END OF TEST" markers at the boundaries. A script that does this is a script whose logs are easy to slice.

**Extract just the timestamps and first word:**

```sh
awk '{print $1, $2, $3, $5}' /var/log/messages | grep myfirst
```

For high-level scanning of a busy log.

**Count how many of each message type:**

```sh
dmesg | grep myfirst | awk '{print $2}' | sort | uniq -c | sort -rn
```

This counts messages grouped by their second word, which (with the convention in Section 2) is usually the category like INFO, WARN, ERR, or DEBUG. Useful for spotting "why is my driver producing 30,000 DEBUG messages and 2 INFO messages?"

### Permanent Configuration: `/etc/syslog.conf` and `/etc/newsyslog.conf`

For most driver work, the default syslog configuration is fine. Chapter 23 does not ask the reader to change it. But it is worth knowing the two files exist and roughly what they do.

`/etc/syslog.conf` is read by `syslogd` at startup and on `SIGHUP`. It contains rules that map "facility and priority" to "destination". Facility is the kind of producer (`kern`, `auth`, `mail`, `daemon`, `local0` through `local7`), and priority is the syslog priority level. A driver's messages always have facility `kern` (because they come from the kernel) and the priority the driver specified.

A useful experiment, after the reader has read Chapter 23, is to add a line like:

```text
kern.debug					/var/log/myfirst-debug.log
```

to `/etc/syslog.conf`, touch the file, and reload syslogd with `service syslogd reload`. Now every kernel debug-level message goes to a separate file. If the reader then enables verbose mode on the `myfirst` driver (Section 8's sysctl), the separate file captures only the verbose output and the main `messages` file stays clean. This is a trick worth knowing; it lets a reader enable deep verbose logging without flooding `/var/log/messages` with everything.

`/etc/newsyslog.conf` is read by `newsyslog` when it rotates logs. A typical rule looks like:

```text
/var/log/messages    644  5    100    *     JC
```

The fields, left to right: file path, mode, number of rotations to keep, max size in KB, rotation time, flags. For the `myfirst-debug.log` file the reader just added, an appropriate newsyslog rule keeps the logs from growing without bound:

```text
/var/log/myfirst-debug.log  644  3  500  *  JC
```

Again, this is not a chapter requirement; it is a pattern worth knowing.

### Correlating Messages Across a Boot Cycle

One of the common debugging tasks is "the driver worked after the last boot but now it doesn't". The reader wants to compare what happened at the last boot to what is happening now. Two paths:

**Path one:** read `/var/run/dmesg.boot`, which captured the boot-time messages, and compare to the current `dmesg`:

```sh
diff <(grep myfirst /var/run/dmesg.boot) <(dmesg | grep myfirst)
```

This shows exactly which messages are new since boot. Useful for finding runtime surprises.

**Path two:** grep through the archives of `/var/log/messages`:

```sh
zgrep myfirst /var/log/messages.*.gz | head -100
```

This shows the driver's history over the last few days, across reboots. Useful for finding patterns like "it started failing on Tuesday".

A driver that logs its version at attach makes this much easier. If `dmesg.boot` shows `myfirst0: myfirst PCI driver 1.5-power attached` and the current boot shows `myfirst0: myfirst PCI driver 1.6-debug attached`, the reader knows immediately that the two are different versions and that a code change happened between them. Chapter 23's Stage 1 exercise adds exactly this version-at-attach line for that reason.

### Exercise: Generate a Known Driver Message and Confirm It Appears

A small hands-on companion for Section 3. The reader should:

1. Rebuild and load the `1.5-power-stage1` driver from Section 2's exercise. Confirm the attach banner appears in `dmesg`.

2. Trigger the simulator's `sim_force_dma_error=1` sysctl to make the next DMA transfer fail. Run a DMA transfer. Confirm the error message appears both in `dmesg` and in `/var/log/messages`.

3. Produce a verbose informational line: set the (not yet existing) debug_verbose sysctl to 1. Because the sysctl does not yet exist, this step will fail, and the reader should note which way it failed. This is intentional; Section 8 adds the sysctl.

4. For the lines that did appear, use `awk` or `grep` to extract only the ones with `myfirst0:` prefix, and count them.

5. Create a `/var/log/myfirst-debug.log` file by adding a `kern.debug` line to `/etc/syslog.conf` as sketched above. Reload `syslogd`. Confirm the file is created. Confirm no messages appear in it yet (because the `myfirst` driver does not yet produce `LOG_DEBUG` output). Make a note to return to this in Section 8.

The exercise is short. Its value is that the reader has now actually used `dmesg`, `/var/log/messages`, `grep`, and `syslog.conf` on real driver output. Future chapters will assume these tools are familiar.

### Wrapping Up Section 3

Section 3 followed the log from the moment the driver calls `device_printf` or `device_log` to the moment a user sees the output. Messages go first into the kernel's circular message buffer, accessible through `dmesg`. A boot-time snapshot is preserved in `/var/run/dmesg.boot`. The `syslogd` daemon reads new messages and writes them to `/var/log/messages` according to the rules in `/etc/syslog.conf`. The `newsyslog` daemon rotates the files according to `/etc/newsyslog.conf`. Console output is automatic for high-priority messages and gated for low-priority ones.

With the logging pipeline clear, Section 4 turns to the other essential piece of the debugging foundation: the debug kernel. A driver whose bugs always panic quietly and produce nothing for the log is a driver that cannot be debugged. A debug kernel makes those silent bugs loud.



## Section 4: Kernel Debug Builds and Options

A debug kernel is not a different kernel; it is the same kernel with additional checks, additional debug aids, and a small set of features compiled in that a release kernel does not have. The driver source does not change. The user-space tools do not change. What changes is that the running kernel now catches more of its own bugs, preserves more context when a panic happens, and offers more ways to inspect itself.

This section is the longest in the chapter because the debug kernel is the ground the rest of the chapter stands on. Every later tool depends on having the right options set. DTrace depends on `KDTRACE_HOOKS`. `ddb` depends on `DDB` and `KDB`. `INVARIANTS` catches bugs that would otherwise produce silent memory corruption. `WITNESS` catches lock order violations that would otherwise produce intermittent panics weeks later. Each option has a cost, a benefit, and a context in which it is appropriate; Section 4 walks through them.

### The Core Debug Options

FreeBSD's kernel is configured through a kernel config file, such as `/usr/src/sys/amd64/conf/GENERIC`. The config file is a list of `options` lines that enable or disable compile-time features. A debug kernel is produced by starting from `GENERIC` (or another baseline config), adding debug options, rebuilding, installing, and booting the result.

The options that matter for driver debugging fall into four groups: the kernel debugger, the consistency checks, the lock debug infrastructure, and the tracing infrastructure.

**Kernel debugger group.** These enable the in-kernel debugger `ddb` and the infrastructure that supports it.

- `options KDB`: enables the Kernel Debugger framework. This is the umbrella option that allows any backend (`DDB`, `GDB` over serial, etc.) to attach.
- `options DDB`: enables the in-kernel DDB debugger. DDB is the backend most driver authors use. It runs inside the kernel and understands kernel data structures.
- `options DDB_CTF`: enables loading of CTF (Compact C Type Format) data into the kernel, which lets `ddb` and DTrace print type-aware information about kernel structures. Needs `makeoptions WITH_CTF=1` in the config.
- `options DDB_NUMSYM`: enables symbol-number lookups inside DDB. Useful for poking at specific addresses.
- `options KDB_UNATTENDED`: on a panic, do not pause to enter `ddb`. Instead, the kernel dumps core (if `dumpdev` is configured), reboots, and continues. This is the right setting for lab VMs where the reader does not want to lose the kernel to a debugger session on every panic.
- `options KDB_TRACE`: on any entry into the debugger (panic or otherwise), automatically print a stack trace. Saves a step in almost every diagnostic session.
- `options BREAK_TO_DEBUGGER`: allows a break on the serial or USB console to drop into `ddb`. Useful on headless test systems.
- `options ALT_BREAK_TO_DEBUGGER`: alternative key combination for the same effect.

A typical debug kernel config includes all of these. The `GENERIC` kernel on amd64 already enables `KDB`, `KDB_TRACE`, and `DDB`; the additional options the reader enables are `KDB_UNATTENDED`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, and friends.

**Consistency check group.** These enable extra runtime assertions in the kernel code.

- `options INVARIANTS`: enable assertions in the kernel. Almost every `/usr/src/sys/*` file contains `KASSERT` calls, and those `KASSERT` calls only do something when `INVARIANTS` is enabled. Without `INVARIANTS`, a `KASSERT(ptr != NULL, ("oops"))` compiles to nothing. With `INVARIANTS`, it panics the kernel on failure. The cost is measurable (maybe 10% of kernel CPU time on a busy system) but usually negligible for driver development, because the kernel spends less time per operation than the application using it. The benefit is that bugs the driver would otherwise silently corrupt data over are caught at the moment they happen.
- `options INVARIANT_SUPPORT`: the machinery that `INVARIANTS` itself uses. Required if `INVARIANTS` is enabled; also required separately if any loadable module was built with `INVARIANTS`. Forgetting this produces "unresolved symbol" errors at module load.
- `options DIAGNOSTIC`: light-weight additional checks, less aggressive than `INVARIANTS`. Some subsystems use this for checks that are cheap enough to run in production but too slow for tight loops.

**Lock debug group.** These enable the infrastructure that verifies the driver's synchronisation discipline.

- `options WITNESS`: enable the lock-order verifier. WITNESS tracks every lock the kernel takes, records the order in which locks are acquired, and complains loudly when a thread acquires locks in an order that could deadlock if another thread held them the opposite way. A driver with a lock-order bug will produce a WITNESS report the first time the bad order is exercised, even if no actual deadlock occurs. This is an enormous debugging win, because it catches the bug before the deadlock manifests.
- `options WITNESS_SKIPSPIN`: skip the lock-order check for spin locks. Spin locks have their own constraints that the general checker is not designed to evaluate, and the checker can produce false positives on them. Enabling `WITNESS_SKIPSPIN` keeps the checker useful for the common mutex case.
- `options WITNESS_KDB`: on a lock-order violation, drop immediately into `ddb` instead of just logging. Aggressive; appropriate for a VM where a manual `ddb` session is easy.
- `options DEBUG_LOCKS`: additional debugging of the generic lock API (separate from WITNESS). Catches use of uninitialised locks, locks taken in the wrong context, and related issues.
- `options LOCK_PROFILING`: instrumentation that lets `lockstat` measure lock contention. Not strictly a debug option (it is a profiling option), and expensive; use it only when looking for contention, not by default.
- `options DEBUG_VFS_LOCKS`: VFS-specific lock debugging. Useful only for filesystem driver work.

**Tracing infrastructure group.** These enable the DTrace and KTR frameworks.

- `options KDTRACE_HOOKS`: the kernel-wide DTrace hooks. Without this, DTrace has nothing to attach to. Lightweight even when no DTrace script is running, because the hooks are mostly no-op function pointer slots.
- `options KDTRACE_FRAME`: unwind frame information that DTrace uses for `stack()` actions. Needed for meaningful stack traces inside DTrace scripts.
- `makeoptions WITH_CTF=1`: a `makeoptions` (not `options`) line that enables CTF generation for the kernel. CTF is the type-data DTrace and DDB use to understand kernel structure layouts.
- `options KTR`: the in-kernel event-ring-buffer tracer. Different from `ktrace`; KTR is a high-performance in-kernel buffer of kernel events that is mostly useful for low-level kernel hackers. Most driver authors do not need `KTR` and can leave it off; Chapter 23 mentions it only to name it correctly.
- `options KTR_ENTRIES`, `KTR_COMPILE`, `KTR_MASK`: the KTR configuration knobs. See `/usr/src/sys/conf/NOTES` for the full list.

For Chapter 23 the reader needs `KDB`, `DDB`, `DDB_CTF`, `KDB_UNATTENDED`, `KDB_TRACE`, `INVARIANTS`, `INVARIANT_SUPPORT`, `WITNESS`, `WITNESS_SKIPSPIN`, `KDTRACE_HOOKS`, and `makeoptions DEBUG=-g` for symbols. If the reader followed Chapter 22's prerequisites, these are already enabled.

### Building a Debug Kernel

Building a custom kernel is a standard FreeBSD operation. The workflow:

1. Copy the `GENERIC` config to a new name:

   ```sh
   cd /usr/src/sys/amd64/conf
   sudo cp GENERIC MYDEBUG
   ```

2. Edit `MYDEBUG` to add the debug options. A minimal addition:

   ```text
   ident MYDEBUG

   options INVARIANTS
   options INVARIANT_SUPPORT
   options WITNESS
   options WITNESS_SKIPSPIN
   options KDB_UNATTENDED

   makeoptions DEBUG=-g
   makeoptions WITH_CTF=1
   ```

   `DDB`, `KDB`, `KDTRACE_HOOKS`, and `DDB_CTF` are already in `GENERIC` on amd64 as of FreeBSD 14, so they do not need to be re-added. If you are on a non-x86 architecture, check the corresponding `conf/GENERIC` file to see what is already included.

   A small but frequent point of confusion: `/usr/src/sys/amd64/conf/` holds the kernel *configuration source files*. The older `/usr/src/sys/amd64/compile/` directory some tutorials mention is a build-output directory that `config(8)` historically populated; on a modern FreeBSD system the actual build products live under `/usr/obj/usr/src/amd64.amd64/sys/<KERNCONF>/`, and the `compile/` path is no longer the place you edit or look for source.

3. Build the kernel:

   ```sh
   cd /usr/src
   sudo make -j4 buildkernel KERNCONF=MYDEBUG
   ```

   On a reasonable VM this takes ten to twenty minutes. On a slower VM, half an hour. The build is the longest single step in the debug workflow, but it runs once per config change, not once per test.

4. Install the new kernel:

   ```sh
   sudo make installkernel KERNCONF=MYDEBUG
   ```

   This copies the kernel files into `/boot/kernel` and backs up the previous kernel to `/boot/kernel.old`. If the new kernel fails to boot, the reader can select `kernel.old` from the boot loader's menu and continue.

5. Reboot.

6. Verify:

   ```sh
   uname -a
   ```

   The output should mention `MYDEBUG` in the kernel's identification string.

7. Confirm debug options are active:

   ```sh
   sysctl debug.witness.watch
   sysctl kern.witness
   ```

   On a `WITNESS`-enabled kernel, these produce output. On a non-`WITNESS` kernel, the sysctls do not exist.

The total time from "want a debug kernel" to "running a debug kernel" is usually under an hour. The time invested is recovered within the first debugging session, because the debug kernel is the kernel that catches bugs.

### When to Use `INVARIANTS`

`INVARIANTS` is the most important single debug option for a driver developer. It is also the most misunderstood.

When a kernel programmer writes `KASSERT(condition, ("message %d", val))`, the intent is: this condition must be true at this point; if it is ever false, something has gone wrong in a way the code cannot recover from. Without `INVARIANTS`, `KASSERT` compiles to nothing; the condition is not checked. With `INVARIANTS`, `KASSERT` evaluates the condition and calls `panic` if it fails, printing the message.

Consider a concrete example from the `myfirst` driver. The DMA submission path (Chapter 21) holds a buffer lock, sets an in-flight flag, and calls `bus_dmamap_sync`. A bug that calls the submission path twice without waiting for completion is a double-submit bug, and it can corrupt DMA. A `KASSERT` at the top of the submission path catches it:

```c
static int
myfirst_dma_submit(struct myfirst_softc *sc, ...)
{
    KASSERT(!sc->dma_in_flight,
        ("myfirst_dma_submit: previous transfer still in flight"));
    ...
}
```

Without `INVARIANTS`, this line does nothing. With `INVARIANTS`, the first time a bug submits a second transfer before the first completes, the kernel panics with the message. The reader looks at the backtrace, sees the double-submit, and fixes the bug.

This is the shape of `INVARIANTS`'s value: it turns silent corruption into loud panics. The trade-off is that a driver author who writes no `KASSERT`s gets no benefit from `INVARIANTS`; the option is only useful if the code it runs against has assertions. The `myfirst` driver at Stage 1 has a few; Section 7 adds more; Section 8 moves them into the debug header.

Other assertion helpers exist alongside `KASSERT`:

- `MPASS(expr)`: a compact form of `KASSERT(expr, (...))`. Expands to the same check with a compiler-provided message. Useful when the condition is self-explanatory.
- `VNASSERT(expr, vp, msg)`: assertion specialised for vnodes. Driver authors rarely use this directly.
- `atomic_testandset_int`, `atomic_cmpset_int`: not assertion helpers, but common operations in driver synchronisation that have their own debug-aware variants.

Read `KASSERT` in its context: it is a documentation aid as much as a debug aid. A reader looking at an unfamiliar function's source finds the `KASSERT`s at the top and immediately knows the invariants the code expects. A driver that accumulates `KASSERT`s over time becomes self-documenting in a way plain prose does not match.

### When to Use `WITNESS` and `DEBUG_LOCKS`

`WITNESS` is the other half of the driver-debugging pair. Where `INVARIANTS` catches logic bugs, `WITNESS` catches synchronisation bugs.

The basic idea: every lock has an identifier, the kernel records the set of locks each thread holds, and when a thread tries to acquire a lock, `WITNESS` checks whether the order of acquisition is consistent with previous acquisitions in the kernel's history. If thread A takes lock X then lock Y, and later thread B takes lock Y then lock X, the two threads form a potential deadlock: if A holds X waiting for Y while B holds Y waiting for X, neither can proceed. `WITNESS` flags the inconsistency the second time a thread tries to acquire the locks in the wrong order, even if the actual deadlock does not happen during the test.

A `WITNESS` complaint looks something like:

```text
lock order reversal:
 1st 0xfffff80001a23200 myfirst_sc (myfirst_sc) @ /usr/src/sys/dev/myfirst/myfirst_pci.c:523
 2nd 0xfffff80001a23240 some_other_lock (some_other_lock) @ /some/other/file.c:789
witness_order_list_add: lock order reversal
```

The message identifies both locks, their addresses, the files and line numbers where they were acquired, and the reversal. A driver author reads this and immediately knows where to look.

`WITNESS` interacts with the other debug options. `WITNESS_KDB` drops into `ddb` on a violation instead of just logging. `WITNESS_SKIPSPIN` skips spin locks, which have different rules (they cannot sleep, they have priority handling, and the ordering discipline is slightly different). `DEBUG_LOCKS` adds extra checks on top of `WITNESS`: detection of double-unlock, detection of unlock by the wrong thread, detection of use of uninitialised mutexes. For driver development, enabling all three (`WITNESS`, `WITNESS_SKIPSPIN`, `DEBUG_LOCKS`) is the default recommendation.

The cost of `WITNESS` is measurable. Every lock acquisition adds a few dozen instructions and one cache line of memory touch. On a driver-heavy workload this can be 20 or 30 percent slower than a non-debug kernel. For development and testing, this is acceptable. For a production kernel, `WITNESS` is usually off.

Section 7's common-bug walkthrough includes a `WITNESS` lab: the reader deliberately introduces a lock order violation into the `myfirst` driver, rebuilds, reloads, triggers the path, and watches `WITNESS` catch it. That lab is the fastest way to internalise how the tool works.

### When to Use `DEBUG_MEMGUARD` and `MEMGUARD`

`MEMGUARD` is a heavier memory-debug allocator. It allocates memory with guard pages on either side, detects writes past the allocation boundary, and detects use-after-free. It is enabled with `options DEBUG_MEMGUARD` and configured at runtime through `sysctl vm.memguard.desc`.

MEMGUARD is expensive: it uses far more memory than a normal allocator because of the guard pages, and its allocation path is slower. The right use is targeted: you enable it for a specific malloc type the driver uses heavily, not for the whole kernel. If the `myfirst` driver has a malloc type `M_MYFIRST`, the reader can enable MEMGUARD for just that type:

```sh
sysctl vm.memguard.desc="M_MYFIRST"
```

After a reboot (MEMGUARD needs to initialise its arena at boot), allocations of `M_MYFIRST` memory will go through MEMGUARD. A write past the end of an allocation panics immediately. A use after free panics immediately. The reader can run tests and have the kernel catch heap bugs in the driver's memory without slowing the rest of the system down.

Chapter 23 does not make heavy use of MEMGUARD, because the `myfirst` driver's memory patterns are simple. Section 7 mentions it as a tool in the kit for drivers that do more complex allocation. For the reader who eventually writes a driver with dynamic queues, buffer pools, or long-lived allocations, MEMGUARD is worth returning to.

### Trade-Offs: Performance vs. Debug Visibility

The temptation, when first learning the options, is to enable everything. This is the right choice during development. It is the wrong choice for production.

A kernel with `INVARIANTS`, `WITNESS`, `DEBUG_LOCKS`, `KDTRACE_HOOKS`, `DDB`, and `MEMGUARD` on is noticeably slower than `GENERIC`: maybe 20% slower overall, 50% slower on lock-heavy workloads, depending on what the machine is doing. On a development workstation, 20% slower is fine. On a production server, 20% slower means 20% more hardware to do the same work.

The distinction between debug kernels and production kernels is sharp in practice. Developers run debug kernels, because they care about finding bugs. Users run release kernels, because they care about performance. A driver that is correct under `WITNESS` and `INVARIANTS` is correct, period; those options do not change driver behavior, they only check it. A driver that passes a `WITNESS` lab run under a debug kernel will also pass under a release kernel. This is the invariant that makes the pattern work: you develop against a strict kernel and deploy against a permissive one, and the strict kernel's extra checks are what keep the permissive one correct.

For the reader, the practical advice: run a debug kernel on the VM you use for driver work, all the time. Do not switch to a release kernel for daily development. The feedback on driver mistakes is worth the performance cost a hundred times over. When you are ready to test the driver on a production-style kernel (for timing measurements, for integration tests), boot a release kernel for that specific test, confirm correct behavior, and then go back to the debug kernel.

### A Survey of `ddb` Commands

DDB is the in-kernel debugger. When the kernel panics on a system where `DDB` is compiled in, instead of rebooting immediately, the system drops into a debugger prompt on the console:

```text
KDB: enter: panic
[ thread pid 42 tid 100049 ]
Stopped at:  kdb_enter+0x3b:  movq    $0,kdb_why
db>
```

At that prompt, the reader can inspect the kernel's state. The commands are terse and direct. A full tour belongs in a separate reference; what follows is the list a driver author uses most.

**Inspection commands:**

- `bt` or `backtrace`: show the current thread's backtrace. This is the first command the reader runs on every panic.
- `ps`: show the process table. Useful for seeing what was running.
- `show thread <tid>`: show details of a specific thread.
- `show proc <pid>`: show details of a specific process.
- `show pcpu`: show per-CPU state, including the current thread on each CPU.
- `show allpcpu`: show all per-CPU state at once.
- `show lockchain <addr>`: show the chain of threads blocked on a specific lock.
- `show alllocks`: show all locks currently held, across all threads. Useful for diagnosing apparent deadlocks.
- `show mbufs`: show mbuf statistics, if the network stack is involved.
- `show malloc`: show kernel malloc statistics, grouped by type. Useful for finding a driver that leaked memory before the panic.
- `show registers`: show the current CPU's registers.

**Navigation commands:**

- `x/i <addr>`: disassemble at an address. Useful for looking at the instruction where a page fault happened.
- `x/xu <addr>`: dump memory as hex bytes.
- `x/sz <addr>`: dump memory as a null-terminated string.

**Control commands:**

- `continue`: resume execution. If the kernel was stable enough to enter `ddb`, this can sometimes let the machine continue normally. Often the safer choice is `panic`.
- `reset`: reboot the machine immediately.
- `panic`: force a panic, which triggers a crash dump if `dumpdev` is set.
- `call <func>`: call a kernel function by name. Advanced; rarely useful in driver debugging.

**Thread-walking commands:**

- `show all procs`: list all processes.
- `show sleepq`: show sleeping queues.
- `show turnstile`: show turnstile state (used for blocking locks).
- `show sema`: show semaphores.

A driver author does not memorise this list. The pattern is: when you enter `ddb`, run `bt` first, then `ps`, then `show alllocks`, then `show mbufs`, then `show malloc`. Those five commands capture 80% of what most driver panics need.

A useful habit is to print the commands and keep them next to the keyboard for the first few `ddb` sessions. The first session feels awkward because the muscle memory is not there. By the third session, the commands are fluent.

### A Brief Introduction to `kgdb`

`ddb` is the live debugger. `kgdb` is the post-mortem debugger. When a kernel panics with `dumpdev` configured, the panic routine writes the full kernel memory image to the dump device. On the next boot, `savecore(8)` copies the dump from the dump device into `/var/crash`. A reader can then open it with `kgdb`:

```sh
sudo kgdb /boot/kernel/kernel.debug /var/crash/vmcore.0
```

(Note: `kernel.debug` is the debug-symbol version of the kernel, produced when the kernel was built with `makeoptions DEBUG=-g`. Without `DEBUG=-g`, `kgdb` can still open the dump but produces less useful output.)

Inside `kgdb`, the reader has something close to a normal `gdb` interface. The commands are the same: `bt`, `frame N`, `info locals`, `print variable`, `list`, and so on. The difference is that the machine is not running; the kernel is paused forever at the moment of the panic, and every inspection is against that frozen state. You can walk up the stack, look at data structures, follow pointers, and reason about what led to the panic. You cannot step forward.

A typical `kgdb` session on a panic looks like this:

```text
(kgdb) bt
#0  doadump (...)
#1  kern_reboot (...)
#2  vpanic (...)
#3  panic (...)
#4  witness_checkorder (...)
...

(kgdb) frame 10
#10 myfirst_pci_detach (dev=0xfffff80001a23000) at myfirst_pci.c:789
789            mtx_destroy(&sc->lock);

(kgdb) list
784         mtx_lock(&sc->lock);
785         sc->detaching = true;
786         /* wait for all users */
787         while (sc->refs > 0)
788             cv_wait(&sc->cv, &sc->lock);
789            mtx_destroy(&sc->lock);
790         return (0);
791     }

(kgdb) print sc->refs
$1 = 0

(kgdb) print sc->lock
$2 = {
   ...
}
```

The reader walks the stack to find the driver frame, looks at what the driver was doing, and identifies the bug. In this example, the driver was destroying a mutex that was still locked, because line 789 calls `mtx_destroy` without unlocking. The bug is immediately visible.

Chapter 23 does not make heavy use of `kgdb`, because the labs are designed to be diagnosable from `ddb` and `dmesg` alone. Section 7 mentions `kgdb` in the common-bug walkthrough for a class of bugs where post-mortem inspection is the right tool. The reader who wants more should read the `kgdb(1)` man page and experiment on a known panic.

### Exercise: Build a Debug Kernel and Confirm Symbols Are Present With `kgdb`

A short exercise that grounds the section.

1. If not already done, build and install a `MYDEBUG` kernel as described above, with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, and `makeoptions DEBUG=-g`. Reboot.

2. Confirm the running kernel is the debug build:

   ```sh
   uname -v | grep MYDEBUG
   sysctl debug.witness.watch
   ```

3. Configure a dump device. This is usually a swap partition:

   ```sh
   sudo dumpon /dev/adaNpY
   ```

   where `adaNpY` is your swap partition. On a typical VM this is `ada0p3`.

4. Trigger a deliberate panic to produce a crash dump. The safest way is to run `sysctl debug.kdb.panic=1` as root:

   ```sh
   sudo sysctl debug.kdb.panic=1
   ```

   The system enters `ddb` (because `KDB_UNATTENDED` was not set for this exercise; if it was set, the system writes a dump and reboots). Run `bt`, `ps`, and `show alllocks`, then type `panic` to let the dump proceed. The system writes to the dump device and reboots.

5. After reboot, verify that `savecore(8)` has copied the dump:

   ```sh
   ls /var/crash
   ```

   You should see `vmcore.0`, `info.0`, and `kernel.0`.

6. Open the dump with `kgdb`:

   ```sh
   sudo kgdb /var/crash/kernel.0 /var/crash/vmcore.0
   ```

7. Inside `kgdb`, run `bt`. You should see the panic stack. Walk up a few frames with `frame N`. Confirm that source lines and function arguments are visible; this is what the debug symbols buy.

8. Quit `kgdb` with `quit`.

The exercise is not about fixing a bug; it is about proving that the debug infrastructure works. From this point on, any real panic the reader causes is fully debuggable: the crash is preserved, the symbols are present, the debugger opens the dump. That is the foundation. Without it, debugging is mostly guessing.

### Wrapping Up Section 4

Section 4 turned the reader's kernel into a debug kernel. The options enable assertions, lock-order checking, DTrace hooks, debug symbols, and the in-kernel debugger. The cost is a slower system; the benefit is a system that catches its own bugs loudly. `ddb` is the live inspection tool; `kgdb` is the post-mortem inspection tool. Both work against the same debug kernel the reader is now running.

With the logging pipeline (Section 3) and the debug kernel (Section 4) in place, the reader has the infrastructure that every later tool assumes. Section 5 introduces DTrace, the tool that turns the debug kernel into a live tracing and measurement platform.



## Section 5: Using DTrace for Live Kernel Inspection

DTrace is the scalpel of the FreeBSD debugging toolkit. Where `printf` is a blunt instrument and `ddb` is a hammer, DTrace is a scalpel: it lets you ask specific, sharp questions about the kernel's live behavior and get answers without modifying the source, rebuilding the kernel, or stopping the system. A driver author who learns DTrace reaches a new kind of productivity: the ability to say "I wonder how often the driver's interrupt fires" and get an answer in ten seconds without changing a line of code.

This section introduces DTrace at the level of detail a driver author needs. It does not try to be a DTrace reference; the excellent `dtrace(1)` man page, the DTrace Guide available online, and the book *Illumos DTrace Toolkit* are the deeper references. What Section 5 gives the reader is enough DTrace to instrument the `myfirst` driver, measure its behavior, and understand what DTrace can and cannot do.

### What Is DTrace?

DTrace is a **dynamic tracing framework**. The word "dynamic" is important: it does not require the kernel to have been built with knowledge of every trace point; it does not require the running code to be patched; it does not require the reader to rebuild and reboot. You load the DTrace module, ask for a probe, and the framework attaches instrumentation to the running kernel code. When the instrumented code runs, the instrumentation fires, and DTrace records the event. When you are done, you detach, and the instrumentation is removed.

The framework originated in Solaris and was ported to FreeBSD. It lives under `/usr/src/cddl/` (the CDDL-licensed portion of the source tree), because it inherits the Solaris licensing. The user-space tool is `dtrace(1)`; the kernel-side infrastructure is a set of modules that can be loaded and unloaded on demand.

DTrace has three organising concepts: **providers**, **probes**, and **actions**.

A **provider** is a source of trace points. Examples of providers FreeBSD ships with: `fbt` (function boundary tracing, one probe per entry and exit of every kernel function), `syscall` (one probe per system call entry and return), `sched` (scheduler events), `io` (block I/O events), `vfs` (filesystem events), `proc` (process events), `vm` (virtual memory events), `sdt` (statically defined tracing, for probes compiled into the kernel), and `lockstat` (lock operations). Some providers (`fbt`, `syscall`) are always available once the module is loaded; others (`sdt`) only exist if the kernel code explicitly defines probes.

A **probe** is a specific point the reader can trace. Probes have four-part names in the form `provider:module:function:name`. For example, `fbt:kernel:myfirst_pci_attach:entry` is the `fbt` provider's entry probe on the function `myfirst_pci_attach` in the kernel module `kernel` (the main kernel binary, not a loadable module). A wildcard `*` matches any segment: `fbt::myfirst_*:entry` matches every function whose name starts with `myfirst_`, in any module, at function entry.

An **action** is what DTrace does when a probe fires. The simplest action is `{ trace(...) }`, which records the argument. More interesting actions aggregate: `@counts[probefunc] = count()` counts how many times each function's probe fired. The D language (DTrace's scripting language) is C-like but safer: it has no loops, no function calls, no memory allocation, and runs in the kernel under strict safety constraints.

The power of DTrace comes from the combination: you enable a specific probe, you attach a specific action, you observe only the events you care about, and you do it without touching the source.

### Enabling DTrace Support

The kernel options needed for DTrace were covered in Section 4: `options KDTRACE_HOOKS`, `options KDTRACE_FRAME`, `options DDB_CTF`, and `makeoptions WITH_CTF=1`. If the reader built the `MYDEBUG` kernel with those options, DTrace is ready to go.

The user-space side needs the DTrace module to be loaded. The umbrella module is `dtraceall`, which pulls in all the providers:

```sh
sudo kldload dtraceall
```

Alternatively, individual providers can be loaded:

```sh
sudo kldload dtrace          # core framework
sudo kldload dtraceall       # all providers (recommended for development)
sudo kldload fbt             # just function boundary tracing
sudo kldload systrace        # just syscall tracing
```

For development, `dtraceall` is the simplest choice: all the providers the reader might want are available.

To confirm DTrace is working:

```sh
sudo dtrace -l | head -20
```

This lists the first twenty probes DTrace knows about. On a default FreeBSD 14 kernel with `dtraceall` loaded, the output includes hundreds of thousands of probes, most from the `fbt` provider (every kernel function has an entry and return probe). A quick count:

```sh
sudo dtrace -l | wc -l
```

A typical number is 40,000 to 80,000 probes, depending on what modules are loaded.

To list only the probes in the `myfirst` driver's functions:

```sh
sudo dtrace -l -n 'fbt::myfirst_*:'
```

If the driver is loaded, this produces a list of every `myfirst_*` function's entry and return probes. If the driver is not loaded, the list is empty: `fbt` probes only exist for code the kernel has loaded.

### Writing Simple DTrace Scripts

The simplest DTrace invocation traces a single probe and prints when it fires:

```sh
sudo dtrace -n 'fbt::myfirst_pci_attach:entry'
```

The syntax is `provider:module:function:name`. The double colon in `fbt::myfirst_pci_attach:entry` uses the default empty module (which matches any module). DTrace prints one line per probe firing:

```text
CPU     ID                    FUNCTION:NAME
  2  28791         myfirst_pci_attach:entry
```

This confirms the function was called, on CPU 2, with the internal probe ID 28791. For a one-shot "did this function run?" question, that is already enough.

A slightly more useful script traces the entry and return:

```sh
sudo dtrace -n 'fbt::myfirst_pci_attach:entry,fbt::myfirst_pci_attach:return'
```

Each line shows when entry and return fired. The two together tell the reader that the function ran and returned without panicking.

To trace many functions at once, use a wildcard:

```sh
sudo dtrace -n 'fbt::myfirst_*:entry'
```

Every `myfirst_*` function's entry produces a line. For the reader debugging an attach path, this is a cheap way to see the call sequence in real time.

To capture arguments, add a D-language action:

```sh
sudo dtrace -n 'fbt::myfirst_pci_suspend:entry { printf("%d: suspending %s", timestamp, stringof(args[0]->name)); }'
```

The `args[0]` refers to the first argument of the probed function. For FBT probes, `args[0]` is the first argument to the function. For `myfirst_pci_suspend`, the first argument is a `device_t`, and `args[0]->name` (if FreeBSD's device_t layout has a `name` field accessible to DTrace) shows the device name.

Note: `device_t` is actually a pointer to an opaque structure, and DTrace can only follow type-aware pointer chains if the CTF data is loaded. This is why `options DDB_CTF` and `makeoptions WITH_CTF=1` matter: without them, DTrace sees `args[0]` as an integer and cannot dereference it meaningfully. With them, DTrace knows `args[0]` is a `device_t` (which is a `struct _device *`) and can walk the structure.

A more interesting script counts how many times each `myfirst_*` function fires:

```sh
sudo dtrace -n 'fbt::myfirst_*:entry { @counts[probefunc] = count(); }'
```

Let this run for a minute while the driver is exercised. Press Ctrl-C. DTrace prints a histogram:

```text
  myfirst_pci_attach                                                1
  myfirst_pci_detach                                                1
  myfirst_pci_suspend                                              42
  myfirst_pci_resume                                               42
  myfirst_intr_filter                                           10342
  myfirst_rx_task                                                5120
  myfirst_dma_submit                                             1024
```

The reader sees, at a glance, the driver's call distribution: two attach/detach, forty-two suspend/resume cycles, ten thousand interrupt filters, five thousand rx tasks, one thousand DMA submits. Any unexpected number is a signal to investigate. If the reader expected one-to-one between interrupts and tasks, and sees two-to-one, there is a bug.

### Measuring Function Latency

One of DTrace's most useful idioms is measuring how long a function takes. The pattern is to timestamp entry, timestamp return, and subtract:

```sh
sudo dtrace -n '
fbt::myfirst_pci_suspend:entry { self->start = timestamp; }
fbt::myfirst_pci_suspend:return /self->start/ {
    @times = quantize(timestamp - self->start);
    self->start = 0;
}'
```

The first clause stores the entry timestamp in a thread-local variable `self->start`. The second clause, predicated on `self->start` being non-zero, computes the delta, adds it to a quantize aggregation (a logarithmic histogram), and clears the variable. Let this run while the reader exercises suspend cycles, press Ctrl-C, and DTrace prints the histogram:

```text
           value  ------------- Distribution ------------- count
            1024 |                                         0
            2048 |@@@@                                     4
            4096 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@             28
            8192 |@@@@@@@@                                 8
           16384 |@@                                       2
           32768 |                                         0
```

The numbers are in nanoseconds. This says: most suspend calls took between 4,096 and 8,192 nanoseconds (4 to 8 microseconds), with a tail reaching 16,384 nanoseconds (16 microseconds). For a suspend call that should be fast, this is the kind of measurement that tells the reader whether the driver's discipline is working.

For a different kind of insight, use `avg()` or `max()` aggregations:

```sh
sudo dtrace -n '
fbt::myfirst_pci_suspend:entry { self->start = timestamp; }
fbt::myfirst_pci_suspend:return /self->start/ {
    @avg = avg(timestamp - self->start);
    @max = max(timestamp - self->start);
    self->start = 0;
}'
```

This prints the average and maximum suspend time at the end. Useful for summary reports.

### Using the `syscall` Provider

The `syscall` provider shows every system call. This is especially useful for driver work, because user-space calls to the driver (open, read, write, ioctl) go through the syscall layer first. A script that watches for system calls on a specific file descriptor is a lightweight way to see how user programs use the driver:

```sh
sudo dtrace -n 'syscall::ioctl:entry /execname == "myftest"/ { printf("%s ioctl on fd %d", execname, arg0); }'
```

This prints every `ioctl` system call made by a process named `myftest`. The `execname` variable is a built-in DTrace variable that holds the current process's executable name. Running the test program `myftest` (a small user program that exercises the driver) while this script runs gives a live view of every ioctl the program issues.

### Using the `sched` Provider

The `sched` provider tracks scheduler events: thread switches, wake-ups, enqueues. It is useful for understanding concurrency in the kernel, including driver interrupt handling:

```sh
sudo dtrace -n 'sched:::on-cpu /execname == "myftest"/ { @[cpu] = count(); }'
```

This counts, per CPU, how many times the `myftest` program was scheduled onto each CPU during the trace. Useful for understanding whether the scheduler is keeping the program on one core or bouncing it across cores, which affects the driver's cache behavior.

### Using the `io` Provider

The `io` provider shows block I/O events. Not directly relevant to `myfirst` (a generic PCI driver), but useful to know about. A script to show every disk I/O:

```sh
sudo dtrace -n 'io:::start { printf("%s %d", execname, args[0]->bio_bcount); }'
```

For storage drivers, `io` is essential. For `myfirst` it is peripheral. Chapter 23 does not explore it in depth.

### Statically Defined Tracing (SDT)

The `fbt` provider instruments every function entry and return. That is flexible but blunt: you get entry/return probes for every function, and you often want probes at specific interesting points inside a function.

SDT (Statically Defined Tracing) solves this. SDT lets the driver author add named probes to specific places in the source, and DTrace makes them visible. When DTrace is not watching, the probes are essentially free: they compile to a single no-op instruction. When DTrace attaches, the no-op is replaced with a trap that fires the probe.

The SDT machinery lives in `/usr/src/sys/sys/sdt.h`. The basic macros are:

- `SDT_PROVIDER_DEFINE(name)`: declares a provider. Usually done once per driver, in the main source file.
- `SDT_PROBE_DEFINEN(provider, module, function, name, "arg1-type", "arg2-type", ...)`: declares a probe with N typed arguments.
- `SDT_PROBEN(provider, module, function, name, arg1, arg2, ...)`: fires the probe with the given arguments.

A minimal example for the `myfirst` driver. In `myfirst_pci.c`, near the top:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE2(myfirst, , attach, entry,
    "struct myfirst_softc *", "device_t");
SDT_PROBE_DEFINE2(myfirst, , attach, return,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE3(myfirst, , dma, submit,
    "struct myfirst_softc *", "bus_addr_t", "size_t");
SDT_PROBE_DEFINE2(myfirst, , dma, complete,
    "struct myfirst_softc *", "int");
```

Then in the driver code:

```c
int
myfirst_pci_attach(device_t dev)
{
    struct myfirst_softc *sc = device_get_softc(dev);
    int error;

    SDT_PROBE2(myfirst, , attach, entry, sc, dev);
    ...
    error = ...;  /* real attach code */
    ...
    SDT_PROBE2(myfirst, , attach, return, sc, error);
    return (error);
}
```

From user space, the reader can now observe `myfirst` probes through DTrace:

```sh
sudo dtrace -n 'myfirst:::' | head
```

This shows every myfirst probe. To watch only DMA events:

```sh
sudo dtrace -n 'myfirst:::dma-submit'
sudo dtrace -n 'myfirst:::dma-complete'
```

To count submissions per DMA size:

```sh
sudo dtrace -n 'myfirst:::dma-submit { @[args[2]] = count(); }'
```

This prints a histogram of DMA transfer sizes at the end of the trace. For a driver debugging session, that is the kind of data that turns speculation into evidence.

Chapter 23's Stage 2 adds exactly these probes to the `myfirst` driver. The exercise at the end of Section 5 walks through the addition. The corresponding files live in `examples/part-05/ch23-debug/stage2-sdt/`.

Real drivers that use SDT are worth studying. `/usr/src/sys/dev/virtio/virtqueue.c` defines:

```c
SDT_PROVIDER_DEFINE(virtqueue);
SDT_PROBE_DEFINE6(virtqueue, , enqueue_segments, entry, "struct virtqueue *",
    ...);
SDT_PROBE_DEFINE1(virtqueue, , enqueue_segments, return, "uint16_t");
```

and fires them in `virtqueue_enqueue`:

```c
SDT_PROBE6(virtqueue, , enqueue_segments, entry, vq, desc, head_idx, ...);
...
SDT_PROBE1(virtqueue, , enqueue_segments, return, idx);
```

The reader can run `sudo dtrace -l -n 'virtqueue:::' ` on a system with virtio-backed devices (common in VMs) and see these probes immediately. Tracing them gives a live view of virtqueue activity that would be impossible to reproduce with `printf`.

### Useful Providers for Driver Work

A summary of the providers a driver author uses most:

- **`fbt`**: function boundary tracing. Every kernel function has entry and return probes. Best for "did this function run?" and "what arguments did it see?".
- **`sdt`**: statically defined tracing. Probes the driver explicitly adds. Best for observing driver-specific events at specific points.
- **`syscall`**: system call tracing. Best for seeing what user programs are asking the kernel to do.
- **`sched`**: scheduler events. Best for understanding concurrency and CPU usage.
- **`io`**: block I/O events. Best for storage work.
- **`vfs`**: filesystem events. Best for filesystem driver work.
- **`proc`**: process lifecycle events. Best for process creation, exit, and signal delivery.
- **`lockstat`**: lock operations. Best for lock contention analysis; expensive when active.
- **`priv`**: privilege-related events. Useful for security auditing.

Most driver work uses `fbt`, `sdt`, and `syscall` heavily, and touches the others occasionally.

### Common DTrace Pitfalls

A few patterns that trip new DTrace users.

**Buffer too small.** DTrace has per-CPU buffers; if a script fires probes faster than DTrace can drain them, events are dropped. The reader sees a message like `dtrace: X drops on CPU N`. The fix is usually to make the predicate tighter (fire less often), or to increase buffer size with `-b 16m`.

**Forgetting to clear thread-local variables.** In the latency-measurement pattern, the script sets `self->start` on entry and checks `self->start != 0` on return. If the reader forgets to clear it (`self->start = 0;` after use), the variable accumulates state and later traces get confused. Always pair set with clear.

**Using `trace()` when `printf()` would be clearer.** `trace(x)` prints the value in a terse format that is hard to read. `printf("x=%d", x)` is usually better.

**Running as a non-privileged user.** DTrace requires root. Running `dtrace` as a regular user fails with a permission error. Always use `sudo dtrace`.

**Probes that do not exist.** If a probe name is mistyped or the module is not loaded, `dtrace` prints an error. The fix is to confirm the probe exists with `dtrace -l` before trying to use it.

**The `stringof()` trap.** When printing a kernel string pointer, the reader often needs `stringof(ptr)`, not `ptr`, because DTrace does not automatically follow kernel pointers. Forgetting this produces garbage output.

### Exercise: Use DTrace to Measure How Long a Driver Read Operation Takes

A hands-on companion for Section 5.

1. Ensure the `MYDEBUG` kernel is running and `dtraceall` is loaded.

2. Write a user program that reads from `/dev/myfirst0` a hundred times. The program can be as simple as:

   ```c
   #include <fcntl.h>
   #include <unistd.h>
   int main(void) {
       int fd = open("/dev/myfirst0", O_RDWR);
       char buf[4096];
       for (int i = 0; i < 100; i++)
           read(fd, buf, sizeof(buf));
       close(fd);
       return 0;
   }
   ```

   Compile with `cc -o myftest myftest.c`.

3. In one terminal, run the DTrace latency script on the driver's `read` path. Assuming the driver's read function is `myfirst_read`:

   ```sh
   sudo dtrace -n '
   fbt::myfirst_read:entry { self->start = timestamp; }
   fbt::myfirst_read:return /self->start/ {
       @times = quantize(timestamp - self->start);
       self->start = 0;
   }'
   ```

4. In another terminal, run the user program:

   ```sh
   ./myftest
   ```

5. Press Ctrl-C on the DTrace script. Read the histogram. Most reads should be in the low microseconds (a few thousand nanoseconds).

6. Take a screenshot or save the output. Compare to your expectation. If a read is taking longer than you expect, that is the start of a debugging session.

Optional: run the same script against `fbt::myfirst_intr_filter:entry` and observe the interrupt latency distribution.

### Wrapping Up Section 5

Section 5 introduced DTrace as the scalpel of the debugging toolkit. DTrace is dynamic: probes attach to running code without rebuilds. It is safe: the D language has no loops or memory allocation. It is broad: providers expose tens of thousands of observation points across the kernel. For driver work, the combination of `fbt` (automatic probes on every function) and `sdt` (custom probes at specific points) gives the reader the ability to answer almost any "what is the driver doing?" question without modifying the source.

The cost of DTrace when inactive is essentially zero: probes exist in the kernel as no-op instructions or function-pointer slots. The cost when active depends on what the script does: a simple count is cheap, a complex histogram is more expensive, and a script that prints on every probe can flood the buffer. Understanding the trade-off is part of writing effective DTrace scripts.

With logging (Section 2), log pipelines (Section 3), debug kernels (Section 4), and DTrace (Section 5) in place, the reader has four complementary views into kernel behavior. Section 6 adds the last essential view for driver work: `ktrace`, the tool that shows what a user program is doing as it crosses into the kernel through the driver's interface.



## Section 6: Tracing Kernel Activity With `ktrace` and `kdump`

DTrace traces the kernel from the kernel side: which functions ran, how long they took, what state they touched. `ktrace` traces the same activity from the user side: which system calls a process made, what arguments it passed, what return values it got, what signals it received. Together, the two tools give a complete view. DTrace shows what the kernel did; `ktrace` shows what the program asked for.

For driver work, `ktrace` is the tool of choice when the question is "what is the user program doing?". A driver that gets strange arguments can be traced back: maybe the program is calling `ioctl` with the wrong command number, or `write` with a zero-length buffer, or `open` with the wrong flags. `ktrace` answers those questions by recording every syscall the program issues.

### What Is `ktrace`?

`ktrace(1)` is a user-space tool that tells the kernel to record a trace of one or more processes' activities into a file. The recorded events include:

- Every system call entry and return, with arguments and return value.
- Every signal delivered to the process.
- Every `namei` (path-name lookup) event.
- Every I/O buffer passed to read or write.
- Context switches, at a coarse level.

The trace is written to a binary file (by default `ktrace.out`). To read it, the reader runs `kdump(1)`, which translates the binary trace into human-readable text.

`ktrace` is a simple tool, much older than DTrace, and it shows its age in some interfaces. But for the core question of "what did this process ask the kernel to do?", it is ideal.

The implementation is in `/usr/src/sys/kern/kern_ktrace.c` and the user-facing header is in `/usr/src/sys/sys/ktrace.h`. The user-space tools are under `/usr/src/usr.bin/ktrace/` and `/usr/src/usr.bin/kdump/`.

The kernel needs `options KTRACE` to support `ktrace`. This option is enabled in `GENERIC` by default on every architecture; the reader does not need to rebuild a kernel to use `ktrace`.

### Starting a Trace

The simplest invocation attaches to a running process by PID:

```sh
sudo ktrace -p 12345
```

This tells the kernel to start recording the process with PID 12345. The kernel writes events to `ktrace.out` in the current directory. When the reader is done, they stop the trace:

```sh
sudo ktrace -C
```

Or, more precisely, stop tracing for a specific PID:

```sh
sudo ktrace -c -p 12345
```

To run a command from scratch with tracing enabled:

```sh
sudo ktrace /path/to/program arg1 arg2
```

This starts the program and traces it from the first syscall. When the program exits, the trace file remains for the reader to examine.

Useful flags:

- `-d`: trace descendants too. If the program forks, the children are also traced.
- `-i`: inherit the trace across `exec`. If the program `exec`s another program, the trace continues. Without `-i`, the trace stops at exec.
- `-t <mask>`: select which event types to record. The mask is a set of letter codes, like `cnios` (calls, namei, I/O, signals). Default is most types.
- `-f <file>`: write to a specific file instead of `ktrace.out`.
- `-C`: clear all tracing (stop all traces).

A common combination:

```sh
sudo ktrace -di -f mytrace.out -p 12345
```

Trace the process, all descendants, through exec, into `mytrace.out`.

### Reading Output With `kdump`

The binary trace file is useless on its own. `kdump` translates it:

```sh
kdump -f mytrace.out
```

Output looks like:

```text
 12345 myftest  CALL  open(0x8004120a0,0x0)
 12345 myftest  NAMI  "/dev/myfirst0"
 12345 myftest  RET   open 3
 12345 myftest  CALL  ioctl(0x3,0x20006601,0x7fffffffe8c0)
 12345 myftest  RET   ioctl 0
 12345 myftest  CALL  read(0x3,0x7fffffffe900,0x1000)
 12345 myftest  GIO   fd 3 read 4096 bytes
                 "... data bytes ..."
 12345 myftest  RET   read 4096/0x1000
```

Each line has a PID, a process name, an event type, and event-specific data:

- `CALL` lines show a syscall entry, with arguments as hex numbers.
- `NAMI` lines show a path-name lookup, with the resolved string.
- `RET` lines show a syscall return, with the value.
- `GIO` lines show generic I/O buffers, followed by a dump of the buffer contents.
- `PSIG` lines show signal delivery.

The output is dense but readable with practice. A common pattern for driver work:

1. Run the user program under `ktrace`.
2. Look for the `open` of `/dev/myfirst0`. Note the file descriptor the kernel returned.
3. Filter subsequent lines to the operations on that fd: `ioctl`, `read`, `write`, `close`.
4. Note the exact arguments, especially for `ioctl` where the command number encodes which operation the driver was asked to perform.

For example, the `ioctl(0x3,0x20006601,0x7fffffffe8c0)` line above shows the program issuing an ioctl on fd 3 with command `0x20006601`. The command is the `_IO`/`_IOR`/`_IOW`/`_IOWR` encoding; the low 16 bits are the command number and the high bits are the direction and size. A driver author debugging an ioctl bug can look at this command, compare it to the definitions in the driver's header, and confirm the program is sending the command the driver expects.

### Useful `kdump` Filters

`kdump` takes several flags that filter the output:

- `-t <mask>`: filter by type, same mask as `ktrace -t`.
- `-E`: show elapsed time since the previous event.
- `-R`: show relative timestamps instead of absolute.
- `-l`: show the PID and process name on every line.
- `-d`: hex-dump I/O buffers (on by default for `GIO` lines).
- `-H`: more readable hex dump.

A common refinement:

```sh
kdump -f mytrace.out -E -t cnri
```

Elapsed time, syscall entry/return plus namei plus I/O.

Piping to `grep` is also useful:

```sh
kdump -f mytrace.out | grep -E 'myfirst|ioctl|open|read|write|close' | less
```

This shows only lines relevant to the driver.

### Finding Driver-Related Syscalls

A user program talks to a driver through four main syscalls: `open`, `ioctl`, `read`, `write`, and `close`. A driver author tracing a user program watches for these:

- **`open("/dev/myfirst0", ...)`**: the program opens the device. The return value is a file descriptor the subsequent syscalls use.
- **`ioctl(fd, cmd, arg)`**: the program issues a device-specific command. The `cmd` encodes the operation; `arg` is usually a pointer to a structure.
- **`read(fd, buf, size)`**: the program reads `size` bytes into `buf`.
- **`write(fd, buf, size)`**: the program writes `size` bytes from `buf`.
- **`close(fd)`**: the program closes the descriptor.

A trace that shows these in order is the reader's view of the user side of the driver's interface. A bug where the program is passing the wrong size to `read` shows up immediately as `read(fd, buf, 0)` (zero size) in the trace. A bug where the program is forgetting to call `close` shows up as the absence of a `close` call before exit. A bug where the program is using an undefined `ioctl` command shows up as an unusual hex value in the `cmd` argument.

### Comparing `ktrace` and DTrace

The two tools overlap in some ways and complement each other in others. A rough guide:

Use `ktrace` when:

- You want to see what a user program is doing, with the program's perspective.
- You want a record you can save and review later.
- You want minimal setup (no kernel rebuild, no script writing).
- You want to catch the arguments to syscalls, including pointer-destination strings and I/O buffers.

Use DTrace when:

- You want to see what is happening inside the kernel.
- You want to measure latency, distribution, or contention.
- You want to aggregate data across many events.
- You need to trace without modifying the program or running it under a wrapper.

For most driver work, the reader uses both. `ktrace` answers "what is the program asking for?". DTrace answers "what is the driver doing about it?". The combination is the full picture.

### A Note on `KTR` vs. `ktrace`

The names confuse new driver authors. `ktrace` (lowercase) is the user-space tracing tool described above. `KTR` (uppercase) is the in-kernel event tracing framework, enabled with `options KTR`. They are different.

`KTR` is a high-performance circular buffer for kernel events, intended for kernel developers debugging the kernel itself. Driver authors can add `CTR` (category-0, category-1, etc.) macros to their code and observe them through `ddb`'s `show ktr` command. In practice, `KTR` is used by a few core kernel subsystems (scheduler, locking, some device subsystems) and rarely by individual drivers. Chapter 23 does not pursue `KTR` further; the reader who is curious can read `/usr/src/sys/sys/ktr.h` for the macro definitions and `/usr/src/sys/conf/NOTES` for the configuration options.

### Exercise: Trace a Simple User Program Accessing Your Driver

A hands-on companion for Section 6.

1. Write a small user program `myftest.c` that opens `/dev/myfirst0`, issues one ioctl, reads 4 KB, writes 4 KB, and closes. The simplest version:

   ```c
   #include <sys/ioctl.h>
   #include <fcntl.h>
   #include <unistd.h>
   #include <stdio.h>

   int
   main(void)
   {
       int fd, error;
       char buf[4096];

       fd = open("/dev/myfirst0", O_RDWR);
       if (fd < 0) { perror("open"); return 1; }

       error = ioctl(fd, 0x20006601 /* placeholder */, NULL);
       printf("ioctl: %d\n", error);

       error = read(fd, buf, sizeof(buf));
       printf("read: %d\n", error);

       error = write(fd, buf, sizeof(buf));
       printf("write: %d\n", error);

       close(fd);
       return 0;
   }
   ```

   Compile: `cc -o myftest myftest.c`.

2. Trace the program:

   ```sh
   sudo ktrace -di -f mytrace.out ./myftest
   ```

3. Read the trace:

   ```sh
   kdump -f mytrace.out | less
   ```

4. Find the lines relating to `/dev/myfirst0`:

   ```sh
   kdump -f mytrace.out | grep -E 'myfirst0|CALL|RET|NAMI' | less
   ```

5. Confirm that each syscall is there, with the expected arguments. For the ioctl, decode the command number by hand: `0x20006601` means direction IOC_OUT (high byte 0x20), size 0 (next 14 bits), type 'f' (0x66), command 01. A driver that defined `MYFIRST_IOC_RESET = _IO('f', 1)` would see exactly this command.

6. (Optional) Run the program under DTrace in a second terminal, counting the driver functions:

   ```sh
   sudo dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }' &
   ./myftest
   ```

   Compare the DTrace count to the ktrace sequence. The reader should see that each user syscall triggered specific driver functions: `open` → `myfirst_open`, `ioctl` → `myfirst_ioctl`, `read` → `myfirst_read`, and so on.

The exercise grounds the tools in a specific, concrete trace. Every driver author has this pattern in their toolkit for the rest of their career: user-side trace with `ktrace`, kernel-side trace with DTrace, correlated by time and PID.

### Wrapping Up Section 6

Section 6 introduced `ktrace` and `kdump`, the user-side tracing pair that complements DTrace. `ktrace` records the syscalls a process makes; `kdump` translates the record into text; the combination shows the driver's interface from the user program's perspective. For a driver author, this view is essential for debugging bugs that originate on the user side of the driver boundary.

With logging, log analysis, debug kernels, DTrace, and `ktrace` in hand, the reader has the five tools that handle most driver-debugging questions. Section 7 turns to the classes of bugs these tools most often find, with the characteristic symptom of each and the tool that best diagnoses it.

## Section 7: Diagnosing Common Driver Bugs

The previous six sections built a toolbox: logging, log inspection, debug kernels, DTrace, and `ktrace`. Section 7 uses that toolbox on the bugs readers will actually meet. Every experienced FreeBSD driver author has hit each of these at least once. The purpose of this section is to describe the symptoms, explain the underlying cause, and match each bug class to the tool that diagnoses it most efficiently.

None of the bugs below are exotic. They arise from ordinary coding mistakes, incomplete understanding of the kernel environment, or assumptions that hold on one platform but not another. The reader who learns to recognise the symptoms will save hours of frustration, because the first step toward a fix is always correct classification.

### 7.1 Memory Leaks: the Quiet Growth

A memory leak in a kernel module is harder to detect than one in a user program. There is no `valgrind` equivalent that runs transparently, and the kernel does not exit when the module unloads, so a leak continues to consume memory even after the driver is gone. For long-running systems, a small leak is fatal: the pool it allocates from grows indefinitely, other subsystems start failing, and eventually the machine runs out of kernel memory and panics.

The characteristic symptom is simple: a `malloc` pool that grows but never shrinks. The tool that exposes the symptom is `vmstat -m`:

```sh
vmstat -m | head -1
vmstat -m | grep -E 'Type|myfirst'
```

The output looks like:

```text
         Type InUse MemUse Requests  Size(s)
      myfirst    12     3K       48  256
```

The three columns that matter are `InUse`, `MemUse`, and `Requests`. `InUse` is the number of allocations from this pool that are currently outstanding. `MemUse` is the total memory those allocations occupy, in kilobytes. `Requests` is the total number of allocations made since the pool was created, including those that were later freed.

A healthy pool has `InUse` that stays roughly constant under a steady workload. A leaking pool has `InUse` that grows without bound.

To confirm a leak, run the workload that exercises the driver, then take two snapshots of `vmstat -m` several minutes apart:

```sh
vmstat -m | grep myfirst
# run the workload for 5 minutes
vmstat -m | grep myfirst
```

If the first shows `InUse=12` and the second shows `InUse=4800`, and the workload was not expected to add four thousand allocations, the driver is leaking.

The second step is to identify the offending path. Every `malloc(9)` call pairs with an expected `free(9)`. The leak is in whichever path allocates without freeing. Search the driver for `malloc(..., M_MYFIRST, ...)` and for `free(..., M_MYFIRST)`, and verify that every allocation has a matching free on every exit path.

The most common leak patterns in driver code are:

1. **Error-path leaks.** An allocation succeeds, a later step fails, and the error return skips the `free`. The fix is a single cleanup path with `goto fail;` labels, of the kind explored in Chapter 18.

2. **Conditional leaks.** Memory is freed only when a flag is set, and sometimes the flag is not set. The fix is to free unconditionally or to track ownership more carefully.

3. **Forgotten context-destructor.** An object is allocated per open file descriptor and stored on `si_drv1` or similar, but the `d_close` handler does not free it. The fix is to treat `d_close` (or the `cdev`-destroy callback) as the symmetric counterpart to `d_open`.

4. **Timer or task-queue leak.** A task is scheduled, its cleanup routine allocates memory for deferred processing, but the task is cancelled before it runs and the allocated buffer is never freed.

Once the path is identified, add the missing `free`, reload the module, rerun the workload, and confirm with `vmstat -m` that `InUse` now stabilises. A small bit of added logging is often useful: a `device_printf` in the allocation path and another in the free path quickly exposes the ratio of allocations to frees in `dmesg`. For a driver using a `DPRINTF` macro, a dedicated `MYF_DBG_MEM` class makes memory tracing optional and configurable at runtime, which is exactly what Section 8 will implement for the `myfirst` driver.

DTrace can also help here, especially in kernels with `KDTRACE_HOOKS`. A simple one-liner counts the driver's `malloc` and `free` calls and shows any imbalance:

```sh
dtrace -n 'fbt::malloc:entry /execname == "kernel"/ { @["malloc"] = count(); }
           fbt::free:entry   /execname == "kernel"/ { @["free"]   = count(); }'
```

A much more driver-specific approach is to add DTrace probes to the driver itself, one at each allocation and one at each free, and let DTrace count the ratio. This is the technique Section 8 demonstrates for the `myfirst` driver.

### 7.2 Race Conditions: the Rare, Destructive Bug

Race conditions are the hardest driver bugs to reproduce and the easiest to miss. A race happens when two threads access the same state without correct synchronisation, and the resulting behaviour depends on the relative timing of the two threads. Under light load the race may never fire; under heavy load it fires hundreds of times per second.

The symptoms of race conditions vary widely:
- sporadic panics at unpredictable places
- data corruption (values that should never exist)
- occasional lock-related assertion failures such as "mutex myfirst_lock not owned"
- impossible-looking stacks where two threads appear to be inside the same mutex-protected region

No single symptom proves a race, but any pattern of bugs that appears only at high load should raise suspicion.

The single most effective FreeBSD tool for finding races is `WITNESS`, as introduced in Section 4. A kernel built with `options WITNESS` examines every lock operation and panics on any violation of the declared lock order, on any attempt to acquire a spin lock while holding a sleep lock, on any call to a sleeping function while holding a spin lock, and on many related mistakes.

When `WITNESS` panics or prints a violation, it produces a stack trace showing:
- which lock was being acquired
- which locks were already held
- the declared order that the acquisition would violate
- both stacks (the current lock and the conflicting one)

The fix is usually one of:

1. **Reorder the acquisitions.** If code takes lock A then lock B while another path takes B then A, one of them must change.
2. **Add a missing lock.** If state is accessed without any mutex, add the appropriate `MTX_DEF` mutex around the access.
3. **Remove a redundant lock.** Sometimes two locks protect overlapping state, and one can be eliminated.
4. **Switch lock types.** Spin locks and sleep locks have different rules. A region that needs to sleep must use a sleep lock. A region called from interrupt context must often use a spin lock.

A driver that hits no `WITNESS` violations is not free of races, because `WITNESS` catches only lock-ordering issues. Races over state that is accessed without any lock at all require a different approach: careful reading of the code, DTrace probes at the suspect points to confirm timing, and occasionally `mtx_assert(&sc->sc_mtx, MA_OWNED)` assertions sprinkled through the critical sections. `INVARIANTS` enables these assertions, which is one more reason to run a debug kernel during development.

Between `WITNESS`, `INVARIANTS`, `mtx_assert`, and DTrace counters on the paths where contention is suspected, most races can be narrowed down within an hour or two. Races that survive this arsenal are rare and almost always involve lock-free data structures, atomic operations, or memory-ordering assumptions that require careful review with a senior engineer.

### 7.3 bus_space and bus_dma Misuse

A new driver author often runs into bus-access bugs that share a distinct family of symptoms:

- reads return 0xFF, 0xFFFFFFFF, or otherwise suspicious constants
- writes appear to succeed but the device does not react
- the machine works for seconds or minutes and then hangs or panics
- behaviour is correct on one architecture (amd64) and wrong on another (arm64, riscv64)

Each of these points to a `bus_space` or `bus_dma` misuse.

The first mistake is skipping the handle. `bus_space_read_4()` takes a `bus_space_tag_t` and a `bus_space_handle_t`, obtained from `rman_get_bustag()` and `rman_get_bushandle()` on the resource allocated during attach. Using the raw physical address of the register, or a pointer obtained by a direct cast of the resource, bypasses the platform's bus abstraction. On amd64 the program may appear to work (the kernel has the MMIO region mapped in a way that tolerates this), but on arm64 the register access fails.

The fix is mechanical: always go through `bus_space_read_N()` / `bus_space_write_N()` or the newer `bus_read_N()` / `bus_write_N()` resource-based helpers, never dereference raw pointers for device memory.

The second mistake is incorrect size. `bus_space_read_4` reads a 32-bit value. If the register is 16-bit and the code reads 4 bytes, it reads into an adjacent register as well, and the adjacent register's value now appears at bits 16 through 31 of the returned value. Worse, some hardware does not tolerate a mismatched size and responds with an error. The fix is to use the correct `read_1`, `read_2`, `read_4`, or `read_8` variant for each register's width, as documented in the device's datasheet.

The third mistake is incorrect offset. The `bus_space` handle refers to the base of the mapped region; the offset is added to compute the register address. A typo in the offset reads a different register. For example, reading offset `0x18` instead of `0x10` yields an unexpected value, and the driver's subsequent logic is based on a false reading. The fix is to define every offset as a named constant in a header file, and refer to the constant rather than the number: `#define MYFIRST_REG_STATUS 0x10`, `#define MYFIRST_REG_CONFIG 0x14`, and so on.

For DMA, the most common misuse is freeing or reusing a buffer while the device is still reading from or writing to it. The characteristic symptom is intermittent data corruption, sometimes only under high load. The cause is missing `bus_dmamap_sync` calls, a freed `bus_dmamem` buffer while the descriptor is still enqueued, or an incorrect direction in `bus_dmamap_sync`.

The diagnostic approach is to log every DMA map, sync, and unmap operation, then cross-check the log against the descriptor ring's state. A DTrace one-liner on the driver's DMA paths is usually enough to spot the mistake:

```sh
dtrace -n 'fbt::myfirst_dma_sync:entry { printf("%s dir=%d", probefunc, arg1); }'
```

For a full treatment of DMA rules and pitfalls, Chapter 21 is the reference. The debug chapter's role is to help the reader recognise the symptoms and narrow the cause.

### 7.4 Use-After-Detach

A subtler class of bug arises when the driver's detach path returns, the driver's softc is freed, but some other part of the kernel still holds a reference to the freed memory. Examples include:

- an interrupt arriving after `bus_release_resource` but before the handler is torn down
- a callout that fires after detach, dereferencing a freed softc
- a DTrace probe in the driver that fires from a task still running at unload time
- a character-device node held open by a user process, receiving I/O while the driver is unloading

The symptoms are nearly always fatal: page faults in driver code reached after detach, kernel panics with corrupted stacks, or spurious data in the kernel message buffer just before the panic.

The fix has several components, each of which the detach path must implement in a specific order:

1. First, prevent new entries: set a flag in the softc (`sc->sc_detaching = 1`), or take a write lock that all entry points check, so new calls see the driver as going away.

2. Wait for in-progress callers to finish. `bus_teardown_intr` drains the interrupt handler. `callout_drain` waits for a pending callout to complete. `taskqueue_drain` drains any deferred tasks. `destroy_dev_sched_cb` waits for open file descriptors to close.

3. Only after every external caller is drained, release the resources the driver allocated in attach: free memory, release IRQs, release memory resources, destroy mutexes.

The principle is simple: detach is the mirror image of attach, and every resource allocated in attach must be released in detach in the reverse order. Violations of this principle produce use-after-detach bugs.

The debug tool of choice is the debug kernel itself. A `DEBUG_MEMGUARD`-enabled kernel can be configured to poison freed memory, so an access to a freed softc produces an immediate page fault with a clear stack, instead of a subtle corruption that may take hours to surface.

### 7.5 Interrupt Handler Mistakes

Driver authors new to the kernel sometimes treat interrupt handlers as ordinary functions and make common but harmful mistakes:

- calling a sleeping function (`malloc(..., M_WAITOK, ...)`, `tsleep`, `mtx_lock` of a sleep lock, `copyin`, `uiomove`) from a filter-level interrupt handler
- attempting to hold a sleep lock for an extended period
- reading or writing a large data block inside the interrupt handler instead of a `taskqueue`
- not ack'ing the interrupt, so the handler fires forever in a loop

Each of these has a distinct symptom. Sleeping in interrupt context produces a `WITNESS` violation or, in release kernels, a silent corruption of the scheduler state. Holding a sleep lock too long causes other threads to spin or sleep, and latency balloons. Doing heavy work in the handler blocks subsequent interrupts and degrades the whole system's responsiveness. Not ack'ing the interrupt pins a CPU at 100% on interrupt handling forever.

The cure is always the same: make the interrupt handler short and atomic. It should:
1. Read the status register to determine the cause.
2. Acknowledge the interrupt by writing to the status register.
3. Hand off any substantial work to a `taskqueue` or `ithread`, as explored in Chapter 19.
4. Return.

Complex processing, memory allocation, and long-running operations all belong in the deferred path, not the handler itself.

DTrace is particularly useful for diagnosing interrupt-handler performance. The `intr` provider, or an `fbt` probe on the handler entry, can measure how long each invocation takes:

```sh
dtrace -n 'fbt::myfirst_intr:entry { self->t = timestamp; }
           fbt::myfirst_intr:return /self->t/ {
               @ = quantize(timestamp - self->t); self->t = 0; }'
```

A healthy handler returns in a few microseconds. If the quantization shows invocations in the hundreds of microseconds or milliseconds, the handler is doing too much work and should be refactored.

### 7.6 Lifecycle Sequencing Errors

The driver's lifecycle moves through probe, attach, open, close, detach, and unload. Each method has rules about what it may do and what must have happened before. Breaking the rules produces characteristic bugs:

- calling `bus_alloc_resource_any` during probe (it must happen in attach) yields partial allocations and confusion in the probe-ordering logic
- doing substantial work in probe slows boot significantly, because the same probe runs for every candidate device
- creating the `cdev` before the hardware is initialised lets user processes open the device and receive I/O on uninitialised state
- destroying the `cdev` in detach while other threads still hold it open corrupts the devfs state
- doing significant work in module unload instead of detach leaves resources allocated per attach unreleased

The cure is to keep each lifecycle method focused on its job:

- **probe** only identifies the device and returns `BUS_PROBE_DEFAULT` or an error; no allocations, no registration
- **attach** allocates resources, initialises the softc, sets up interrupts, creates the `cdev`, registers the device
- **detach** reverses attach exactly, in reverse order
- **open / close** manage per-file state without touching device-wide resources
- **unload** performs only module-level cleanup; per-device cleanup belongs in detach

When a driver follows this structure, lifecycle bugs are rare. When it deviates, they are common and painful.

### 7.7 Copy-To/From-User Mistakes

The `copyin(9)`, `copyout(9)`, and related `fueword` / `suword` functions transfer data across the user/kernel boundary. They are protected by the virtual memory system: if the user-space address is invalid, the copy returns `EFAULT` instead of panicking. However, this protection only applies if the copy is done through these functions. A driver that dereferences a user-space pointer directly will panic the moment the user pointer is bad, which in real-world deployments is most of the time.

The symptom is a panic with a user-space address in the faulting instruction, stack pointing at the driver's read/write/ioctl path.

The fix is mandatory: every time user data crosses the boundary, use `copyin`/`copyout` or a `uiomove` through a `struct uio`. Never cast a user pointer to a kernel pointer and dereference it. This rule is absolute.

For ioctl specifically, the `d_ioctl` handler receives a kernel pointer, because the kernel has already copied the fixed-size argument from user space. But if the ioctl's argument is a structure that contains a pointer to a larger user buffer, that embedded pointer is still user-space, and `copyin` is required to safely access the buffer it points to.

### 7.8 Module-Level Bugs

Some bugs affect the whole module rather than a single device:

- module fails to load: the loader prints an error in `dmesg`; read it and fix the symptom (missing dependency, symbol resolution failure, version mismatch)
- module fails to unload: usually means a device is still attached. Detach all devices (`devctl detach myfirst0`) before `kldunload`
- module unloads but leaves garbage: the unload path (`module_t` event handler, or `DRIVER_MODULE_ORDERED` unload) did not reverse module load. Fix by reading the unload path and making it symmetric to load.

The debug tools are simple: `dmesg | tail` shows the load/unload messages, `kldstat -v` shows the module state, `vmstat -m` shows whether the module's malloc pool was drained on unload.

### 7.9 A Debug Checklist

Here is a compact checklist that ties tools to symptoms. Readers can keep this at hand during driver development.

| Symptom                                | First tool                  | Second tool          |
|----------------------------------------|-----------------------------|----------------------|
| Bad or missing `dmesg` line            | `dmesg`, `/var/log/messages` | add `device_printf` |
| `InUse` grows in `vmstat -m`           | `vmstat -m`                 | DTrace malloc count  |
| Panic on lock order                    | `WITNESS` trace              | `ddb> show locks`   |
| Sporadic corruption under load         | `WITNESS`, `INVARIANTS`      | DTrace timing        |
| Register reads return 0xFFFFFFFF        | review `bus_space` handle   | datasheet, offset    |
| Machine hangs on device use            | DTrace `fbt::myfirst_*`     | `ddb> bt`           |
| Panic after detach                     | `DEBUG_MEMGUARD`             | audit detach order  |
| Slow system under driver load          | DTrace `intr` provider      | shorten handler      |
| ioctl with wrong command               | `ktrace` / `kdump`           | decode command      |
| Panic with user-space faulting address | review `copyin`/`copyout`   | audit ioctl pointer |
| Module will not unload                  | `kldstat -v`                | `devctl detach`     |

The table is not exhaustive, but it covers the vast majority of real driver bugs. With it and the tools from Sections 1 through 6, the reader has a structured approach to most field diagnostics.

### Wrapping Up Section 7

Section 7 turned the toolbox of logging, log analysis, debug kernels, DTrace, and `ktrace` on the bugs the reader is most likely to hit: memory leaks, race conditions, bus-access errors, use-after-detach, interrupt mistakes, lifecycle errors, user/kernel boundary bugs, and module-level issues. For each class, the section described the symptom, the underlying cause, and the tool that diagnoses it most efficiently. The checklist at the end makes the mapping explicit, and future sessions at the keyboard should let the reader reach for the right tool on the first try rather than the third.

The remaining gap is the driver itself. So far in this chapter, `myfirst` has been used mostly as the target of external tools. Section 8 closes that gap by refactoring the driver to expose trace points and a debug-verbosity knob of its own, so that instrumentation becomes an organic part of the code rather than an afterthought. The version bumps to `1.6-debug` and the driver acquires `myfirst_debug.h` and the structure that will stay with it through the remainder of the book.

## Section 8: Refactoring and Versioning With Trace Points

The previous seven sections of this chapter were about learning to use tools that already exist in FreeBSD: `printf`, `dmesg`, debug kernels, DTrace, `ktrace`, and the discipline of reading their output. Section 8 turns inward. The `myfirst` driver that arrived from Chapter 22 as version `1.5-power` has no real debug infrastructure of its own. Every logging statement is unconditional. There is no knob the operator can turn, no sysctl to request more verbose output when a problem arises, and no static trace points to hook with DTrace. Section 8 fixes that gap.

The work in this section is small in lines of code but large in impact. By the end, the driver will have:

1. A `myfirst_debug.h` header that defines a `DPRINTF` macro, verbosity bits, and SDT trace points.
2. A sysctl tree (`dev.myfirst.0.debug`) that sets the verbosity at runtime.
3. An entry-exit-error trace pattern applied consistently across the driver.
4. Three SDT provider probes that expose the open, close, and I/O events.
5. A `DEBUG.md` document in the example tree describing how to configure and read the new output.
6. A version bump to `1.6-debug` recorded in `myfirst_version` and `MODULE_VERSION`.

The resulting driver will be the platform for the remaining chapters of the book. Every new subsystem added in Parts 5, 6, and 7 will hook into this framework, so that the trace infrastructure grows with the driver rather than being bolted on at the end.

### 8.1 Why a Debug Header

Larger FreeBSD drivers have a dedicated header file for debug infrastructure. The pattern is visible in `/usr/src/sys/dev/ath/if_ath_debug.h`, `/usr/src/sys/dev/bwn/if_bwn_debug.h`, `/usr/src/sys/dev/iwn/if_iwn_debug.h`, and many others. Each of these headers:

1. defines a `DPRINTF` macro that checks a verbosity bitmask in the softc
2. declares a set of verbosity classes as `#define MYF_DBG_INIT 0x0001`, `MYF_DBG_OPEN 0x0002`, and so on
3. optionally declares SDT probes that map to the driver's functional boundaries

Putting this in a header has two benefits. First, the debug infrastructure is a separate concern, factored cleanly away from the functional code. Second, when a reader wants to understand the driver's tracing story, the header is the one file to read. The pattern is mature, widely used, and is what `myfirst` will adopt in this section.

The header will be kept under `examples/part-05/ch23-debug/stage3-refactor/myfirst_debug.h`, and the driver source will include it at the top:

```c
#include "myfirst_debug.h"
```

The companion changes in the driver source are additive: existing `device_printf` calls can remain, and new calls are added through the `DPRINTF` macro.

### 8.2 Declaring Debug Classes

The first step is to define the verbosity classes. A class is a single bit in a 32-bit mask. The driver reserves one bit per functional area. For `myfirst`, eight classes are more than enough at this stage:

```c
/* myfirst_debug.h */
#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/sdt.h>

#define MYF_DBG_INIT    0x00000001  /* probe/attach/detach */
#define MYF_DBG_OPEN    0x00000002  /* open/close lifecycle */
#define MYF_DBG_IO      0x00000004  /* read/write paths */
#define MYF_DBG_IOCTL   0x00000008  /* ioctl handling */
#define MYF_DBG_INTR    0x00000010  /* interrupt handler */
#define MYF_DBG_DMA     0x00000020  /* DMA mapping/sync */
#define MYF_DBG_PWR     0x00000040  /* power-management events */
#define MYF_DBG_MEM     0x00000080  /* alloc/free trace */

#define MYF_DBG_ANY     0xFFFFFFFF
#define MYF_DBG_NONE    0x00000000

#endif /* _MYFIRST_DEBUG_H_ */
```

The value `MYF_DBG_NONE` is the default: no debug output at all. `MYF_DBG_ANY` enables every class, useful during development. A typical operator configuration might enable only `MYF_DBG_INIT | MYF_DBG_OPEN` to get lifecycle events without the per-I/O chatter.

Each bit is declared as a single hex digit or pair, so the operator can set the mask with a simple value: `sysctl dev.myfirst.0.debug=0x3` enables initialisation and open tracing. The commented names make the purpose of each bit clear.

### 8.3 The DPRINTF Macro

Next, the macro that gates logging on the mask:

```c
#ifdef _KERNEL
#define DPRINTF(sc, m, ...) do {                                        \
        if ((sc)->sc_debug & (m))                                        \
                device_printf((sc)->sc_dev, __VA_ARGS__);                \
} while (0)
#endif
```

The macro takes three arguments: the softc pointer, the bitmask, and the format string plus variadic arguments. It expands to a test of `sc->sc_debug & m`, followed by a call to `device_printf` if the test succeeds. The `do { ... } while (0)` pattern is the standard idiom for multi-statement macros that must behave like a single statement in `if`/`else` contexts.

The cost of the `DPRINTF` call when the bit is clear is a single load from `sc->sc_debug`, a bitwise AND, and a branch. The branch is almost always predicted as "not taken", so the cost in practice is negligible. The driver can sprinkle `DPRINTF` calls freely throughout the code, and the user pays nothing when debug is disabled.

When the bit is set, the call becomes a normal `device_printf`, appearing in `dmesg` like any other kernel log. Nothing in the behaviour differs from the usual logging path, except that the logging is now conditional.

### 8.4 Adding `sc_debug` to the softc

The softc gains one new field, a `uint32_t sc_debug`. Its value is manipulated by a sysctl, which the attach function registers. The relevant excerpt from `myfirst_debug.c`:

```c
struct myfirst_softc {
        device_t        sc_dev;
        struct mtx      sc_mtx;
        struct cdev    *sc_cdev;
        uint32_t        sc_debug;       /* debug verbosity mask */
        /* other fields as in 1.5-power */
};
```

The attach function initialises the field to zero and registers the sysctl:

```c
sc->sc_debug = 0;
sysctl_ctx_init(&sc->sc_sysctl_ctx);
sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
    OID_AUTO, "debug",
    CTLFLAG_RW, 0, "debug verbosity tree");

SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0, "debug class bitmask");
```

After attach, the sysctl appears as:

```sh
sysctl dev.myfirst.0.debug.mask
```

and the operator can change it at will:

```sh
sysctl dev.myfirst.0.debug.mask=0x3     # enable INIT + OPEN
sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF   # enable all classes
sysctl dev.myfirst.0.debug.mask=0        # disable
```

The detach routine destroys the sysctl context the same way it does today:

```c
sysctl_ctx_free(&sc->sc_sysctl_ctx);
```

One detail matters: placing the sysctl field near the top of the softc puts it in the first cache line, where its access cost during DPRINTF is smallest. This is a small performance point, but the book's emphasis on principled coding should note it.

### 8.5 The Entry / Exit / Error Pattern

With `DPRINTF` in place, the driver's functions can use a consistent pattern for tracing. Each substantial function logs entry, exit (or error), and any interesting intermediate state. For a small function like `myfirst_open`, this looks like:

```c
static int
myfirst_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d uid=%d flags=0x%x\n",
            td->td_proc->p_pid, td->td_ucred->cr_uid, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count >= MYFIRST_MAX_OPENS) {
                error = EBUSY;
                goto out;
        }
        sc->sc_open_count++;
out:
        mtx_unlock(&sc->sc_mtx);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_OPEN, "open failed: error=%d\n", error);
        else
                DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n",
                    sc->sc_open_count);

        return (error);
}
```

Three principles guide the pattern:

1. The entry log shows who called and with what arguments. For open, this is `pid`, `uid`, and flags.
2. The exit log shows the outcome: either "failed: error=N" or "ok: ..." with any relevant state.
3. Intermediate state is logged when it matters. For open, the new open count is useful.

The pattern is the same for close, read, write, and ioctl. Each function has two or three `DPRINTF` calls bracketing the work.

This discipline pays off the first time a bug appears. Without the pattern, the developer must add logging retroactively, guessing where the problem is. With the pattern, turning on a single bit (`sysctl dev.myfirst.0.debug.mask=0x2` for `MYF_DBG_OPEN`) produces a complete trace of every open and close, with arguments and outcomes. The diagnostic time drops from hours to minutes.

### 8.6 Adding SDT Probes

The static `DPRINTF` macro generates human-readable messages in `dmesg`. SDT probes produce machine-readable events that DTrace can aggregate, filter, and time. Both have their place. The driver declares SDT probes for the three most interesting events, and a thoughtful user can attach custom scripts at will.

In `myfirst_debug.h`, the probe declarations look like:

```c
#include <sys/sdt.h>

SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);
```

The matching definitions live in the driver source (`myfirst_debug.c`):

```c
#include <sys/sdt.h>

SDT_PROVIDER_DEFINE(myfirst);
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
```

The `DEFINE` macros register the probes with the kernel's SDT infrastructure. `DEFINEn` takes `n` arguments, each with a C-style type string that describes what the DTrace script will receive as `arg0`, `arg1`, etc.

The driver then fires the probes at the appropriate locations:

```c
/* in myfirst_open */
SDT_PROBE2(myfirst, , , open, sc, flags);

/* in myfirst_close */
SDT_PROBE2(myfirst, , , close, sc, flags);

/* in myfirst_read or myfirst_write */
SDT_PROBE4(myfirst, , , io, sc, is_write, (size_t)uio->uio_resid, uio->uio_offset);
```

The cost of a probe that no script has attached to is a single branch around a no-op, the same negligible cost as `DPRINTF` with a clear bit. When a DTrace script does attach, the branch is taken, the arguments are recorded, and the script sees them as `args[0]`, `args[1]`, and so on.

With these probes, a reader can now run scripts like:

```sh
dtrace -n 'myfirst::: { @[probename] = count(); }'
```

This counts every `myfirst` probe event. To see the bytes transferred per second:

```sh
dtrace -n 'myfirst:::io { @["bytes"] = sum(arg2); }'
```

Or to trace every open with PID and flags:

```sh
dtrace -n 'myfirst:::open { printf("open pid=%d flags=0x%x", pid, arg1); }'
```

The driver now exposes its behaviour in two forms: human-readable via `DPRINTF`, and machine-parseable via SDT. An operator can pick the right form for the job.

### 8.7 Writing DEBUG.md

The example tree gains a short document (`examples/part-05/ch23-debug/stage3-refactor/DEBUG.md`) that explains the debug and trace infrastructure to a reader who downloads the files. The document is short but covers:

1. What `DPRINTF` is and how to enable it.
2. The table of class bits.
3. Example `sysctl` commands to enable each class.
4. The list of SDT probes and the argument order.
5. Three example DTrace one-liners.
6. How to combine `DPRINTF` and SDT for end-to-end debugging.

The document is not long: about thirty lines. Its purpose is to make the tooling self-documenting, so a reader who picks up the example in a year still knows how to use it.

### 8.8 Bumping the Version

Every chapter in the book that changes the driver's behaviour bumps its version. The rule is: the version string in the driver matches the version shown in the example-tree README and in the chapter's text.

In Chapter 22, the driver reached `1.5-power`. Section 8 of Chapter 23 takes it to `1.6-debug`. The changes in `myfirst_debug.c` are:

```c
static const char myfirst_version[] = "myfirst 1.6-debug";

MODULE_VERSION(myfirst, 16);
```

The text above the version constant says explicitly what changed:

```c
/*
 * myfirst driver - version 1.6-debug
 *
 * Added in this revision:
 *   - DPRINTF macro with 8 verbosity classes
 *   - sysctl dev.myfirst.0.debug.mask for runtime control
 *   - SDT probes for open, close, io
 *   - Entry/exit/error pattern across all methods
 */
```

The `attach` function logs the version at the `MYF_DBG_INIT` level, so the operator can confirm the loaded driver:

```c
DPRINTF(sc, MYF_DBG_INIT, "attach: %s loaded\n", myfirst_version);
```

With the verbosity enabled, a reader who loads the driver sees:

```text
myfirst0: attach: myfirst 1.6-debug loaded
```

which confirms both the module identity and the debug infrastructure.

### 8.9 Wrapping Section 8

Section 8 refactored the `myfirst` driver to carry its own debug infrastructure. The driver now has a verbosity mask, a sysctl to set it at runtime, a consistent entry-exit-error pattern in each function, and three SDT probes at the functional boundaries. The debug header can be included in future chapters, so every subsystem added later in the book can hook into the same framework.

The driver's version advances from `1.5-power` to `1.6-debug`. The example tree gets a `stage3-refactor` directory with the final source, a `DEBUG.md` document, and a `README.md` that guides the reader through building, loading, and exercising the new infrastructure.

With Section 8 complete, the chapter has covered the full arc: understand why kernel debugging is hard, use every tool FreeBSD provides, recognise the common bugs, and finally build the driver so that it supports its own debugging. The remaining material in the chapter is the lab sequence, the challenges, and the closing material that ties this chapter to Chapter 24.

## Hands-On Labs

The labs in this chapter form a progression. Each one reinforces a specific tool from Sections 1 through 6, and the final lab applies the refactor from Section 8 so the reader walks away with a driver that is instrumented and ready for the rest of the book.

None of these labs is long. All five can be completed in a single evening, though spreading them across two or three sessions gives the reader time to absorb each tool.

### Lab 23.1: A First DDB Session

**Goal:** Enter the kernel debugger deliberately, inspect the driver's state, and exit safely.

**Prerequisites:** A debug kernel built as described in Section 4, with `options KDB` and `options DDB`. The `myfirst` driver from Chapter 22 (version `1.5-power`) loaded.

**Steps:**

1. Confirm the debug kernel is running:

   ```sh
   sysctl kern.version
   sysctl debug.kdb
   ```

   The second command should show DDB among the available backends.

2. Load the driver and confirm the device node:

   ```sh
   sudo kldload myfirst
   ls /dev/myfirst0
   ```

3. Enter DDB with a keyboard break. On the system console, press `Ctrl-Alt-Esc` on a VGA console, or send the BREAK character on a serial console. The prompt appears:

   ```
   KDB: enter: manual entry
   [thread pid 42 tid 100024 ]
   Stopped at      kdb_enter+0x37: movq    $0,0x158e4fa(%rip)
   db>
   ```

4. Show the device tree:

   ```
   db> show devmap
   ```

   Locate `myfirst0` in the output.

5. Print the backtrace of the current thread:

   ```
   db> bt
   ```

6. Print all processes:

   ```
   db> ps
   ```

   Confirm the reader's own shell is visible in the list.

7. Continue the kernel:

   ```
   db> continue
   ```

   The system returns to normal operation.

**What to observe:**

- The kernel halted cleanly on the break.
- All devices, processes, and kernel threads were inspectable.
- The `continue` command returned the system to normal without any side effect.

Record the time spent and any notes in the lab logbook under "Chapter 23, Lab 1". The reader should come away understanding that DDB is a safe tool when used deliberately.

### Lab 23.2: Measuring the Driver with DTrace

**Goal:** Use DTrace to measure the `myfirst` driver's open, close, and I/O rates under a simple workload.

**Prerequisites:** A kernel with `options KDTRACE_HOOKS` and `makeoptions WITH_CTF=1` loaded. The `myfirst` driver loaded at version `1.5-power` (SDT probes are not yet present at this stage; the lab uses `fbt` only).

**Steps:**

1. Confirm DTrace works:

   ```sh
   sudo dtrace -l | head -5
   ```

2. Start a DTrace script that counts entries to each `myfirst` function:

   ```sh
   sudo dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'
   ```

3. In a second terminal, exercise the driver:

   ```sh
   for i in $(seq 1 100); do cat /dev/myfirst0 > /dev/null; done
   ```

4. Stop the DTrace script (Ctrl-C in the first terminal) and read the count:

   ```
     myfirst_open                                             100
     myfirst_close                                            100
     myfirst_read                                             100
   ```

5. Now measure the time spent in `myfirst_read`:

   ```sh
   sudo dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
                   fbt::myfirst_read:return /self->t/ {
                       @ = quantize(timestamp - self->t); self->t = 0; }'
   ```

6. Exercise the driver with 1000 reads, stop DTrace, and read the quantization.

**What to observe:**

- Every `cat` produced one open, one read, and one close, matching the count.
- The time per read is small (microseconds) and concentrated in one bucket of the quantization.
- No modification was made to the driver; the measurement is non-invasive.

### Lab 23.3: User-Side Tracing with ktrace

**Goal:** Trace a user program that exercises the `myfirst` driver and verify the syscall sequence.

**Prerequisites:** The driver loaded. The test program `myftest.c` from Section 6.4 compiled.

**Steps:**

1. Compile the test program (from Section 6.4):

   ```sh
   cc -o myftest myftest.c
   ```

2. Run it under `ktrace`:

   ```sh
   sudo ktrace -di -f mytrace.out ./myftest
   ```

3. Dump the trace:

   ```sh
   kdump -f mytrace.out | less
   ```

4. Locate the syscalls relevant to the driver:

   ```sh
   kdump -f mytrace.out | grep -E 'myfirst0|CALL|RET'
   ```

5. Correlate the trace with the driver's DTrace counts from Lab 23.2. Every user-side syscall should match one or more kernel-side function entries.

**What to observe:**

- The user program's view of the driver is complete in the trace.
- Arguments, return values, and error codes are all visible.
- User-side tracing complements the DTrace kernel-side view.

### Lab 23.4: Finding a Memory Leak with vmstat -m

**Goal:** Introduce a deliberate leak, detect it with `vmstat -m`, and fix it.

**Prerequisites:** The driver source under the reader's control. A compiled, loadable module.

**Steps:**

1. Modify `myfirst_open` to allocate a small buffer and store it in `si_drv2`, without freeing it in `myfirst_close`:

   ```c
   /* in myfirst_open */
   dev->si_drv2 = malloc(128, M_MYFIRST, M_WAITOK | M_ZERO);
   ```

2. Build and load the driver.

3. Run a workload that opens and closes the device many times:

   ```sh
   for i in $(seq 1 1000); do cat /dev/myfirst0 > /dev/null; done
   ```

4. Check the memory pool:

   ```sh
   vmstat -m | grep myfirst
   ```

   The `InUse` column should show 1000 (or more), and `MemUse` should show roughly 128 KB.

5. Fix the leak by adding `free(dev->si_drv2, M_MYFIRST);` to `myfirst_close`.

6. Build and reload the driver. Run the workload again.

7. Check the pool:

   ```sh
   vmstat -m | grep myfirst
   ```

   `InUse` should now stabilise near zero after the workload.

**What to observe:**

- The `vmstat -m` output exposed the leak immediately.
- Without a specialist tool, the leak would have gone unnoticed until the kernel ran out of memory.
- The fix is mechanical and easy once the symptom is identified.

Do not leave the leak-inducing code in the driver after the lab. Revert to the clean version before continuing.

### Lab 23.5: Installing the 1.6-debug Refactor

**Goal:** Apply the Section 8 refactor to the `myfirst` driver and confirm the new infrastructure works.

**Prerequisites:** The driver source from Chapter 22 under the reader's control. A development environment able to build kernel modules.

**Steps:**

1. Create `myfirst_debug.h` exactly as shown in Section 8.2, and the SDT declarations from Section 8.6.

2. Update the softc to add `uint32_t sc_debug;`.

3. Register the sysctl in `myfirst_attach` as shown in Section 8.4.

4. Replace unconditional `device_printf` calls with `DPRINTF(sc, MYF_DBG_<class>, ...)` calls as appropriate.

5. Define the SDT providers and probes in `myfirst_debug.c` (or `myfirst.c`), and fire them in open, close, and read/write.

6. Bump `myfirst_version` to `1.6-debug` and `MODULE_VERSION` to 16.

7. Build:

   ```sh
   cd /path/to/myfirst-source
   make clean && make
   ```

8. Reload:

   ```sh
   sudo kldunload myfirst
   sudo kldload ./myfirst.ko
   ```

9. Confirm the module loaded at the new version:

   ```sh
   kldstat -v | grep myfirst
   ```

10. Confirm the sysctl is present:

    ```sh
    sysctl dev.myfirst.0.debug.mask
    ```

11. Enable full verbosity and open the device:

    ```sh
    sudo sysctl dev.myfirst.0.debug.mask=0xFFFFFFFF
    cat /dev/myfirst0 > /dev/null
    dmesg | tail
    ```

    Expected output includes lines like:

    ```
    myfirst0: open: pid=1234 uid=1001 flags=0x0
    myfirst0: open ok: count=1
    myfirst0: read: size=4096 off=0
    ```

12. Disable verbosity and confirm the messages stop:

    ```sh
    sudo sysctl dev.myfirst.0.debug.mask=0
    cat /dev/myfirst0 > /dev/null
    dmesg | tail
    ```

13. Attach DTrace to the SDT probes:

    ```sh
    sudo dtrace -n 'myfirst::: { @[probename] = count(); }'
    ```

    In a second terminal, exercise the driver:

    ```sh
    for i in $(seq 1 100); do cat /dev/myfirst0 > /dev/null; done
    ```

    Stop DTrace and confirm the probes fired.

**What to observe:**

- Runtime control of verbosity is immediate and responsive.
- The DPRINTF path produces human-readable messages; SDT probes produce machine-parseable events.
- The two forms are complementary and independent.
- Both are turned off by default, so production users pay no runtime cost.

The driver is now at version `1.6-debug`, with infrastructure that will support every later chapter. Record the upgrade in the logbook.

## Challenge Exercises

The exercises below build on the chapter material. They are open-ended enough that each reader will arrive at slightly different answers. Take your time; work incrementally; use the tools from the chapter at each step.

### Challenge 23.1: An SDT Probe in a Real Driver

Pick a driver from `/usr/src/sys/dev/` that already contains SDT probes. Good candidates include `virtqueue.c`, some `ath` files, or `if_re.c`.

For the chosen driver:

1. List its probes with `dtrace -l -P <provider>`.
2. Write a one-liner that counts events grouped by probe name.
3. Write a second one-liner that aggregates by one of the probe arguments (for example, a packet length or a command number).
4. Explain in three sentences what the driver's author gained by adding the probes.

### Challenge 23.2: A Custom DTrace Script

Write a DTrace script that:

1. Attaches to the `myfirst` SDT probes (added in Lab 23.5).
2. Tracks the lifetime of each open file descriptor by pid.
3. Prints a summary when a fd closes, showing: pid, how many reads, how many writes, total bytes, and the elapsed time between open and close.

The script should be at most 50 lines of D code. Test it by running a small user program that opens, reads, writes, and closes `/dev/myfirst0` several times.

### Challenge 23.3: A WITNESS Experiment

Modify the driver to contain a deliberate lock-ordering mistake. For example, add two mutexes `sc_mtx_a` and `sc_mtx_b`, and arrange one code path to take A then B while another path takes B then A.

Build with `options WITNESS`, load, and trigger the mistake. Capture the resulting kernel panic or WITNESS output. Describe in three sentences what WITNESS showed, why it caught the issue, and what the fix would be.

Be sure to revert the mistake after the exercise. Do not leave a broken driver loaded.

### Challenge 23.4: Extended Error Categorisation

Choose any four error paths in the `myfirst` driver (for example, the `EBUSY` path in `myfirst_open`, an `EINVAL` path in `myfirst_ioctl`, and so on). For each:

1. Identify the underlying cause that would produce this error in the real world.
2. Add a `DPRINTF(sc, MYF_DBG_<class>, ...)` that captures the cause clearly.
3. Write a brief note in `DEBUG.md` explaining what to look for in `dmesg` when the error occurs.

The goal is to make every error in the driver self-explanatory without requiring the reader to cross-reference the source code.

### Challenge 23.5: Boot-Time Debug

Modify the driver to log the attach-time version and hardware details only when `bootverbose` is set. The effect should be: a normal boot shows one line, a verbose boot (requested with `boot -v`) shows detailed configuration.

Read `/usr/src/sys/sys/systm.h` for the declaration of `bootverbose`, and `/usr/src/sys/kern/subr_boot.c` for examples. Describe in two sentences what FreeBSD uses `bootverbose` for, and why it is better than a driver-specific verbose flag for the particular case of early boot.

## Troubleshooting and Common Mistakes

Every reader will hit at least a few of the issues below. Each one is recorded with the symptom, the likely cause, and the path to a fix.

**"My kldload fails with `link_elf: symbol X undefined`."**

- Cause: the module depends on a kernel symbol that is not in the running kernel. Usually this means the kernel is older than the module source, or the module was compiled against a different kernel build.
- Fix: rebuild the kernel and module from the same source tree, using the same compile flags. Confirm `uname -a` and the module's build directory match.

**"DTrace says `invalid probe specifier`."**

- Cause: the probe name is mistyped, or the provider/module/function field does not exist at runtime.
- Fix: run `dtrace -l | grep <provider>` to list the available probes, and pick a name from the list. Remember that wildcards must match an existing name.

**"WITNESS panics at boot with `WITNESS_CHECKORDER`."**

- Cause: an early driver (often third-party) violates the declared lock order. Under a stock kernel the violation was silent; under WITNESS it causes an immediate panic.
- Fix: disable the offending driver temporarily, or set `debug.witness.watch=0` at boot time to disable the check, or rebuild the offending driver with a corrected lock order.

**"DPRINTF messages do not appear in `dmesg`."**

- Cause: the corresponding bit in `sc_debug` is not set.
- Fix: confirm the sysctl with `sysctl dev.myfirst.0.debug.mask`. Set the bit: `sysctl dev.myfirst.0.debug.mask=0xFF`. Retry.

**"DTrace says `probe description myfirst::: matched 0 probes`."**

- Cause: either the driver is not loaded, or CTF was not generated (the SDT probes are declared but not visible to DTrace).
- Fix: run `kldstat | grep myfirst` and confirm the module is loaded. If it is, rebuild the kernel with `makeoptions WITH_CTF=1` and reboot.

**"Memory leak persists after my fix."**

- Cause: there is more than one leak. The first fix addressed one path, but another path is still leaking.
- Fix: re-examine the `vmstat -m` numbers. If they still grow, trace the allocator: add `device_printf` at every `malloc` and `free` call, reload, exercise the driver, and count the appearances.

**"The driver unloads, but `vmstat -m` still shows nonzero InUse."**

- Cause: the unload path did not release all memory. This is often because something was allocated by open but not freed by close, and the close path was never called before detach.
- Fix: verify `destroy_dev_sched_cb` is used to wait for outstanding opens before `detach` proceeds. Every `si_drv1` or `si_drv2` buffer allocated per open must be freed in close or in the detach-ordered callback.

**"My `fbt` probe has a misleading name."**

- Cause: some drivers use inline functions or static helpers that the compiler may have inlined away. The compiled binary shows only the containing function.
- Fix: compile the module with `-O0` or add `__noinline` to the helper. This is useful for debugging only; release builds should use the normal optimisation level.

**"ktrace records are too long to read."**

- Cause: default ktrace captures many event classes at once.
- Fix: limit to specific classes with `-t` flag: `ktrace -t c ./myftest` captures only syscalls, not I/O or NAMI.

**"The kernel boots but prints no driver messages at all."**

- Cause: either the driver is not built into the kernel, or the driver's probe returned a non-zero error.
- Fix: `kldstat -v | grep myfirst` confirms the module is loaded. `dmesg | grep -i 'probe'` finds the probe messages. If the module is loaded but the driver is not attached, check `device_set_desc` and the probe function's return value.

## Appendix: Real-World Debug Patterns in the FreeBSD Tree

The patterns in Sections 7 and 8 are based on conventions already used by production drivers. A reader who inspects the tree will see variations on these conventions everywhere. This appendix gives a short tour.

**`if_ath_debug.h`** (`/usr/src/sys/dev/ath/if_ath_debug.h`) defines approximately 40 verbosity classes covering reset, interrupt, RX, TX, beacon, rate control, channel selection, and many others, each expressed as a bit in a 64-bit mask. The class names follow the `ATH_DEBUG_<subsystem>` pattern (for example `ATH_DEBUG_RECV`, `ATH_DEBUG_XMIT`, `ATH_DEBUG_RESET`, `ATH_DEBUG_BEACON`), and the `DPRINTF` macro follows the standard pattern. The driver uses `ATH_DEBUG_<class>` in hundreds of places throughout the source, giving operators fine-grained control over trace output.

**`virtqueue.c`** (`/usr/src/sys/dev/virtio/virtqueue.c`) uses SDT probes heavily. A single `SDT_PROVIDER_DEFINE(virtqueue)` at the top declares the provider, followed by a dozen `SDT_PROBE_DEFINEn` calls for each significant event: enqueue, dequeue, notify, and so on. The driver fires the probes inside the hot path, and the probes are no-ops when no script is attached. DTrace scripts like `dtrace -n 'virtqueue::: { @[probename] = count(); }'` give instant visibility into virtio device activity.

**`if_re.c`** (`/usr/src/sys/dev/re/if_re.c`) mixes `device_printf`, `printf`, and `if_printf`. The `device_printf` calls appear at attach and detach, where the device identity is useful. `if_printf` appears in the packet paths, where the interface identity matters. `printf` appears in the shared helpers, where neither identity is available. The division of responsibility is pragmatic, not ideological.

**`uart_core.c`** (`/usr/src/sys/dev/uart/uart_core.c`) defines `UART_DBG_LEVEL` and a family of `UART_DBG` macros that check the level and log through `printf`. The level is set at compile time via `options UART_POLL_FREQ` and `options UART_DEV_TOLERANCE_PCT`, among others. The design is static: debug decisions are fixed at build time rather than runtime. This is the right choice for a driver that must run in very early boot, when sysctl is not yet available.

**`random_harvestq.c`** (`/usr/src/sys/dev/random/random_harvestq.c`) uses WITNESS extensively. Every lock in the file has a `mtx_assert(&lock, MA_OWNED)` check at the head of each function that relies on the lock. When WITNESS is enabled in the build, these assertions catch any caller that forgot to acquire the lock. In release builds, the assertion compiles out and has zero cost.

Across the tree, the pattern is consistent: drivers expose their internals through a combination of static debug logging, SDT probes, and assertion macros. Each of these costs nothing at runtime when disabled, and gives rich visibility when enabled. The reader who adopts this pattern in their own driver writes code that scales gracefully from first-day debugging through production monitoring.

## Appendix: Worked Case Studies in Driver Debugging

The material in this chapter is most useful when it is rehearsed against concrete bugs. This appendix walks through three realistic case studies. Each one begins with a symptom the reader might encounter, follows the diagnostic steps the tools of this chapter enable, arrives at a root cause, and records the fix. The three cases together cover the three broad categories of driver bugs: a correctness bug in the logging path, a lock-order bug, and a performance bug in the interrupt path.

The cases are written as narrative rather than as a dry sequence of commands because debugging is a narrative process. The experienced developer does not follow a single algorithm; they read a trace, form a hypothesis, test it, refine it, and iterate. These walkthroughs try to preserve that texture while keeping each step reproducible.

### Case Study 1: The Missing Message

**Symptom.** A reader has added a `DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n", sc->sc_open_count)` line to `myfirst_open`. They set `sysctl dev.myfirst.0.debug.mask=0x2` to enable `MYF_DBG_OPEN`, open the device, and check `dmesg`. Nothing appears.

**First hypothesis: the bit is wrong.** The reader double-checks the mask. `MYF_DBG_OPEN` is `0x02`, so setting `mask=0x2` should enable it. The hypothesis is wrong.

**Second hypothesis: the device is not being opened.** The reader runs `cat /dev/myfirst0 > /dev/null` and checks `dmesg`. Still nothing.

**Third hypothesis: the sysctl is not actually setting the field.** The reader reads the sysctl back:

```sh
sysctl dev.myfirst.0.debug.mask
```

The output shows `0x0`. The write did not take effect. This is the real problem.

**Narrowing down.** The reader inspects the attach code and finds:

```c
SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RD, &sc->sc_debug, 0, "debug class bitmask");
```

Note: `CTLFLAG_RD` (read-only), not `CTLFLAG_RW`. The sysctl was declared read-only, so the `sysctl` command appeared to succeed but actually silently refused the write.

**Fix.** Change the flag to `CTLFLAG_RW`:

```c
SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0, "debug class bitmask");
```

Rebuild, reload, set the mask, open the device, and the message appears.

**Lessons learned.** Three tools combined to find this bug in under a minute: `dmesg` showed the symptom (missing message), `sysctl` exposed the real state (write not taking effect), and the driver source made the cause visible (`CTLFLAG_RD`). Without any one of these, the reader might have wasted ten minutes on a wrong hypothesis. The tools do not replace thinking; they provide the evidence that thinking needs.

The sysctl read-back is a useful discipline in general. Any time a driver value is supposed to be writable, test the write by reading it back. This one habit will catch a class of bugs that have bitten every FreeBSD developer at least once.

### Case Study 2: The Intermittent Panic

**Symptom.** Under light load the driver works perfectly. Under a stress workload (say, one hundred simultaneous readers), the kernel panics with:

```text
panic: mutex myfirst_lock recursed on non-recursive mutex myfirst
```

The panic is intermittent: sometimes the workload runs for five minutes before panicking, sometimes it panics immediately. The reader confirms that a debug kernel with `options WITNESS` and `options INVARIANTS` is in use.

**First step: read the stack.** The panic message includes a stack trace. The relevant lines point at `myfirst_read` as the recursion site, with `myfirst_read` itself already on the stack higher up. Somehow, `myfirst_read` is being called while a previous call to `myfirst_read` is still executing, and both calls are on the same thread.

**Hypothesis.** Something inside `myfirst_read` is triggering a secondary `myfirst_read` call. The most likely candidate is a `uiomove`, which calls the user program's page-fault path, which in very unusual circumstances can call back into device I/O.

**Verification.** The reader adds two `DPRINTF` calls, one at entry and one just before `uiomove`, each capturing the current thread:

```c
DPRINTF(sc, MYF_DBG_IO, "read entry: tid=%d\n", curthread->td_tid);
DPRINTF(sc, MYF_DBG_IO, "read about to uiomove: tid=%d\n", curthread->td_tid);
```

Rebuild, reload, run the workload. The kernel log shows two "read entry" messages for the same tid before any "about to uiomove", which confirms the hypothesis.

**Diagnosis.** The `mtx_lock(&sc->sc_mtx)` call in `myfirst_read` is held during `uiomove`. The `uiomove` target happens to be in a memory-mapped region of `/dev/myfirst0`, because the test workload was using `mmap` output. The page fault that `uiomove` triggers re-enters `myfirst_read` to service the fault, which attempts to acquire `sc->sc_mtx` a second time.

**Fix.** The lock should be released before `uiomove`. In general, a sleep lock must never be held across a function that may page-fault.

```c
/* buggy version */
mtx_lock(&sc->sc_mtx);
error = uiomove(sc->sc_buffer, sc->sc_bufsize, uio);
mtx_unlock(&sc->sc_mtx);

/* fixed version */
mtx_lock(&sc->sc_mtx);
/* snapshot the buffer so we can release the lock */
tmp_buffer = sc->sc_buffer;
tmp_bufsize = sc->sc_bufsize;
mtx_unlock(&sc->sc_mtx);
error = uiomove(tmp_buffer, tmp_bufsize, uio);
```

Rebuild, reload, run the stress workload. The panic is gone.

**Lessons learned.** `WITNESS` did not catch this directly because it is a recursion, not a lock-order violation. But `INVARIANTS` turned the recursion into a clean panic instead of silent corruption, and the DPRINTF trace made the chronology visible. The rule "do not hold a sleep lock across `uiomove` or any function that may sleep" is stated in `locking(9)` but is easy to forget in practice. Every sleep-lock acquisition in driver code deserves a momentary "could the wrapped call sleep or fault?" check.

### Case Study 3: The Slow Interrupt

**Symptom.** A driver works correctly but the system is sluggish when the device is active. Shell responsiveness drops, interactive applications stutter, and `top` shows high system CPU.

**First step: confirm the driver is the cause.** The reader unloads the driver and compares:

```sh
# before unload
top -aC1 -bn1 | head -20
sudo kldunload myfirst
# after unload
top -aC1 -bn1 | head -20
```

If the sluggishness disappears when the driver unloads, the driver is the source.

**Second step: identify the expensive code path.** DTrace's `profile` provider samples the kernel at a fixed rate and shows where time is being spent:

```sh
sudo dtrace -n 'profile-1001hz /arg0/ { @[stack(10)] = count(); } tick-10s { exit(0); }'
```

After ten seconds of the workload, the output shows the hottest stacks. If `myfirst_intr` dominates the sample, the interrupt handler is too heavy.

**Third step: measure how long each interrupt takes.**

```sh
sudo dtrace -n 'fbt::myfirst_intr:entry { self->t = timestamp; }
                fbt::myfirst_intr:return /self->t/ {
                    @ = quantize(timestamp - self->t); self->t = 0; }'
```

A healthy handler completes in under 10 microseconds (10 000 nanoseconds). If the quantization shows most invocations in the 100 000 to 1 000 000 nanosecond range (100 microseconds to 1 millisecond), the handler is doing too much work.

**Fourth step: read the handler code.** The reader inspects `myfirst_intr` and finds:

```c
static void
myfirst_intr(void *arg)
{
        struct myfirst_softc *sc = arg;
        struct mbuf *m;

        mtx_lock(&sc->sc_mtx);
        m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
        if (m != NULL) {
                myfirst_process_data(sc, m);
                m_freem(m);
        }
        mtx_unlock(&sc->sc_mtx);
}
```

The handler does real work: it allocates an mbuf, processes data, and frees the mbuf. `myfirst_process_data` includes several packets of work, each with their own allocation and processing. Under load, each interrupt can hold the CPU for hundreds of microseconds.

**Fix.** The heavy work moves to a taskqueue. The handler itself does the absolute minimum: acknowledges the interrupt and signals the taskqueue.

```c
static void
myfirst_intr(void *arg)
{
        struct myfirst_softc *sc = arg;

        /* Acknowledge the interrupt */
        bus_write_4(sc->sc_res, MYFIRST_REG_INTR_STATUS, 0);
        /* Schedule deferred processing */
        taskqueue_enqueue(sc->sc_tq, &sc->sc_process_task);
}

static void
myfirst_process_task(void *arg, int pending)
{
        struct myfirst_softc *sc = arg;
        struct mbuf *m;

        mtx_lock(&sc->sc_mtx);
        m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
        if (m != NULL) {
                myfirst_process_data(sc, m);
                m_freem(m);
        }
        mtx_unlock(&sc->sc_mtx);
}
```

The taskqueue is set up in `attach` as explored in Chapter 19.

Rebuild, reload, run the workload. Shell responsiveness returns, `top` shows the driver contributing a normal amount of system CPU, and the latency quantization of `myfirst_intr` is now concentrated in the 1 to 10 microsecond bucket.

**Lessons learned.** Three DTrace measurements guided the fix: `profile-1001hz` located the hot code, `fbt::myfirst_intr:entry/return` measured the handler duration, and the final post-fix measurement confirmed the improvement. No source code was modified during diagnosis; the modifications were made only after the bug was understood.

The rule that interrupt handlers must be short is familiar to driver authors, but enforcement is not automatic. Debug builds do not panic on a slow handler. The only way to catch this category of bug is to measure, and DTrace is the right tool for the job. A mature driver team runs `fbt::<driver>_intr:entry/return` profiling as part of every release cycle; their handlers stay lean because the measurements make any drift visible.

### Case Study 4: The Vanishing Device

**Symptom.** After `kldunload myfirst`, a second `kldload myfirst` sometimes panics with a page fault in `devfs_ioctl`, stack pointing at the old `myfirst_ioctl`. The panic does not happen every time, but under load (for example, if a daemon is keeping `/dev/myfirst0` open across the unload) it is consistently fatal.

**First step: read the panic.** The stack shows `devfs_ioctl -> myfirst_ioctl -> (address not found)`. The "address not found" is the faulting instruction. This is a use-after-free of function code: the kernel is trying to call a function whose memory has been unmapped.

**Hypothesis.** The driver's unload path did not wait for a process that had the device open. The process issued an ioctl between the unload and the re-load; the ioctl dispatched to the now-unmapped `myfirst_ioctl`.

**Verification.** The reader inspects `myfirst_detach` and finds:

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        destroy_dev(sc->sc_cdev);
        bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem);
        mtx_destroy(&sc->sc_mtx);
        return (0);
}
```

The `destroy_dev` call is the issue: it immediately destroys the device node, but in-flight ioctls on the node may still be executing. When `destroy_dev` returns, the driver thinks it is safe to release resources. But the ioctl is still dispatching when those resources go away.

**Fix.** Use `destroy_dev_sched_cb` (or equivalently, `destroy_dev_sched` and then wait for completion) to defer the actual destruction until no thread is inside the device's methods.

```c
static int
myfirst_detach(device_t dev)
{
        struct myfirst_softc *sc = device_get_softc(dev);

        /* Tell devfs to destroy the node after all callers finish */
        destroy_dev_sched_cb(sc->sc_cdev, myfirst_detach_cb, sc);
        /* Return success; cleanup happens in the callback */
        return (0);
}

static void
myfirst_detach_cb(void *arg)
{
        struct myfirst_softc *sc = arg;

        bus_release_resource(sc->sc_dev, SYS_RES_MEMORY, 0, sc->sc_mem);
        mtx_destroy(&sc->sc_mtx);
}
```

The detach returns immediately; the actual resource release happens in the callback, which is scheduled to fire only when every caller inside the `cdev`'s methods has exited.

Rebuild, reload repeatedly under the problematic workload. The panic is gone.

**Lessons learned.** This is the classic use-after-detach bug described in Section 7.4. The debug kernel's `DEBUG_MEMGUARD` option would have caught it earlier by poisoning the freed memory, turning the latent corruption into an immediate page fault on the next call. The fix uses a FreeBSD primitive (`destroy_dev_sched_cb`) that exists precisely for this case. Reading the `cdev`-management documentation in `cdev(9)` is the exercise that prevents this bug in new code.

### Case Study 5: The Corrupted Output

**Symptom.** A user program reads from `/dev/myfirst0` expecting a specific pattern (say, the sequence `0x01, 0x02, 0x03, ...`). Most of the time the pattern is correct. Occasionally, on a heavily loaded system, the pattern is off: a byte or two is wrong, or the sequence is shifted. There is no panic, no error code, just subtly wrong data.

**First step: reproduce reliably.** The reader writes a small test program that reads repeatedly and compares against the expected pattern. Under load, the mismatches occur at roughly one in ten thousand reads. The reader confirms that unloading the driver and using a simple `/dev/zero` test produces no mismatches, which rules out the user program as the source of the bug.

**Hypothesis 1: a race on the buffer.** If two threads are reading concurrently, and the buffer is shared without locking, they might corrupt each other. The reader inspects the driver and finds:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;

        /* Generate the pattern into sc->sc_buffer */
        for (int i = 0; i < sc->sc_bufsize; i++)
                sc->sc_buffer[i] = (uint8_t)(i + 1);

        return (uiomove(sc->sc_buffer, sc->sc_bufsize, uio));
}
```

The buffer is per-softc, shared across all opens. Two threads calling `read` simultaneously write to the same buffer; the second thread overwrites the first's content before the first has finished copying. The `uiomove` of the first thread then reads partially from the first thread's pattern and partially from the second thread's. The result is a garbled sequence.

**Verification.** The reader adds a `DPRINTF` at the start and end of the read, capturing the thread's tid:

```c
DPRINTF(sc, MYF_DBG_IO, "read start: tid=%d\n", curthread->td_tid);
/* ... generate and uiomove ... */
DPRINTF(sc, MYF_DBG_IO, "read end: tid=%d\n", curthread->td_tid);
```

When the log is examined during a corruption event, two `start` messages appear between a single `start` and its `end`. The race is confirmed.

**Fix.** The buffer must either be per-call (allocated on each read) or the access must be serialised with a mutex:

```c
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        uint8_t *buf;
        int error;

        buf = malloc(sc->sc_bufsize, M_MYFIRST, M_WAITOK);
        for (int i = 0; i < sc->sc_bufsize; i++)
                buf[i] = (uint8_t)(i + 1);

        error = uiomove(buf, sc->sc_bufsize, uio);
        free(buf, M_MYFIRST);
        return (error);
}
```

A per-call allocation removes the shared state entirely, and the bug goes away. A lock-based fix is faster for small buffers but allows only one reader at a time; the trade-off depends on the driver's use case.

**Lessons learned.** The corruption is subtle precisely because it does not produce a panic or an error. `WITNESS` cannot help here because there is no lock to order. `INVARIANTS` cannot help because the shared state does not trigger an assertion. The only way to find this is: test the output, observe the corruption, form a hypothesis about races, add instrumentation to confirm the hypothesis, and fix. The DPRINTF pattern is the instrumentation; a disciplined test harness is the reproduction. Together, they solve the problem.

This class of bug is the strongest argument for driver authors to write test harnesses from the start. A small program that reads from the device and compares against an expected output catches many bugs in minutes that would otherwise survive for months in the field.

### Summary of the Five Cases

The five case studies covered a deliberately diverse set of bugs. Case 1 was a logging correctness bug, found in seconds once the sysctl was read back. Case 2 was a lock-recursion triggered by a paging path, found by combining a panic trace and DPRINTF. Case 3 was a performance bug in the interrupt handler, diagnosed with DTrace profiling. Case 4 was a use-after-detach in the `cdev` path, diagnosed with the panic stack and fixed with the correct FreeBSD primitive. Case 5 was a shared-state race in the read path, found with a test harness and DPRINTF.

Three patterns recur across the cases:

1. **Tools find symptoms; reading code finds causes.** No tool pointed directly at the buggy line. Each tool narrowed the search: `vmstat -m`, WITNESS, DTrace, DDB. Then the reader opened the source and found the mistake.

2. **Reproduction is more important than tooling.** A bug that reproduces reliably can be diagnosed with almost any tool. A bug that does not reproduce must be reproduced first, usually by scripting a workload that exercises the suspect path.

3. **Debug infrastructure pays its cost many times over.** The `DPRINTF` macro, the SDT probes, the `sc_debug` sysctl: each of these took five minutes to add, and each saved hours of debugging later. The reader who writes instrumented code from the start is the reader who avoids the long, painful debug sessions.

With these patterns in mind, the reader is ready to carry the chapter's methods forward. Every new piece of the `myfirst` driver added in later chapters will be developed with `DPRINTF` calls, SDT probes, and test harnesses in place; the drop in debugging pain will be noticeable from Chapter 24 onward.

## Appendix: Extended DTrace Techniques for Driver Work

This appendix extends Section 5 with a focused tour of DTrace techniques that come up often in driver development. Section 5 introduced the basics; this appendix shows how to combine them productively. Readers who find DTrace useful may want to keep this appendix as a cheat sheet.

### Predicates and Selective Tracing

A predicate narrows a probe to events that match a condition. It appears in `/ ... /` between the probe description and the action:

```sh
dtrace -n 'fbt::myfirst_read:entry /execname == "myftest"/ {
               @ = count(); }'
```

Only reads from processes named `myftest` are counted. Predicates can reference `pid`, `tid`, `execname`, `zonename`, `uid`, and the probe arguments `arg0`, `arg1`, etc.

A common pattern is to filter by softc pointer to focus on a single device instance:

```sh
dtrace -n 'fbt::myfirst_read:entry /args[0] == 0xfffff8000a000000/ {
               @ = count(); }'
```

The literal address of the softc comes from `devctl` or from inspecting the driver's sysctl. When multiple `myfirst` devices are present, this isolates one at a time.

### Aggregations That Tell a Story

Aggregations are DTrace's statistical workhorses. Section 5 introduced `count()` and `quantize()`; other aggregation functions are equally useful:

- `sum(x)`: running total of `x`
- `avg(x)`: running average of `x`
- `min(x)`: minimum of `x` seen
- `max(x)`: maximum of `x` seen
- `lquantize(x, low, high, step)`: linear quantization with bounds
- `llquantize(x, factor, low, high, steps)`: logarithmic quantization with bounds

A typical per-function latency report uses `quantize` because it reveals distribution:

```sh
dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
           fbt::myfirst_read:return /self->t/ {
               @ = quantize((timestamp - self->t) / 1000);
               self->t = 0; }'
```

Divide by 1000 to convert nanoseconds to microseconds, which makes the buckets meaningful. Add an `@buf` indexed aggregation to see latency by buffer size:

```sh
dtrace -n '
fbt::myfirst_read:entry {
    self->t = timestamp;
    self->len = args[1]->uio_resid;
}
fbt::myfirst_read:return /self->t/ {
    @["read", self->len] = quantize((timestamp - self->t) / 1000);
    self->t = 0;
}'
```

This aggregates separately for each distinct `uio_resid`, revealing whether large reads are disproportionately slow.

### Thread-Local Variables

The `self->` prefix introduces a thread-local variable: each thread sees its own copy. This is how Section 5's latency measurement works: the entry probe stores the timestamp in `self->t`, and the return probe reads it. Because both probes fire on the same thread, the variable is unambiguous.

Common uses:

```sh
self->start    /* time of entry for a specific function */
self->args     /* saved arguments for use in the return probe */
self->error    /* error code captured from return */
```

Thread-local variables are zeroed by default. Use them freely; their cost is one word of memory per live thread.

### Arrays and Associative Access

DTrace arrays are sparse associative arrays, indexed by any expression. Aggregations use them implicitly (the `@[key]` syntax), but ordinary variables can use them too:

```sh
dtrace -n 'fbt::myfirst_read:entry {
               counts[args[0], execname]++; }'
```

Though in practice, aggregations are preferable because they accumulate correctly across all CPUs.

### Speculative Tracing

For a high-rate event where most cases are uninteresting, DTrace's speculative tracing buffers output until a decision is made:

```sh
dtrace -n '
fbt::myfirst_read:entry {
    self->spec = speculation();
    speculate(self->spec);
    printf("read entry: pid=%d", pid);
}
fbt::myfirst_read:return /self->spec && args[1] == 0/ {
    speculate(self->spec);
    printf("read return: bytes=%d", arg1);
    commit(self->spec);
    self->spec = 0;
}
fbt::myfirst_read:return /self->spec && args[1] != 0/ {
    discard(self->spec);
    self->spec = 0;
}'
```

Only reads that return successfully (`args[1] == 0`) have their output committed; failed reads are discarded. This produces a quiet, focused log under heavy load.

Speculative tracing is one of DTrace's most effective features and is rarely needed for small drivers. But for a driver with thousands of events per second, it makes the difference between useful output and an unreadable flood.

### Predicate Chains for Narrow Tracing

A common need is to trace one function call tree from a specific entry point. Predicates can chain using thread-local flags:

```sh
dtrace -n '
syscall::ioctl:entry /pid == $target/ { self->trace = 1; }
fbt::myfirst_*: /self->trace/ { printf("%s: %s", probefunc, probename); }
syscall::ioctl:return /self->trace/ { self->trace = 0; }'
```

Run with `-p <pid>` to target a specific process. The trace flag is set on syscall entry and cleared on return, so only driver functions called during the ioctl are logged. This pattern is fundamental for isolating a specific user action.

### Profiling the Kernel

The `profile` provider samples the kernel at a regular rate. Useful probes include:

- `profile-97`: samples every 97th of a second, on every CPU
- `profile-1001hz`: samples at 1001 Hz, slightly offset from any whole-second workload
- `profile-5ms`: samples every 5 ms

Each sample records the current stack. Aggregating by stack reveals where time is spent:

```sh
dtrace -n 'profile-1001hz /arg0/ { @[stack(8)] = count(); }
           tick-10s { exit(0); }'
```

The `arg0` predicate filters out idle CPUs. The `stack(8)` records the top 8 frames. The `tick-10s` ends the run after 10 seconds.

The output looks like:

```text
  kernel`myfirst_process_data+0x120
  kernel`myfirst_intr+0x48
  kernel`ithread_loop+0x180
  kernel`fork_exit+0x7f
  kernel`fork_trampoline+0xe
         15823
```

15 823 samples at that stack, out of about 100 000 total (10 seconds * 1001 Hz * a few CPUs). The driver's interrupt path dominates. Combined with the latency measurement above, this is enough to justify a refactor.

### Pretty-Printing Structures

A CTF-enabled kernel gives DTrace the types of every kernel structure. This lets `print()` show fields by name instead of raw memory:

```sh
dtrace -n 'fbt::myfirst_read:entry { print(*args[0]); }'
```

The output shows every field of the `struct myfirst_softc` the read is operating on, by field name, with human-readable values.

The feature depends on CTF having been generated (`makeoptions WITH_CTF=1`). Without CTF, DTrace falls back to address-based printing, which is much less readable.

### Ring-Buffered Output

For very long DTrace runs, the default output fills up memory. The `-b` flag sets a ring buffer that discards older records when full:

```sh
dtrace -b 16m -n 'myfirst::: { trace(timestamp); }'
```

A 16 MB ring holds roughly the last 16 MB of trace output. Older data is overwritten as newer data arrives, which is typically fine for diagnosis: the interesting window is the last few seconds before a problem, not the previous hour.

### Exiting Cleanly

DTrace accumulates aggregations in memory until the script exits. For a long run, the reader must exit cleanly to see the aggregations:

```sh
dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }
           tick-60s { printf("%Y\n", walltimestamp); printa(@);
                      clear(@); }'
```

Every 60 seconds, the aggregate is printed and cleared. The output is a running report, rather than a single report at exit. This pattern is ideal for overnight runs: the output contains per-minute snapshots, and the reader can see how the driver's behaviour evolves.

### Summary

These techniques together cover roughly 80% of the DTrace skills a driver author needs. The remaining 20% come from the DTrace Guide and from reading existing scripts in `/usr/share/dtrace/toolkit`.

Two rules of thumb guide advanced use:

1. **Start with the simplest script that might answer the question.** Add predicates only when the script produces too much output. Add aggregations only when counts and times matter.

2. **Validate DTrace's output against a known workload.** Run a simple test case first (say, one open, one read, one close), confirm the DTrace output matches, and only then trust the tool on a complex workload.

With these rules, DTrace is the most capable debugging tool in the FreeBSD arsenal, and the one that rewards every hour of study with months of saved debugging time.

## Appendix: WITNESS Trace Interpretation

WITNESS is often a reader's first exposure to interpreting a kernel diagnostic trace. This appendix walks through the format of a WITNESS violation message and explains what each field means.

### The General Format

A WITNESS violation looks like:

```text
lock order reversal: (non-sleepable after sleepable)
 1st 0xfffff8000a000000 myfirst_lock (myfirst, sleep mutex) @ myfirst.c:123
 2nd 0xfffff8000a001000 myfirst_intr_lock (myfirst, spin mutex) @ myfirst.c:234
lock order myfirst_intr_lock -> myfirst_lock established at:
#0 0xffffffff80abcdef at kdb_backtrace+0x1f
#1 0xffffffff80abcdee at _witness_debugger+0x4f
#2 0xffffffff80abcdaf at witness_checkorder+0x21f
#3 0xffffffff80c00000 at _mtx_lock_flags+0x8f
#4 0xffffffff80ffff00 at myfirst_intr+0x30
#5 ...
panic: witness
```

The message has three sections: the header line, the lock description section, and the backtrace of where the reversed order was first established.

### The Header

```text
lock order reversal: (non-sleepable after sleepable)
```

This says a spin (non-sleepable) mutex was acquired while a sleep (sleepable) mutex was already held. The reverse order (spin first, then sleep) is allowed; this order (sleep first, then spin) is not.

Common header variants:

- `lock order reversal: (sleepable after non-sleepable)`: means the declared order was later broken by an acquisition that goes the other way
- `spin lock recursion`: attempted to acquire a spin lock that the current thread already holds, which is not allowed except for recursive spin locks
- `spin lock held too long`: a spin lock was held for longer than the threshold (usually multiple seconds)
- `blocking on condition variable with spin lock`: tried to sleep while holding a spin lock

### The Lock Descriptions

```text
 1st 0xfffff8000a000000 myfirst_lock (myfirst, sleep mutex) @ myfirst.c:123
 2nd 0xfffff8000a001000 myfirst_intr_lock (myfirst, spin mutex) @ myfirst.c:234
```

- **0xfffff8000a000000**: the address of the mutex in memory
- **myfirst_lock**: the human-readable name passed to `mtx_init`
- **(myfirst, sleep mutex)**: the class (usually the driver name) and the type
- **@ myfirst.c:123**: the source location of the `mtx_init` call (when WITNESS is set up with DEBUG_LOCKS)

Both locks appear, first the one that was already held, then the one being acquired.

### The Backtrace

The backtrace shows where the reversed order was first recorded. WITNESS remembers the first instance of each (A, B) pair it sees; later violations reference the earlier occurrence. If the first occurrence was valid at the time (for example, both locks were acquired correctly) but a later path reverses them, the backtrace points at the earlier, correct acquisition. The reader may need to search the source for the specific reversal path, which is usually the current stack trace (not shown in this snippet).

Two frames to look for:

1. The function that currently holds the first lock and is trying to acquire the second (or vice versa).
2. The function that first acquired the two locks in the original order.

Comparing the two reveals the conflict.

### Reading a Real Example

A common example in new drivers:

```text
lock order reversal: (non-sleepable after sleepable)
 1st 0xfff0 myfirst_lock (sleep mutex) @ myfirst.c:100
 2nd 0xfff1 ithread_lock (spin mutex) @ kern_intr.c:234
```

The new driver's code at `myfirst.c:100` acquires `myfirst_lock` (a sleep mutex). Somewhere deeper in the code path, an interrupt scheduling function tries to acquire `ithread_lock` (a spin mutex used by the interrupt subsystem). This is the `sleep-then-spin` violation, which is always a bug: a spin lock should never be acquired while a sleep lock is held.

The fix: rework the code so that the spin lock (or the subsystem that uses it) is not called while holding `myfirst_lock`. Common approaches include releasing `myfirst_lock` before the call, and then reacquiring it afterward if needed.

### Asserting Lock Ownership

Independent of WITNESS, `mtx_assert(&lock, MA_OWNED)` at the top of each function that assumes a lock is held is a strong defensive coding pattern. When `INVARIANTS` is set, the assertion fires on any violation. Combined with WITNESS, these two checks catch the vast majority of lock-related bugs at their first occurrence, long before they corrupt production data.

### When WITNESS Is Too Noisy

Occasionally WITNESS produces violations in code that is actually correct, usually because the lock acquisition is guarded by a runtime check that WITNESS cannot see. For those cases, `mtx_lock_flags(&lock, MTX_DUPOK)` or `MTX_NEW` tell WITNESS to allow the acquisition. Use these flags sparingly; most WITNESS violations are real.

## Appendix: Building a Driver-Specific Debug Toolkit

The debug macros and SDT probes added to `myfirst` in Section 8 are a starting point. Over the course of a driver's life, the toolkit grows: more classes, more probes, custom sysctls, perhaps a debug-only ioctl that dumps internal state. This appendix outlines patterns for extending the toolkit.

### Debug-Only Sysctls

A sysctl under `dev.myfirst.0.debug.*` is the natural home for runtime debug controls. Add nodes liberally:

- `dev.myfirst.0.debug.mask`: the class bitmask
- `dev.myfirst.0.debug.loglevel`: the syslog priority for DPRINTF output
- `dev.myfirst.0.debug.trace_pid`: a PID to focus tracing on
- `dev.myfirst.0.debug.count_io`: a counter of I/O events processed
- `dev.myfirst.0.debug.dump_state`: write-only, triggers a one-shot state dump

Each of these costs very little in size and is enormously useful during field debugging. The rule is: if a piece of state would be useful to inspect during a bug hunt, expose it via sysctl.

### A Debug Ioctl

For state too complex or large to expose as a sysctl, a debug-only ioctl is a good choice. Define:

```c
#define MYFIRST_IOC_DUMP_STATE  _IOR('f', 100, struct myfirst_debug_state)

struct myfirst_debug_state {
        uint64_t        open_count;
        uint64_t        read_count;
        uint64_t        write_count;
        uint64_t        error_count;
        uint32_t        current_mask;
        /* ... */
};
```

The ioctl handler copies the current state into the user buffer. A small user program reads and prints it:

```c
struct myfirst_debug_state s;
int fd = open("/dev/myfirst0", O_RDONLY);
ioctl(fd, MYFIRST_IOC_DUMP_STATE, &s);
printf("opens=%llu, reads=%llu, writes=%llu, errors=%llu\n",
    s.open_count, s.read_count, s.write_count, s.error_count);
```

A debug ioctl is especially useful when the state of interest is not a single number but a structure, and when sysctl's type constraints are awkward.

### Counters for Key Events

The driver's softc gains a set of counters, one per significant event. The counters are simple `uint64_t` fields incremented with `atomic_add_64` or under the softc lock. They are exposed via sysctl or the debug ioctl, and they are cleared at the reader's request:

```c
sc->sc_stats.opens++;       /* in open */
sc->sc_stats.reads++;       /* in read */
sc->sc_stats.errors++;      /* on any error path */
```

At ten counters per driver, the memory cost is under 100 bytes, and the visibility into the driver's behaviour is immense. Production monitoring systems can poll the counters periodically and alert on anomalies.

### A Self-Test Ioctl

For complex drivers, a self-test ioctl can be invaluable. It runs a sequence of internal tests (each one a function in the driver) and reports which passed and which failed. The results go back as a small structure. An operator debugging a field issue can run the self-test and immediately know whether any of the driver's subsystems are broken.

The self-test ioctl is not a substitute for unit testing or integration testing. It is a diagnostic shortcut for the field.

### Integration with rc Scripts

A debug-aware rc script (in `/usr/local/etc/rc.d/myfirst_debug`) can set the debug mask at boot:

```sh
#!/bin/sh
# PROVIDE: myfirst_debug
# REQUIRE: myfirst
# KEYWORD: shutdown

. /etc/rc.subr

name="myfirst_debug"
rcvar="myfirst_debug_enable"
start_cmd="myfirst_debug_start"
stop_cmd="myfirst_debug_stop"

myfirst_debug_start()
{
        echo "Enabling myfirst debug mask"
        sysctl dev.myfirst.0.debug.mask=${myfirst_debug_mask:-0x0}
}

myfirst_debug_stop()
{
        echo "Disabling myfirst debug"
        sysctl dev.myfirst.0.debug.mask=0
}

load_rc_config $name
run_rc_command "$1"
```

An operator sets `myfirst_debug_enable=YES` and `myfirst_debug_mask=0x3` in `/etc/rc.conf`, reboots, and the debug infrastructure engages automatically. This pattern is how production systems manage debug verbosity across many devices.

### Summary

The infrastructure in Section 8 is enough for the book's running example. Real drivers grow beyond it, acquiring a dozen or more debug-oriented features over time. The pattern is always the same: make instrumentation cheap when off, rich when on, and universally controllable by the operator. With that discipline, a driver remains diagnosable throughout its life.

## Appendix: A Lab Reference for Chapter 23

This last appendix collects the labs into a table with time estimates and dependencies, to help the reader plan their session:

| Lab  | Topic                                         | Est. time | Dependencies                     |
|------|-----------------------------------------------|-----------|-----------------------------------|
| 23.1 | First DDB session                             | 15 min    | Debug kernel with KDB/DDB         |
| 23.2 | DTrace measurement of driver                  | 20 min    | KDTRACE_HOOKS, CTF, driver loaded |
| 23.3 | User-side ktrace of driver access             | 15 min    | ktrace binary, driver loaded      |
| 23.4 | Memory leak introduction and fix              | 30 min    | Ability to rebuild the module     |
| 23.5 | Installing the 1.6-debug refactor             | 60 min    | Section 8 material, rebuild setup |

Total focused time: about two hours and twenty minutes, plus time to absorb the material. The book's intent is that the reader does each lab at a pace that allows genuine understanding rather than rushing.

Optional follow-ups:

- **23.6 (optional).** Combine `ktrace` and DTrace on the same test program, and correlate the user-side trace with the driver-side events.
- **23.7 (optional).** Add a debug ioctl (as described in the Debug Toolkit appendix) and write a small user program that triggers it.

These two extensions solidify the chapter's themes and build practical muscles that every later chapter will exercise.

## Appendix: Complete Annotated Listing of myfirst_debug.h and myfirst_debug.c Changes

To consolidate Section 8 into a single reference, this appendix collects the full text of the files as they appear after the 1.6-debug refactor, with inline comments explaining each block. A reader who prefers to see the finished code in one place can use this section; the example tree under `examples/part-05/ch23-debug/stage3-refactor/` contains the same text as downloadable files.

### myfirst_debug.h

```c
/*
 * myfirst_debug.h - debug and tracing infrastructure for the myfirst driver
 *
 * This header is included from the driver's source files. It provides:
 *   - a bitmask of debug verbosity classes
 *   - the DPRINTF macro for conditional device_printf
 *   - declarations for SDT probes that the driver fires at key points
 *
 * The matching SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE calls live in the
 * driver source, which owns the storage for the probe entries.
 */

#ifndef _MYFIRST_DEBUG_H_
#define _MYFIRST_DEBUG_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>

/*
 * Debug verbosity classes.  Each class is a single bit in sc->sc_debug.
 * The operator sets sysctl dev.myfirst.0.debug.mask to a combination of
 * these bits to enable the corresponding categories of output.
 *
 * Add new classes here when the driver grows new subsystems.  Use the
 * next unused bit and update DEBUG.md accordingly.
 */
#define MYF_DBG_INIT    0x00000001  /* probe/attach/detach */
#define MYF_DBG_OPEN    0x00000002  /* open/close lifecycle */
#define MYF_DBG_IO      0x00000004  /* read/write paths */
#define MYF_DBG_IOCTL   0x00000008  /* ioctl handling */
#define MYF_DBG_INTR    0x00000010  /* interrupt handler */
#define MYF_DBG_DMA     0x00000020  /* DMA mapping/sync */
#define MYF_DBG_PWR     0x00000040  /* power-management events */
#define MYF_DBG_MEM     0x00000080  /* malloc/free trace */
/* Bits 0x0100..0x8000 reserved for future driver subsystems */

#define MYF_DBG_ANY     0xFFFFFFFF
#define MYF_DBG_NONE    0x00000000

/*
 * DPRINTF - conditionally log a message via device_printf when the
 * given class bit is set in the softc's debug mask.
 *
 * Usage: DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d\n", pid);
 *
 * When the bit is clear, the cost is one load and one branch, which
 * is negligible in practice.  When the bit is set, the cost equals
 * a normal device_printf call.
 */
#ifdef _KERNEL
#define DPRINTF(sc, m, ...) do {                                        \
        if ((sc)->sc_debug & (m))                                        \
                device_printf((sc)->sc_dev, __VA_ARGS__);                \
} while (0)
#endif

/*
 * SDT probe declarations.  The matching SDT_PROBE_DEFINE calls are in
 * myfirst_debug.c (or in the main driver source if preferred).
 *
 * Probe argument conventions:
 *   open  (softc *, flags)            -- entry, before access check
 *   close (softc *, flags)            -- entry, before state update
 *   io    (softc *, is_write, resid, off) -- entry, into read or write
 */
SDT_PROVIDER_DECLARE(myfirst);
SDT_PROBE_DECLARE(myfirst, , , open);
SDT_PROBE_DECLARE(myfirst, , , close);
SDT_PROBE_DECLARE(myfirst, , , io);

#endif /* _MYFIRST_DEBUG_H_ */
```

The header has three sections: the class bitmask, the DPRINTF macro, and the SDT probe declarations. Each is small, self-contained, and designed to grow as the driver grows.

### The SDT Definitions in myfirst_debug.c

```c
/*
 * myfirst_debug.c - storage for the SDT probe entries.
 *
 * This file exists to hold the SDT_PROVIDER_DEFINE and SDT_PROBE_DEFINE
 * declarations.  By convention in the myfirst driver, these live in a
 * dedicated source file to keep the main driver uncluttered.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sdt.h>
#include "myfirst_debug.h"

/*
 * The provider "myfirst" exposes all of our static probes to DTrace.
 * Scripts select probes with "myfirst:::<name>".
 */
SDT_PROVIDER_DEFINE(myfirst);

/*
 * open: fired on every successful or attempted device open.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags from the open call
 */
SDT_PROBE_DEFINE2(myfirst, , , open,
    "struct myfirst_softc *", "int");

/*
 * close: fired on every device close.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int flags
 */
SDT_PROBE_DEFINE2(myfirst, , , close,
    "struct myfirst_softc *", "int");

/*
 * io: fired on every read or write call, at function entry.
 *   arg0 = struct myfirst_softc *
 *   arg1 = int is_write (0 for read, 1 for write)
 *   arg2 = size_t resid (bytes requested)
 *   arg3 = off_t offset
 */
SDT_PROBE_DEFINE4(myfirst, , , io,
    "struct myfirst_softc *", "int", "size_t", "off_t");
```

The file is short, intentionally. Its sole purpose is to hold the storage for the probe entries. If the driver grows more probes, the reader adds them here.

### Firing the Probes in the Driver

Inside `myfirst.c` (or whichever file implements each method), the probes fire at the appropriate call sites:

```c
/*
 * myfirst_open: device open method.
 * Called when a user process opens /dev/myfirst0.
 */
static int
myfirst_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error = 0;

        DPRINTF(sc, MYF_DBG_OPEN, "open: pid=%d uid=%d flags=0x%x\n",
            td->td_proc->p_pid, td->td_ucred->cr_uid, flags);

        /* Fire the SDT probe at entry, before any state change. */
        SDT_PROBE2(myfirst, , , open, sc, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count >= MYFIRST_MAX_OPENS) {
                error = EBUSY;
                goto out;
        }
        sc->sc_open_count++;
out:
        mtx_unlock(&sc->sc_mtx);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_OPEN, "open failed: error=%d\n", error);
        else
                DPRINTF(sc, MYF_DBG_OPEN, "open ok: count=%d\n",
                    sc->sc_open_count);

        return (error);
}

/*
 * myfirst_close: device close method.
 * Called when the last reference to the device is released.
 */
static int
myfirst_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
        struct myfirst_softc *sc = dev->si_drv1;

        DPRINTF(sc, MYF_DBG_OPEN, "close: pid=%d flags=0x%x\n",
            td->td_proc->p_pid, flags);

        SDT_PROBE2(myfirst, , , close, sc, flags);

        mtx_lock(&sc->sc_mtx);
        if (sc->sc_open_count > 0)
                sc->sc_open_count--;
        DPRINTF(sc, MYF_DBG_OPEN, "close ok: count=%d\n", sc->sc_open_count);
        mtx_unlock(&sc->sc_mtx);

        return (0);
}

/*
 * myfirst_read: device read method.
 */
static int
myfirst_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        DPRINTF(sc, MYF_DBG_IO, "read: pid=%d resid=%zu off=%jd\n",
            curthread->td_proc->p_pid,
            (size_t)uio->uio_resid, (intmax_t)uio->uio_offset);

        SDT_PROBE4(myfirst, , , io, sc, 0,
            (size_t)uio->uio_resid, uio->uio_offset);

        error = myfirst_read_impl(sc, uio);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_IO, "read failed: error=%d\n", error);
        return (error);
}

/*
 * myfirst_write: device write method.
 */
static int
myfirst_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct myfirst_softc *sc = dev->si_drv1;
        int error;

        DPRINTF(sc, MYF_DBG_IO, "write: pid=%d resid=%zu off=%jd\n",
            curthread->td_proc->p_pid,
            (size_t)uio->uio_resid, (intmax_t)uio->uio_offset);

        SDT_PROBE4(myfirst, , , io, sc, 1,
            (size_t)uio->uio_resid, uio->uio_offset);

        error = myfirst_write_impl(sc, uio);

        if (error != 0)
                DPRINTF(sc, MYF_DBG_IO, "write failed: error=%d\n", error);
        return (error);
}
```

Each method follows the entry-exit-error pattern:

1. A `DPRINTF` at entry, showing who called and with what arguments.
2. An `SDT_PROBE` after the entry log, before the actual work begins.
3. An error-path `DPRINTF` on any non-zero return.
4. A success-path `DPRINTF` showing the outcome when useful.

### Registering the Sysctl in myfirst_attach

The sysctl that controls `sc_debug` is registered during device attach:

```c
/*
 * Build the debug sysctl tree:  dev.myfirst.N.debug.*
 */
sysctl_ctx_init(&sc->sc_sysctl_ctx);

sc->sc_sysctl_tree = SYSCTL_ADD_NODE(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->sc_dev)),
    OID_AUTO, "debug",
    CTLFLAG_RW, 0, "debug and tracing controls");

SYSCTL_ADD_U32(&sc->sc_sysctl_ctx,
    SYSCTL_CHILDREN(sc->sc_sysctl_tree),
    OID_AUTO, "mask",
    CTLFLAG_RW, &sc->sc_debug, 0,
    "debug class bitmask (see myfirst_debug.h for class definitions)");
```

The detach path tears the sysctl tree down:

```c
sysctl_ctx_free(&sc->sc_sysctl_ctx);
```

### The Version Bump

At the top of the driver source, the version string and the MODULE_VERSION are updated:

```c
static const char myfirst_version[] = "myfirst 1.6-debug";

/* ...driver methods, attach, detach, etc... */

MODULE_VERSION(myfirst, 16);
```

And near the top of `myfirst_attach`, the version is logged:

```c
DPRINTF(sc, MYF_DBG_INIT, "attach: %s loaded\n", myfirst_version);
```

With the mask set to include `MYF_DBG_INIT`, the operator sees a clean version-reporting line on every attach.

### A Sample dmesg With All Classes Enabled

When all classes are enabled and the device is exercised, `dmesg` shows a comprehensive log:

```text
myfirst0: attach: myfirst 1.6-debug loaded
myfirst0: open: pid=1234 uid=1001 flags=0x0
myfirst0: open ok: count=1
myfirst0: read: pid=1234 resid=4096 off=0
myfirst0: close: pid=1234 flags=0x0
myfirst0: close ok: count=0
```

Every event the driver cares about is visible, tagged with the device name, the PID, and the relevant arguments. A field engineer receiving this log can reconstruct the sequence of events without needing source access.

With all classes disabled, `dmesg` shows only the attach line (if `MYF_DBG_INIT` is enabled) or nothing (if the mask is 0). The driver runs at full speed with no observability cost.

### A Sample DTrace Session With the Probes

The SDT probes allow machine-readable analysis:

```sh
$ sudo dtrace -n 'myfirst::: { @[probename] = count(); } tick-10s { exit(0); }'
dtrace: description 'myfirst::: ' matched 3 probes
CPU     ID                    FUNCTION:NAME
  0  49012                        :tick-10s

  close                                                     43
  open                                                      43
  io                                                       120
```

Over the 10 seconds of sampling, the driver processed 43 open/close cycles and 120 I/O events. The aggregate is generated without any modification to the driver and at essentially no runtime cost, because no script attached means the probes are inert.

### Summary of the Refactor

The 1.6-debug refactor adds roughly 100 lines of code distributed as:

- `myfirst_debug.h`: 50 lines (classes, DPRINTF, SDT declarations)
- `myfirst_debug.c`: 30 lines (SDT provider and probe definitions)
- Driver source changes: 20 lines (sysctl registration, DPRINTF calls, probe firings)

The investment is small; the return is enormous. Every subsequent chapter of the book inherits this infrastructure without additional cost, and every field issue in the driver's future gains a clear diagnostic path.

## Appendix: What the Kernel Does When a Driver Panics

A reader who has followed the chapter so far understands the debugging tools well. The final appendix covers what happens when something goes catastrophically wrong: a panic. The behaviour of the kernel during a panic is not optional for a driver author to understand, because any bug severe enough to panic produces a specific sequence of events that the driver must be correct about.

### The Panic Sequence

When any kernel code calls `panic()`, the following happens, in order:

1. The panic message is printed to the console, typically via `printf`.
2. The CPU raises its priority to the maximum, stopping most ordinary kernel activity.
3. If the kernel was built with `options KDB`, and `kdb_unattended` is not set, control transfers to the kernel debugger. The operator sees the `db>` prompt.
4. If the operator (or `kdb_unattended=1`) continues the panic, the kernel initiates a crash dump. The dump writes the current memory image to the crash-dump device (typically swap).
5. The kernel reboots (unless `panic_reboot_wait_time` is set).

On the next boot, `savecore(8)` extracts the dump from the swap device, writes it to `/var/crash/vmcore.N`, and renumbers the counter. The reader can then run `kgdb` against the dump to inspect post-mortem state.

### Driver Responsibilities

The panic path places specific requirements on drivers:

1. **Do not panic during detach.** A driver that panics during `detach` prevents clean shutdown of the rest of the kernel. Every detach path should be panic-free.

2. **Do not allocate memory in the panic handler.** If the driver registers a shutdown hook that runs during panic, the hook must not call `malloc` or any function that may sleep.

3. **Do not rely on user space during panic.** User programs are frozen during panic; the driver cannot send them messages.

4. **Do not wait for interrupts during panic.** Interrupts may be disabled during the panic path; a driver that waits for one will hang.

These rules apply to shutdown hooks and to the rare paths the kernel traverses during panic. In normal operation, drivers do not run in the panic path, but certain callbacks do.

### Reading a Crash Dump

The crash dump is a full memory image of the kernel at the moment of panic. `kgdb` opens it with full symbolic information if the kernel has debug info:

```sh
sudo kgdb /boot/kernel/kernel /var/crash/vmcore.0
```

Inside `kgdb`, the most useful commands are:

```console
(kgdb) bt            # backtrace of the thread that panicked
(kgdb) info threads  # list all threads
(kgdb) thread N      # switch to thread N
(kgdb) frame N       # show frame N (in the current backtrace)
(kgdb) list          # show source at the current frame
(kgdb) info locals   # show local variables
(kgdb) print var     # show a variable
(kgdb) print *sc     # show the softc pointer contents
(kgdb) ptype struct myfirst_softc  # show the structure type
```

Common investigation steps:

1. `bt` to see what panicked.
2. `list` to see the source line.
3. `info locals` to see the local state.
4. `print` fields of interest.
5. `thread apply all bt` to see all threads, in case the panic was triggered by one thread's interaction with another.

### Combining Kernel and Driver Symbols

If the driver is built as a module, `kgdb` needs to load its symbols too:

```console
(kgdb) add-symbol-file /boot/kernel/myfirst.ko <address>
```

The `<address>` is the load address of the module, which `kldstat -v` shows in the running system, or which appears in the panic output. Once loaded, `kgdb` can decode frames inside the module with full source visibility.

### Crash Dump Size

A full crash dump is as large as the kernel's memory usage at the time of the panic, typically gigabytes. The dump device (usually swap) must have enough space. Systems tuned for rapid panic recovery often use a dedicated dump device, not swap, to avoid swap-space constraints.

For drivers-only debugging, a mini dump captures only the kernel's own memory, not user memory. Set `dumpdev=none` or `dumpdev=mini` in `loader.conf` to tune this. Mini dumps are small (tens of megabytes) and load quickly into `kgdb`.

### Summary

The panic path is rare in production but essential to understand. A driver that handles its errors cleanly rarely triggers a panic; a driver that does panic produces a crash dump the author can analyse with `kgdb`. The combination of `INVARIANTS`, `WITNESS`, `DEBUG_MEMGUARD`, and a functioning crash-dump pipeline makes panics into valuable diagnostic events rather than catastrophes.

## Appendix: Chapter 23 at a Glance

This single-page summary is intended as a recap the reader can keep open while they work. Each entry names a concept, points to where it was introduced, and gives the minimal command or code to exercise it.

### Tools in order of first use

1. **`printf` / `device_printf`** (Section 2): Basic kernel logging. Use `device_printf(dev, "msg\n")` inside driver methods.
2. **`log(LOG_PRIORITY, ...)`** (Section 2): Syslog-routed logging, with a priority level. Use for messages that belong in `/var/log/messages`.
3. **`dmesg`** (Section 3): Read the kernel message buffer. Combine with `grep` and `tail` for filtered output.
4. **`/var/log/messages`** (Section 3): Persistent log history. `syslogd(8)` writes to it; `newsyslog(8)` rotates it.
5. **Debug kernel** (Section 4): Rebuild with `KDB`, `DDB`, `INVARIANTS`, `WITNESS`, `KDTRACE_HOOKS`, and `DEBUG=-g`. Slower but diagnostic.
6. **`ddb`** (Section 4): Interactive kernel debugger. Enter via `sysctl debug.kdb.enter=1` or a console break. Use `bt`, `show locks`, `ps`, `continue`.
7. **`kgdb`** (Section 4): Post-mortem debugger. Open `/boot/kernel/kernel` and `/var/crash/vmcore.N` together.
8. **DTrace** (Section 5): Live tracing and measurement. `dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'`.
9. **SDT probes** (Section 5, Section 8): Static trace points in the driver source. `dtrace -n 'myfirst::: { @[probename] = count(); }'`.
10. **`ktrace`/`kdump`** (Section 6): User-side syscall tracing. `sudo ktrace -di -f trace.out ./program; kdump -f trace.out`.
11. **`vmstat -m`** (Section 7.1): Per-pool kernel memory view. Used to detect leaks.

### Reflexes to build

- Add a `device_printf` before you suspect a bug, not after.
- Check `dmesg` before guessing about driver behavior.
- Rebuild with `INVARIANTS` and `WITNESS` during development.
- Write DTrace one-liners instead of compile-reload cycles when the question is "does this fire?".
- Maintain `vmstat -m` stability as a continuous invariant, not an after-the-fact check.
- Never hold a sleep lock across `uiomove` or any sleeping call.
- Make interrupt handlers short. Push work to `taskqueue`.
- Match every `malloc` with a `free`. Test the match with the tools.

### The 1.6-debug refactor

- Adds `myfirst_debug.h` with 8 verbosity classes and the DPRINTF macro.
- Adds a `sysctl dev.myfirst.0.debug.mask` for runtime control.
- Adds three SDT probes: `myfirst:::open`, `myfirst:::close`, `myfirst:::io`.
- Follows the entry/exit/error pattern across every method.
- Bumps `myfirst_version` to `1.6-debug` and `MODULE_VERSION` to 16.

### Recommended next steps

- Complete all five labs if time allows.
- Attempt Challenge 23.2, the custom DTrace script.
- Keep a personal lab logbook entry for this chapter.
- Read `/usr/src/sys/dev/ath/if_ath_debug.h` as a real-world reference.
- Before starting Chapter 24, confirm the driver is at version `1.6-debug` and the debug mask is responsive.

This concludes the chapter's reference material. The "Wrapping Up" and bridge sections that follow prepare the reader for Chapter 24.

## Wrapping Up

Chapter 23 introduced the working methods and tools of FreeBSD kernel debugging. The chapter's structure followed a deliberate arc: explain why kernel debugging is different, teach the core logging and inspection tools, build a debug kernel, master DTrace and ktrace, recognise the common bug classes, and finally refactor the `myfirst` driver to support its own observability.

Along the way the chapter has earned its place in the book. It sits between the architectural work of Part 4 and the integration work of Part 5 because the remaining chapters will add substantial new code to `myfirst` (devfs hooks, ioctl, sysctl, network interfaces, GEOM interfaces, USB support, virtio support) and each of those chapters depends on the ability to see what the driver is doing and to fix problems as they arise. The tooling and instrumentation are not an afterthought; they are how the rest of the book stays manageable.

The driver itself is now at version `1.6-debug`. Its softc carries an `sc_debug` mask; the tree has a `sysctl dev.myfirst.0.debug` subtree; the DPRINTF macro gates logging by class; and three SDT probes expose the open, close, and I/O events. Every later chapter in Parts 5, 6, and 7 will extend the same framework: new classes will appear in `myfirst_debug.h`, and new probes will be declared as the driver acquires new functional boundaries.

### What the reader can now do

- Use `device_printf`, `log`, and the `DPRINTF` macro pattern to log structured kernel events.
- Read `dmesg` and `/var/log/messages` to locate driver messages and correlate them with events.
- Build a debug kernel with `KDB`, `DDB`, `INVARIANTS`, `WITNESS`, and `KDTRACE_HOOKS`.
- Write DTrace one-liners that count, aggregate, and time kernel events.
- Use `ktrace` and `kdump` to observe user-side interactions with the driver.
- Recognise the most common driver bugs and match them to the right debug tool.
- Declare SDT probes and expose them to DTrace scripts.
- Bump the driver's version and carry the debug infrastructure forward.

### What lies ahead

With the debug infrastructure in place, Chapter 24 (Integrating with the Kernel: devfs, ioctl, and sysctl) will extend the driver's interface to user space. The reader will create richer `cdev` entries, define custom ioctls, and register tuning sysctls for the driver's runtime parameters. Every one of those additions can now be instrumented and traced with the framework from this chapter, so the work will be grounded in visible, debuggable behaviour from the first line.

The debug arsenal built in Chapter 23 will see heavy use from here on. In Chapter 24 the reader will add their first real ioctl, and the first time an argument arrives with the wrong value, they will turn on `MYF_DBG_IOCTL`, read the log, and find the mistake in seconds rather than hours. In Chapter 25 (Writing Character Drivers in Depth), the reader will use DTrace to confirm that the driver's select/poll path is firing correctly. In Chapter 26 and beyond, each new subsystem hook will be logged through DPRINTF and probed with SDT. The debug chapter's return on investment is long and steady.

A final encouragement: the tools of this chapter are not glamorous. They do not produce working drivers on their own. But they are what turns a frustrating six-hour bug into a crisp twenty-minute diagnosis. Every experienced FreeBSD developer has, on at least a dozen occasions, traced a problem through `vmstat -m`, then DTrace, then the source code, and emerged with a precise fix. The reader who masters the toolbox of this chapter joins that group and is now ready for the richer integration work that follows.

## Bridge to Chapter 24

Chapter 24 (Integrating with the Kernel: devfs, ioctl, and sysctl) opens the next arc of the book. Up to Chapter 23 the driver has been a single `cdev` node with fixed behaviour. Chapter 24 makes the interface expressive: readers will extend the `cdev` into richer devfs entries, define custom ioctls to configure the device, and register sysctl nodes for runtime parameters.

The bridge is direct. The debug mask introduced in Section 8 is a sysctl. The ioctls in Chapter 24 will be traced through `MYF_DBG_IOCTL`. The custom devfs nodes will log their creation and destruction through `MYF_DBG_INIT`. When the reader builds new features in Chapter 24, the debug framework is ready to observe them.

See you in Chapter 24.

## Reference: Chapter 23 Quick-Reference Card

The table below summarises the tools, commands, and patterns introduced in Chapter 23 for quick lookup.

### Logging

| Call                              | Use                                                            |
|-----------------------------------|----------------------------------------------------------------|
| `printf("...")`                   | Very basic logging; no device identity.                        |
| `device_printf(dev, "...")`       | Standard driver log; prefixes with device name.                |
| `log(LOG_PRI, "...")`             | Syslog with priority; sent to `/var/log/messages`.             |
| `DPRINTF(sc, CLASS, "...")`       | Conditional on `sc->sc_debug & CLASS`; standard driver macro.  |

### Syslog priority levels

| Priority     | Meaning                                                         |
|--------------|-----------------------------------------------------------------|
| `LOG_EMERG`  | System is unusable.                                             |
| `LOG_ALERT`  | Action must be taken immediately.                               |
| `LOG_CRIT`   | Critical conditions.                                            |
| `LOG_ERR`    | Error conditions.                                               |
| `LOG_WARNING`| Warning conditions.                                             |
| `LOG_NOTICE` | Normal but significant condition.                               |
| `LOG_INFO`   | Informational.                                                  |
| `LOG_DEBUG`  | Debug-level messages.                                           |

### Kernel options for debugging

| Option                       | Effect                                                          |
|------------------------------|-----------------------------------------------------------------|
| `options KDB`                | Includes the kernel debugger framework.                         |
| `options DDB`                | Interactive debugger UI on console.                             |
| `options DDB_CTF`            | CTF support in DDB for type-aware printing.                     |
| `options KDB_TRACE`          | Automatic backtrace on entry to KDB.                            |
| `options KDB_UNATTENDED`     | Panics reboot instead of dropping into DDB.                     |
| `options INVARIANTS`         | Enables `KASSERT`, `MPASS`, and other kernel assertions.        |
| `options WITNESS`            | Tracks lock order and panics on violations.                     |
| `options WITNESS_KDB`        | Drops to KDB on a WITNESS violation.                            |
| `options DEBUG_MEMGUARD`     | Poisons freed memory; catches use-after-free.                   |
| `options KDTRACE_HOOKS`      | Enables DTrace probes.                                          |
| `options KDTRACE_FRAME`      | Produces frame pointers for DTrace stack walks.                 |
| `makeoptions WITH_CTF=1`     | Generates CTF in the kernel and modules.                        |
| `makeoptions DEBUG=-g`       | Includes full DWARF in the kernel and modules.                  |

### Common DTrace one-liners

```sh
# Count every function entry in the driver
dtrace -n 'fbt::myfirst_*:entry { @[probefunc] = count(); }'

# Measure the time spent in a specific function
dtrace -n 'fbt::myfirst_read:entry { self->t = timestamp; }
           fbt::myfirst_read:return /self->t/ {
               @ = quantize(timestamp - self->t); self->t = 0; }'

# Count SDT probes fired by the driver
dtrace -n 'myfirst::: { @[probename] = count(); }'

# Show syscall frequency for a named process
dtrace -n 'syscall:::entry /execname == "myftest"/ { @[probefunc] = count(); }'

# Aggregate I/O sizes through the driver
dtrace -n 'myfirst:::io { @ = quantize(arg2); }'
```

### ktrace / kdump workflow

```sh
# Record a process under ktrace
sudo ktrace -di -f trace.out ./myprogram

# Dump in human-readable form
kdump -f trace.out

# Filter for syscalls only
kdump -f trace.out | grep -E 'CALL|RET|NAMI'
```

### Debugging checklist (abbreviated)

1. Is `dmesg` clean? If not, read the last message before the problem.
2. Is `vmstat -m` stable for the driver's pool? If not, look for a leak.
3. Does `WITNESS` panic on the workload? If yes, the lock order is wrong.
4. Do the `fbt::myfirst_*:entry` counts match the expected activity? If not, check the paths.
5. Does the user-side `ktrace` show the expected syscall sequence? If not, check the ioctl or command setup.
6. Is the driver at the expected version? `kldstat -v | grep myfirst` and confirm `MODULE_VERSION`.

## Reference: Glossary of Chapter 23 Terms

**CTF**: Compact C Type Format. A kernel type-information format that DDB and DTrace use to print typed structures. Requires `makeoptions WITH_CTF=1`.

**DDB**: The built-in FreeBSD kernel debugger. Interactive, console-based. Activated by `options DDB`.

**DDB_CTF**: A DDB option that lets the debugger print typed values using CTF type information.

**device_printf**: The FreeBSD-standard logging function for driver messages. Prefixes output with the device's name.

**DPRINTF**: The conventional driver macro for conditional logging, gated on a verbosity mask.

**DTrace**: A dynamic tracing framework. Uses `fbt`, `syscall`, `sdt`, `io`, `sched`, `lockstat`, and other providers.

**fbt**: Function Boundary Tracing. A DTrace provider that traces entry and return of every kernel function.

**INVARIANTS**: A kernel build option that enables `KASSERT`, `MPASS`, and related assertions. Standard for debug builds.

**KASSERT**: An assertion macro that evaluates only when `INVARIANTS` is set. Panics if the condition is false.

**KDB**: The kernel debugger framework. `DDB` is one backend.

**KDTRACE_HOOKS**: The kernel build option that enables DTrace probes.

**kdump**: The user-space tool that reads `ktrace` output files and prints them in human-readable form.

**ktrace**: The user-space tool that records syscalls and other events of a given process.

**log**: The kernel function for sending messages to syslog with a priority level.

**MPASS**: Similar to `KASSERT` but with a compile-time-only check. Zero cost in release builds.

**myfirst_debug.h**: The debug header introduced in Section 8, declaring verbosity classes and SDT probe structures.

**sc_debug**: The `uint32_t` field in the driver's softc that controls which `DPRINTF` categories are active.

**SDT**: Statically Defined Tracing. Compile-time probes that DTrace can attach to.

**SDT_PROBE_DEFINE**: Macro family that registers a new SDT probe in the kernel or driver.

**syslog**: The BSD logging subsystem; message priorities map to destinations per `/etc/syslog.conf`.

**vmstat -m**: A user-space tool that shows per-pool kernel memory statistics.

**WITNESS**: A kernel lock-order verification system. Panics on lock-order violations when `options WITNESS` is set.

**1.6-debug**: The driver version after Section 8. Carries the verbosity mask, sysctl, DPRINTF pattern, and SDT probes.
