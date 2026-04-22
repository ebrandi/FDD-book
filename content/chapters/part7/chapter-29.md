---
title: "Portability and Driver Abstraction"
description: "Creating portable drivers across different FreeBSD architectures"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 29
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 270
---

# Portability and Driver Abstraction

## Introduction

You arrive at Chapter 29 with a particular kind of experience. You have written three drivers that look very different on the surface. In Chapter 26 you connected a character device to `cdevsw` and taught `/dev` how to reach your code. In Chapter 27 you fed blocks into GEOM and let a filesystem sit on top. In Chapter 28 you shaped a network interface, registered it with the stack, and watched packets flow back and forth. Each of those drivers was cut to fit a specific subsystem, and each one taught you a specific contract with the FreeBSD kernel. If you read the three side by side, you would notice that they also share a great deal: a softc, a probe and attach path, resource allocation, teardown, careful handling of concurrency, and attention to the load and unload lifecycle.

This chapter is about that shared skeleton. More importantly, it is about what happens to that skeleton when the rest of the world changes around it. The machine you built your driver on today is unlikely to be the only machine that ever runs it. A driver written for an x86 workstation in 2026 might be compiled for an ARM server in 2027, for a RISC-V embedded board in 2028, and for a FreeBSD 15 release in 2029. Along the way, the hardware it talks to might change from a PCI card to a USB dongle to a simulated device in `bhyve`. The kernel API might evolve. The surrounding system might change from FreeBSD to NetBSD for one downstream consumer. The code that started small and single-purpose has to become something that survives that turbulence with minimal surgery.

That survival is what we mean by **portability**. Portability is not one property. It is a small family of related properties. It includes the ability to be compiled on a CPU architecture that you did not originally target. It includes the ability to attach to a different physical bus, such as PCI one day and USB another. It includes the ability to switch between a real device and a simulated stand-in during testing without rewriting half the file. It includes the ability to coexist with kernel API churn, so that a rename or a deprecation in a future release of FreeBSD does not force you to throw away your driver. And, occasionally, it includes the ability to share code with cousins of FreeBSD like NetBSD and OpenBSD without turning the whole codebase into a compatibility maze.

Chapter 29 teaches you how to design, structure, and refactor a driver so that each of those kinds of change becomes a local problem rather than a global rewrite. The chapter is not about a new subsystem. You have already met them. It is about the engineering discipline that allows a driver to age gracefully. It is the part of driver authorship that does not show up on the first day, when the module loads cleanly and the device responds, but that matters enormously on day one thousand, when the same module has to ship on three architectures, two buses, and four FreeBSD releases while staying readable to new contributors.

We will not invent a new driver from scratch. Instead, we will take a driver shape that is familiar to you and evolve it. You will learn how to separate hardware-dependent code from logic that does not care about hardware. You will learn how to hide a physical bus behind a backend interface, so that a PCI variant and a USB variant can share the same core. You will learn how to split a single `.c` file into a small family of files that each have a clear responsibility. You will learn how to use the kernel's endian helpers, fixed-width types, and `bus_space`/`bus_dma` abstractions so that your register accesses work on little-endian and big-endian machines alike, and on 32-bit as well as 64-bit word sizes. You will learn how to use conditional compilation without turning the source into a thicket of `#ifdef`. And you will learn how to step back and version the driver so that users of your code know what it supports and what it does not.

Through all of this, the tone remains the same as in earlier chapters. Portability looks abstract when you read about it in a textbook, but it is concrete when you sit in front of a driver source file and decide which function goes where. We will stay close to that second view. Every pattern you meet will be tied to a real FreeBSD practice, and often to a real file under `/usr/src/` that you can open and read alongside the text.

Before you begin, resist the temptation to read this chapter as a set of rules. It is not. It is a set of **habits**. Each habit exists because a future version of you will thank the current version of you for having formed it. A driver that never has to be ported will still benefit from being written as if it might be, because the same habits that help portability also help readability, testability, and long-term maintenance. Do not treat this chapter as optional polish. Treat it as the part of craft that separates a driver that works from a driver that keeps working.

## Reader Guidance: How to Use This Chapter

This chapter is a little different from the three that came before it. Chapters 26, 27, and 28 were each built around a concrete subsystem with a concrete API. Chapter 29 is built around a way of thinking. You will still read code, and you will still do labs, but the subject is how driver code is **organised**, not which kernel function it calls. That distinction affects the best way to study it.

If you choose the **reading-only path**, plan for roughly two to three focused hours. By the end you will recognise the common structural patterns that mature FreeBSD drivers use to stay portable and will know what to look for when you open an unfamiliar driver source tree. This is a legitimate way to use the chapter on a first pass, because the ideas here become more useful once you have seen them in action in your own work.

If you choose the **reading-plus-labs path**, plan for about five to eight hours across one or two sessions. You will take a small reference driver and progressively refactor it into a portable, multi-backend shape. You will split a single file into a core and two backend files, you will introduce a backend interface, and you will make the driver compile cleanly with and without optional backends enabled. The labs are deliberately incremental so that each step stands on its own and leaves you with a working module rather than a broken one.

If you choose the **reading-plus-labs-plus-challenges path**, plan for a weekend or a few evenings. The challenges push the refactoring further: adding a third backend, exposing version metadata to userland, simulating a big-endian environment, and writing a build matrix that proves the driver compiles in every documented configuration. Each challenge stands alone and uses only material from this chapter and the earlier ones.

You should continue to work on a throwaway FreeBSD 14.3 machine, as in earlier chapters. The labs do not require special hardware, a complicated bus, or a particular NIC. They work on a plain virtual machine with the source tree installed, a working compiler, and the usual kernel module build tools. A snapshot before you begin is cheap, and the only thing you need to undo a mistake is the ability to reboot.

A word on prerequisites. You should be comfortable with everything from Chapters 26, 27, and 28: writing a kernel module, allocating a softc, managing the probe and attach lifecycle, registering with `cdevsw`, GEOM, or `ifnet` as appropriate, and reasoning about the load and unload path. You should also be comfortable with `make` and with the FreeBSD `bsd.kmod.mk` build system, since a good deal of the chapter is about how to split and compile a multi-file driver. If any of that feels uncertain, a quick skim of the earlier chapters will save you time here.

Finally, do not skip the troubleshooting section near the end. Portability refactors fail in a handful of characteristic ways, and learning to recognise those patterns early is more useful than memorising which macro lives in which header. A portable driver that mostly works is worse than a non-portable one that definitely works, because the portability gives you a false sense of confidence. Treat the troubleshooting material as part of the lesson.

### Work Section by Section

The chapter is structured as a deliberate progression. Section 1 defines what portability really means for a driver, and why we care. Section 2 walks through isolating hardware-dependent code from logic that is not. Section 3 teaches how to hide backends behind an interface so that the core driver does not need to know whether it is talking to a PCI card or a USB dongle. Section 4 organises those pieces into a clear file layout. Section 5 deals with architectural portability: endianness, alignment, and word size. Section 6 teaches disciplined use of conditional compilation and build-time feature selection. Section 7 takes a brief look at cross-BSD compatibility, enough to be useful without turning the chapter into a NetBSD porting manual. Section 8 steps back and addresses refactoring and versioning for long-term maintenance.

You are meant to read these sections in order. Each one assumes the previous ones are fresh in your mind, and each lab builds on the results of the one before it.

### A Gentle Warning About Over-Engineering

Before we dive in, a word of caution. Portability patterns can become an end in themselves if you are not careful. It is possible to build a driver so layered, so abstracted, and so wrapped that no one, including you, can follow what actually happens on a given code path. The goal of this chapter is not to maximise abstraction; it is to choose the right level of abstraction for the driver at hand.

For a small driver with one backend and no prospect of a second one, the simplest portable design is a single `.c` file that uses `bus_read_*`, endian helpers, and fixed-width types throughout. No backend interface. No file split. The investment stops there, and that is enough. The refactor to a multi-backend layout can happen later, when the need appears.

For a driver that already supports two or three variants, the full multi-file layout earns its keep, and the refactor to get there saves more time than it costs. For a driver that might one day support additional variants but does not yet, the middle ground is a single file with clean internal separation (a backend interface as a struct, even if only one backend is instantiated), ready to be split into multiple files when the second backend arrives.

Matching the complexity of the design to the complexity of the problem is the craft. Read this chapter with that in mind. Not every pattern in it belongs in every driver. The patterns belong where they pay for themselves.

### Keep the Reference Driver Close

The labs in this chapter centre on a small reference driver called `portdrv`. You will find it under `examples/part-07/ch29-portability/`, staged in the same way as the examples from earlier chapters. Each lab directory contains the state of the driver at that step, along with the Makefile and any support files needed to compile and test it. Do not just read the lab; type the changes, build the module, and load it. The feedback loop is tight because refactoring either compiles or it does not, and running the module after each step confirms that you did not accidentally break the driver while moving code around.

### Open the FreeBSD Source Tree

Several sections in this chapter point to real FreeBSD drivers as concrete examples of portable structure. The files of interest include `/usr/src/sys/dev/uart/uart_core.c`, `/usr/src/sys/dev/uart/uart_bus_pci.c`, `/usr/src/sys/dev/uart/uart_bus_fdt.c`, `/usr/src/sys/dev/uart/uart_bus_acpi.c`, `/usr/src/sys/dev/uart/uart_bus.h`, `/usr/src/sys/modules/uart/Makefile`, `/usr/src/sys/sys/_endian.h`, `/usr/src/sys/sys/bus_dma.h`, and the per-architecture bus headers under `/usr/src/sys/amd64/include/_bus.h`, `/usr/src/sys/arm64/include/_bus.h`, `/usr/src/sys/arm/include/_bus.h`, and `/usr/src/sys/riscv/include/_bus.h`. Open them as the text points to them. Their shape is half the lesson.

### Keep the Lab Logbook

Continue the lab logbook you started in Chapter 26. For this chapter, log a short entry at the end of each lab: which files you edited, which build options you enabled, which backends compiled, and any warnings the compiler produced. Portability is easy to claim and hard to verify, and a paper trail of your experiments is the fastest way to notice when you have quietly regressed.

### Pace Yourself

If your understanding blurs during a particular section, stop. Re-read the previous subsection. Try a small experiment, for example recompiling the driver with one backend disabled and confirming that it still builds. Portability refactors reward slow, deliberate steps. A hurried split that leaves a function in the wrong file is harder to fix than an unhurried one that places it correctly the first time.

## How to Get the Most Out of This Chapter

Chapter 29 is not a reference list. It is a workshop about structure. The most effective way to use it is to keep the driver under your hands while you read, and to ask of every pattern you meet: what problem does this solve, and what would happen if the problem did not exist? That question is the fastest path to internalising the habits.

### Refactor in Small Steps

Do not try to go from a single-file driver to a fully portable, multi-backend, cross-BSD-compatible architecture in one sitting. The labs are broken into discrete steps for a reason. Each step makes exactly one change to the code and confirms that the module still loads cleanly after it. That discipline mirrors real-world practice. Professional driver refactors are rarely large, dramatic rewrites; they are a long sequence of small, reviewable moves, each of which can be reverted if something goes wrong.

### Read the Compiler Output

The compiler is your collaborator during a portability refactor. A missed include, a stale prototype, a function moved to a file that does not list it in `SRCS`, these become compiler errors the moment you build, and the messages are precise if you read them carefully. When the build fails, resist the urge to guess; open the file the compiler is pointing at and fix the specific symbol it cannot find. Over a chapter's worth of labs, you will develop a feel for how the build system reacts to different kinds of reorganisation.

### Use Version Control as a Safety Net

Commit before each refactoring step. The project's git history gives you a zero-cost way to step back to a known good state. If a split goes wrong, reverting the change is a single command, not an hour of hunting for what you broke. You do not need to push these commits anywhere; they are for you.

### Compare Against Real FreeBSD Drivers

When the chapter points to a real file under `/usr/src/sys/`, open it. The `uart(4)` driver in particular is a superb study in backend abstraction: its core logic in `uart_core.c` knows nothing about the bus, and its backend files add PCI, FDT, and ACPI attachment with a small amount of code each. Reading those files alongside this chapter will teach you what "good" looks like far better than any synthetic example could.

### Take Notes on Patterns, Not Syntax

The specific macros and function names in this chapter are less important than the patterns behind them. A backend interface defined as a struct of function pointers is a pattern you will use for the rest of your career; the particular spelling of the functions in `portdrv` is a one-time lesson. Write down the patterns in your logbook. Revisit them when you design your own drivers.

### Trust the Build System

FreeBSD's `bsd.kmod.mk` is mature, and it does a great deal of work for you. A multi-file driver is not significantly more complicated to build than a single-file driver; you add a line to `SRCS` and the build keeps working. Lean on that. Do not write your own Makefile machinery when the existing one does the job.

### Take Breaks

Refactoring work has a particular cognitive cost. You are holding two versions of the code in your head at once, the one before the move and the one after, and your mind gets fatigued more quickly than during new development. Two or three focused hours are usually more productive than an uninterrupted seven-hour sprint. If you notice yourself copy-pasting without reading, stand up for ten minutes.

With those habits in place, let us begin.

## Section 1: What Does "Portable" Mean for a Device Driver?

Portability is one of those words that everyone uses and few people pin down. When someone says a driver is portable, they might mean any of half a dozen different things. Before we can talk about how to achieve it, we need to be clear about what we are actually trying to achieve. This section takes that step carefully, because every later section depends on sharing the same vocabulary.

### A Working Definition

For this book, a driver is portable when changing one of the following does not force a change in the driver's core logic:

- the CPU architecture it is compiled for
- the bus through which it reaches the hardware
- the specific revision of the hardware behind the bus
- the version of FreeBSD it is running on, within reasonable limits
- the physical deployment, such as real hardware versus a simulator

Notice that this definition is negative. It does not say that a portable driver runs everywhere without modification, because no driver does. It says that when any of these elements change, the change is **local**. The core logic stays where it was. The hardware-specific or platform-specific code absorbs the change. That locality is the practical meaning of portability, and everything in this chapter is about how to arrange the code so that change is local.

A driver that is not portable will usually reveal itself in three ways. First, a small change in the environment produces diffs scattered across the entire file. Second, the author has to read the whole file to know where to make a change, because information about the bus or the architecture is sprinkled throughout the logic. Third, adding support for a new backend requires either duplicating the entire driver or carving `#ifdef` blocks through every function. If you have ever tried to support a second variant of a device this way, you already know how fragile the result becomes.

### Portability Across Architectures

FreeBSD runs on a wide variety of CPU architectures, and all of them can load kernel modules. The ones you are most likely to encounter in practice are `amd64` (the 64-bit x86 family), `i386` (legacy 32-bit x86), `arm64` (the 64-bit ARM family), `armv7` (the 32-bit ARM family), `riscv` (both 32-bit and 64-bit RISC-V), and `powerpc` and `powerpc64` for older and server-class PowerPC systems. A driver that is portable across architectures compiles and runs correctly on all of those without the author having to know anything specific about, say, `amd64`'s calling conventions or `arm64`'s alignment rules.

The CPU architectures differ along several axes that a driver author must take seriously. They differ in **endianness**: most of the common ones today are little-endian, but `powerpc` and `powerpc64` can be configured as big-endian and are historically big-endian. They differ in **word size**: 32-bit and 64-bit systems have different natural integer widths, which matters when you store an address in a variable. They differ in **alignment**: `amd64` will forgive an unaligned 32-bit access, but `arm` on some cores will fault. They differ in the physical layout of memory and the behaviour of memory barriers, but those are issues that the kernel's synchronisation primitives already solve for you if you use them correctly.

A driver written carelessly will not compile at all on some of these architectures, or worse, will compile but misbehave. A driver written with a little care compiles cleanly everywhere and does the same thing everywhere. The difference between those two outcomes is almost always a matter of using the right types, the right endian helpers, and the right accessor functions, rather than a matter of deep kernel knowledge.

### Portability Across Buses

The second axis of portability is the bus through which the driver talks to the hardware. The classical FreeBSD driver talks to a PCI device. But the same hardware function can appear behind other buses too: on USB, on a platform-specific memory-mapped region attached via Device Tree (FDT) on embedded systems, on a Low Pin Count or SMBus interface, or on nothing at all when the device is simulated in software. The `/usr/src/sys/dev/uart/` tree is a lovely example: `uart_bus_pci.c`, `uart_bus_fdt.c`, `uart_bus_acpi.c`, and several smaller files each teach the shared UART core how to be reached over a particular bus, and the core itself, in `uart_core.c`, does not care which one was used.

When a driver is portable across buses, adding a new bus is a matter of writing one small file that translates between the bus API and the driver's internal API, then listing that file in the kernel's build system. The core does not need to change. If you have a PCI-based variant of a device today and the same silicon turns up soldered to a board tomorrow, your driver picks up the new attachment in a few hundred lines rather than a few thousand.

### Portability Across Systems

The third axis of portability is cross-system: can your driver be shared with other operating systems, especially the other BSDs? This is the form of portability that matters least for most FreeBSD-only projects and the one that consumes the most effort when it does matter, which is why we will treat it lightly in this chapter. The short answer is that NetBSD and OpenBSD share a great deal of history with FreeBSD, and a driver that is carefully written in the FreeBSD dialect can often be translated to those systems with a modest amount of work. A driver written carelessly, with FreeBSD assumptions scattered throughout, usually cannot be translated without a major rewrite.

For our purposes, cross-BSD compatibility means writing your driver so that FreeBSD-specific conventions are introduced in a small number of well-known places and can be wrapped or replaced when needed. You will see that pattern in Section 7.

### Portability Across Time

A fourth form of portability is subtler but matters enormously over the life of a project: portability across time. FreeBSD is a living system. APIs evolve, macros are renamed, and older conventions give way to new ones. The `if_t` opaque handle for network interfaces, for example, replaced the raw `struct ifnet *` pointer across FreeBSD 13 and 14; drivers that used the opaque API continued to compile, while drivers that used the raw structure had to be updated. A driver that is careful about which APIs it uses, which headers it includes, and how it detects the kernel version it is compiled against, will survive those evolutions with little more than a version check at the top of a few files.

You will meet the `__FreeBSD_version` macro shortly. Think of it as a version stamp that the kernel itself carries, and that your driver can test against to adapt to the API surface of the specific release it is built on. Used sparingly, it is a quiet, effective tool. Used profligately, it turns the source into a patchwork.

### Portability Across Deployments

A final, often forgotten, form of portability is portability between real hardware and a simulator. In Chapter 17 you learned how valuable it is to be able to test a driver without the real device present. A driver whose backend can be switched at build time between "the real device" and "a simulated stand-in" is much easier to develop, review, and test than one that can only run against the true hardware. This form of portability is the cheapest to achieve, because it costs only a small amount of up-front design, and it pays for itself every time a bug is caught on a CI runner that does not have the hardware.

### Why Portability Matters

It is tempting to treat portability as a nice-to-have, something you will address "later" when the driver stabilises. In practice, portability is cheapest when it is designed in from the start, and most painful when it is bolted on after the driver has grown to ten thousand lines. There are three concrete reasons portability earns its keep.

First, hardware varies. The device you support today is rarely the only device you will ever support. Silicon vendors produce variants. A sensor that lived on I2C last year may appear over USB this year and over PCIe the year after, with the same programming model underneath. If your core logic is hardware-independent, those variants are cheap to support. If it is not, they are expensive.

Second, platforms evolve. A driver that ships on `amd64` this year may need to run on `arm64` next year as embedded deployments grow. A driver that assumes little-endian memory will misbehave on a big-endian target, and the bugs will be subtle because most of the time the code looks right. Arranging the driver so that endianness is handled in a small number of obvious places means the move to a new architecture is a one-afternoon exercise, not a one-month forensic investigation.

Third, time passes. A long-lived driver outlives its author. A driver that is structured cleanly can be maintained by a newcomer who has no context. A driver that is a tangle of conditional blocks and hardware assumptions cannot, and tends to be rewritten from scratch by the next maintainer. The accumulated cost of those rewrites, measured over the life of a project, is far higher than the cost of writing the original driver in a portable style.

### Code Duplication vs Modular Design

Before we go further, it is worth naming a temptation that every driver author faces: duplication. When a second variant of a device appears, the fastest way to support it is to copy the existing driver and change the parts that are different. This works, in the short term. It is also one of the most expensive decisions a project can make, because every bug found in one copy must be found in the other, every improvement must be duplicated, and every security issue must be patched twice. After the third or fourth variant, the project has become unmaintainable.

The alternative is modular design. The parts that are the same live in one place. The parts that are different live in small, backend-specific files. The core is compiled once. Each backend is compiled separately and linked against the core. When a bug is fixed in the core, all variants benefit. When a new variant appears, only a small file needs to be added. This is the approach FreeBSD drivers in the mainline tree overwhelmingly adopt, for the same reasons.

Chapter 29 teaches you to recognise where duplication is creeping in and how to pull it apart before it calcifies. That skill pays dividends for the rest of your career in systems programming.

### A Tale of Two Drivers

Before closing this section, it helps to compare two hypothetical drivers that a team might write for the same device, one with portability in mind from the start and one without. The story is composite, but every scenario in it has occurred in real FreeBSD projects, usually more than once.

Driver A is written in a hurry to support a single PCI card on `amd64`. The author writes every register access as a direct call to `bus_read_4(sc->sc_res, offset)`. The PCI probe, the attach function, the ioctl switch, and the register dumping code all live in a single file of about twelve hundred lines. The author knows about endianness in theory but assumes the host is little-endian because the target is `amd64`. The driver ships, works, and passes its first round of customer testing. The team declares victory.

Six months later, a second customer requests support for a USB variant of the same device. The programming model is identical; the mechanism is different. The author opens Driver A and considers the options. Option one is to add a second attach path and scatter `if (sc->sc_is_usb)` branches through the file; the author quickly sees that this means touching more than a hundred functions, each of which would grow a new branch. Option two is to copy Driver A to Driver A'-USB and change the parts that differ. The author takes option two because it is faster. Now there are two drivers. Every bug fix must be applied twice, once to each copy, and after the third such fix the team notices they have already forgotten one of them. A maintenance tax quietly begins to accumulate.

A year after that, a third customer wants the same silicon supported on an `arm64` board. The PCI driver is almost correct on `arm64`, but the endianness of a specific register was wrong, and the author did not notice because they had never built on a big-endian platform. The USB driver works, but only after the author realises that a structure they had cast a `volatile` pointer to was not aligned on `arm64` and was producing bus faults. They add a third copy, Driver A''-USB-ARM, with fixes for the alignment and the endianness. The team now maintains three drivers. The ratio of bugs to fixes has climbed. The total lines of code have tripled, but the shared behaviour has not.

Driver B is written by a different team at the same time, for the same device. The author begins by drawing the line between hardware-dependent and hardware-independent code in the first hour, before writing any logic. They create a backend interface with four operations: `attach`, `detach`, `read_reg`, and `write_reg`. They implement a PCI backend first, and write the core against the interface. They also implement a simulation backend during development, because it is easier to test the core without needing a real card. The driver ships at about eighteen hundred lines, spread across six files.

Six months later, the USB request arrives. The author writes a `portdrv_usb.c` file of about three hundred lines, adds one line to the Makefile, and ships. The core is unchanged. The USB backend is reviewed in isolation, because it does not touch any other file. The bug fixes to the core benefit both backends automatically.

A year after that, the `arm64` request arrives. The author builds on `arm64`, notices one warning from the compiler about a signed comparison with `uint32_t`, fixes the warning, and ships. The endianness is handled because every multi-byte value on a register boundary went through `le32toh` or `htole32` from day one. The alignment is handled because the author used `bus_read_*` and `memcpy` rather than raw pointers. The `arm64` port is a one-day job, not a month.

Two different teams, same device, same time, same customers. At the end of eighteen months, Driver A has three copies, two thousand lines of drift between them, and a steady trickle of bugs that affect one copy and not the others. Driver B has one copy, one backend interface, and a changelog that reads like a straight line. The teams that wrote them are by now worth different amounts to the project.

The moral is simple but easy to forget: **the cheap path at the start of a driver is often the expensive path over its lifetime**. Portability is an investment that pays dividends, but only if you make the investment early. Retrofitting portability into a driver that has already grown to several thousand lines is possible and sometimes necessary, but it takes more time than writing the driver from scratch in the portable style. This is not an argument for over-engineering; it is an argument for making the right small moves in the first week, before the driver has time to calcify.

### The Cost of Deferring Portability

The "we'll add portability later" argument sounds reasonable and is almost always wrong. There are three specific costs that a late refactor pays and an early design does not.

**The detection cost.** Portability bugs in a driver that was never tested portably are invisible. They lie in wait until the day someone tries to build on a new platform or a new release. By that time, other work depends on the driver, and a fix that touches a core assumption can cascade through the project. An early investment in portable patterns means the bugs are caught in the first week, not the third year.

**The reviewer cost.** A driver that mixes hardware-dependent code with core logic is harder to review, because every change is potentially a change to the architectural abstraction. Reviewers either slow down or, more commonly, start approving patches without fully understanding them. The project's quality silently decays. A cleanly split driver gives reviewers a clear signal: a patch in the core deserves scrutiny because it affects every backend; a patch in a backend file is local and can be approved more quickly.

**The contributor cost.** New contributors are scared by tangled code. A driver that has obvious structure, where new features look like additions to an existing pattern, attracts contributions. A driver that is a wall of nested conditions repels them. Over the life of a project, this is the difference between a healthy community and a lone maintainer struggling to keep up.

None of these costs are visible in the first week. All three appear by the end of the first year. By the third year they dominate the project. That is why portability, discussed in the abstract, sounds optional and, discussed with real projects in mind, is nothing of the sort.

### Why FreeBSD Makes This Easier Than Most

A final note, and a slightly optimistic one. Among mainstream kernels, FreeBSD is one of the easier to target for portability, because the project has taken the portable abstractions seriously from the beginning. `bus_space(9)` exists so that driver authors do not have to write architecture-specific register access. `bus_dma(9)` exists so that DMA behaves identically across platforms. Newbus exists so that bus-specific probe logic is separable from core driver logic. The endian helpers in `/usr/src/sys/sys/endian.h` exist so that a single expression works on little-endian and big-endian hosts. The fixed-width types in `/usr/src/sys/sys/types.h` exist so that "how wide is an int on this platform" never matters.

Contrast this with projects that grew organically across architectures without a plan, and you will find drivers littered with `#ifdef CONFIG_ARM`, with raw pointer casts to device memory, and with ad hoc byte-swap code inlined at every register access. FreeBSD could have ended up that way and did not, because the core maintainers cared about portability from early on. Your drivers can inherit that discipline for free; the cost is learning which abstractions to use and using them consistently.

This is the spirit in which the rest of the chapter is written. The tools are in place. The patterns are clear. The investment is small if you make it early. Let us begin.

### Wrapping Up Section 1

You now have a working definition of portability and a set of axes along which it varies: architecture, bus, operating system, time, and deployment. You also have a sense of why portability matters as a practical engineering concern, not a theoretical nicety, illustrated by the contrast between a careless driver and a careful one over a span of eighteen months. The next sections turn from definition to method. In Section 2, we begin the practical work of portability by isolating the parts of a driver that depend on hardware details from the parts that do not. That distinction is the first structural move every portable driver makes, and it is the move that pays the biggest dividends for the smallest cost.

## Section 2: Isolating Hardware-Dependent Code

The first concrete move toward a portable driver is to separate code that depends on specific hardware details from code that does not. This sounds obvious when stated in the abstract, and it is surprisingly easy to get wrong in practice. Hardware-dependent code tends to leak into unexpected places, because the path from a piece of data to the hardware is long, and every step along the way has the potential to encode an assumption that should have stayed elsewhere.

This section introduces the idea in concrete terms, shows you the kinds of code that count as hardware-dependent, and then walks through the two most common mechanisms FreeBSD gives you for putting that code behind an abstraction: the `bus_space(9)` family of macros for register access, and the `bus_dma(9)` family for buffers that the device reads and writes directly. By the end of this section, you will recognise the pattern of hardware isolation when you see it in real drivers and know how to apply it to your own.

### What Counts as Hardware-Dependent?

Let us be precise about what we mean. Hardware-dependent code is code whose correctness depends on facts that are specific to a particular piece of silicon or to the bus it sits on. Classic examples include:

- the numeric offset of a register within the device's memory-mapped region
- the exact bit layout of a status word
- the width of a FIFO in bytes
- the required sequence of register writes to initialise the device
- the endianness in which the device expects to see multi-byte values
- the alignment constraints of DMA buffers for a particular DMA engine

Note that none of these facts are true across every device your driver might ever have to handle. If tomorrow the hardware vendor releases a revision with a different FIFO depth, every line that assumed the old depth must change. If the same silicon appears on USB instead of PCI, every `bus_space_read_*` call must become something else. Those are the seams where portability either holds or breaks.

By contrast, code is hardware-**independent** when it deals with the data once it is out of the device's grip. A function that walks a queue of pending requests, assigns sequence numbers, manages timeouts, or coordinates with the upper half of the kernel (like GEOM, `ifnet`, or `cdevsw`) does not need to know whether the data came in over PCI, USB, or a simulator. As long as the hardware-dependent layer has lifted the data up into a clean, uniform representation, the rest of the driver does not care.

The goal, then, is to draw a line down through the driver. Above the line lives hardware-independent logic. Below the line lives everything that knows about registers, DMA, or bus-specific APIs. The higher the line can be drawn without distorting the code, the more portable the driver becomes.

### A Small Thought Experiment

Before we introduce tools, try a thought experiment. Imagine a driver for a made-up "widget" device that performs some simple I/O. The device has two registers: a control register at offset 0x00 and a data register at offset 0x04. Your driver writes a byte to the data register, sets a bit in the control register to start a transfer, polls another bit in the control register until it clears, and then reads a status from the data register.

Now imagine the same widget is produced in two physical forms. One form is a PCI card where both registers are reached through a memory-mapped BAR. Another form is a USB dongle where both registers are reached by USB control transfers. The programming model is identical, but the mechanism for actually reading and writing the registers is completely different.

How do you write the core of the driver, the part that performs transfers and polls for completion, so that it works with both forms?

The answer, which drives the rest of this section, is to stop writing register accesses directly. Instead, write accessors. The core logic calls `widget_read_reg(sc, offset)` and `widget_write_reg(sc, offset, value)`. Those accessors are backend-specific: the PCI backend maps them to `bus_space_read_4` and `bus_space_write_4`, and the USB backend maps them to USB control transfers. The core logic does not know, and does not need to know, which backend is in use.

This is the fundamental move of hardware isolation, and it scales. Once your driver reads and writes registers only through accessors, swapping the backend becomes a local change. Add a third backend for a simulated device? Write a third set of accessors that read and write an in-memory buffer. Nothing else in the driver changes.

### A Closer Look at the uart Driver

Before we get to the FreeBSD primitives, it helps to trace the shape of a real, mature driver so you can see the ideas in situ. Open `/usr/src/sys/dev/uart/uart_core.c` and scroll slowly. You will see that the file contains the state machine for a UART: the TTY integration, the transmit and receive paths, the interrupt routing, and the lifecycle hooks. You will not find `bus_space_read_4` anywhere in this file (or rather, you will find it only in helpers that are themselves abstracted behind class-specific methods), and you will not find any mention of PCI, FDT, or ACPI. The core does not know which bus the UART is sitting on. It knows only the programming model of a UART.

Now open `/usr/src/sys/dev/uart/uart_bus_pci.c`. This file is short. It declares a table of vendor and device IDs, implements a PCI probe that matches against the table, implements a PCI attach that allocates a BAR and wires it into the class, and registers the driver with Newbus via `DRIVER_MODULE`. It is the PCI backend for the UART. Compare with `/usr/src/sys/dev/uart/uart_bus_fdt.c`, which does the same job for Device Tree platforms, and `/usr/src/sys/dev/uart/uart_bus_acpi.c` for ACPI platforms.

Finally, open `/usr/src/sys/dev/uart/uart_bus.h`. This is the header that ties the pieces together. It defines `struct uart_class`, which is essentially the UART's backend interface: a set of function pointers plus some configuration fields. Each hardware variant of the UART provides a `struct uart_class` instance. The core calls through the class rather than calling any variant directly.

This is the same pattern we will build in Section 3, only in the specific dialect that FreeBSD uses for UARTs. Reading the UART driver alongside this chapter will cement the ideas much better than reading the chapter alone. Keep `/usr/src/sys/dev/uart/uart_bus.h` open in a second window as you read the rest of Section 2 and the whole of Section 3.

### The bus_space(9) Abstraction

FreeBSD already gives you a tool that solves this problem for memory-mapped and I/O-port devices: the `bus_space(9)` API. You have encountered it before in Chapter 16, so this is a recap rather than a fresh introduction. The purpose of `bus_space` is to let you write a single register access that works correctly regardless of the CPU architecture, regardless of whether the device is reached through memory-mapped I/O or I/O ports, and regardless of the specific machine's bus topology.

At the programming model level, you hold two opaque values for each device: a `bus_space_tag_t` and a `bus_space_handle_t`. Once you have them, you access the device through calls such as:

```c
uint32_t value = bus_space_read_4(sc->sc_tag, sc->sc_handle, REG_CONTROL);
bus_space_write_4(sc->sc_tag, sc->sc_handle, REG_CONTROL, value | CTRL_START);
```

On `amd64`, the tag and the handle are simple integers. You can confirm this by opening `/usr/src/sys/amd64/include/_bus.h`, where both are defined as `uint64_t`. On `arm64`, the tag is a pointer to a structure of function pointers and the handle is a `u_long`. You can see this in `/usr/src/sys/arm64/include/_bus.h`. The ARM family needs the indirection because different platforms within ARM can map memory differently, and a function-pointer dispatch handles all of them uniformly.

This architectural difference is essential to portability. If you bypass `bus_space` and use a raw `volatile uint32_t *` pointer to the device's memory, your code will work on `amd64` and will fail silently or loudly on some ARM platforms. If you use `bus_space`, the same code works on both. That is the single most important portability gain you can get from this chapter with the smallest effort.

The `bus_space` functions come in a family that varies along three axes. First, the width: `bus_space_read_1`, `bus_space_read_2`, `bus_space_read_4`, and on 64-bit architectures `bus_space_read_8`. Second, the direction: `read` or `write`. Third, the multiplicity: the plain variant accesses a single value, the `_multi_` variants access a buffer of values, and the `_region_` variants access a contiguous region with an implied increment. For most drivers the single-value variants are the workhorses, and you will reach for the others when you have FIFOs or packet buffers to move in bulk.

You can confirm the presence of these functions by opening `/usr/src/sys/sys/bus.h`. The header has a long series of macro definitions for `bus_space_read_1`, `bus_space_write_1`, `bus_space_read_2`, and so on, each defined in terms of the architecture-specific primitives.

### Wrapping bus_space for Clarity

Even when you are committed to using `bus_space`, you should go one step further and wrap the calls in driver-specific accessors. Why? Because raw `bus_space_read_4(sc->sc_tag, sc->sc_handle, REG_CONTROL)` appears dozens of times in a growing driver, and each appearance is three arguments of visual noise when the only thing that matters is the offset and the value. More importantly, the raw call hardcodes the idea that the device is reached through `bus_space`. If you ever want to add a different backend, every one of those calls must change.

A driver-local wrapper gives you both a cleaner call site and a single place to change. The wrapper looks like this:

```c
static inline uint32_t
widget_read_reg(struct widget_softc *sc, bus_size_t off)
{
	return (bus_space_read_4(sc->sc_btag, sc->sc_bhandle, off));
}

static inline void
widget_write_reg(struct widget_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_btag, sc->sc_bhandle, off, val);
}
```

Now the rest of the driver calls `widget_read_reg(sc, REG_CONTROL)` and `widget_write_reg(sc, REG_CONTROL, val)`. The call sites read like specifications of intent rather than like plumbing. And when you add a USB backend in the next section, you can change the body of these two functions to dispatch on the backend type without touching any of the hundreds of callers.

This pattern, *wrap the primitive, then call the wrapper*, is the first and most common tool of driver portability. Make it a habit. Whenever you find yourself typing `bus_space_read_*` or `bus_space_write_*` in a file other than the backend-specific one, stop and ask whether a wrapper would serve you better.

### Conditional Register Access by Bus

Once wrappers are in place, the next question is what to do when the same driver must support multiple buses. There are two common approaches.

The first approach is **compile-time selection**. The driver is built once per bus, and the wrappers are implemented differently in each build. This is the approach `uart(4)` takes in some contexts. Each bus backend file builds its own set of attach helpers and calls the core's API. The wrappers do not need runtime dispatch because each backend compiles against its own specialisation. This approach produces the smallest, fastest binary, but it requires you to build the driver once per bus backend you want to support.

The second approach is **runtime dispatch**. The driver is built once and includes support for all enabled backends. At attach time, the driver detects which backend is actually in use and stores a function pointer in the softc. Each call through the wrapper costs one indirection. This approach is slightly more flexible at the cost of a small runtime overhead.

For most drivers, especially beginner-level ones, the clearest approach is a hybrid: build the core once, build each backend as a separately compiled source file, and use a small backend table per-instance that the core uses to dispatch. The wrappers are inline functions that read the backend table and call through it. You will see exactly this pattern in Section 3 when we introduce the backend interface formally.

### The bus_dma(9) Abstraction

For devices that perform direct memory access (DMA) to transfer data between system memory and the device, the analogous abstraction is `bus_dma(9)`. DMA is hardware-dependent in ways that register access is not: the physical address that the device sees is not always the same as the kernel virtual address your code holds, alignment requirements vary by device and by bus, and some architectures need explicit cache-flush or cache-invalidate operations between the CPU and the DMA engine to maintain consistency.

Open `/usr/src/sys/sys/bus_dma.h` and look at the core API. Without going into the full depth of `bus_dma`, the shape of the interface is:

```c
int bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filtfunc, void *filtfuncarg,
    bus_size_t maxsize, int nsegments, bus_size_t maxsegsz,
    int flags, bus_dma_lock_t *lockfunc, void *lockfuncarg,
    bus_dma_tag_t *dmat);

int bus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags,
    bus_dmamap_t *mapp);

int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, bus_dmamap_callback_t *callback,
    void *callback_arg, int flags);

void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t dmamap,
    bus_dmasync_op_t op);

void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t dmamap);
void bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);
```

The full mechanics of `bus_dma` are covered in depth in later chapters. For the purpose of this chapter you need only the portability consequence: **use `bus_dma` for all device-visible buffers**, and hide the use behind driver-local helpers just as you did for register access. The reason is identical. A direct `bus_dmamap_load` call encodes the assumption that you are using `bus_dma` and makes it hard to substitute an in-memory buffer for simulated testing, or a different DMA framework for an unusual platform. A wrapper function such as `widget_dma_load(sc, buf, len)` hides the assumption.

The callback-based nature of `bus_dmamap_load` is itself a portability feature worth pausing on. The callback is invoked with the list of physical segments the buffer was mapped into. On a system with a simple 1:1 virtual-to-physical mapping, that list will have one entry. On a system that needs bounce buffers to reach limited address ranges, the list might have several entries. Your driver writes one callback, and that callback handles both cases uniformly. That is `bus_dma`'s portability contribution: the callback sees a simple model, and the complexity lives under `bus_dma` itself.

### A Practical Header-Per-Backend Pattern

Putting these ideas together, a portable driver often uses a small family of header files and source files to make the separation visible in the filesystem layout. The core includes `widget_common.h` for types and prototypes that are not tied to any bus. The core source, typically `widget_core.c`, contains the hardware-independent logic. Each bus backend lives in its own `.c` file, such as `widget_pci.c` or `widget_usb.c`, and includes `widget_backend.h` for the definitions of the backend interface. The Makefile lists the core and whichever backends are enabled in `SRCS`, and the kernel build does the rest.

You will see this layout applied step by step in Section 4 with the `portdrv` reference driver. Before we get there, we need to formalise what a "backend interface" looks like. That is the subject of the next section.

### Common Mistakes When Isolating Hardware Code

A handful of mistakes recur often enough to be worth calling out. Each one is small in isolation but damaging in aggregate.

The first mistake is **hidden access**. A helper function somewhere deep in the driver reaches into the softc and performs a register access directly instead of going through the accessor. The result is a single leak, but the leak can be enough to defeat the abstraction. When you read a driver, look for calls to `bus_space_*` outside the designated backend files; they are warning signs.

The second mistake is **offset leakage**. The driver writes `bus_space_read_4(tag, handle, 0x20)` with a literal offset. Literal offsets are fine as constants named `REG_CONTROL` in a header, but they are dangerous as bare integers. A bare offset tells you nothing about what the register does, and renumbering the register on a future hardware revision becomes a search-and-replace exercise. Always define register names as macros or enumerations in the header, and use the names in the code.

The third mistake is **type confusion**. A register returns a 32-bit value, but the code stores the result in an `int` or an `unsigned long`. On a 64-bit system this often works by accident, because the value fits. On a 32-bit system with a signed `int`, a register whose top bit is set becomes negative, and comparisons break. Always match the type to the register width, using `uint8_t`, `uint16_t`, `uint32_t`, or `uint64_t` as appropriate.

The fourth mistake is **endianness assumptions at the accessor layer**. The `bus_space` functions return the value as the host sees it, in host byte order. If the device uses a specific byte order in its registers, the conversion happens elsewhere in the code, using `htole32` or `be32toh` as needed. Do not bake the conversion into the accessor, because other parts of the driver may be accessing the same register for administrative purposes and do not want the conversion applied.

The fifth mistake is **no accessor at all**. The driver uses raw `volatile uint32_t *` pointers to map registers. This works on some architectures and fails on others. It defeats the entire point of `bus_space`. If you are reading a driver that does this, assume it is broken on at least one architecture even if you cannot immediately say which one.

### Subregions, Barriers, and Burst Operations

Three `bus_space` features are worth a short tour, because they come up often enough in real drivers that you will meet them even in small code bases.

The **subregion** feature lets you split a mapped region into logical parts and hand each part to a different piece of code. If your device's BAR is 64 KB and the driver conceptually has three separate register banks at offsets 0, 0x4000, and 0x8000, you can create three subregions with `bus_space_subregion` and give each to the function that cares about it. Each subregion has its own tag and handle, and accesses to it naturally carry offsets from the start of the subregion, not from the start of the whole BAR. The result is code that reads `bus_space_read_4(bank_tag, bank_handle, REG_CONTROL)` rather than `bus_space_read_4(whole_tag, whole_handle, BANK_B_BASE + REG_CONTROL)`. The offsets become local to the bank and do not carry the whole BAR's address arithmetic in every call site.

The **barrier** feature is about ordering. When you write to a device register, the kernel has two separate guarantees to provide. First, that the write actually reaches the device, rather than sitting in a CPU write buffer or a bus bridge. Second, that writes happen in the order the programmer expected. On most common `amd64` configurations, these guarantees come for free; on `arm64` and some other platforms, they do not. The primitive is `bus_space_barrier(tag, handle, offset, length, flags)`, where `flags` is a combination of `BUS_SPACE_BARRIER_READ` and `BUS_SPACE_BARRIER_WRITE`. The call says: "all reads or writes in the specified range before this point must complete before any after it." You use it when your driver depends on the order in which the device sees register accesses, for example when arming an interrupt after setting up a DMA descriptor.

The **burst** feature handles fast moves to or from device memory. When you need to copy a packet into a FIFO or out of a DMA buffer, calling `bus_space_write_4` in a loop is correct but slow. The `bus_space_write_multi_4` and `bus_space_read_multi_4` functions do the whole burst in one call, and on architectures that have specialised move instructions they use them. For a network driver transferring frames at high rates, burst helpers can be the difference between wire speed and half wire speed, and the cost of using them is only a slightly different call signature.

None of these features is mandatory for a small driver. All of them are worth knowing about, because their absence in a growing driver leads to workarounds that are harder to read and slower to run than the primitive they were avoiding.

### Layered Wrapping: From Primitive to Domain

The accessors shown earlier in this section are the first layer of wrapping around `bus_space`. Real drivers sometimes benefit from a second layer that lifts the accessors into a domain-specific vocabulary. The idea is simple: once you have `portdrv_read_reg(sc, off)`, you can build on top of it with functions like `portdrv_read_status(sc)`, `portdrv_arm_interrupt(sc)`, or `portdrv_load_descriptor(sc, idx, addr, len)`. The core of the driver then speaks in the device's vocabulary rather than in raw register accesses.

```c
static inline uint32_t
portdrv_read_status(struct portdrv_softc *sc)
{
	return (portdrv_read_reg(sc, REG_STATUS));
}

static inline bool
portdrv_is_ready(struct portdrv_softc *sc)
{
	return ((portdrv_read_status(sc) & STATUS_READY) != 0);
}

static inline void
portdrv_arm_interrupt(struct portdrv_softc *sc, uint32_t mask)
{
	portdrv_write_reg(sc, REG_INTR_MASK, mask);
}
```

Code that calls `portdrv_is_ready(sc)` instead of `(portdrv_read_reg(sc, REG_STATUS) & STATUS_READY) != 0` reads like intent rather than plumbing. When the definition of "ready" changes on a newer revision of the device, the change happens in one inline function rather than at every call site. This is the same pattern as the primitive accessors, only applied to the next layer up.

Do not overdo it. A wrapper per trivial register read is wrappering for its own sake. Add a domain-level wrapper when its name is meaningfully more informative than the register access, or when the operation is non-trivial enough that the reader benefits from a name. For a single register read, the primitive wrapper is usually enough.

### Accessor Layering Inside the FreeBSD Tree

You can see this layered approach used in mature FreeBSD drivers. Look at `/usr/src/sys/dev/e1000/` for the Intel Gigabit Ethernet driver. The file `e1000_api.c` exposes dispatch functions like `e1000_reset_hw`, `e1000_init_hw`, and `e1000_check_for_link`, each of which forwards to a chip-family helper (for example, the 82571 helpers live in `e1000_82571.c`, the 82575 helpers live in `e1000_82575.c`, and so on). Those chip-specific helpers do their register work through `E1000_READ_REG` and `E1000_WRITE_REG`, which are defined in `/usr/src/sys/dev/e1000/e1000_osdep.h` as macros that expand to `bus_space_read_4` and `bus_space_write_4` on the tag and handle carried inside the driver's `struct e1000_osdep`. Four layers: bus primitive, register accessor macro, chip-family helper, and driver-facing API in `if_em.c`. Each layer adds a little more meaning, and each layer can be changed without disturbing the others.

For your own drivers, two layers is usually the right target: a primitive accessor that wraps `bus_read_*` or dispatches through the backend, and a set of domain helpers that lift the primitive into the vocabulary of the device. Three layers, if the driver is large and spans multiple subsystems. More than three, and the layering is probably obscuring rather than clarifying.

### When the Line Between HW-Dependent and HW-Independent Blurs

The line between hardware-dependent and hardware-independent code is usually clear, but there are edge cases worth naming.

One edge case is **timing logic**. Suppose the driver must wait up to 50 microseconds for a status bit to appear after writing a register. The wait itself is hardware-dependent, because the 50-microsecond figure comes from the device's datasheet. But the scaffolding, the loop that polls, the timeout that gives up, and the error return, is hardware-independent. The right design is to have a small helper in the core that takes the poll predicate as a function pointer or inline expression, and a hardware-specific constant for the timeout. The core owns the polling loop; the backend owns the constant.

```c
/* Core. */
static int
portdrv_wait_for(struct portdrv_softc *sc,
    bool (*predicate)(struct portdrv_softc *sc),
    int timeout_us)
{
	int i;

	for (i = 0; i < timeout_us; i += 10) {
		if (predicate(sc))
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}
```

The backend provides the predicate (which may be trivial, such as reading a status register) and the timeout. This keeps the loop structure in one place and lets the individual device details live where they belong.

Another edge case is **state machines**. The state machine of a typical request processing loop is hardware-independent: idle, pending, in-flight, complete. The transitions, however, may depend on hardware events. A common pattern is to keep the state machine in the core and invoke backend methods for the operations that might differ between backends. The core says "start the transfer"; the backend does whatever that means. The state transitions happen in the core based on the backend's return values.

A third edge case is **logging and telemetry**. You want one `device_printf` in the core when a transfer fails, not one per backend. But the printf may want to include backend-specific information such as the register that caused the error. The solution is to expose a backend method like `describe_error(sc, buf, buflen)` that fills a buffer with a human-readable summary, and have the core call it. The core owns the log line; the backend owns the vocabulary.

The common theme in all these edge cases is the same: **put structure in the core, and put details in the backend**. When you are unsure where a piece of code belongs, ask which category it is, structure or detail. Structure is about what happens. Details are about exactly how the hardware is touched to make it happen.

### Wrapping Up Section 2

You have now seen the first structural move every portable driver makes: draw a clean line between hardware-dependent and hardware-independent code, and express the line in actual files and accessor functions rather than in comments. The `bus_space(9)` and `bus_dma(9)` abstractions are FreeBSD's gifts to that effort, because they already hide most architecture-specific complexity. Wrapping them in driver-local accessors gives you the final piece: a single place to change when the backend changes. Layered wrapping, disciplined use of subregions and barriers, and careful placement of edge-case code between core and backend make the line between them stable rather than fragile.

The next section takes the step beyond accessors. If the driver has to support multiple backends in the same binary, what does the interface between the core and the backends look like? That is where the idea of a **backend interface**, expressed as a struct of function pointers, becomes essential.

## Section 3: Abstracting Device Behavior

In Section 2 we drew a line between hardware-dependent and hardware-independent code, and wrapped the primitive bus operations behind driver-local accessors. That move handles the simple case, where the driver talks to one kind of device over one kind of bus. In this section we handle the harder and more interesting case: a driver that supports multiple backends in the same build.

The technique that makes this manageable is straightforward to describe and profoundly useful in practice. The driver's core defines an **interface**: a struct of function pointers that describes what operations a backend must provide. Each backend provides an instance of that struct, filled in with its own implementations. The core calls through the struct rather than calling any backend directly. Adding a new backend is a matter of writing one more struct instance and one more set of functions; the core is untouched.

This pattern is everywhere in FreeBSD. The Newbus framework itself is built on a more elaborate version of it, called `kobj`, and you have been using `kobj` indirectly every time you filled in a `device_method_t` array in earlier chapters. In this chapter we introduce a lighter version that you can apply to your own drivers without the full ceremony of `kobj`.

### Why a Formal Interface Beats If-Else Chains

The first instinct of a programmer who needs to support two backends is often to branch on a backend type at each call site. Something like this:

```c
static void
widget_start_transfer(struct widget_softc *sc)
{
	if (sc->sc_backend == BACKEND_PCI) {
		bus_space_write_4(sc->sc_btag, sc->sc_bhandle,
		    REG_CONTROL, CTRL_START);
	} else if (sc->sc_backend == BACKEND_USB) {
		widget_usb_ctrl_write(sc, REG_CONTROL, CTRL_START);
	} else if (sc->sc_backend == BACKEND_SIM) {
		sc->sc_sim_state.control |= CTRL_START;
	}
}
```

This works for one function. It becomes unbearable by the tenth. Every function acquires a switch statement. Every new backend adds another branch to every switch. The number of edits grows as the product of functions and backends. After the third backend the driver is barely readable, and any change to the shape of a backend operation requires visits to a dozen files.

The structural fix is to replace the chain with a single indirection. Define a struct of function pointers:

```c
struct widget_backend {
	const char *name;
	int   (*attach)(struct widget_softc *sc);
	void  (*detach)(struct widget_softc *sc);
	uint32_t (*read_reg)(struct widget_softc *sc, bus_size_t off);
	void     (*write_reg)(struct widget_softc *sc, bus_size_t off, uint32_t val);
	int   (*start_transfer)(struct widget_softc *sc,
	                        const void *buf, size_t len);
	int   (*poll_done)(struct widget_softc *sc);
};
```

Each backend provides an instance of this struct. The PCI backend defines `widget_pci_read_reg`, `widget_pci_write_reg`, and the rest, and then fills in a `struct widget_backend` with those function pointers:

```c
const struct widget_backend widget_pci_backend = {
	.name           = "pci",
	.attach         = widget_pci_attach,
	.detach         = widget_pci_detach,
	.read_reg       = widget_pci_read_reg,
	.write_reg      = widget_pci_write_reg,
	.start_transfer = widget_pci_start_transfer,
	.poll_done      = widget_pci_poll_done,
};
```

The USB backend does the same, the simulation backend does the same, and so on. The softc carries a pointer to whichever backend this instance is using:

```c
struct widget_softc {
	device_t  sc_dev;
	const struct widget_backend *sc_be;
	/* ... bus-specific fields, kept opaque to the core ... */
	void     *sc_backend_priv;
};
```

And the core calls through the pointer:

```c
static void
widget_start_transfer(struct widget_softc *sc, const void *buf, size_t len)
{
	(void)sc->sc_be->start_transfer(sc, buf, len);
}
```

The core does not branch. The core does not mention `pci`, `usb`, or `sim`. The core calls an operation, and the operation is looked up through a pointer. This is the fundamental move. Read it a few times and let it settle; the rest of the section is elaboration and concrete detail.

### Setting Up the Backend at Attach Time

Each attach path installs the right backend into the softc. The PCI attach code looks roughly like this:

```c
static int
widget_pci_attach(device_t dev)
{
	struct widget_softc *sc = device_get_softc(dev);
	int err;

	sc->sc_dev = dev;
	sc->sc_be = &widget_pci_backend;

	/* Allocate bus resources, store them in sc->sc_backend_priv. */
	err = widget_pci_alloc_resources(sc);
	if (err != 0)
		return (err);

	/* Hand off to the core, which will use the backend through sc->sc_be. */
	return (widget_core_attach(sc));
}
```

The USB attach is structurally identical: allocate its bus resources, install `widget_usb_backend`, and call `widget_core_attach`. The simulation attach is identical too: install `widget_sim_backend`, which does not allocate anything real, and call `widget_core_attach`.

The lesson is that **the per-backend attach path is small, and its job is to prepare the softc for the core**. Once the softc has a valid backend and valid backend-private state, the core takes over and does the universal work of wiring the driver into the rest of the kernel.

### How the Real UART Driver Does This

Open `/usr/src/sys/dev/uart/uart_bus.h` and jump to the `struct uart_class` definition. You will see the pattern we have just described, with a FreeBSD-specific twist. The twist is that the UART driver uses the `kobj(9)` framework, which adds a layer of compile-time dispatch on top of function pointers, but the essence is the same. A `struct uart_class` carries a `uc_ops` pointer to an operations struct, along with some configuration fields, and each hardware-specific variant of the driver provides its own `uart_class` instance.

Now open `/usr/src/sys/dev/uart/uart_bus_pci.c`. The `device_method_t uart_pci_methods[]` array at the top of the file declares which functions implement the PCI-specific device methods. Compare with `/usr/src/sys/dev/uart/uart_bus_fdt.c`, which does the same job for Device Tree-based platforms. Both files are short because they do very little: they install the right class, allocate resources, and hand off to the core in `uart_core.c`.

This is exactly the pattern you are learning in this section, only dressed in the formal `kobj` idiom that FreeBSD uses for bus drivers. When you write your own lighter-weight abstraction for a smaller driver, you can use a plain struct of function pointers, and the result is easier to read and reason about than a full `kobj`. For a driver with a handful of backends and a simple method set, the plain struct is the right choice.

### Choosing the Set of Operations

Designing a backend interface is a matter of taste and judgement. There is no mechanical formula, but a few principles help.

First, **every operation that varies by backend belongs in the interface; every operation that does not varies does not**. If the same code is correct regardless of whether the backend is PCI or USB, put that code in the core. Do not put it in the interface, because each backend would then have to implement it, and the implementations would be identical, which defeats the point.

Second, **keep the interface narrow**. The fewer operations, the less work to implement a new backend and the less surface area to maintain. If two operations always appear together at call sites, consider merging them into one operation that does both.

Third, **prefer coarse operations over fine ones**. An operation like `start_transfer(sc, buf, len)` is coarse: it captures the intent of a transfer in one call. An equivalent pair like `write_reg(sc, DATA, ...)` followed by `write_reg(sc, CONTROL, CTRL_START)` is fine-grained, and it forces the core to know the register layout. Coarse operations let the backends implement the sequence however they need to, including in ways the core cannot know. This is also the key to backend simulation: a simulator can "perform" a transfer without actually writing any registers.

Fourth, **leave room for private state**. Each backend may need its own per-instance data that the core does not care about. A `void *sc_backend_priv` in the softc, owned by the backend, is the usual pattern. The PCI backend stores its bus resources there. The USB backend stores its USB device handles. The simulation backend stores its in-memory register map. The core never reads or writes this field; it simply passes the softc through.

Fifth, **define a versioned struct if you want to evolve the interface over time**. When the interface is small and the number of backends is also small, you can add a field and update all backends in one commit. When the backends are external to your tree or maintained separately, you may want a version field in the struct, so a backend can refuse to attach if the core expects a newer shape. This is the same trick FreeBSD itself uses in some of its KPIs.

### Avoiding If-Else Creep

Once the interface is in place, keep watch for a subtle form of regression: the reintroduction of backend-specific branches. This can happen innocently. A new feature is added that only makes sense on one backend, and rather than extend the interface, the developer writes `if (sc->sc_be == &widget_pci_backend) { ... }` somewhere in the core. This works, once. Repeat it three or four times and the driver is no longer cleanly abstracted; the backend is leaking back into the core through tag comparisons.

The right fix for a feature that only one backend supports is to add an operation to the interface that the other backends implement trivially. If only the PCI backend supports, say, interrupt coalescing, add `set_coalesce(sc, usec)` to the interface and implement it as a no-op in the USB and simulation backends. The core calls it unconditionally. This keeps the core ignorant of backend identity while still allowing per-backend specialisation.

### Backend Discovery and Registration

A last practical question: how does a backend get attached in the first place? The answer varies by bus. On PCI, the kernel's Newbus machinery walks the PCI tree and calls each candidate driver's probe function. You write `widget_pci_probe` and register it with `DRIVER_MODULE(widget_pci, ...)`; the kernel does the rest. On USB, the `usbd` framework performs an analogous walk over USB devices and calls each candidate driver's probe. On a purely virtual or simulated backend, there is no bus to walk, and the backend is usually attached through a module init hook that creates a single instance.

The important portability point is this: **each bus's registration machinery is part of the backend, not the core**. The core never calls `DRIVER_MODULE` or `USB_PNP_HOST_INFO` or any bus-specific registration macro. The core exports a clean entry point (`widget_core_attach`) that each backend calls when its own attach logic is ready. Everything bus-specific happens in the backend file. This is what keeps the core clean enough to move unchanged to a new bus.

### Common Mistakes When Designing an Interface

A few pitfalls recur often enough to call out.

**Over-abstracting.** Not every operation belongs in the interface. An operation that only one backend ever needs should live in that backend's private code, not be forced into the struct. An interface full of stub `return 0;` implementations has grown too wide.

**Under-abstracting.** Conversely, an interface that forces each backend to replicate the same five lines of setup logic has grown too narrow. If three backends all begin their `start_transfer` with the same two lines, move those two lines into the core and have the interface operation receive already-validated input.

**Exposing backend types.** If the core has a `switch` on backend identity, the abstraction is broken. The core should know which operations to call, not which backend is providing them.

**Mixing synchronisation policies.** Each backend must honour the core's locking policy. If the core calls `start_transfer` with the softc lock held, every backend must respect that and not sleep. If the core calls `attach` without a lock, the backend must not assume one is held. An interface is not just a set of signatures; it is also a contract about the surrounding context.

**Ignoring lifetime.** If the core calls `backend->attach` and gets back a non-zero error code, it must not call `backend->detach` afterwards, because nothing was attached. Document the contract, and write the backends to honour it. Confusion here leads to double frees and use-after-free bugs.

### A Closer Look at `kobj(9)`

Because Newbus itself uses `kobj(9)`, it is worth taking a moment to look at what the kernel does for you when you write `device_method_t`. The mechanism may look intimidating, but the underlying idea is the same function-pointer dispatch we have been discussing, only formalised.

Open `/usr/src/sys/kern/subr_kobj.c` and look at how classes are registered. A `kobj_class` holds a name, a list of method pointers, an ops table that the kernel builds at module load time, and a size for instance data. When you write:

```c
static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,  portdrv_pci_probe),
	DEVMETHOD(device_attach, portdrv_pci_attach),
	DEVMETHOD(device_detach, portdrv_pci_detach),
	DEVMETHOD_END
};
```

you are building one of these method lists. The kernel compiles the list into a dispatch table at runtime, and every call such as `DEVICE_PROBE(dev)` becomes a lookup into the table followed by a call through a function pointer. From the programmer's perspective, the call site is a single macro; from the machine's perspective, it is one indirection.

For a small backend abstraction, the plain struct-of-function-pointers pattern we showed earlier is enough and is easier to read. When the interface grows beyond about eight methods and begins to accumulate default implementations, `kobj` starts to earn its keep. The ability to have a subclass inherit methods from a base class, to default a method to a no-op when a class does not provide it, and to be statically checked via the method-ID header is worth the additional machinery. For most drivers in this book, you will not need `kobj` directly, but recognising that it is doing the same thing your plain struct does makes the Newbus machinery feel less magical.

### Stacked Backends and Delegation

A related, more advanced pattern is the **stacked** or **delegating** backend. Sometimes a backend is naturally a thin wrapper over another backend, with some small transformation applied. Imagine a backend that forces all register writes to be logged for debugging, or one that adds a delay before every read to simulate a slow bus, or one that records register traffic into a ring buffer for later replay. Each of these is functionally a wrapper around a real backend.

The pattern is straightforward. The wrapping backend's struct includes a pointer to an inner backend:

```c
struct portdrv_debug_priv {
	const struct portdrv_backend *inner;
	void                         *inner_priv;
	/* logging state, statistics, etc. */
};

static uint32_t
portdrv_debug_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_debug_priv *dp = sc->sc_backend_priv;
	void *saved_priv = sc->sc_backend_priv;
	uint32_t val;

	sc->sc_backend_priv = dp->inner_priv;
	val = dp->inner->read_reg(sc, off);
	sc->sc_backend_priv = saved_priv;

	device_printf(sc->sc_dev, "read  0x%04jx -> 0x%08x\n",
	    (uintmax_t)off, val);
	return (val);
}
```

The debug backend borrows the real backend's implementation for the actual register read, and adds logging around it. The core does not know it is talking to a wrapper. At attach time, the driver can install either the plain PCI backend or the debug backend that wraps it. This is a useful pattern for development, for recording a trace of a boot failure, or for building a simulator that replays a recorded trace.

Not every driver needs stacked backends. When you do need them, the pattern above is the template. The interface stays the same; a backend instance simply has the flexibility to delegate to another instance rather than doing the work itself.

### Making the Interface Contract Explicit

A good backend interface is not just a struct; it is a contract about what each method does, what preconditions it has, and what side effects it produces. The contract lives in a comment at the top of the header:

```c
/*
 * portdrv_backend.h - backend interface contract.
 *
 * The core acquires sc->sc_mtx (a MTX_DEF) before calling any method
 * except attach and detach. Methods must not sleep while holding this
 * lock. The core releases the lock before calling attach and detach,
 * because those methods may allocate memory with M_WAITOK or perform
 * other sleepable operations.
 *
 * read_reg and write_reg must be side-effect-free from the core's
 * point of view, other than the corresponding register access.
 * start_transfer may record pending work but must not block; it
 * returns 0 on success or an errno on failure. poll_done returns
 * non-zero when the transfer has completed and zero otherwise.
 *
 * Backends may return EOPNOTSUPP for an operation they do not
 * support. The core treats EOPNOTSUPP as "feature not present",
 * and its callers degrade gracefully.
 */
```

This kind of comment is not decoration. It is the body of knowledge that a new contributor needs to implement a new backend correctly. Without it, the author of the third backend must reverse-engineer the contract from the existing two, and usually gets it slightly wrong. With it, the third backend is a straightforward exercise.

Include in the contract at least: the locking rules (which locks the core holds at each method call), the sleep rules (which methods may sleep), the return-value conventions, and any side effects the core depends on. If the contract allows methods to be `NULL`, state it. If some methods are required and some are optional, state that too.

### Minimal vs. Rich Interfaces

A recurring design question is how rich the interface should be. Should the backend expose a single `do_transfer` method that covers every transfer type, or a family of specialised methods for read, write, command, and status? Should the core do high-level request queuing and ask the backend to perform each step, or should the backend own the queue and report completions upward?

There is no universal answer, but two principles help.

**If the operation differs only in the vocabulary, unify it.** `read_reg` and `write_reg` differ only in whether the access is outbound or inbound. They are naturally two methods, but one could reasonably be a single `access_reg(sc, off, direction, valp)`. In practice, having two methods is clearer than a direction flag, because the call sites read better. Split when the split is obvious.

**If the operations differ in content, keep them separate.** A device might support both "command" transfers and "data" transfers that look completely different at the register level. Fold them into one method and you force the backend to dispatch on a flag inside. Keep them as two methods and the backend's job becomes obvious. Split when the split is substantive.

A useful test: write the documentation for the method you are considering. If the documentation needs to describe two behaviours distinguished by a parameter, split. If it describes one behaviour with parameters that vary the value, keep.

### The "Soft" Contract of a Backend

Beyond the explicit contract, there is a soft contract about resource management. Each backend should be consistent in who allocates what, when each piece of state is valid, and who frees it at shutdown. The usual convention is:

- The backend's per-attach code allocates its private state in `sc_backend_priv` and any bus resources it needs. It returns before the core begins using the interface.
- The core allocates its own state (locks, queues, character devices) after the backend is installed, in `portdrv_core_attach`.
- On detach, the core tears down its own state first, then calls the backend's `detach` method, which releases the backend's resources and frees `sc_backend_priv`.

This ordering matters. If the core tears down its character device before the backend stops DMA, an in-flight transfer might fire a callback into a destroyed object. If the backend frees `sc_backend_priv` before the core finishes a write, the core's access panics with a use-after-free. Document the teardown order in the backend contract, and honour it in every backend.

Double-checking the soft contract is an excellent activity to perform when you write a new backend. Walk through the attach and detach paths mentally, keeping track of which object is alive and which is being torn down. Most bugs in a well-written backend are life-cycle bugs, and they are easiest to catch before the code runs than after.

### Wrapping Up Section 3

The backend interface is the conceptual spine of a portable driver. Accessors handle small-scale variation like register width and architecture, but the backend interface handles large-scale variation like the entire bus. Once you have an interface, adding a backend is a one-file change, and the core is inoculated against bus-specific concerns. The first time you experience this, you will understand why the pattern is so universal in kernel code. Understanding `kobj(9)` as a more elaborate form of the same idea, and recognising when stacked backends or a careful contract are worth the effort, are the next steps toward writing drivers that scale beyond a single variant.

In Section 4 we turn the backend interface into a concrete file layout: which functions live in which file, which header each file includes, and how the build system pulls everything together. The ideas are the same; the payoff is a working multi-file driver you can build and load.

## Section 4: Splitting Code Into Logical Modules

A portable driver is not usually one file. It is a small family of files, each with a clear responsibility, and a build system that knows how to assemble them. This section takes the backend interface from Section 3 and expresses it as a concrete directory layout that you can type, build, and load. The file layout is not sacred, but it is a well-established convention in FreeBSD, and following it will make your driver look familiar to anyone else who reads it.

The goal is a layout in which each file answers exactly one question. A reader picking up the driver for the first time should be able to guess which file to open based on what they are looking for, without having to grep. That is what "logical modules" means in this chapter: not module in the `.ko` sense, but module in the structural sense, a unit of code with one purpose.

### The Canonical Layout

Start with the reference driver `portdrv`. After refactoring, it looks like this on disk:

```text
portdrv/
├── Makefile
├── portdrv.h           # cross-file public prototypes and types
├── portdrv_common.h    # types shared between core and backends
├── portdrv_backend.h   # the backend interface struct
├── portdrv_core.c      # hardware-independent logic
├── portdrv_pci.c       # PCI backend
├── portdrv_usb.c       # USB backend (optional, conditional)
├── portdrv_sim.c       # simulation backend (optional, conditional)
├── portdrv_sysctl.c    # sysctl tree, helpers
├── portdrv_ioctl.c     # ioctl handlers, if the driver exposes them
└── portdrv_dma.c       # DMA helpers, if the driver uses DMA
```

That is the target. Not every driver needs every file. A small driver that does not do DMA has no `portdrv_dma.c`. A driver that has only one backend has no separate backend files and may keep the hardware-specific code in a single `portdrv_pci.c` alongside the core. The layout scales up as the driver grows, and the key idea is that **new responsibilities get new files rather than new sections in an existing file**.

### Responsibilities Per File

Let us walk through the intended job of each file, so that the layout feels less abstract.

- **`portdrv.h`**: the public header for the driver. This is what `portdrv_sysctl.c` and other internal files include to see the softc and the core entry points. It pulls in common types, forward declares the softc, and exposes the functions each file needs to call across file boundaries.

- **`portdrv_common.h`**: the subset of types that the backends also need. Splitting this out from `portdrv.h` is useful because the backend files should not need to see every internal detail of the core. If the softc has, say, a field that is only relevant to ioctl handling, that field's type can stay in `portdrv.h`, while the pointer to the backend interface is exposed in `portdrv_common.h`. In smaller drivers you can merge the two headers if the split feels forced.

- **`portdrv_backend.h`**: the single authoritative definition of the backend interface: the struct of function pointers, the constants for backend identification, and the declarations of any helpers shared among backends. Every backend file includes it. The core also includes it. This is the file you open when you want to know what the core expects from a backend.

- **`portdrv_core.c`**: the hardware-independent logic. Attach and detach for the driver as a whole, the request queue, the softc allocation, the character device or interface registration, the callback invocation path. The core does not include any bus-specific header like `dev/pci/pcivar.h` or `dev/usb/usb.h`. If you find yourself including such a header in `portdrv_core.c`, something has gone wrong.

- **`portdrv_pci.c`**: everything that knows about PCI. The `device_method_t` array for PCI, the PCI probe, the PCI-specific allocator, the PCI interrupt handler if the driver uses one, and the `DRIVER_MODULE` registration. The PCI backend's implementation of each function in the backend interface lives here. This file includes `dev/pci/pcivar.h` and the other PCI headers; the core does not.

- **`portdrv_usb.c`**: the USB backend, with its own attach, its own registration through the USB framework, and its own implementation of the backend interface. Parallel in structure to the PCI backend, but reaching the hardware over a completely different API.

- **`portdrv_sim.c`**: the simulation backend. A pure software implementation that honours the same interface but stores register state in memory and synthesises completions. Useful for testing and CI; essential for development when the real hardware is not available.

- **`portdrv_sysctl.c`**: the driver's sysctl tree and the helper functions that read and set its variables. Keeping sysctl out of the core has two benefits. First, the core file stays focused on I/O. Second, the sysctl tree becomes easy to extend without adding noise to the core.

- **`portdrv_ioctl.c`**: the switch statement that dispatches ioctl commands, one helper per command. Large drivers accumulate many ioctls, and moving them to their own file keeps the main flow of the core readable. A small driver with two ioctls can keep them in the core.

- **`portdrv_dma.c`**: helpers for DMA setup and teardown. The functions `portdrv_dma_create_tag`, `portdrv_dma_alloc_buffer`, `portdrv_dma_free_buffer`, and so on, each of which wraps a `bus_dma` primitive. These helpers are used by the backends but do not depend on any specific bus. Isolating them into their own file makes it obvious what the DMA surface of the driver is.

### A Minimal Core File

The core file is where most of your non-trivial logic will live. Here is a sketch of what `portdrv_core.c` looks like, showing just the structural elements. Type it in, build it, and load it; the details are filled out in the lab at the end of the chapter.

```c
/*
 * portdrv_core.c - hardware-independent core for the portdrv driver.
 *
 * This file knows about the backend interface and the softc, but
 * does not include any bus-specific header. Backends are installed
 * by per-bus attach paths in portdrv_pci.c, portdrv_usb.c, etc.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mutex.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

static MALLOC_DEFINE(M_PORTDRV, "portdrv", "portdrv driver state");

int
portdrv_core_attach(struct portdrv_softc *sc)
{
	KASSERT(sc != NULL, ("portdrv_core_attach: NULL softc"));
	KASSERT(sc->sc_be != NULL,
	    ("portdrv_core_attach: backend not installed"));

	mtx_init(&sc->sc_mtx, "portdrv", NULL, MTX_DEF);

	/* Call the backend-specific attach step. */
	if (sc->sc_be->attach != NULL) {
		int err = sc->sc_be->attach(sc);
		if (err != 0) {
			mtx_destroy(&sc->sc_mtx);
			return (err);
		}
	}

	/* Hardware is up; register with the upper half of the kernel
	 * (cdev, ifnet, GEOM, etc. as appropriate). */
	return (portdrv_core_register_cdev(sc));
}

void
portdrv_core_detach(struct portdrv_softc *sc)
{
	portdrv_core_unregister_cdev(sc);
	if (sc->sc_be->detach != NULL)
		sc->sc_be->detach(sc);
	mtx_destroy(&sc->sc_mtx);
}

int
portdrv_core_submit(struct portdrv_softc *sc, const void *buf, size_t len)
{
	/* All of this logic is hardware-independent. */
	return (sc->sc_be->start_transfer(sc, buf, len));
}
```

Notice what is not in this file. No `#include <dev/pci/pcivar.h>`. No PCI probe. No USB descriptors. No register offsets. The core is pure logic and delegation. It is small enough that a reader can hold the whole file in their head, and it does not change when a new backend is added.

### A Minimal Backend File

The per-backend file is the only place where bus-specific headers appear. A sketch of `portdrv_pci.c`:

```c
/*
 * portdrv_pci.c - PCI backend for the portdrv driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

struct portdrv_pci_softc {
	struct resource *pci_bar;
	int              pci_bar_rid;
	struct resource *pci_irq;
	int              pci_irq_rid;
	void            *pci_ih;
};

static uint32_t
portdrv_pci_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	return (bus_read_4(psc->pci_bar, off));
}

static void
portdrv_pci_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	bus_write_4(psc->pci_bar, off, val);
}

static int
portdrv_pci_attach_be(struct portdrv_softc *sc)
{
	/* Finish any backend-specific setup after resources are claimed. */
	return (0);
}

static void
portdrv_pci_detach_be(struct portdrv_softc *sc)
{
	/* Tear down any backend-specific state. */
}

const struct portdrv_backend portdrv_pci_backend = {
	.name           = "pci",
	.attach         = portdrv_pci_attach_be,
	.detach         = portdrv_pci_detach_be,
	.read_reg       = portdrv_pci_read_reg,
	.write_reg      = portdrv_pci_write_reg,
	.start_transfer = portdrv_pci_start_transfer,
	.poll_done      = portdrv_pci_poll_done,
};

static int
portdrv_pci_probe(device_t dev)
{
	if (pci_get_vendor(dev) != PORTDRV_VENDOR ||
	    pci_get_device(dev) != PORTDRV_DEVICE)
		return (ENXIO);
	device_set_desc(dev, "portdrv (PCI backend)");
	return (BUS_PROBE_DEFAULT);
}

static int
portdrv_pci_attach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);
	struct portdrv_pci_softc *psc;
	int err;

	psc = malloc(sizeof(*psc), M_PORTDRV, M_WAITOK | M_ZERO);
	sc->sc_dev = dev;
	sc->sc_be = &portdrv_pci_backend;
	sc->sc_backend_priv = psc;

	psc->pci_bar_rid = PCIR_BAR(0);
	psc->pci_bar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &psc->pci_bar_rid, RF_ACTIVE);
	if (psc->pci_bar == NULL) {
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
		return (ENXIO);
	}

	err = portdrv_core_attach(sc);
	if (err != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, psc->pci_bar_rid,
		    psc->pci_bar);
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
	}
	return (err);
}

static int
portdrv_pci_detach(device_t dev)
{
	struct portdrv_softc *sc = device_get_softc(dev);
	struct portdrv_pci_softc *psc = sc->sc_backend_priv;

	portdrv_core_detach(sc);
	if (psc != NULL) {
		if (psc->pci_bar != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY,
			    psc->pci_bar_rid, psc->pci_bar);
		free(psc, M_PORTDRV);
		sc->sc_backend_priv = NULL;
	}
	return (0);
}

static device_method_t portdrv_pci_methods[] = {
	DEVMETHOD(device_probe,  portdrv_pci_probe),
	DEVMETHOD(device_attach, portdrv_pci_attach),
	DEVMETHOD(device_detach, portdrv_pci_detach),
	DEVMETHOD_END
};

static driver_t portdrv_pci_driver = {
	"portdrv_pci",
	portdrv_pci_methods,
	sizeof(struct portdrv_softc)
};

DRIVER_MODULE(portdrv_pci, pci, portdrv_pci_driver, 0, 0);
MODULE_VERSION(portdrv_pci, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, 1, 1, 1);
```

Again, notice what is present and what is not. The PCI-specific header includes are all here. The PCI probe is here. The `DRIVER_MODULE` macro is here. None of it is in the core. If you are writing a USB backend next, that file looks structurally identical but includes USB headers instead and registers with the USB subsystem.

### Header Organisation

The three headers `portdrv.h`, `portdrv_common.h`, and `portdrv_backend.h` look like this in miniature.

`portdrv.h` is the driver's public face within the driver's files:

```c
#ifndef _PORTDRV_H_
#define _PORTDRV_H_

#include <sys/malloc.h>

/* Softc, opaque to anything outside the core. */
struct portdrv_softc;

/* Core entry points called by backends. */
int  portdrv_core_attach(struct portdrv_softc *sc);
void portdrv_core_detach(struct portdrv_softc *sc);
int  portdrv_core_submit(struct portdrv_softc *sc,
          const void *buf, size_t len);

MALLOC_DECLARE(M_PORTDRV);

#endif /* !_PORTDRV_H_ */
```

`portdrv_common.h` contains the types that backends and the core share:

```c
#ifndef _PORTDRV_COMMON_H_
#define _PORTDRV_COMMON_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

/* Forward declaration of the backend interface. */
struct portdrv_backend;

/* The softc, visible to the backends so they can install sc_be, etc. */
struct portdrv_softc {
	device_t                     sc_dev;
	const struct portdrv_backend *sc_be;
	void                        *sc_backend_priv;

	struct mtx                   sc_mtx;
	struct cdev                 *sc_cdev;

	/* other common fields ... */
};

#endif /* !_PORTDRV_COMMON_H_ */
```

`portdrv_backend.h` contains the interface and the declarations of each backend's canonical instance:

```c
#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

#include <sys/types.h>
#include <machine/bus.h>

#include "portdrv_common.h"

struct portdrv_backend {
	const char *name;
	int   (*attach)(struct portdrv_softc *sc);
	void  (*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void     (*write_reg)(struct portdrv_softc *sc, bus_size_t off,
	                      uint32_t val);
	int   (*start_transfer)(struct portdrv_softc *sc,
	                        const void *buf, size_t len);
	int   (*poll_done)(struct portdrv_softc *sc);
};

extern const struct portdrv_backend portdrv_pci_backend;
extern const struct portdrv_backend portdrv_usb_backend;
extern const struct portdrv_backend portdrv_sim_backend;

#endif /* !_PORTDRV_BACKEND_H_ */
```

These headers are short on purpose. A header that takes a page to read is a header that is hiding something. Split the types where they are used, and keep each header free of unnecessary prototypes.

### The Makefile

The build side of this layout is pleasantly simple in FreeBSD. `bsd.kmod.mk` handles multi-file modules naturally. The Makefile looks like this:

```make
# Makefile for portdrv - Chapter 29 reference driver.

KMOD=	portdrv
SRCS=	portdrv_core.c \
	portdrv_sysctl.c \
	portdrv_ioctl.c \
	portdrv_dma.c

# Backends are selected at build time.
.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+=	portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_USB) && ${PORTDRV_WITH_USB} == "yes"
SRCS+=	portdrv_usb.c
CFLAGS+= -DPORTDRV_WITH_USB
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

# If no backend was explicitly selected, enable the simulation
# backend so that the driver still builds and loads.
.if !defined(PORTDRV_WITH_PCI) && \
    !defined(PORTDRV_WITH_USB) && \
    !defined(PORTDRV_WITH_SIM)
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SYSDIR?=	/usr/src/sys

.include <bsd.kmod.mk>
```

Build it with any combination of backends:

```sh
make clean
make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
```

The resulting `portdrv.ko` contains only the files you asked for. Dropping a backend is as easy as removing its make variable. This is the compile-time selection approach, and it works well for drivers whose backend set is known at build time.

### Compare with `/usr/src/sys/modules/uart/Makefile`

Open `/usr/src/sys/modules/uart/Makefile` and compare. The structure is similar in spirit: a `SRCS` list that names each file, plus conditional blocks that select backends and hardware-specific helpers based on architecture. The UART build is more elaborate than ours because the driver supports many backends and many hardware variants, but the shape is recognisable. Studying this file alongside the one in the reference driver will make both more understandable.

Take a few minutes to walk through the UART Makefile from the top. Notice how the `SRCS` list is built up through additions, not a single assignment: `SRCS+= uart_tty.c`, `SRCS+= uart_dev_ns8250.c`, and so on. Each addition can be guarded by a conditional that checks an architecture macro or a feature flag. The result is a single Makefile that produces different builds on `amd64`, `arm64`, and `riscv`, with different sets of backends enabled on each. That is the structure you want your own drivers to have once they grow to cover more than one platform.

### Makefile Idioms for Portable Drivers

A few Makefile idioms recur so often that they deserve explicit naming. You will see each of these in real FreeBSD module Makefiles and in the reference driver.

The **unconditional core list**:

```make
KMOD=	portdrv
SRCS=	portdrv_core.c portdrv_ioctl.c portdrv_sysctl.c
```

This is always the starting point: the files that are always compiled, regardless of configuration.

The **conditional backend list**:

```make
.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCS+=	portdrv_x86_helpers.c
.endif

.if ${MACHINE} == "arm64"
SRCS+=	portdrv_arm64_helpers.c
.endif
```

This pattern lets a file be included only on a specific architecture. It is less common in drivers than in kernel subsystems, but it is the right tool when genuinely architecture-specific code exists.

The **per-feature block**:

```make
.if defined(PORTDRV_WITH_DMA) && ${PORTDRV_WITH_DMA} == "yes"
SRCS+=		portdrv_dma.c
CFLAGS+=	-DPORTDRV_WITH_DMA
.endif
```

Each feature that can be toggled has one block. The block adds a source file and a preprocessor flag. This is the pattern you will use most often.

The **header dependency**:

```make
beforebuild: genheader
genheader:
	sh ${.CURDIR}/gen_registers.sh > ${.OBJDIR}/portdrv_regs.h
```

When a header is generated from a data file (for example, from a vendor-provided register description), this pattern ensures the header is up to date before the main build runs. Use it sparingly; each generated artefact is a maintenance point.

The **CFLAGS discipline block**:

```make
CFLAGS+=	-Wall
CFLAGS+=	-Wmissing-prototypes
CFLAGS+=	-Wno-sign-compare
CFLAGS+=	-Werror=implicit-function-declaration
```

This turns on warnings that catch common portability bugs. Treat new warnings as bugs, and fix them as they appear. The `-Werror=implicit-function-declaration` flag in particular catches the class of bug where a file calls a function without including the appropriate header; on some platforms the compiler would silently assume an `int` return type and proceed, producing a latent bug.

The **include-path addition**:

```make
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/include
CFLAGS+=	-I${SYSDIR}/contrib/portdrv_vendor_headers
```

When the driver has its own header directory, or when it depends on vendor-supplied headers checked into a contrib area, these `-I` additions are how you tell the compiler where to look. Keep the list short; each additional path increases the surface for header conflicts.

### When Multiple Modules Share Code

A question that arises as drivers proliferate is how to share helper code among them. The two approaches are:

**Library module.** A separate `.ko` file provides helper functions, and the dependent drivers declare `MODULE_DEPEND` on it. This is clean when the helpers are substantial, but each module load now requires loading the helper module first.

**Shared source file.** Multiple drivers list the same `.c` file in their `SRCS`. Each driver gets its own copy of the functions, compiled into its own `.ko`. This is simpler but duplicates code in every driver's binary.

For helpers under a few hundred lines, the shared source approach is usually right. The duplication is small and the build is simpler. For helpers over a thousand lines, the library module pays for itself because updates to the helper affect only one module, not every driver that uses it.

A third approach is to move the helper into the base kernel and have it exported. This is the right choice when the helper is genuinely general-purpose and belongs to the kernel ABI. For driver-specific helpers, it is overkill.

### Benefits of the Split

Why go to this much trouble? The benefits are not cosmetic.

**Faster builds.** A change to `portdrv_ioctl.c` recompiles one file and relinks the module. A monolithic driver recompiles everything every time.

**Easier testing.** The simulation backend can be loaded alone, without any hardware, and the core can be exercised completely. Unit-testing a driver this way changes what is possible.

**Cleaner API surface.** Each file exposes only what the others need. When a function is declared in `portdrv.h`, that declaration is a promise that the function is part of the driver's internal API. Functions that are not declared there are implicitly private to their file.

**Easier review.** A code reviewer who knows the layout can skim a patch by looking at which files changed. A patch that touches `portdrv_pci.c` only is doing PCI-specific work. A patch that touches `portdrv_core.c` is doing something that affects all backends, and deserves closer scrutiny.

**Lower cost to add a backend.** Once the layout is in place, a new backend is a single new file. In a monolithic driver, a new backend is a rewrite of half the file.

### Common Mistakes When Splitting a Driver

Some pitfalls are easy to blunder into.

**Moving too little.** A split that leaves bus-specific headers in the "core" file has not moved far enough. The core should include only subsystem headers (`sys/bus.h`, `sys/conf.h`, etc.), not bus-specific ones. If you catch yourself including `dev/pci/pcivar.h` in the core, you have left PCI-specific logic behind.

**Moving too much.** A split that moves every helper into its own file creates a dozen tiny files and a mesh of cross-dependencies. Balance is the goal. If three functions are always used together and are meaningful only together, they belong in the same file.

**Circular includes.** When header A includes header B and header B includes header A, the compiler complains about incomplete types. The fix is almost always a forward declaration in one of the headers, so the other header can include it safely.

**Silent API breakage.** When you move a function from `portdrv.c` to `portdrv_sysctl.c`, its declaration must be visible to anyone who calls it. Check the compiler warnings; a missing declaration usually shows up as an implicit function declaration warning before it turns into a link-time error.

**Forgotten entries in `SRCS`.** A new file that is not listed in `SRCS` is not compiled. The driver will compile and link if you are unlucky and all its symbols are already available elsewhere, or it will fail to link with a mysterious undefined-symbol error. When you add a file, always add it to `SRCS` in the same commit.

### Header Hygiene and Include Graphs

A source file's `#include` lines are a window on its dependencies. A file that includes `dev/pci/pcivar.h` is declaring, in the cleanest way a C programmer can, that it knows about PCI. A file that only includes `sys/param.h`, `sys/bus.h`, `sys/malloc.h`, and the driver's own headers is declaring independence from any specific bus. The discipline of keeping include lists small and focused is called header hygiene, and it is the quiet partner of the backend interface.

Three rules keep include graphs clean.

**Rule one: include what you use, declare what you do not.** If a file uses `bus_dma_tag_create`, it must include `<sys/bus.h>` and `<machine/bus.h>`. If it merely takes a pointer to a `bus_dma_tag_t` but does not dereference it, a forward declaration in a header may be enough. Over-including pulls in dependencies; under-including causes implicit function declaration warnings. Both are fixable, but the right target is exactly what is needed.

**Rule two: include the smallest header that gives you what you need.** `<sys/param.h>` is a fine catch-all, but it pulls in a large transitive graph. If you only need fixed-width integer types, `<sys/types.h>` may be enough. The build succeeds either way; the compilation time and the dependency graph shrink when you are disciplined.

**Rule three: never rely on transitive includes.** If your `.c` file uses `memset`, it must include `<sys/libkern.h>` (or `<string.h>` in userland), even if another header has already pulled it in for you. Transitive includes are not part of the contract; they change when another header is refactored. An explicit include is robust; an implicit one is a ticking bug.

When you split a driver according to the layout in this section, re-inspect every `.c` file's includes. Delete the ones you do not need; add the ones you were getting transitively. After the sweep, each file should have an includes list that matches what it actually does. The result is a source tree where a change to one header does not cascade unexpectedly through the rest.

A useful mental model is that the include graph is the **build-time equivalent of the backend interface**. Both declare what depends on what. Both are kept clean by discipline. Both pay for themselves when the driver is refactored. If the backend interface is the dependency graph of the running code, the include graph is the dependency graph of the source code, and both deserve the same care.

### Managing `CFLAGS` and Symbol Visibility

A multi-file driver eventually needs some build-time knobs for its own compilation. These usually take the form of additions to `CFLAGS` in the Makefile:

```make
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-Wall -Wmissing-prototypes
CFLAGS+=	-DPORTDRV_VERSION='"2.0"'
```

The first line makes the driver's own headers discoverable; the second turns on warnings that catch common portability mistakes; the third embeds a version string directly into the compiled module. Each flag is small; together they give the driver a small surface of control that does not leak into the source.

Be careful with symbol visibility. In a kernel module, every non-static function is a potentially exported symbol. If two drivers both define a function named `portdrv_helper`, the kernel will accept whichever loads first and reject the second, producing a confusing error. Prefix your symbols: name functions `portdrv_core_attach`, not `attach`; name functions `portdrv_pci_probe`, not `probe`. Static functions never become symbols and can be named more tersely.

This is not just a style issue. On FreeBSD, a kernel module's symbol table is shared with every other loaded module. An unprefixed name collides with everything else the kernel has ever loaded. Use a consistent prefix, and prefer `static` whenever the function does not need to be visible outside its file.

### A Dependency Graph on Paper

A good exercise when you first split a driver is to draw its include graph on paper. A box for each file; an arrow from file A to file B when A includes B. The graph should be acyclic, flow downward from source files to the most primitive headers, and contain no surprising edges.

For the reference driver `portdrv`, the graph looks roughly like this:

```text
portdrv_core.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h  ---> portdrv_common.h
	+-> <sys/param.h>, <sys/bus.h>, <sys/malloc.h>, ...

portdrv_pci.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h
	+-> <dev/pci/pcivar.h>, <dev/pci/pcireg.h>
	+-> <sys/bus.h>, <machine/bus.h>, <sys/rman.h>, ...

portdrv_sim.c
	|
	+-> portdrv.h
	+-> portdrv_common.h
	+-> portdrv_backend.h
	+-> <sys/malloc.h>
```

The PCI backend is the only file that reaches into `dev/pci/`. The core is the only file that pulls in the universal subsystem headers. The simulation backend is the lightest of the three because it talks to no real hardware.

Draw this graph before you commit a new file layout. If an arrow goes the wrong way, fix the includes before you write any more code. A graph that flows downward is a sign of healthy layering; a graph with cycles is a sign of a design that wants rethinking.

### Static Analysis and Continuous Reviews

Multi-file drivers benefit from a small amount of static analysis. The two tools that pay for themselves almost immediately are:

**`make -k -j buildkernel`** with warnings turned on. The FreeBSD build is usually clean, but a new file will often expose a forgotten prototype, an unused variable, or a signed/unsigned mismatch. Read every warning; fix every warning.

**`cppcheck`** or **`scan-build`** on the driver sources. Neither is perfect, but each catches a class of bug that the compiler does not. A missing free, a dereference of a possibly-NULL pointer, a dead branch, and a use-after-free are all common hits. Run one of them periodically, not necessarily on every commit.

For a large driver, consider running `clang-tidy` with a moderate check set. For a small driver, the compiler's warnings plus occasional human review are usually enough.

A multi-file driver is easier to review than a monolithic one precisely because each file has a small scope. Reviewers can take a file at a time, understand its responsibility, and check its conformance to the layout. A reviewer who knows the conventions can move through a large patch quickly because each file's role is obvious from its place in the layout. This is the review-cost payoff mentioned earlier, and it compounds over the life of the driver.

### Wrapping Up Section 4

You now have a concrete file layout you can apply to any driver: a core file, a small family of backend files, a header per concern, and a Makefile that lets you select backends at build time. The layout is not mandatory, but it matches what real FreeBSD drivers use in the wild, and it scales from small drivers to large ones without reorganisation. Disciplined header hygiene, a clean include graph, and consistent symbol prefixing are the companions of the layout; together they make the driver's source tree a pleasure to navigate.

The layout also teaches the reader where to look. A new maintainer who knows the conventions can navigate the driver on the first day rather than the tenth. Over the life of a long-lived project, that is worth more than it looks.

Section 5 turns from file organisation to the topic that is easiest to get subtly wrong: supporting multiple CPU architectures. The same driver must compile on `amd64`, `arm64`, `riscv`, and the rest of FreeBSD's ports, and it must behave correctly on each one. The tools are almost all about endianness, alignment, and word size, and using them properly is less effort than it looks.

## Section 5: Supporting Multiple Architectures

Architectural portability is the form of portability that requires the least code and the most attention. The kernel and the compiler already do most of the work for you; your job as a driver author is not to add cleverness but to avoid adding bugs. Every common architectural bug in driver code has a corresponding FreeBSD idiom that prevents it. This section introduces those idioms and shows how to use them.

The three axes along which architectures differ in ways that drivers can feel directly are endianness, alignment, and word size. We will take them in order, then look at how to test architectural portability without owning hardware for every architecture.

### How `bus_space` Varies Across Platforms

To understand why the abstraction matters, it helps to look briefly at how it is implemented on each of the architectures FreeBSD supports. The mechanism is different on each one, but the interface you program against is the same.

On `amd64`, the tag is simply a type tag that distinguishes memory-mapped I/O from I/O ports, and the handle is the virtual address of the mapped region. A `bus_space_read_4` call on `amd64` becomes, under the covers, a load instruction to the mapped address. The call is a thin wrapper over a raw load; there is essentially no indirection cost.

On `arm64`, the tag is a pointer to a table of function pointers, and the handle is the base address of the mapped region. A `bus_space_read_4` call on `arm64` becomes an indirect call through the function pointer table. The indirection is necessary because different ARM platforms can map memory with different cacheability and ordering attributes, and the function table encodes those details per platform. The cost is a single indirect call, which is measurable in tight loops but negligible in a typical driver.

On `riscv`, the mechanism is close to `arm64`, with similar rationale. On older `powerpc`, the tag and handle encode endianness configuration, so that a `bus_space_read_4` on a big-endian bus attached to a little-endian CPU does the right byte swap without the driver knowing.

All of this is invisible to the driver. Your code calls `bus_read_4(res, offset)` and gets the right value, on every architecture, at every bus configuration. The price is that you cannot shortcut the API; the moment you drop to a raw pointer, you lose all of this machinery, and your driver works on exactly one platform.

### Endianness in Three Questions

Endianness is the order in which the bytes of a multi-byte value are stored in memory. On a little-endian system like `amd64`, `arm64`, or `riscv`, the least significant byte of a 32-bit value comes first in memory, and the most significant byte comes last. On a big-endian system like some configurations of `powerpc`, the order is reversed. The CPU does not see the bytes individually in either case; it reads the word as a whole and interprets it according to the system's endianness. The problem for drivers is that hardware does not always share the CPU's view.

There are three questions a driver author must ask, and three classes of answer.

**Question 1: Does the device have a native byte order?** Many devices do. A PCI device may specify that a particular register contains a 32-bit value in little-endian order. Ethernet frames, by contrast, are big-endian. When the device's byte order differs from the CPU's byte order, the driver must convert between them explicitly.

**Question 2: Does the bus perform any implicit conversion?** Usually not. `bus_space_read_4` returns the 32-bit value as the host sees it, in host byte order. If the device stored that value in a different byte order, the conversion is the driver's responsibility. The same is true for DMA buffers read and written through `bus_dma`. The bytes in the buffer are the bytes the device wrote, in whatever order the device prefers; your code has to interpret them correctly.

**Question 3: Which direction am I converting?** Going from the device to the host, use `le32toh`, `be32toh`, and their 16-bit and 64-bit companions. Going from the host to the device, use `htole32`, `htobe32`, and their companions. The name convention is simple: the first form is the source, the second form is the destination, and `h` stands for host.

### The Endian Helpers in Detail

Open `/usr/src/sys/sys/_endian.h` and look at how the helpers are defined. On a little-endian host, `htole32(x)` is just `(uint32_t)(x)` because no swap is needed, and `htobe32(x)` is `__bswap32(x)` because the device expects the opposite order. On a big-endian host, the pattern is reversed: `htobe32(x)` is the identity and `htole32(x)` performs the swap. This means that your code can call the same helper on every architecture, and the right thing happens under the covers.

The full set of helpers you will use in driver code is:

```c
htole16(x), htole32(x), htole64(x)
htobe16(x), htobe32(x), htobe64(x)
le16toh(x), le32toh(x), le64toh(x)
be16toh(x), be32toh(x), be64toh(x)
```

Plus the network-byte-order shortcuts `htons`, `htonl`, `ntohs`, `ntohl`, which are equivalent to the `htobe`/`betoh` family for 16-bit and 32-bit values.

Use them whenever you store a multi-byte value in a device register or DMA buffer, or whenever you read one. Never write:

```c
sc->sc_regs->control = 0x12345678;  /* Wrong if the device expects LE. */
```

Write instead:

```c
widget_write_reg(sc, REG_CONTROL, htole32(0x12345678));
```

The cost at runtime is zero on a little-endian host, because the compiler inlines the helper to a no-op. The benefit on a big-endian host is correctness.

### A Simple Mental Rule

When I am teaching this pattern to new driver authors, I use one rule that catches most mistakes before they happen: **every multi-byte value that leaves the CPU or enters it through hardware must pass through an endian helper**. Registers. DMA buffers. Packet headers. Descriptor rings. Every single one. If you see a `*ptr = value` or `value = *ptr` pattern on a multi-byte quantity that is device-visible, and the endpoints do not use an endian helper, that is a bug or a lucky accident, and the only way to tell which is to know the architecture you are running on.

The same rule, applied consistently, means that moving a driver to a big-endian platform becomes almost free. If the endian helpers were in place from the start, the platform change does nothing to the code. If they were not, the move is a hunt through the entire driver for every read and write that touched device memory, and every one of them is a potential bug.

### Alignment and the NO_STRICT_ALIGNMENT Macro

The second architectural axis is alignment. A 32-bit value is *aligned* at an address that is a multiple of four. On `amd64`, unaligned accesses are permitted and only slightly slower than aligned ones. On some ARM cores, an unaligned access can produce a bus fault or silently corrupt data depending on the specific instruction and configuration.

The kernel's policy is that driver code should produce aligned accesses. If you cast a buffer pointer to `uint32_t *` and dereference it, the buffer had better be aligned to four bytes. Most kernel allocators give you naturally aligned memory, so this usually happens for free. It becomes tricky when parsing a wire protocol that does not align its fields, like an Ethernet frame whose IP header is misaligned by two bytes because of the Ethernet header's odd size.

FreeBSD exposes the macro `__NO_STRICT_ALIGNMENT`, defined in `/usr/src/sys/x86/include/_types.h` and its architecture peers, which drivers occasionally check to see whether the platform tolerates unaligned accesses. For general-purpose driver code, you should not rely on it. Instead, use byte-at-a-time accessors, or `memcpy`, to copy multi-byte values out of unaligned memory before interpreting them. The compiler is good at optimising `memcpy(&val, ptr, sizeof(val))` into a single aligned load on architectures that support it, and a byte-at-a-time load on architectures that need it.

A typical pattern for safely reading an unaligned 32-bit value from a byte buffer is:

```c
static inline uint32_t
load_unaligned_le32(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return (le32toh(v));
}
```

This works on every architecture. The `memcpy` handles alignment; the `le32toh` handles endianness. Use this kind of helper whenever you pull a multi-byte value from a wire format or an on-device buffer that may not be aligned.

### Word Size: 32 vs 64 Bits

The third architectural axis is word size. On a 32-bit platform, a pointer is four bytes and `long` is typically four bytes. On a 64-bit platform, both are eight bytes. Most of the FreeBSD kernel and all of the common architectures today are 64-bit, but not all of them, and even 64-bit platforms sometimes run 32-bit compatibility code.

The failure mode to watch for is writing `int` or `long` where the size matters. A register value is a specific number of bits, not "whatever `int` happens to be." If a register is 32 bits, write `uint32_t`. If it is 16 bits, write `uint16_t`. This rule is easy to remember and completely eliminates a whole class of bugs.

FreeBSD exposes these types through `/usr/src/sys/sys/types.h` and its indirect includes. The standard fixed-width typedefs are:

```c
int8_t,   uint8_t
int16_t,  uint16_t
int32_t,  uint32_t
int64_t,  uint64_t
```

And the kernel also exposes `intmax_t`, `uintmax_t`, `intptr_t`, and `uintptr_t` when you need to store an integer that must be at least as wide as any other integer, or that must be the width of a pointer.

Do not use `u_int32_t` or `u_char` in new code. They are legacy names that still exist for backward compatibility but are not the preferred style. The convention in modern FreeBSD driver code is the `uint32_t` family.

### Bus Address Types

A related family of types governs addresses that the device and the bus use. These are defined per architecture in `/usr/src/sys/amd64/include/_bus.h`, `/usr/src/sys/arm64/include/_bus.h`, and the rest. The key types are:

- `bus_addr_t`: a bus address, typically `uint64_t` on 64-bit platforms.
- `bus_size_t`: a size on the bus, the same underlying width as `bus_addr_t`.
- `bus_space_tag_t`: the opaque tag handed to `bus_space_*` functions.
- `bus_space_handle_t`: the opaque handle into the mapped region.
- `bus_dma_tag_t`: the opaque tag used by `bus_dma`.
- `bus_dmamap_t`: the opaque DMA map handle.

Using these types instead of bare `uint64_t` or `void *` is how the kernel enforces the architectural abstraction. On `amd64`, they may be simple integers; on `arm64`, some are pointers to structures. Your code compiles the same way everywhere and does the right thing on each platform.

### Testing on Emulated Architectures

One of the most practical questions is: if you only have an `amd64` machine, how do you test that the driver works on `arm64` or on a big-endian system? The answer is emulation.

QEMU, which FreeBSD supports well, can run `arm64` guests on an `amd64` host. The performance is slower than native, but enough for functional testing. A simple workflow is:

1. Install the FreeBSD `arm64` release into a QEMU image.
2. Boot the image under QEMU with enough disk and RAM.
3. Cross-build the driver (or build it inside the guest).
4. Load the module and run your tests.

FreeBSD's build infrastructure supports cross-building via `make buildkernel TARGET=arm64 TARGET_ARCH=aarch64`. For kernel modules, the same `TARGET` and `TARGET_ARCH` variables apply. You end up with an `arm64` module that you can copy into the `arm64` guest and test there.

For big-endian testing, QEMU's `powerpc64` and `powerpc` targets exist, and FreeBSD has releases for both. Big-endian PowerPC testing is particularly valuable because it is the fastest way to find hidden endianness bugs: the driver that works perfectly on `amd64` but mangles every register on PowerPC has almost certainly missed an `htole32` somewhere.

For everyday work, it is enough to run the driver once on a cross-architecture guest as part of your release process. You do not need to do it on every commit. But you should not skip it entirely, because the bugs it finds are the bugs your users would otherwise find for you.

### Case Study: A DMA Descriptor Ring

A concrete example of how these idioms combine is a DMA descriptor ring. Many devices expose a ring of descriptors in shared memory: each descriptor describes one transfer, and the device walks the ring autonomously as it services transfers. The ring is a prime example of a shared structure where endianness, alignment, word size, and cache coherency all interact.

A typical descriptor might look like this in C:

```c
struct portdrv_desc {
	uint64_t addr;      /* bus address of the payload */
	uint32_t len;       /* payload length in bytes */
	uint16_t flags;     /* control bits */
	uint16_t status;    /* completion status */
};
```

Four considerations apply to this structure.

**Endianness.** The device reads and writes these fields. The byte order in which it interprets them comes from the datasheet. If the device expects little-endian, every write from the CPU must pass through `htole64` or `htole32` as appropriate, and every read from the device must pass through `le64toh` or `le32toh`.

**Alignment.** The device reads the whole descriptor in one transaction, so the descriptor must be aligned on at least its natural boundary. For this layout, that is 8 bytes because of the 64-bit `addr` field. The `bus_dma_tag_create` call that allocates the ring must set `alignment` to at least 8.

**Word size.** The `addr` field is 64 bits because the device may need to reach any physical address on a 64-bit system. On a 32-bit system with a 32-bit-only device, you could declare it `uint32_t`, but the portable choice is `uint64_t` with the upper bits zero when unused.

**Cache coherency.** After the CPU writes a descriptor and before the device reads it, the CPU's caches must be flushed to memory so the device sees the update. After the device writes the status field and before the CPU reads it, the CPU's caches for the ring memory must be invalidated so it does not read a stale value. The `bus_dmamap_sync` call with `BUS_DMASYNC_PREWRITE` handles the first case; with `BUS_DMASYNC_POSTREAD` handles the second.

Putting these together, the code to enqueue a descriptor looks roughly like this:

```c
static int
portdrv_enqueue(struct portdrv_softc *sc, bus_addr_t payload_addr, size_t len)
{
	struct portdrv_desc *d;
	int idx;

	/* Pick a ring slot. */
	idx = sc->sc_tx_head;
	d = &sc->sc_tx_ring[idx];

	/* Populate the descriptor in device byte order. */
	d->addr   = htole64(payload_addr);
	d->len    = htole32((uint32_t)len);
	d->flags  = htole16(DESC_FLAG_VALID);
	d->status = 0;

	/* Ensure the CPU's writes reach memory before the device reads. */
	bus_dmamap_sync(sc->sc_ring_tag, sc->sc_ring_map,
	    BUS_DMASYNC_PREWRITE);

	/* Notify the device. Use a barrier to ensure the notification
	 * is seen after the descriptor writes. */
	bus_barrier(sc->sc_bar, 0, 0,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	portdrv_write_reg(sc, REG_TX_DOORBELL, (uint32_t)idx);

	sc->sc_tx_head = (idx + 1) % sc->sc_ring_size;
	return (0);
}
```

Every one of the four considerations is visible in these few lines. The `htole*` calls handle endianness. The `bus_dmamap_sync` call handles cache coherency. The `bus_barrier` call handles ordering. The `uint64_t` for `addr` handles word size.

And, crucially, this code is identical on `amd64`, `arm64`, and `powerpc64`. The helpers become the right operation on each platform under the covers. The driver author writes one version and it works everywhere.

### A Register Layout Worth Doing Carefully

Here is a register layout from a hypothetical device, written as a C structure:

```c
struct widget_regs {
	uint32_t control;       /* offset 0x00 */
	uint32_t status;        /* offset 0x04 */
	uint32_t data;          /* offset 0x08 */
	uint8_t  pad[4];        /* offset 0x0C: padding */
	uint64_t dma_addr;      /* offset 0x10 */
	uint16_t len;           /* offset 0x18 */
	uint16_t flags;         /* offset 0x1A */
};
```

Two things about this are worth noticing. First, padding is explicit. If the register block has a hole between `data` and `dma_addr`, write a `pad[]` member. Do not rely on the compiler to insert padding of the right size, because the compiler does not know about the device. Second, the types match the register widths exactly. A 16-bit `len` field is `uint16_t`, not `int` or `u_int`.

Do not, however, read and write these registers by casting a pointer to the structure. The pattern

```c
struct widget_regs *r = (void *)sc->sc_regs_vaddr;
r->control = htole32(value);
```

is a portability trap. On `amd64` it works. On `arm64` it may work or may fault depending on the alignment of the mapping. On some ARM cores it may produce implementation-specific behaviour. The kernel provides `bus_space` for a reason; use it.

The structure above is useful for documentation and for constant names, but the code should access the registers through accessors that use `bus_space` or `bus_read_*`/`bus_write_*`.

### Using the Newer `bus_read_*`/`bus_write_*` Helpers

FreeBSD provides a slightly friendlier family of functions, `bus_read_1`, `bus_read_2`, `bus_read_4`, `bus_read_8`, and their `bus_write_*` counterparts. They take a `struct resource *` rather than the pair (`tag`, `handle`), so they are shorter at the call site. Internally they use `bus_space` under the covers. For modern drivers they are the preferred idiom.

```c
uint32_t v = bus_read_4(sc->sc_bar, REG_CONTROL);
bus_write_4(sc->sc_bar, REG_CONTROL, v | CTRL_START);
```

These functions are defined in `/usr/src/sys/sys/bus.h`. Use them in preference to the older `bus_space_*` forms when you are writing new code; both work correctly on all supported architectures.

### Common Mistakes When Supporting Multiple Architectures

A short list of pitfalls, each of which would bite a naive driver on at least one FreeBSD platform.

**Using `int` for a register width.** If the register is 32 bits, use `uint32_t`. An `int` is the right size on every FreeBSD architecture today, but it is signed, and bit patterns with the top bit set become negative numbers. That bites when you compare the value or shift it.

**Casting pointers to device memory.** Writing through a `volatile uint32_t *` pointer to a mapped region works on some architectures and not others. Use `bus_read_*`/`bus_write_*`. Always.

**Packing without packing attributes.** If you really must use a struct to describe a wire-format layout, use `__packed` to make sure the compiler does not insert alignment padding. But be aware that accessing fields of a packed struct is itself subject to alignment hazards on strict-alignment architectures. A safer pattern is to read fields out of a byte buffer with `memcpy` and endian helpers, as shown earlier.

**Assuming `sizeof(long) == 4` or `sizeof(long) == 8`.** On 32-bit platforms, `long` is 4; on 64-bit platforms, it is 8. If the size matters, use `uint32_t` or `uint64_t` explicitly.

**Forgetting about 32-bit platforms.** Even when the mainline FreeBSD world is largely 64-bit, there are still 32-bit ARM deployments, 32-bit MIPS deployments, and legacy `i386` deployments. Testing only on `amd64` is not a portability guarantee. At minimum, cross-build on a 32-bit target periodically.

### Memory Barriers and Ordering

Register and memory accesses do not always reach their targets in the order your code writes them. Modern CPUs reorder loads and stores as an optimisation, and so do bus bridges and memory controllers. On most common `amd64` configurations, the hardware's reordering is conservative enough that drivers rarely notice. On `arm64`, `riscv`, and other weakly-ordered architectures, the reordering is more aggressive, and a driver that depends on a specific order of register accesses can fail in mysterious ways if it does not tell the hardware about that dependency.

The tool for expressing ordering is the **memory barrier**. FreeBSD provides two families:

- `bus_space_barrier(tag, handle, offset, length, flags)` for ordering with respect to device accesses. The `flags` argument is a combination of `BUS_SPACE_BARRIER_READ` and `BUS_SPACE_BARRIER_WRITE`.
- `atomic_thread_fence_rel`, `atomic_thread_fence_acq`, and friends for ordering with respect to normal memory. These are found in `/usr/src/sys/sys/atomic_common.h` and the per-architecture atomic files.

A typical use of `bus_space_barrier` occurs when you arm an interrupt after setting up a DMA descriptor:

```c
/* Program the DMA descriptor. */
portdrv_write_reg(sc, REG_DMA_ADDR_LO, htole32((uint32_t)addr));
portdrv_write_reg(sc, REG_DMA_ADDR_HI, htole32((uint32_t)(addr >> 32)));
portdrv_write_reg(sc, REG_DMA_LEN, htole32(len));

/* Ensure the descriptor writes are visible before we arm the IRQ. */
bus_barrier(sc->sc_bar, 0, 0,
    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

/* Now it is safe to tell the device to start. */
portdrv_write_reg(sc, REG_DMA_CTL, DMA_CTL_START);
```

On a strongly ordered architecture such as `amd64`, the `bus_barrier` call is essentially free and the barrier is implicit in most cases. On `arm64`, the barrier is a real instruction that prevents the CPU from reordering the register writes, and without it the device could see the `DMA_CTL_START` register written before the address registers, which is almost certainly a bug.

Use barriers whenever your code depends on a specific order of register accesses, particularly across the handoff between the CPU and the device. Read your datasheet for the cases where a specific write order is required, and bracket those sections with barriers. Omitting them is the fastest way to write a driver that works on your desk and fails in production on a different machine.

### Cache Coherency and `bus_dmamap_sync`

The third ordering issue is cache coherency. On some architectures, the CPU has caches that are not automatically coherent with DMA. If you write to a DMA buffer from the CPU and then tell the device to read it, the device may see stale data that is still in the CPU cache. Conversely, if the device writes into a DMA buffer, the CPU may read stale data that was cached before the write.

FreeBSD handles this through `bus_dmamap_sync`. After writing to a buffer that the device will read, you call:

```c
bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, BUS_DMASYNC_PREWRITE);
```

After the device has written to a buffer that the CPU will read, you call:

```c
bus_dmamap_sync(sc->sc_dmat, sc->sc_dmap, BUS_DMASYNC_POSTREAD);
```

On `amd64`, where the caches are coherent with DMA, these calls are usually no-ops. On architectures where they are not coherent, `bus_dmamap_sync` emits the right cache-flush or cache-invalidate operation. A driver that uses `bus_dma` but skips these sync calls works on `amd64` and fails on other architectures.

As with endian helpers, the rule is uniform: every DMA buffer that is shared between the CPU and the device must pass through `bus_dmamap_sync` at the handoff. The buffer goes from CPU to device: `BUS_DMASYNC_PREWRITE`. The buffer comes back from device to CPU: `BUS_DMASYNC_POSTREAD`. The sync calls cost nothing on coherent systems and are essential on non-coherent ones. Use them consistently, and architectural portability for DMA buffers is largely free.

### Bounce Buffers and Address Limits

Some devices cannot reach every address in the system's physical memory. A 32-bit device on a 64-bit system can only address the first four gigabytes of physical memory; anything above that is unreachable. FreeBSD's `bus_dma` handles this through **bounce buffers**: if the physical address of a buffer is outside the device's reach, `bus_dma` allocates a temporary buffer that is within reach, copies data between the real buffer and the bounce buffer, and presents the device with the bounce buffer's address.

Bounce buffers are invisible to the driver if the driver uses `bus_dma` correctly. The `lowaddr` and `highaddr` fields of `bus_dma_tag_create` tell the kernel what the device can and cannot reach. If the driver sets `lowaddr` to `BUS_SPACE_MAXADDR_32BIT`, the kernel will use bounce buffers for any buffer above 4 GB. The `bus_dmamap_sync` calls handle the copy automatically.

Bounce buffers are not free. They cost memory, they cost a copy per operation, and they cost some latency. Well-designed devices avoid them by supporting 64-bit addressing. But a driver author has no control over the device's address width; the only portability-relevant decision is to declare the limits correctly and let `bus_dma` handle the rest. A driver that lies about the device's limits will corrupt memory silently on a large system. A driver that tells the truth will work on every system, with bounce buffers when needed.

### Architectural Smoke Tests

Architectural portability is verified by running the driver on a different architecture. Even one such test, once, catches a surprising amount of latent bugs. The minimum viable smoke test is:

1. Build or download a FreeBSD release for a different architecture, typically `arm64` or `powerpc64`.
2. Install the release into a QEMU guest.
3. Build the driver inside the guest, or cross-build it on the host.
4. Load the module.
5. Exercise the driver's main features.
6. Observe whether the driver behaves as expected.

Even this minimum is revealing. The first time you run your driver on a big-endian host, any missing `htole32` or `be32toh` will manifest as obviously wrong register values. The first time you run it on `arm64`, any raw pointer cast to device memory will usually cause an alignment fault on the first access. The first time you build it on `riscv`, any hard-coded assumption about instruction set extensions will fail at compile time. Each bug takes minutes to find because its symptom is immediate.

You do not need to do this on every commit. Once per release is enough for most drivers. The cost is low (a QEMU guest and an hour of your time), and the bugs you catch are the bugs your users would otherwise report.

### The Realities of Non-x86 FreeBSD

A practical note on which architectures matter in the FreeBSD world today. At the time of writing, `amd64` is the overwhelmingly dominant production architecture, followed by `arm64` (growing rapidly in embedded and server deployments), `riscv` (growing in research and specific embedded niches), and `powerpc64` (used in certain HPC and legacy environments). The older `i386` platform is still supported for legacy deployments. The ARM 32-bit family (`armv7`) is supported but less common than `arm64`.

For a driver that is expected to live in the mainline tree, working correctly on `amd64`, `arm64`, and at least compiling on `riscv` is usually sufficient. The `powerpc64` port is a useful test for big-endian issues even if you do not have end users on the platform. The `i386` port is a useful test for 32-bit word-size issues. Supporting all of these is the project-wide default; dropping support for any of them is a decision that needs a specific reason.

For a driver that is maintained outside the tree, the question is whose hardware you are supporting. If your users are on `amd64` only, you can legitimately target only that platform. But the habit of writing portable code costs little and protects against surprise, and you will often find that the same driver turns out to be useful on a platform you did not originally target.

### Aside: CHERI-BSD and Morello

One corner of the FreeBSD universe sits outside the production architectures and deserves a brief note for curious readers. If you ever encounter a board called **Morello**, or an operating system tree called **CHERI-BSD**, you are looking at a research platform rather than a mainline target. It is worth knowing what these names mean, because they change assumptions that this chapter has otherwise treated as universal.

**What CHERI is.** CHERI stands for Capability Hardware Enhanced RISC Instructions. It is an instruction-set extension developed jointly by the University of Cambridge Computer Laboratory and SRI International. The goal is to replace the plain pointer model that C has used since the 1970s with **capability pointers**: pointers that carry their own bounds, permissions, and a hardware-checked validity tag. A capability is not just an integer address. It is a small object that the processor knows how to dereference, and it traps if the program tries to use it outside the region it was granted access to.

The practical effect is that many classes of memory-safety bug become hardware-detectable. A buffer overflow that walks off the end of an array no longer corrupts adjacent memory; it traps. A use-after-free no longer quietly reads whatever lives at the old address; the freed capability can be revoked so its tag bit becomes invalid, and any attempt to dereference it traps. Pointer forgery in the classical sense becomes impossible, because capabilities cannot be synthesised from plain integers.

**What Morello is.** Morello is Arm's prototype implementation of CHERI on top of the Armv8.2-A instruction set. The Morello Program, announced by Arm together with UK Research and Innovation in 2019, produced the first widely available CHERI-capable hardware: development boards and a reference system-on-chip that researchers, universities, and some industry partners could obtain and program. The Morello board is the closest thing the world currently has to a production-grade CHERI machine, but it is explicitly a research prototype. It is not sold as a general-purpose server platform, and it is not expected to become one in its current form.

**What CHERI-BSD is.** CHERI-BSD is a fork of FreeBSD maintained by the Cambridge CHERI project that targets CHERI hardware, including Morello. It is the operating system in which most of the practical work of porting userland and kernel code to a capability architecture takes place. CHERI-BSD is a research platform. It tracks FreeBSD closely but is not a mainline FreeBSD release, it is not supported by the FreeBSD Project in the same sense as `amd64` or `arm64`, and it is not a target that a typical driver author needs to ship for today. Its purpose is to let researchers and early adopters study what happens to a real operating system kernel when the pointer model changes beneath it.

**What a driver author needs to know.** Even if you never touch CHERI-BSD, it is useful to know the shape of the assumptions it breaks, because those assumptions appear in ordinary driver code without anyone noticing. Three points stand out.

First, **capability pointers carry sub-object bounds**. A pointer into the middle of a `struct softc` is not merely a byte address; it is bounded to whatever subrange the allocator or language construct gave it. If a driver uses pointer arithmetic to walk from one field to another without going through the enclosing pointer, that arithmetic can trap on CHERI even when it appeared harmless on `amd64`. The remedy is usually to derive pointers from the structure's base pointer using proper C idioms and to avoid casts that erase type information. This is the same discipline this chapter has been recommending for non-CHERI portability; CHERI simply makes the failure mode immediate rather than occasional.

Second, **freed capabilities can be revoked**. CHERI-BSD can sweep memory to invalidate capabilities whose backing allocation has been released, so that dangling pointers cannot be used even indirectly. A driver that stashes a pointer in a sysctl node, a device-tree entry, or some cross-subsystem handle, and then frees the underlying allocation without telling the kernel, can leave behind a revoked capability. On `amd64` the use-after-free might simply read garbage; on CHERI it traps deterministically. That is generally a good thing, but it means the driver's lifecycle discipline has to be honest about every reference it hands out.

Third, **kernel APIs look similar but carry capability semantics underneath**. The `bus_space(9)` and `bus_dma(9)` interfaces look the same at the source level on CHERI-BSD as on mainline FreeBSD, but the values they return are capabilities rather than plain addresses. A driver that uses these APIs through the intended accessors will usually port with little change. A driver that casts a `bus_space_handle_t` to a `uintptr_t` and then back, or that manufactures pointers to device memory by integer arithmetic, will break, because the capability metadata is lost in the integer round-trip.

Some drivers port to CHERI-BSD with minimal changes because they already stayed inside the accessors this chapter has been recommending. Others need explicit **capability discipline**: reviewing every cast, every hand-rolled pointer arithmetic, and every place where a pointer is stored in a non-pointer type. The CHERI-BSD project publishes porting experience reports for various subsystems, and reading one or two of them is a fast way to see what that discipline looks like in practice. The volume of change needed varies widely by driver, and no one should assume either that a given driver is trivially CHERI-clean or that it needs a total rewrite, without actually trying.

**Where to look.** The foundational technical description is the 2014 ISCA paper *The CHERI Capability Model: Revisiting RISC in an Age of Risk*, which is listed in the FreeBSD documentation tree at `/usr/src/share/doc/papers/bsdreferences.bib`. Subsequent papers from the same group describe the Morello implementation, capability revocation, compiler support, and real-world porting experience. For practical materials, the CHERI-BSD project and the Cambridge Computer Laboratory's CHERI research group maintain documentation, build instructions, and porting notes; search for the project names rather than relying on a URL printed here, because research project sites move over time.

The point of mentioning CHERI-BSD in a portability chapter is not to push you toward it. It is to tell you that the habits this chapter has been teaching, using accessors rather than raw pointers, respecting the distinction between addresses and opaque handles, and avoiding gratuitous casts, pay dividends on more than one architecture. They are the same habits that make a driver survive on `arm64`, on `riscv`, and on experimental capability machines. The further you stay from clever pointer tricks, the smaller your driver's CHERI surprise surface becomes, even if you never compile for CHERI at all.

### Wrapping Up Section 5

You have met the three axes along which architectures differ, and the FreeBSD idioms that address each. Endianness is handled by the endian helpers; alignment by `bus_read_*` and `memcpy`; word size by fixed-width types. Memory barriers handle the ordering of accesses that are visible to the device, and `bus_dmamap_sync` handles the coherency between CPU caches and DMA buffers. Used consistently, these idioms reduce architectural portability to almost no extra work per driver and almost no extra risk. A driver that uses them well on `amd64` will usually compile cleanly and run correctly on `arm64`, `riscv`, `powerpc`, and the others with nothing more than a recompile and a smoke test.

In Section 6, we turn from architectural portability to build-time flexibility: how to use conditional compilation and kernel build options to select features, enable debugging, and switch between real and simulated backends without cluttering the source with `#ifdef` soup.

## Section 6: Conditional Compilation and Build-time Flexibility

Conditional compilation is the art of making one source tree produce different binaries depending on what was requested at build time. It is tempting, it is sometimes necessary, and it is spectacularly easy to abuse. A driver that uses `#ifdef` thoughtfully is cleaner and more maintainable than one that does not. A driver that reaches for `#ifdef` at every decision becomes a thicket that no one wants to touch.

This section explains when conditional compilation is the right tool, which forms of it are preferred in FreeBSD, and how to keep the source readable in the presence of real build-time variation.

### The Three Levels of Conditional Compilation

Conditional compilation in a FreeBSD driver happens at three distinct levels. Each has a different purpose, and choosing the right level for a given problem prevents most of the ugliness that conditional compilation is infamous for.

**Level one: architecture-specific code.** Sometimes you must write different code for different CPU architectures. An example is a specific byte-swapping inline assembly for a platform that lacks a general compiler builtin. FreeBSD handles this by putting the per-architecture code in a per-architecture file. Look at how `/usr/src/sys/amd64/include/_bus.h` differs from `/usr/src/sys/arm64/include/_bus.h`. Each file contains the definitions appropriate for its architecture. The core source does not use `#ifdef __amd64__` to select between them; it includes `<machine/bus.h>`, and the kernel build picks the right concrete file via include paths. This mechanism is called the **machine-specific header**, and you should prefer it whenever you need more than a few lines of per-architecture code. If you put a large block of `#ifdef __arm64__` in a driver source file, you are probably reinventing this mechanism badly.

**Level two: optional features.** Optional features are things that your driver can be built with or without, and whose presence is a choice, not an architectural necessity. Simulation backends, debug tracing, sysctl nodes, optional protocol support. FreeBSD handles these through the kernel options system: you add `options PORTDRV_DEBUG` to the kernel configuration or pass `-DPORTDRV_DEBUG` to `make`. Inside the code, you guard the relevant blocks with `#ifdef PORTDRV_DEBUG`. Because the flag is yours, you control it cleanly, and the feature is either compiled in or not.

**Level three: API compatibility.** FreeBSD evolves. A macro gets renamed, a function signature changes, a behaviour that used to be implicit becomes explicit. A driver that wants to compile on both an older release and a newer release uses `__FreeBSD_version` to branch on the kernel version. This is the most subtle form of conditional compilation, because the same logical operation is expressed two ways and you must maintain both until the older version is no longer supported. The right approach is to keep these branches small and to refactor them into short helper functions or macros.

### The `__FreeBSD_version` Macro

Every FreeBSD kernel defines `__FreeBSD_version` in `/usr/src/sys/sys/param.h`. It is a single integer that encodes the release number: for example, `1403000` corresponds to FreeBSD 14.3-RELEASE. You can use it to guard code that depends on a specific API change:

```c
#if __FreeBSD_version >= 1400000
	/* Use the new if_t API. */
	if_setflags(ifp, flags);
#else
	/* Use the older direct struct access. */
	ifp->if_flags = flags;
#endif
```

Two rules about `__FreeBSD_version`. First, use it sparingly; every guard is a branch that must be tested in both configurations. Second, abstract the branch into a helper as soon as it appears more than twice. If you need `if_setflags` in ten places, write a helper macro in a compatibility header and use it everywhere, rather than duplicating the `#if` at every call site.

Compatibility is a compounding cost. Two `#if` blocks are manageable, and twenty are a maintenance burden. Make every one count, and retire them as soon as the old release drops out of support.

For a feel of what this looks like in tree, open `/usr/src/sys/dev/gve/gve_main.c`. The Google Virtual Ethernet driver is current, in-tree, and supports several releases at once, so its `__FreeBSD_version` guards document recent API migrations with minimum ceremony. Two short examples stand out. The first is in the attach path, where the driver sets the interface flags:

```c
#if __FreeBSD_version >= 1400086
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
#else
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_KNOWSEPOCH);
#endif
```

Older releases required drivers to set `IFF_KNOWSEPOCH` to declare that they used the network epoch correctly. From `1400086` onward the kernel no longer needs that opt-in, and drivers stop setting the flag. The branch is small, the accessor call (`if_setflags`) is the same on both sides, and only the flag list differs. This is the minimal shape these guards should take.

The second example is in the module declaration, at the end of the same file:

```c
#if __FreeBSD_version < 1301503
static devclass_t gve_devclass;

DRIVER_MODULE(gve, pci, gve_driver, gve_devclass, 0, 0);
#else
DRIVER_MODULE(gve, pci, gve_driver, 0, 0);
#endif
```

At `1301503`, the `DRIVER_MODULE` macro stopped taking a `devclass_t` argument; newer releases pass `0` in its place and older releases still need a real `devclass_t`. Both branches still register the driver with `DRIVER_MODULE`; the difference is a single argument and the `static` variable that feeds it. When your own driver needs to span this boundary, your guards should look as plain as these. If they do not, it is usually a sign that the compatibility shim wants to live in a small helper macro rather than at every call site.

### The Kernel Options Mechanism

For driver-specific options, FreeBSD's kernel options machinery gives you a clean way to expose feature flags. The mechanism has three parts:

1. The option is declared in a per-driver `options` file that lives alongside the driver's source, for in-tree drivers. For out-of-tree drivers like your reference driver, you define the option via a `CFLAGS+= -DPORTDRV_SOMETHING` in the Makefile and guard code with `#ifdef PORTDRV_SOMETHING`.

2. The option is set or unset at build time. For kernel builds, it goes into the kernel configuration file (`/usr/src/sys/amd64/conf/GENERIC` or a custom file) as `options PORTDRV_SOMETHING`. For module builds, it goes into the Makefile or on the `make` command line as `-DPORTDRV_SOMETHING`.

3. The code reads the option with `#ifdef PORTDRV_SOMETHING`.

This is a slim mechanism, but it is enough for most needs. A driver that uses it carefully has a handful of options, each guarding a small, well-defined piece of functionality, and each with a plain-English description in the README.

### Simulated Mode as a Build Option

A good example of the kernel-options pattern is a simulated mode. The idea is to let the driver be built and loaded without any real hardware, by selecting a software backend that mimics the device. You have already seen this pattern in Section 3; now we express it as a compile-time flag.

The Makefile adds:

```make
.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+=	portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif
```

The core registers the simulated backend in its module load if the flag is set:

```c
static int
portdrv_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
#ifdef PORTDRV_WITH_SIM
		/* Create a single simulated instance. */
		portdrv_sim_create();
#endif
		return (0);
	case MOD_UNLOAD:
#ifdef PORTDRV_WITH_SIM
		portdrv_sim_destroy();
#endif
		return (0);
	}
	return (0);
}
```

And `portdrv_sim.c` is only compiled if the flag is set.

This pattern has several benefits at once. The driver can be developed on a machine without the real hardware. The test suite can be run on any FreeBSD VM. Reviewers can load the driver and exercise the core logic, which is usually where the interesting bugs are, without needing the hardware. And the production builds, which do not pass `PORTDRV_WITH_SIM=yes`, exclude the simulation code entirely so that no stub backend is present in shipping binaries.

### Debug Builds

The second typical option is a debug build. Real drivers usually have a `PORTDRV_DEBUG` option that turns on verbose logging, additional assertions, and sometimes slower paths that validate invariants more aggressively. The pattern is:

```c
#ifdef PORTDRV_DEBUG
#define PD_DBG(sc, fmt, ...) \
	device_printf((sc)->sc_dev, "DEBUG: " fmt "\n", ##__VA_ARGS__)
#else
#define PD_DBG(sc, fmt, ...) do { (void)(sc); } while (0)
#endif
```

Everywhere in the source that needs a conditional debug message uses `PD_DBG(sc, "got %u bytes", len)`. In a release build, the macro evaluates to nothing, so the message is compiled away entirely. In a debug build, the macro prints. The call sites are not guarded with `#ifdef`; the macro is.

This is the key trick for keeping conditional compilation readable: **hide the `#ifdef` inside a macro, and expose a uniform call at the use site**. The source reads the same way in both configurations, and the reader does not have to mentally unroll half a dozen `#if` branches to understand the code.

### Feature Flags Through sysctl and dmesg

A related but distinct question is: how does the user or an operator discover what features this driver was built with? A production driver typically answers this with a sysctl tree and a printed banner at module load.

At module load, the driver prints a short message that identifies itself and the features it supports:

```c
printf("portdrv: version %s loaded; backends:"
#ifdef PORTDRV_WITH_PCI
       " pci"
#endif
#ifdef PORTDRV_WITH_USB
       " usb"
#endif
#ifdef PORTDRV_WITH_SIM
       " sim"
#endif
       "\n", PORTDRV_VERSION);
```

The output in the kernel log tells an operator exactly which backends are compiled in. A build that is supposed to have USB support but whose dmesg shows only `backends: pci` reveals the misconfiguration at a glance.

At runtime, a sysctl tree exposes the same information machine-readably:

```text
dev.portdrv.0.version: 2.0
dev.portdrv.0.backend: pci
dev.portdrv.0.features.dma: 1
dev.portdrv.0.features.debug: 0
```

A script can read these, a monitoring system can alert on them, and a bug report can include them without the reporter having to guess.

### Avoiding `#ifdef` Sprawl

The biggest single pitfall of conditional compilation is its tendency to sprawl. A feature is added and guarded with `#ifdef FEATURE_A`. A month later another feature is added and guarded with `#ifdef FEATURE_B`. Six months later a maintainer adds a bug fix that touches both features, and it acquires `#if defined(FEATURE_A) && defined(FEATURE_B)`. Soon every function has five guards, each guarding a handful of lines, and the reader cannot trace a single path through the code without mentally resolving a cross-product of `#if` branches.

Three rules keep sprawl in check.

**Keep the guards coarse.** Guard an entire function, or an entire file, rather than scattered lines in the middle of a function. If you find yourself adding `#ifdef` inside the body of a function more than once, the function should probably be split: one version for `FEATURE_A`, one for `!FEATURE_A`, and a small dispatcher at the call site.

**Hide the guards in macros.** If you need a conditional operation in many places, wrap it in a macro or inline function that has an empty form in the `#else` branch. The call sites then do not have guards at all.

**Review the guard set periodically.** A feature flag that no one has disabled for two years is effectively mandatory. Delete it. A flag that guards a feature that was removed is dead code. Delete that too. A flag that means something different now than when it was introduced should be renamed.

### Architecture-Specific Conditionals

There are situations where the right tool truly is an `#if` on the architecture macro. An example from real code, from `/usr/src/sys/dev/vnic/nicvf_queues.c`:

```c
#if BYTE_ORDER == BIG_ENDIAN
	return ((i & ~3) + 3 - (i & 3));
#else
	return (i);
#endif
```

This function computes an index whose layout depends on the byte order of the host. There is no way to hide this in a macro because the two forms are genuinely different. The right thing to do is exactly what the real driver does: guard the minimum possible, comment what the two branches do, and move on. Do not try to eliminate every `#if`.

The key is discipline. Use `#if BYTE_ORDER` only where a real architectural difference exists, and keep the conditional block as small as possible. A five-line `#if` block is readable. A fifty-line one usually is not.

### Optional Subsystem Support

Sometimes a driver supports features that depend on optional subsystems. For example, a network driver might support checksum offload only if the kernel was built with `options INET`. The idiom is:

```c
#ifdef INET
	/* Set up IPv4 checksum offload. */
#endif
#ifdef INET6
	/* Set up IPv6 checksum offload. */
#endif
```

These guards are provided by the kernel build system automatically; you do not need to define them yourself. But you do need to respect them. A driver that uses `struct ip` unconditionally will fail to build when `options INET` is not set, even though the kernel supports compiling without IPv4. Check the build in the configuration your users might use; it is usually just a matter of adding a guard.

### Avoiding Runtime Feature Checks That Should Be Compile-Time

A subtle mistake is to implement what ought to be a compile-time decision at runtime. If your driver has a simulation backend and a PCI backend, and the user picks one at build time, there is no reason to carry both in the binary. Making the choice at runtime, for example through a sysctl that toggles which backend is used, wastes memory and complicates testing. Make the choice at build time, exclude the unused backend from the build, and the driver is smaller and simpler.

The converse error is also possible: turning a runtime choice into a compile-time one. If the same binary must support multiple pieces of hardware simultaneously, the choice is genuinely runtime, and a compile-time flag is the wrong tool. The right question to ask each time is: "Do I need both behaviours in the same loaded driver?" If yes, runtime. If no, build-time.

### Options Files and Kernel Configuration

For drivers that ship with the kernel tree itself, FreeBSD provides a more structured way to expose build options: the `options` file. Each options file sits next to the driver's source and lists the options the driver recognises, along with a short description and a default value. The file is named `options` and follows a simple format:

```text
# options - portdrv options
PORTDRV_DEBUG
PORTDRV_WITH_SIM
PORTDRV_WITH_PCI
```

Options listed this way can be enabled in the kernel configuration file, typically in `/usr/src/sys/amd64/conf/GENERIC` or a custom kernel config, as:

```text
options	PORTDRV_DEBUG
options	PORTDRV_WITH_SIM
```

The kernel build system takes care of propagating the `-D` flags to the compiler. Inside the driver, the code guards with `#ifdef PORTDRV_DEBUG` exactly as it would for a Makefile-defined option. The difference is organisational: options files live in the source tree next to the driver, so a reader picking up an unfamiliar driver knows exactly which options it recognises.

For out-of-tree drivers, the Makefile-based approach is simpler and is what we have used in this chapter. For in-tree drivers that expect to be built as part of a kernel configuration, the options file is the canonical mechanism.

### Module Load Hints and `device.hints`

A related but distinct topic is the `device.hints` mechanism. Hints are a way to configure devices at boot time, before the driver has fully initialised. They live in `/boot/device.hints` and take the form:

```text
hint.portdrv.0.mode="simulation"
hint.portdrv.0.debug="1"
```

Inside the driver, these are read with `resource_string_value` or `resource_int_value`:

```c
int mode;
const char *mode_str;

if (resource_string_value("portdrv", 0, "mode", &mode_str) == 0) {
	/* Apply the hint. */
}
```

Hints are a way to configure the driver at runtime without recompilation. They are especially useful for embedded systems where the device parameters are known at system design time but not at driver compile time. For a driver that is portable across hardware variants, hints can be the mechanism by which each variant is recognised.

Hints are not a replacement for compile-time feature selection. They are complementary. Compile-time flags decide what the driver can do; hints decide what it actually does at a given boot. A driver can have both, and most mature drivers do.

### Feature Flags Through `MODULE_PNP_INFO`

For drivers that attach to hardware identified by vendor and device IDs, `MODULE_PNP_INFO` is a way to declare that identification in the module's metadata. The kernel and userland tools read this metadata to decide which driver to load when a specific piece of hardware is present, and `devd(8)` uses it to match devices to modules.

A typical declaration looks like this:

```c
MODULE_PNP_INFO("U32:vendor;U32:device", pci, portdrv_pci,
    portdrv_pci_ids, nitems(portdrv_pci_ids));
```

where `portdrv_pci_ids` is an array of identification records that the driver's probe function also consults. The metadata is embedded in the `.ko` file and is read by `kldxref(8)` to build an index used at boot and at device attachment.

For a portable driver with multiple backends, each backend declares its own `MODULE_PNP_INFO`. The PCI backend declares PCI IDs; the USB backend declares USB IDs; the simulation backend declares nothing because it has no hardware. The kernel picks up the right backend automatically when the matching hardware is present.

### The Limits of `__FreeBSD_version`

`__FreeBSD_version` is valuable, but it has a limit. It tells you what release of the kernel you are compiling against, not what release is actually running. Since kernel modules are tightly coupled to the kernel they load into, these are usually the same, but there are edge cases: modules built on a slightly older tree and loaded into a slightly newer one, or modules built with a custom patch set. In these cases, `__FreeBSD_version` may mislead you.

The more robust approach for runtime decisions is to query the kernel directly through a sysctl:

```c
int major = 0;
size_t sz = sizeof(major);

sysctlbyname("kern.osreldate", &major, &sz, NULL, 0);
if (major < 1400000) {
	/* Older kernel. Use the legacy code path. */
}
```

This reads the actual running kernel's version. For most drivers it is overkill; for drivers that must adapt at runtime, it is the right tool. The usual compile-time `__FreeBSD_version` is a statement about the kernel at build time; `kern.osreldate` is a statement about the kernel at run time.

For driver code specifically, the compile-time check is almost always what you want. The module is almost always loaded into the kernel it was built against, and the kernel's ABI check via `MODULE_DEPEND` catches the rare mismatches.

### Organising Compatibility Shims

When `__FreeBSD_version` guards begin to appear in more than a few places, the right move is to centralise them in a compatibility header. The typical layout is:

```c
/* portdrv_compat.h - compatibility shims for FreeBSD API changes. */

#ifndef _PORTDRV_COMPAT_H_
#define _PORTDRV_COMPAT_H_

#include <sys/param.h>

#if __FreeBSD_version >= 1400000
#include <net/if.h>
#include <net/if_var.h>
#define PD_IF_SETFLAGS(ifp, flags)  if_setflags((ifp), (flags))
#define PD_IF_GETFLAGS(ifp)         if_getflags(ifp)
#else
#define PD_IF_SETFLAGS(ifp, flags)  ((ifp)->if_flags = (flags))
#define PD_IF_GETFLAGS(ifp)         ((ifp)->if_flags)
#endif

#if __FreeBSD_version >= 1300000
#define PD_CV_WAIT(cvp, mtxp)  cv_wait_unlock((cvp), (mtxp))
#else
#define PD_CV_WAIT(cvp, mtxp)  cv_wait((cvp), (mtxp))
#endif

#endif /* !_PORTDRV_COMPAT_H_ */
```

The core code uses `PD_IF_SETFLAGS(ifp, flags)` and `PD_CV_WAIT(cvp, mtxp)` in place of the bare kernel calls. The compatibility header is the one place where `__FreeBSD_version` appears, and the guards are close to each other rather than scattered. When the oldest supported release is dropped, you delete the `#else` branches and leave the modern names in place.

This pattern is especially valuable for drivers that must support a range of kernel releases, such as out-of-tree drivers consumed by users on different systems. An in-tree driver typically only has to support the current release plus whichever earlier ones the project promises to support, which is usually one or two.

### Build-Time Telemetry

A small but useful trick is to embed build-time telemetry in the module. A single line in the Makefile:

```make
CFLAGS+=	-DPORTDRV_BUILD_DATE='"${:!date "+%Y-%m-%dT%H:%M:%S"!}"'
```

captures the build timestamp into a string the driver can print at load time. Combined with a version string and a list of enabled backends, the module's load banner becomes a small diagnostic artefact:

```text
portdrv: version 2.0 built 2026-04-19T14:32:17 loaded, backends: pci sim
```

When a user reports a bug, they can paste the banner into the bug report, and you know exactly which version they are running. This is trivial to add and immensely valuable over the life of the driver.

### Wrapping Up Section 6

Conditional compilation is a power tool. Used with restraint, it keeps optional features clean, lets you support multiple FreeBSD releases, and enables simulation backends without polluting production builds. Used carelessly, it turns source into a maze. The rules for restraint are: prefer machine-specific headers over `#ifdef __arch__`, keep option flags coarse, hide guards inside macros, and delete flags that have outlived their purpose. Options files, device hints, `MODULE_PNP_INFO`, and a compatibility header give you a small family of tools for managing build-time variation without cluttering the core logic.

Section 7 takes a brief detour outside FreeBSD. Drivers sometimes need to share code with NetBSD or OpenBSD, and while that is not a primary goal of this book, knowing the lay of the land saves you from design choices that would hurt cross-BSD portability even when you do not immediately need it.

## Section 7: Adapting for Cross-BSD Compatibility

The three open-source BSDs, FreeBSD, NetBSD, and OpenBSD, share a long history. At the source-code level they diverged from a common ancestor, and although each has evolved in its own direction, they still recognise one another. A device driver that is carefully written in the FreeBSD style can often be ported to NetBSD or OpenBSD with meaningful but bounded effort, and a driver that is casually written cannot.

This section is deliberately short. This is a FreeBSD book, and turning Chapter 29 into a cross-BSD porting manual would crowd out material that belongs in later chapters. What we will do here is enough to let you recognise which parts of a FreeBSD driver travel well and which parts need translation. You will not learn how to port; you will learn what to design against, so that porting becomes a possibility rather than an impossibility.

### Where the BSDs Agree

The areas where FreeBSD, NetBSD, and OpenBSD agree most strongly are the ones your driver leans on most heavily:

- **C language and toolchain.** All three use the same C standards and similar compilers (clang on FreeBSD and OpenBSD; gcc on NetBSD with clang available).
- **The general shape of kernel modules.** All three support loadable kernel modules with a similar life cycle: load, init, attach, detach, unload.
- **The general shape of device drivers.** Probe, attach, detach. Per-instance state. Resource allocation. Interrupt handlers. Newbus-style device trees (NetBSD has `config`; FreeBSD has Newbus; OpenBSD has `autoconf`).
- **Standard C library and POSIX.** Even in userland, the three systems share enough that most userland helpers port trivially.
- **Many device-specific protocols and wire formats.** An Ethernet frame is an Ethernet frame, and an NVMe command is an NVMe command, on any of the three.

A driver whose interesting logic is above the kernel API layer, parsing protocols, managing state machines, and coordinating transfers, is largely portable. The interesting logic typically doesn't know about `bus_dma` or `device_t`; it knows about the hardware's programming model.

### Where the BSDs Differ

The areas where the three BSDs differ are concentrated in the kernel APIs:

- **Bus framework.** FreeBSD's Newbus, NetBSD's `autoconf(9)`, and OpenBSD's `autoconf(9)` (same name, different machinery) are not source compatible. The callbacks are similar in spirit but have different signatures.
- **DMA abstraction.** FreeBSD's `bus_dma(9)` has close cousins in NetBSD and OpenBSD called `bus_dma(9)` as well, but the types and some of the function signatures differ. The general model is shared; the exact API is not.
- **Memory allocation.** `malloc(9)` exists on all three with the same idea but slightly different function families and flags.
- **Synchronisation primitives.** Mutexes, condition variables, and epochs exist in all three, but the specific names and flags differ.
- **Network interfaces.** FreeBSD's `ifnet` and `if_t` opaque handle, NetBSD's `struct ifnet`, and OpenBSD's `struct ifnet` look similar but have diverged in the details.
- **Character devices.** FreeBSD's `cdev` framework, NetBSD's `cdevsw`, and OpenBSD's character-device machinery differ in how they register and dispatch.

This is not a complete list, but it captures the usual sources of porting pain.

### The Cross-BSD-Friendly Style

If you want your driver to be portable across BSDs without committing to maintaining three versions, the cheapest long-term strategy is to write it in a style that isolates the FreeBSD-specific parts. The patterns are the ones you have already learned in this chapter:

- **Isolate hardware-dependent code.** A backend interface hides the bus APIs. Swapping FreeBSD's `bus_dma` for NetBSD's involves changing the backend implementation, not the core.
- **Wrap kernel primitives you use often.** If `malloc(M_TYPE, size, M_NOWAIT)` is replaced in NetBSD by a subtly different spelling, wrapping it in `portdrv_alloc(size)` means one place to change, not a hundred.
- **Keep core logic free of kernel subsystem dependencies.** The request queue, the state machine, and the protocol parsing do not need to include `sys/bus.h` or `net/if_var.h`.
- **Separate registration from logic.** The `DRIVER_MODULE` macro and its equivalents are a small, localised piece of code per driver. Putting it in a dedicated file per BSD is easier than sprinkling it through the driver.
- **Use standard types.** `uint32_t` and `size_t` exist on all three BSDs. `u_int32_t` and `int32` are older spellings that are more portable in the narrow sense but less idiomatic on FreeBSD.

If you have followed the file layout in Section 4, much of this is already in place. The core in `portdrv_core.c` is kernel-subsystem-agnostic. The backends in `portdrv_pci.c` and `portdrv_usb.c` are where the FreeBSD-specific code lives. A hypothetical NetBSD port would add `portdrv_pci_netbsd.c` and keep the core unchanged, up to small type mismatches.

### Compatibility Wrappers

For the cases where you really do need the same source to compile on multiple BSDs, the standard technique is a compatibility header. Something like:

```c
/* portdrv_os.h - OS-specific wrappers */
#ifdef __FreeBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) malloc((sz), M_PORTDRV, (flags))
#define PD_FREE(p)           free((p), M_PORTDRV)
#endif

#ifdef __NetBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) kmem_alloc((sz), (flags))
#define PD_FREE(p)           kmem_free((p), sz)
#endif

#ifdef __OpenBSD__
#include <sys/malloc.h>
#define PD_MALLOC(sz, flags) malloc((sz), M_DEVBUF, (flags))
#define PD_FREE(p)           free((p), M_DEVBUF, sz)
#endif
```

The core uses `PD_MALLOC` and `PD_FREE` everywhere. One header changes per new OS; the core never changes. Apply the same technique to locks, to DMA, to printing, to any FreeBSD primitive that differs in the other BSDs. The result is a driver whose OS-specific surface is a single file and whose core is shared.

Keep in mind the tradeoff. Writing the driver in this style costs you a little upfront: you cannot simply call `malloc` the FreeBSD way; you have to call your wrapper. If you do not actually need cross-BSD portability, the cost buys you nothing. If you do need it, the cost is far less than the alternative of maintaining three drivers.

### Listing the FreeBSD-Specific APIs You Use

A useful exercise before you even consider cross-BSD work is to catalogue the FreeBSD-specific APIs your driver relies on. Go through the source and list every function, macro, and structure name that is unique to FreeBSD. The list typically includes:

- `bus_alloc_resource_any`, `bus_release_resource`
- `bus_setup_intr`, `bus_teardown_intr`
- `bus_read_1`, `bus_read_2`, `bus_read_4`, and the write variants
- `bus_dma_tag_create`, `bus_dmamap_load`, `bus_dmamap_sync`, `bus_dmamap_unload`
- `callout_init_mtx`, `callout_reset`, `callout_stop`, `callout_drain`
- `malloc`, `free`, `MALLOC_DEFINE`
- `mtx_init`, `mtx_lock`, `mtx_unlock`, `mtx_destroy`
- `sx_init`, `sx_slock`, `sx_xlock`
- `device_printf`, `device_get_softc`, `device_set_desc`
- `DRIVER_MODULE`, `MODULE_VERSION`, `MODULE_DEPEND`
- `DEVMETHOD`, `DEVMETHOD_END`
- `SYSCTL_ADD_NODE`, `SYSCTL_ADD_UINT`, etc.
- `if_alloc`, `if_free`, `ether_ifattach`, `if_attach`
- `cdevsw`, `make_dev`, `destroy_dev`

Each entry in this list is a candidate for a wrapper. Not every one needs to be wrapped, and a driver that wraps everything is overengineered. But the list tells you where the porting work would happen if it ever needed to, and it is a useful artifact for maintenance even without any cross-BSD plans.

### When to Commit to Cross-BSD Support

Cross-BSD support is not free. It imposes an ongoing cost on development, because every feature must be tested on every supported BSD. It imposes a cost on readability, because wrappers replace direct API calls. It imposes a cost on performance, though usually a small one, because the wrappers add indirection.

That cost is worth paying when there is a clear reason. Examples:

- **You have users on multiple BSDs.** A device vendor that wants their product supported on both FreeBSD and NetBSD has a business reason to absorb the complexity.
- **The driver is community-developed across BSDs.** Open-source projects sometimes have contributors on all three BSDs, and unifying the code base reduces duplicate work.
- **The driver is funded by a project with cross-BSD requirements.** Some academic projects, research infrastructures, or embedded deployments have mixed-OS requirements built into their plans.

If none of these apply, do not preemptively engineer for cross-BSD support. Follow the patterns in this chapter, because they are good FreeBSD practice in their own right, and stay within FreeBSD. If the need for cross-BSD support appears later, the patterns already in place will make the eventual port cheaper than it would have been otherwise.

### A Concrete Wrapping Example

To make the cross-BSD wrapping approach feel concrete, consider a driver that uses a callout timer. On FreeBSD, the API is the `callout` family. The code might look like:

```c
callout_init_mtx(&sc->sc_timer, &sc->sc_mtx, 0);
callout_reset(&sc->sc_timer, hz / 10, portdrv_timer_cb, sc);
/* ... later ... */
callout_stop(&sc->sc_timer);
callout_drain(&sc->sc_timer);
```

On NetBSD, the analogous API is the `callout` family too, but the signatures differ slightly. On OpenBSD, `timeout` is the equivalent name. A cross-BSD driver wraps the usage:

```c
/* portdrv_os.h */
#ifdef __FreeBSD__
typedef struct callout portdrv_timer_t;
#define PD_TIMER_INIT(t, m)   callout_init_mtx((t), (m), 0)
#define PD_TIMER_ARM(t, ticks, cb, arg) \
    callout_reset((t), (ticks), (cb), (arg))
#define PD_TIMER_STOP(t)      callout_stop(t)
#define PD_TIMER_DRAIN(t)     callout_drain(t)
#endif

#ifdef __NetBSD__
typedef struct callout portdrv_timer_t;
/* ... NetBSD-specific definitions ... */
#endif

#ifdef __OpenBSD__
typedef struct timeout portdrv_timer_t;
#define PD_TIMER_INIT(t, m)   timeout_set((t), (cb), (arg))
#define PD_TIMER_ARM(t, ticks, cb, arg) \
    timeout_add((t), (ticks))
/* ... */
#endif
```

The core code uses the `PD_TIMER_*` macros. The per-OS branches in the header translate between the abstract name and each OS's concrete API. This is the wrapper approach in miniature: one abstraction, many implementations.

Notice that the wrapper does more than rename calls. It also papers over genuine differences in how the APIs work. FreeBSD's `callout_init_mtx` binds the callout to a mutex so the callback runs with the mutex held. OpenBSD's `timeout_set` does not have this binding; the callback must acquire the mutex itself. The wrapper macros absorb this difference in the core's favour, by presenting the FreeBSD semantics as the canonical shape. On OpenBSD, the implementation of `PD_TIMER_INIT` can register a shim callback that acquires the mutex before calling the user's callback.

This shows both the power and the cost of wrapping. The power is that the core code does not need to know about the difference. The cost is that the wrapper layer has non-trivial logic of its own, which must be maintained and tested. Wrapping is not free, and the wrapper layer grows as the set of wrapped APIs grows.

### What "Porting" Actually Involves

A realistic cross-BSD port is rarely a single commit. It is a sequence of small steps, each of which makes the driver a little more portable before the final move to the target OS:

1. **Audit.** List every FreeBSD-specific API the driver uses. The list becomes the work plan.
2. **Wrap.** Introduce wrappers, one API at a time. For each wrapper, replace every call in the driver with the wrapper. Commit after each API.
3. **Test on FreeBSD.** Confirm the wrappers do not change behaviour on FreeBSD. This step matters: a bug introduced during wrapping must be caught before the port, not during it.
4. **Build on the target.** Add the target-specific header file. Implement the wrappers for the target OS. Build and fix compile errors until the driver links.
5. **Load on the target.** Load the module. Fix any missing symbols or dependencies.
6. **Test on the target.** Exercise the driver's features. Fix the bugs that appear.
7. **Iterate.** Each bug fix should flow back into the FreeBSD driver if it exposes a real issue, not a platform-specific quirk.

This is far from a one-day task. For a medium-sized driver, it is often a week or two of focused work. The cost pays off when the port is successful and the shared codebase becomes easier to maintain than two separate drivers.

### When Cross-BSD Means Cross-Platform Generally

Some drivers benefit from running not just on other BSDs but on other operating systems entirely. Linux is the obvious target, but also macOS, Windows, and even user-space reimplementations of kernel services. A driver written with a clean backend interface and a core that speaks only in its own vocabulary is usable as a building block in any of these environments; only the backend and the wrapper layer change.

This is the deep reason the patterns in this chapter matter. They are not just about FreeBSD; they are about how to structure any driver so that the OS is a detail. The specific tools (`bus_dma`, `bus_space`, `__FreeBSD_version`) are FreeBSD's, but the patterns they embody are universal.

### NetBSD and OpenBSD Pointers for the Curious

If you become curious about NetBSD or OpenBSD, a few pointers will save you time.

NetBSD's driver documentation lives in its tree, and the equivalent of `/usr/src/sys` is the same on NetBSD; you can compare the two trees directly. NetBSD's `bus_dma(9)` manual page explains its API with great care. The `config(8)` infrastructure is NetBSD's answer to Newbus and has its own manual.

OpenBSD's driver style is more conservative than FreeBSD's. OpenBSD values simplicity over flexibility, and its drivers tend to be smaller and more direct than FreeBSD's equivalents. If you read OpenBSD driver source, you will see fewer layers of abstraction and fewer build options. This is a design choice; it is neither inherently better nor worse than FreeBSD's style.

Both projects have excellent mailing lists and IRC channels. If you port a driver, asking for review from the community saves time. Both communities are generally welcoming to FreeBSD developers who approach them respectfully.

### Wrapping Up Section 7

Cross-BSD portability is a deep topic, but its essentials are simple: isolate kernel-subsystem dependencies behind wrappers, keep the core logic OS-agnostic, and list the FreeBSD-specific APIs you use so you know where the borders are. If you ever need to port to NetBSD or OpenBSD, you will have only the border to translate; the core goes across unchanged. The wrapping itself is non-trivial work, but the wrapping layer grows slowly and is proportional to the breadth of APIs the driver uses, not to the driver's overall size. For most FreeBSD-only drivers, cross-BSD preparation is not justified; the patterns that support it are worth knowing because they make the driver better even when it stays FreeBSD-only.

In Section 8, we step back and address a different question: how do you package, document, and version a portable driver so that others can build, test, and consume it safely? Good code is not enough; good practice around the code matters too.

## Section 8: Refactoring and Versioning for Portability

A portable driver is an artefact, but portability is also a practice. The code is one part; the way the code is organised, documented, and versioned is the rest. This section addresses that rest. It assumes you have refactored the driver into the shape laid out in Sections 2 through 7, and asks: what more should you do so that your work survives contact with other people and other systems?

The answers involve documentation, versioning, build validation, and a small amount of housekeeping. None of it is glamorous. All of it pays off the first time someone new tries to use your driver.

### A Short README.portability.md

Every portable driver benefits from a short document that states its supported platforms, supported backends, build options, and known limitations. In the `portdrv` reference driver, that document is `README.portability.md`. A reader who picks up the driver for the first time should be able to read it in a few minutes and know whether their environment is supported.

A good portability README has four sections.

**Supported platforms.** List the FreeBSD versions and CPU architectures the driver is tested on, and the ones it is known or expected to work on even if not tested. For example:

```text
Tested on:
- FreeBSD 14.3-RELEASE, amd64
- FreeBSD 14.3-RELEASE, arm64 (QEMU guest)

Expected to work but not regularly tested:
- FreeBSD 14.2-RELEASE and later, amd64
- FreeBSD 14.3-RELEASE, riscv64

Not supported:
- FreeBSD 13.x and earlier (API changes required)
- NetBSD / OpenBSD (see Section 7; see compatibility file portdrv_os.h)
```

**Supported backends.** List each backend, what hardware or software environment it targets, and any constraints. For example:

```text
pci: PCI variant, requires a device with vendor 0x1234, device 0x5678.
usb: USB variant, requires a USB 2.0 or later host controller.
sim: Pure software simulation. No hardware required. Useful for tests.
```

**Build options.** List every `-D` flag that the Makefile recognises and what each one does. Mention which combinations are expected to work:

```text
PORTDRV_WITH_PCI=yes       Enable the PCI backend.
PORTDRV_WITH_USB=yes       Enable the USB backend.
PORTDRV_WITH_SIM=yes       Enable the simulation backend.
PORTDRV_DEBUG=yes          Enable verbose debug logging.

If no backend flag is set, PORTDRV_WITH_SIM is enabled by default
so that a plain "make" produces a loadable module.
```

**Known limitations.** State honestly what the driver does not support. A short, honest list is more useful than a long or evasive one.

```text
The simulation backend does not attempt to emulate interrupt
latency or DMA bandwidth limits. It completes transfers
synchronously. Do not use it as a substitute for performance
testing against real hardware.
```

A portability README lives alongside the driver's source. Update it whenever the supported set changes. If you pick up a driver that lacks one, writing one is the most valuable review activity you can perform.

### Versioning the Driver

Kernel modules carry a version via `MODULE_VERSION`. A portable driver should version three things: the driver as a whole, each backend, and the backend interface.

The driver version is a single integer passed to `MODULE_VERSION`:

```c
MODULE_VERSION(portdrv, 2);
```

Bump it whenever you change something a dependent consumer can observe, for instance when you add a new ioctl, change the semantics of an existing one, or rename a sysctl.

Each backend has its own module registration. Version each one independently:

```c
MODULE_VERSION(portdrv_pci, 1);
MODULE_DEPEND(portdrv_pci, portdrv_core, 1, 2, 2);
```

The `MODULE_DEPEND` call expresses that `portdrv_pci` depends on `portdrv_core` with a version in the range [1, 2] where 2 is the preferred value. The kernel refuses to load the backend against a core it does not understand, which prevents a mismatch from producing mysterious crashes.

The backend interface itself can be versioned in the struct. A `version` field at the top of `struct portdrv_backend` lets the core check that each backend was compiled against the right shape:

```c
struct portdrv_backend {
	uint32_t     version;
	const char  *name;
	int   (*attach)(struct portdrv_softc *sc);
	/* ... */
};

#define PORTDRV_BACKEND_VERSION 2
```

When the core's `portdrv_core_attach` sees `sc->sc_be->version != PORTDRV_BACKEND_VERSION`, it refuses to attach and logs a clear message. This catches the case where someone updates the core but forgets to rebuild a backend, without requiring the kernel to crash to make the point.

Use these mechanisms lightly. Each one imposes a cost at maintenance time, because a version bump must be coordinated. The reward is that when something does go wrong, the error is immediate and clear instead of vague and delayed.

### Document Supported Build Configurations

Alongside the `README.portability.md`, maintain a short **build matrix** that records which combinations of backends and options are expected to compile and which have been recently tested. This is a project-management artefact, not a runtime one, but it is invaluable when a new maintainer takes over or when a reviewer wants to know whether a specific configuration will still work after a change.

A practical build matrix looks like this:

```text
| Config                             | Compiles | Tested | Notes           |
|------------------------------------|----------|--------|-----------------|
| (default: SIM only)                | yes      | yes    | Baseline CI.    |
| PCI only                           | yes      | yes    |                 |
| USB only                           | yes      | yes    |                 |
| PCI + USB                          | yes      | yes    |                 |
| PCI + USB + SIM                    | yes      | yes    | Full build.     |
| PCI + DEBUG                        | yes      | yes    |                 |
| PCI + USB + DEBUG                  | yes      | no     | Should be OK.   |
| None of PCI/USB/SIM (no backend)   | yes      | yes    | SIM auto-enabled|
```

Regenerate the matrix before a release. It is a small chore that saves a large amount of time later. When users file bugs against specific configurations, you can tell at a glance whether the configuration was claimed to work, and if so, whether it was in fact tested at the last release.

### Validating the Build Before a Release

A good release discipline for a portable driver is to validate every documented configuration automatically before each release. Automation makes this cheap. A short shell script that loops over the build matrix, invokes `make clean` and `make`, and reports success or failure, is typically enough:

```sh
#!/bin/sh
# Validate portdrv builds in every supported configuration.

set -e

configs="
    PORTDRV_WITH_SIM=yes
    PORTDRV_WITH_PCI=yes
    PORTDRV_WITH_USB=yes
    PORTDRV_WITH_PCI=yes PORTDRV_WITH_USB=yes
    PORTDRV_WITH_PCI=yes PORTDRV_WITH_USB=yes PORTDRV_WITH_SIM=yes
    PORTDRV_WITH_PCI=yes PORTDRV_DEBUG=yes
"

OLDIFS="$IFS"
echo "$configs" | while read cfg; do
    [ -z "$cfg" ] && continue
    echo "==> Building with: $cfg"
    make clean > /dev/null
    if env $cfg make > build.log 2>&1; then
        echo "    OK"
    else
        echo "    FAIL (see build.log)"
        tail -n 20 build.log
        exit 1
    fi
done
```

Run this before you tag a release. When it passes, you know that every configuration you document as supported actually builds. When it fails, you know the exact configuration that broke and can fix it before shipping. This is the cheapest and most valuable quality practice a portable driver can adopt.

### When to Version Bump

A frequent question from new driver authors is: "When should I bump the module version number?" The short answer is that you bump it when anything observable to consumers changes. The longer answer distinguishes three kinds of change:

**Backwards-compatible additions.** Adding a new ioctl command, a new sysctl, a new module option, without changing the behaviour of existing commands. Bump the minor version. Existing consumers continue to work.

**Backwards-incompatible changes.** Renaming an ioctl, changing the meaning of an existing sysctl, or breaking a previously documented behaviour. Bump the major version. Update the `MODULE_DEPEND` in dependent modules. Document the break in a release note.

**Internal refactoring that users cannot observe.** Moving functions between files, renaming private variables, reformatting. Do not bump the version. Internal changes are internal.

Keep in mind that `MODULE_VERSION` is an integer, and consumers decide how to interpret it. For a driver with a small user base, one version number per observable change is fine. For a driver with a larger community and more dependents, establishing a convention such as "major * 100 + minor" lets you encode more information in a single integer. Either way, document what your version numbers mean so that future maintainers and consumers can interpret them.

### Refactoring for Portability as an Ongoing Discipline

A portable driver is not a finished artefact. Even after the initial refactor, the driver continues to evolve: new features, new hardware variants, new kernel APIs, new bug fixes. Keeping the driver portable is an ongoing discipline, not a one-time achievement.

Three habits help.

**Review new patches for portability hazards.** When a contributor adds a new register access, does it go through the accessor? When they add a new backend-specific function, does it fit the interface, or does it force an `if (sc->sc_be == pci)` in the core? When they add a new type, is it fixed-width? These questions take a few seconds per patch and catch most regressions.

**Re-run the build matrix regularly.** A monthly or weekly automated build of every supported configuration catches silent regressions the moment they appear. If CI resources are limited, run the matrix on pull requests and nightly on the main branch.

**Re-test on a non-amd64 platform periodically.** Even a single `arm64` guest booted under QEMU once a quarter catches a surprising number of architectural bugs. The test does not have to be exhaustive; even just loading the driver and running the simulation backend is enough to reveal many endian and alignment issues.

### The Final Shape of a Portable Driver

After all of this, a portable driver has a recognisable shape. The core is small and does not know about specific buses. Each backend is a thin file that implements a clean interface. The headers are short and each has a clear purpose. The Makefile uses feature flags to select backends and options. The driver uses `uint32_t` and its family for sizes, `bus_read_*` and `bus_write_*` for register access, `bus_dma_*` for DMA, and endian helpers whenever multi-byte values cross the hardware boundary. The driver documents its supported platforms and build options. The driver has a build matrix and a validation script.

A reader opening the source for the first time can find what they need in minutes. A new contributor can add a backend in an afternoon. A reviewer can audit a patch without reading the entire driver. That is the shape.

### ABI vs. API Stability

A subtle distinction is often missed when drivers are versioned: the difference between the **application programming interface** (API) and the **application binary interface** (ABI). The API is what a programmer sees: function names, parameter types, expected behaviour. The ABI is what a linker sees: symbol names, structure layouts, calling conventions, alignment.

A change that leaves the API unchanged can still break the ABI. Adding a field to the middle of a structure is a classic example: source files that use the structure compile unchanged, but binary modules compiled against the old layout will read the wrong fields at runtime because the offsets have shifted.

For kernel modules, ABI stability matters because modules are loaded into a running kernel, and the kernel and the module must agree on the layout of the structures they share. This is the reason `MODULE_DEPEND` includes version numbers: an older module refusing to load against a newer kernel, or vice versa, is safer than silently reading wrong fields.

Two rules help preserve ABI stability over time.

**Rule one: add fields only at the end of structures.** The existing fields retain their offsets, and older consumers read them correctly. Newer consumers, which know about the added field, can access it at the end.

**Rule two: never change the type or size of an existing field.** If you need to widen a field, add a new field and leave the old one for compatibility. This is awkward, but it is the price of ABI stability.

Drivers with a small audience can ignore ABI stability most of the time, because each release rebuilds everything. Drivers whose consumers cannot rebuild everything, such as out-of-tree modules that users compile against their own kernel, must be more careful.

### Upgrade and Downgrade Paths

A mature driver must be usable through upgrade and downgrade. Upgrades are straightforward: install the new module, unload the old one, load the new one. Downgrades are sometimes harder, because the new module may have saved state that the old module cannot read.

For most drivers, state is not persistent, so downgrade is easy. For drivers that store state in a character device's data, in filesystem metadata, or in a database the device itself maintains, downgrade paths deserve thought. Your state format should include a version field; older versions should refuse to read newer formats and produce a clear error; newer versions should read older formats when possible.

Release notes are the simplest rollout mechanism. A release note that says "this version changes the state format; downgrade is not possible without loss of data" is worth more than a sophisticated state-migration system that nobody uses. Communicate the upgrade impact clearly and let your users decide.

### Telemetry and Observability

A portable driver that is deployed widely benefits from lightweight telemetry. Not intrusive logging that floods the kernel log, but unobtrusive counters that an operator can query to see whether the driver is healthy. The usual FreeBSD mechanism is a sysctl tree:

```c
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "transfers_ok",
    CTLFLAG_RD, &sc->sc_stats.transfers_ok,
    "Successful transfers");
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "transfers_err",
    CTLFLAG_RD, &sc->sc_stats.transfers_err,
    "Failed transfers");
SYSCTL_ADD_UQUAD(ctx, tree, OID_AUTO, "queue_depth",
    CTLFLAG_RD, &sc->sc_stats.queue_depth,
    "Current queue depth");
```

Operators can query these with `sysctl dev.portdrv.0`, monitoring systems can scrape them at intervals, and bug reports can include their values without requiring the reporter to do anything special. The cost to the driver is one field per counter and one sysctl registration per field. For a mature driver, the telemetry tree grows naturally as questions arise.

Add the telemetry first, debug second. When a bug report arrives and the sysctl values show what happened, the debugging is much faster than when the values are missing.

### A Post-Refactor Checklist

After a portability refactor, it is worth running through a short checklist before declaring the work complete. The checklist is deliberately terse; each item stands for a subtlety you should confirm.

1. Does the core file include any bus-specific header? If yes, move the include or the using code.
2. Does every register access go through an accessor or a backend method? Grep for `bus_read_`, `bus_write_`, `bus_space_read_`, and `bus_space_write_` in the core file; each occurrence is a candidate for attention.
3. Does the driver use fixed-width types for every value whose size matters? Look for `int`, `long`, `unsigned`, and `size_t` in register access contexts; replace with `uint32_t`, `uint64_t`, and so on as appropriate.
4. Does every multi-byte value that crosses the hardware boundary pass through an endian helper? Look for raw `*(uint32_t *)` casts on DMA buffers and register images.
5. Does the build succeed in every documented configuration? Run the build matrix script.
6. Does the module load cleanly, with the expected banner? Load each configuration in turn and check dmesg.
7. Does the driver detach cleanly? Unload it and confirm no panics and no leaked resources.
8. Is the documentation up to date? Re-read `README.portability.md` and ensure it matches reality.
9. Is the lab logbook current? Note what was done, what surprised you, and what you expect to change in the future.

Running the checklist takes about an hour. Finding a problem during the checklist is cheaper than finding it in production.

### Rollouts and Rollbacks

When a refactored driver is ready to ship, consider the rollout strategy. For a small user base, a single release is enough. For a larger one, a staged rollout reduces risk:

1. **Internal staging.** Deploy to internal test systems first. Run the driver for at least a day before exposing it to external users.
2. **Early adopters.** Release to a small group of external users who have opted in. Collect feedback for a week.
3. **General release.** Ship to all users. Announce the release with a changelog.
4. **Rollback plan.** Document how to revert to the prior version. A driver that can be unloaded and replaced by the previous version is safer than one that is entangled with the rest of the system.

The granularity of the rollout depends on your project. A hobby project does not need staged rollouts. A commercial driver serving hundreds of users does. Think about the balance and choose deliberately.

### Wrapping Up Section 8

You now know not only how to write portable code but how to package it so that others can consume it safely. Versioning, documentation, a build matrix, and a validation script are the small, unglamorous practices that separate a hobbyist driver from a production-ready one. They cost little to adopt and pay off the first time someone else touches the code, including future you. ABI stability, upgrade paths, telemetry, a post-refactor checklist, and a rollout strategy are the companion practices that mature a driver over the long term.

The chapter's remaining material gives you hands-on labs, challenge exercises, and a troubleshooting guide. The labs let you apply every technique in this chapter to the reference driver. The challenges push further, so you can practice the patterns on variations of the same problem. The troubleshooting guide catalogues the failures you are likeliest to meet along the way.

## Hands-On Labs

The labs in this chapter walk you through transforming a small single-file driver into a portable, multi-backend, multi-file driver. The reference implementation, `portdrv`, lives in `examples/part-07/ch29-portability/`. Each lab is self-contained and leaves you with a working module that loads cleanly. You can do them all, or stop at any point; each one is useful on its own.

Before you begin, change into the examples directory and inspect its current state:

```sh
cd /path/to/FDD-book/examples/part-07/ch29-portability
ls
```

You should see a `lab01-monolithic/` directory that contains the starting-point driver, along with `lab02` through `lab07` directories that contain the successive refactoring steps. Each lab directory has its own `Makefile` and README. Work in `lab01-monolithic` first; when a lab is complete, move on to the next directory, which contains the state of the driver after that step.

A quick workflow tip: make a local working copy of the directory for each lab so you can compare your version against the reference afterwards.

```sh
cp -r lab01-monolithic mywork
cd mywork
```

Then follow the instructions.

### Setting Up the Lab Environment

Before you start any of the labs, set up the working environment. A few minutes of preparation saves hours of friction later.

Create a working directory outside the repository, so you do not accidentally commit lab output:

```sh
mkdir -p ~/fdd-labs/ch29
cd ~/fdd-labs/ch29
cp -r /path/to/FDD-book/examples/part-07/ch29-portability/* .
```

Confirm that `make` works with the default lab files before making any changes:

```sh
cd lab01-monolithic
make clean && make
ls *.ko
```

If the build fails, stop and debug now. A lab is not useful if the starting state is broken.

Next, verify you can load and unload the reference driver:

```sh
sudo kldload ./portdrv.ko
dmesg | tail
sudo kldunload portdrv
```

If the load fails, check `dmesg` for an error message. The usual suspects are a missing module dependency or a conflict with another driver. Fix these before beginning the labs.

Finally, start a simple logbook. A plain text file or a Markdown file in your working directory is enough:

```text
=== Chapter 29 Lab Logbook ===
Date: 2026-04-19
System: FreeBSD 14.3-RELEASE, amd64
```

Every lab will add an entry. The logbook is not for public consumption; it is a record for future you.

### Lab 1: Audit the Monolithic Driver

The starting-point driver is a single file that compiles and loads but mixes every concern in one place. The goal of this lab is to **notice** the portability problems without fixing them yet. Training your eye is the first step.

```sh
cd lab01-monolithic
less portdrv.c
```

As you read, make notes in your lab logbook for each of the following:

1. Which lines perform register access? Mark every `bus_space_read_*`, `bus_space_write_*`, `bus_read_*`, `bus_write_*`, and any raw pointer dereferences to device memory.
2. Which lines allocate or manipulate DMA buffers? Mark every `bus_dma_*` call.
3. Which lines include bus-specific headers such as `dev/pci/pcivar.h`?
4. Which lines hardcode specific vendor or device IDs?
5. Which lines use types that might be wrong on a 32-bit platform (`int`, `long`, `size_t`)?
6. Which lines use `htons`, `htonl`, or a related endian helper?
7. Which lines are guarded with `#ifdef`?

Now build and load the driver:

```sh
make clean
make
sudo kldload ./portdrv.ko
dmesg | tail -5
sudo kldunload portdrv
```

Confirm that the module loads cleanly. If it does not, fix the build before moving on.

**Reflection.** At the end of the lab, your logbook should have a short paragraph describing the driver's current portability state. Something like: "The register accesses go through `bus_read_*`, so architectural portability is acceptable, but they are spread across the file without accessors. PCI-specific code is interleaved with core logic. No endian helpers are used anywhere. There is no simulation backend. The driver compiles on `amd64` only; I have not tried `arm64`."

This paragraph is the baseline against which every later lab measures progress.

### Lab 2: Introduce Register Accessors

The first structural change is to centralise register access in accessors, as described in Section 2. You are not yet splitting the file; you are only adding a layer of indirection.

Add two static inline functions at the top of `portdrv.c`:

```c
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}
```

Then, throughout the file, replace every `bus_read_4(sc->sc_bar, X)` with `portdrv_read_reg(sc, X)`, and every `bus_write_4(sc->sc_bar, X, V)` with `portdrv_write_reg(sc, X, V)`. Do this carefully. After every small batch of replacements, rebuild:

```sh
make clean && make
```

And reload:

```sh
sudo kldunload portdrv 2>/dev/null
sudo kldload ./portdrv.ko
dmesg | tail -5
```

Confirm that the driver still works. The accessor layer is functionally invisible; the binary behaviour should be unchanged.

**Checkpoint.** Count the number of `bus_read_*` and `bus_write_*` occurrences in the file after the change. They should all be inside the accessors. If any are outside, find and replace them. That sweep is the whole point of the lab.

### Lab 3: Extract the Backend Interface

Now that accessors exist, add a backend interface. Create a new file `portdrv_backend.h` with the struct definition from Section 3:

```c
#ifndef _PORTDRV_BACKEND_H_
#define _PORTDRV_BACKEND_H_

struct portdrv_softc;

struct portdrv_backend {
	const char *name;
	int   (*attach)(struct portdrv_softc *sc);
	void  (*detach)(struct portdrv_softc *sc);
	uint32_t (*read_reg)(struct portdrv_softc *sc, bus_size_t off);
	void     (*write_reg)(struct portdrv_softc *sc, bus_size_t off,
	                      uint32_t val);
};

extern const struct portdrv_backend portdrv_pci_backend;

#endif
```

In `portdrv.c`, add the softc field:

```c
struct portdrv_softc {
	device_t sc_dev;
	struct resource *sc_bar;
	int sc_bar_rid;
	const struct portdrv_backend *sc_be;   /* new */
	/* ... */
};
```

Rewrite the existing accessor functions to dispatch through the backend:

```c
static inline uint32_t
portdrv_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	return (sc->sc_be->read_reg(sc, off));
}

static inline void
portdrv_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	sc->sc_be->write_reg(sc, off, val);
}
```

Define a first instance of the interface:

```c
static uint32_t
portdrv_pci_read_reg_impl(struct portdrv_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_bar, off));
}

static void
portdrv_pci_write_reg_impl(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	bus_write_4(sc->sc_bar, off, val);
}

const struct portdrv_backend portdrv_pci_backend = {
	.name     = "pci",
	.read_reg = portdrv_pci_read_reg_impl,
	.write_reg = portdrv_pci_write_reg_impl,
	/* attach and detach stay NULL for now. */
};
```

In the existing `portdrv_attach` function, install the backend:

```c
sc->sc_be = &portdrv_pci_backend;
```

Rebuild, load, and confirm the driver still works.

**Checkpoint.** The core logic now never touches `bus_read_*` or `bus_write_*` directly. Every register access goes through the backend. Verify this by grepping for `bus_read_` and `bus_write_` in the file; they should all be inside the `_impl` functions.

### Lab 4: Split the Core and the Backend into Separate Files

Now that the interface is in place, split the file.

Create three files:

- `portdrv_core.c`: contains the softc definition, the register accessors, the attach/detach logic that is not PCI-specific, and the module registration for the core.
- `portdrv_pci.c`: contains the PCI probe, the PCI-specific attach and detach, the `_impl` functions, and the backend struct.
- `portdrv_backend.h`: contains the backend interface.

The details of the split are in the reference implementation under `lab04-split`. Study it after you have done your own split, and compare.

Update the `Makefile` to list both source files:

```make
SRCS= portdrv_core.c portdrv_pci.c
```

Rebuild and load. The driver should load identically to before, but the source is now organised.

**Checkpoint.** Open `portdrv_core.c` and search for `#include <dev/pci/pcivar.h>`. It should not appear. The core is free of PCI-specific includes. That is the milestone.

### Lab 5: Add a Simulation Backend

Add a second backend that does not require any hardware. Create `portdrv_sim.c` with its own implementation of the backend interface:

```c
/* portdrv_sim.c - simulation backend for portdrv */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include "portdrv.h"
#include "portdrv_common.h"
#include "portdrv_backend.h"

struct portdrv_sim_priv {
	uint32_t regs[256];  /* simulated register file */
};

static uint32_t
portdrv_sim_read_reg(struct portdrv_softc *sc, bus_size_t off)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;
	if (off / 4 >= nitems(psp->regs))
		return (0);
	return (psp->regs[off / 4]);
}

static void
portdrv_sim_write_reg(struct portdrv_softc *sc, bus_size_t off, uint32_t val)
{
	struct portdrv_sim_priv *psp = sc->sc_backend_priv;
	if (off / 4 < nitems(psp->regs))
		psp->regs[off / 4] = val;
}

static int
portdrv_sim_attach_be(struct portdrv_softc *sc)
{
	return (0);
}

static void
portdrv_sim_detach_be(struct portdrv_softc *sc)
{
}

const struct portdrv_backend portdrv_sim_backend = {
	.name     = "sim",
	.attach   = portdrv_sim_attach_be,
	.detach   = portdrv_sim_detach_be,
	.read_reg = portdrv_sim_read_reg,
	.write_reg = portdrv_sim_write_reg,
};
```

Add a module-load hook that creates a simulated instance when the driver is loaded without any real hardware:

```c
static int
portdrv_sim_modevent(module_t mod, int type, void *arg)
{
	static struct portdrv_softc *sim_sc;
	static struct portdrv_sim_priv *sim_priv;

	switch (type) {
	case MOD_LOAD:
		sim_sc = malloc(sizeof(*sim_sc), M_PORTDRV, M_WAITOK | M_ZERO);
		sim_priv = malloc(sizeof(*sim_priv), M_PORTDRV,
		    M_WAITOK | M_ZERO);
		sim_sc->sc_be = &portdrv_sim_backend;
		sim_sc->sc_backend_priv = sim_priv;
		return (portdrv_core_attach(sim_sc));
	case MOD_UNLOAD:
		if (sim_sc != NULL) {
			portdrv_core_detach(sim_sc);
			free(sim_priv, M_PORTDRV);
			free(sim_sc, M_PORTDRV);
		}
		return (0);
	}
	return (0);
}

static moduledata_t portdrv_sim_mod = {
	"portdrv_sim",
	portdrv_sim_modevent,
	NULL
};

DECLARE_MODULE(portdrv_sim, portdrv_sim_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(portdrv_sim, 1);
MODULE_DEPEND(portdrv_sim, portdrv_core, 1, 1, 1);
```

Update the Makefile to include the new file:

```make
SRCS= portdrv_core.c portdrv_pci.c portdrv_sim.c
```

Rebuild and load:

```sh
make clean && make
sudo kldload ./portdrv.ko
dmesg | tail -10
```

You should see both the PCI backend registering with the bus and a simulated instance created by the sim backend's module hook. Interact with the simulated instance through its character device (if any) and confirm that register writes and reads behave as stored integers.

**Checkpoint.** You now have a driver that compiles with two backends and runs both simultaneously. The PCI backend manages any real hardware the kernel discovers; the simulation backend gives you a software instance for testing.

### Lab 6: Conditional Compilation of Backends

Make each backend conditional on a build-time flag. Edit the Makefile:

```make
KMOD= portdrv
SRCS= portdrv_core.c

.if defined(PORTDRV_WITH_PCI) && ${PORTDRV_WITH_PCI} == "yes"
SRCS+= portdrv_pci.c
CFLAGS+= -DPORTDRV_WITH_PCI
.endif

.if defined(PORTDRV_WITH_SIM) && ${PORTDRV_WITH_SIM} == "yes"
SRCS+= portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

.if !defined(PORTDRV_WITH_PCI) && !defined(PORTDRV_WITH_SIM)
SRCS+= portdrv_sim.c
CFLAGS+= -DPORTDRV_WITH_SIM
.endif

SYSDIR?= /usr/src/sys
.include <bsd.kmod.mk>
```

Build with various combinations:

```sh
make clean && make PORTDRV_WITH_PCI=yes
make clean && make PORTDRV_WITH_SIM=yes
make clean && make PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
make clean && make
```

Confirm that each build succeeds and produces a module. Load each module in turn and watch the dmesg output. The message at load time should identify which backends are present.

Add a banner to the core's module load event:

```c
static int
portdrv_core_modevent(module_t mod, int type, void *arg)
{
	switch (type) {
	case MOD_LOAD:
		printf("portdrv: version %d loaded, backends:"
#ifdef PORTDRV_WITH_PCI
		       " pci"
#endif
#ifdef PORTDRV_WITH_SIM
		       " sim"
#endif
		       "\n", PORTDRV_VERSION);
		return (0);
	/* ... */
	}
	return (0);
}
```

Now `dmesg` at load time tells you exactly what this build supports.

**Checkpoint.** You can now select, at build time, which backends are present in the module. The source is not polluted with `#ifdef` inside function bodies; the selection happens at the Makefile level, and the banner hides its `#ifdef` chain in a controlled location.

### Lab 7: Endian-Safe Register Access

Pick one register in the driver that represents a multi-byte value in a specific byte order. Most real devices document this in the datasheet; for the reference driver, assume that register `REG_DATA` at offset 0x08 is little-endian.

Modify the accessors to apply endian conversion explicitly. Do **not** bake the conversion into the general register accessor; the hardware registers themselves are in host order through `bus_read_4`. The conversion applies to the **interpretation** of the value.

Add a higher-level helper:

```c
static uint32_t
portdrv_read_data_le(struct portdrv_softc *sc)
{
	uint32_t raw = portdrv_read_reg(sc, REG_DATA);
	return (le32toh(raw));
}

static void
portdrv_write_data_le(struct portdrv_softc *sc, uint32_t val)
{
	portdrv_write_reg(sc, REG_DATA, htole32(val));
}
```

Use these in the core whenever the code is manipulating a value that is stored in little-endian form in the register.

**Checkpoint.** On `amd64` the endian helpers are no-ops, so behaviour does not change. On a big-endian platform, the helpers perform byte swaps that would have been missing before. You are now prepared for architectural portability even if you have not yet tested on a big-endian host.

### Lab 8: Build Matrix Script

Write a short script that builds the driver in every supported configuration and reports success or failure. Save it as `validate-build.sh` in the chapter directory.

```sh
#!/bin/sh
set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
done
```

Run it:

```sh
chmod +x validate-build.sh
./validate-build.sh
```

If all three configurations build, you have a minimal build matrix in place. Every future change to the driver can be checked against the matrix with a single invocation.

**Checkpoint.** The driver builds in every configuration you have advertised. You have a machine-readable way to confirm that fact. This is the last structural piece of a portable driver.

### Lab 9: Exercising the Simulation Backend from Userland

A refactor is only as good as the tests you can run against it. Once the simulation backend is in place, you can exercise the driver's core logic from a small userland program without needing any hardware. This lab walks through a lightweight test harness.

Create a userland program, `portdrv_test.c`, that opens `/dev/portdrv0` (or whatever character device the driver creates for the simulated instance), writes a known value, reads it back, and prints the result:

```c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	const char *dev = (argc > 1) ? argv[1] : "/dev/portdrv0";
	int fd;
	char buf[64];

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return (1);
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, sizeof(buf), "hello from userland\n");
	if (write(fd, buf, strlen(buf)) < 0) {
		perror("write");
		close(fd);
		return (1);
	}

	memset(buf, 0, sizeof(buf));
	if (read(fd, buf, sizeof(buf) - 1) < 0) {
		perror("read");
		close(fd);
		return (1);
	}

	printf("Read back: %s", buf);
	close(fd);
	return (0);
}
```

Compile it with `cc -o portdrv_test portdrv_test.c`. Load the driver with the simulation backend enabled, then run the test program:

```sh
sudo kldload ./portdrv.ko
./portdrv_test
```

If the simulation backend stores and retrieves data correctly, the output should confirm a successful round trip. If the test fails, the simulation backend is the isolated place to debug: no hardware, no driver lifecycle surprises, just the core logic plus the simulated backend.

**Checkpoint.** You have a reproducible end-to-end test that exercises the driver's character-device path, its I/O handling, and its simulation backend without touching any real hardware. This test can run in CI, on a developer's laptop, or inside a `bhyve` guest with no PCI cards.

### Lab 10: Add a Basic sysctl Tree

Introduce a sysctl tree rooted at `dev.portdrv.<unit>` that exposes a handful of runtime-visible values. The goal of this lab is not to wire every statistic into sysctl; it is to establish the structure so that later features can extend it cheaply.

In `portdrv_core.c`, add a pair of helpers:

```c
static void
portdrv_sysctl_init(struct portdrv_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "backend", CTLFLAG_RD,
	    __DECONST(char *, sc->sc_be->name), 0,
	    "Backend name");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "version", CTLFLAG_RD,
	    NULL, PORTDRV_VERSION,
	    "Driver version");

	SYSCTL_ADD_UQUAD(ctx, SYSCTL_CHILDREN(tree),
	    OID_AUTO, "transfers_ok", CTLFLAG_RD,
	    &sc->sc_stats.transfers_ok,
	    "Successful transfers");
}
```

Call `portdrv_sysctl_init(sc)` at the end of `portdrv_core_attach`. After loading the driver, inspect the tree:

```sh
sysctl dev.portdrv.0
```

You should see:

```text
dev.portdrv.0.%desc: portdrv (PCI backend)
dev.portdrv.0.%parent: pci0
dev.portdrv.0.backend: pci
dev.portdrv.0.version: 2
dev.portdrv.0.transfers_ok: 0
```

Exercise the driver and watch the `transfers_ok` counter increase. If it does not, the core is not updating it; that is a bug to fix.

**Checkpoint.** The driver's runtime state is now observable from userland without parsing dmesg. Monitoring systems and ad-hoc bug reports can include a snapshot of the sysctl tree, and you know the driver's health at a glance.

### Lab 11: Runtime Backend Selection

For advanced use, let the driver's simulation backend be enabled or disabled at runtime via a sysctl or a module parameter. This lab introduces the idea of making backend selection flexible at module load time, useful for CI pipelines that want the same module to behave differently depending on the environment.

Add a tunable to the module:

```c
static int portdrv_sim_instances = 0;
SYSCTL_INT(_debug_portdrv, OID_AUTO, sim_instances,
    CTLFLAG_RDTUN, &portdrv_sim_instances, 0,
    "Number of simulation backend instances to create at load time");
TUNABLE_INT("debug.portdrv.sim_instances", &portdrv_sim_instances);
```

In the module's load hook, create the number of simulation instances requested by the tunable:

```c
for (i = 0; i < portdrv_sim_instances; i++)
	portdrv_sim_create_instance(i);
```

At load time, the operator can set the tunable from `/boot/loader.conf`:

```text
debug.portdrv.sim_instances="2"
```

Load the module and confirm two simulated instances appear. The PCI backend, if compiled in, still attaches to any real hardware it finds. The two kinds of instance coexist in the same module, with no conflict between them.

**Checkpoint.** The driver is controllable at load time via a tunable, which means the same binary can behave differently in different deployments without recompilation. This is the kind of runtime flexibility that a single-file driver would find awkward and that a well-structured multi-backend driver handles as a natural extension.

### Lab 12: Write a Minimal CI Harness

Combine the build matrix script from Lab 8 with a basic load test. The goal is a single command that validates the driver in every supported configuration and confirms it loads cleanly in each. Automate this into a CI-like loop that you can run before committing changes.

```sh
#!/bin/sh
# ci-check.sh - validate portdrv in every supported configuration.
set -e

configs="
PORTDRV_WITH_SIM=yes
PORTDRV_WITH_PCI=yes
PORTDRV_WITH_PCI=yes PORTDRV_WITH_SIM=yes
"

echo "$configs" | while read cfg; do
	[ -z "$cfg" ] && continue
	printf "==> Build: %s ... " "$cfg"
	make clean > /dev/null 2>&1
	if env $cfg make > build.log 2>&1; then
		echo "OK"
	else
		echo "FAIL"
		tail -n 20 build.log
		exit 1
	fi
	printf "==> Load : %s ... " "$cfg"
	if sudo kldload ./portdrv.ko > load.log 2>&1; then
		sudo kldunload portdrv > /dev/null 2>&1
		echo "OK"
	else
		echo "FAIL"
		cat load.log
		exit 1
	fi
done
echo "All configurations passed."
```

Run it:

```sh
chmod +x ci-check.sh
./ci-check.sh
```

**Checkpoint.** A single script confirms that every configuration you document as supported actually builds and loads. You can invoke this before every commit, or wire it into a CI system that runs it on every pull request. The cost is a minute or two per run; the reward is catching regressions within minutes of making them.

## Challenge Exercises

The following challenges push the refactoring further and give you room to practice the chapter's patterns on variations of the same problem. Each challenge is independent and draws only on material from this chapter and the earlier ones. Take them at your own pace; they are designed to consolidate mastery rather than to introduce new foundations.

### Challenge 1: Add a Third Backend

Add a third backend to the reference driver. The specifics depend on your interests:

- An MMIO backend that reaches the device through a Device Tree node on an embedded platform.
- A child of an I2C or SPI bus.
- A backend that uses `bhyve`'s virtio framework as a guest.

The structural requirement is the same in each case: a new `portdrv_xxx.c` file, a new implementation of the backend interface, a new entry in the Makefile and a new build-time flag. After the work is done, confirm that the driver still builds in every previously supported configuration and additionally in the new one.

### Challenge 2: Expose Version Metadata via sysctl

Create a sysctl subtree at `dev.portdrv.<unit>.version` that exposes:

- the driver version
- the backend name
- the backend version
- the flags that were set at build time

The sysctl tree should be populated in `portdrv_sysctl.c` (create it if it does not exist) and should read its values from constants defined in the individual backend files. At runtime, the operator can learn everything they need to know about the build by running `sysctl dev.portdrv.0`.

### Challenge 3: Simulate a Big-Endian Host

You probably do not have a big-endian machine. Simulate the experience.

Write a one-time debugging mode, controlled by a build flag `PORTDRV_FAKE_BE`, in which all endian helpers are replaced by macros that assume a big-endian host. The code must still compile and run on `amd64`, but any multi-byte value that should have been converted will now be converted (the wrong way, for correctness, but that is the point). Load the driver with this flag on and exercise the simulation backend. If the driver works, congratulations: your use of endian helpers is consistent enough to survive an endianness flip. If it does not, the places where it fails are the places you missed an `htole` or `be32toh`.

This is a laboratory exercise, not a production pattern. The knowledge you gain, however, is exactly the knowledge that would protect you on a real big-endian platform.

### Challenge 4: Cross-Build for arm64

Install or boot an `arm64` FreeBSD guest in QEMU. Build the driver inside the guest, or cross-build it on your `amd64` host using:

```sh
make TARGET=arm64 TARGET_ARCH=aarch64
```

Copy the resulting `.ko` into the guest and load it. Run the simulation backend and confirm the driver behaves identically. Note any warnings the `arm64` compiler emits that the `amd64` compiler did not; these are often alignment or size mismatches that deserve a fix.

### Challenge 5: Write a README.portability.md

Author a `README.portability.md` that documents:

- the platforms on which the driver has been tested
- the backends it supports
- the build options the Makefile recognises
- any limitations, bugs, or known issues

This challenge is about writing, not coding. The artefact is as important to the driver's portability as the code itself. Keep it short; keep it honest.

### Challenge 6: Create a Versioned Backend Interface

Add a `version` field to `struct portdrv_backend` and have the core reject a backend whose version does not match the core's expected version. Add a second version with a small additional field (for example, `configure_dma`), and confirm that a backend compiled against the older version still loads against a core that tolerates both, while a backend compiled against the newer version refuses to load against an older core.

This challenge is deeper than it looks. Versioning an interface requires careful thought about compatibility. The mechanics are worth practising once in an isolated context so that you know how to reach for them when your own drivers need them.

### Challenge 7: Audit a Real FreeBSD Driver

Pick one of the drivers referenced in this chapter, for example `/usr/src/sys/dev/uart/`, and audit it against the patterns here. Answer:

- How is the core separated from the backends?
- Where are the register accessors, and do they use `bus_read_*`?
- What backends does the driver support, and how is each registered?
- Are there `#ifdef` chains, and are they well-justified?
- How is the Makefile structured, and what options does it expose?
- What could you change to make the driver more portable, in your judgement?

Write a short report in your lab logbook. The exercise is for you; the aim is to train your eye on real code.

### Challenge 8: Measure the Cost of Backend Dispatch

The backend interface introduces one function-pointer indirection per register access. On a driver that performs millions of accesses per second, that indirection might or might not be measurable. Design an experiment to find out.

Use the simulation backend, so the measurement is not muddied by hardware latency. Modify the driver so it performs a tight loop of `portdrv_read_reg` calls in response to an ioctl. Time the loop with `clock_gettime(CLOCK_MONOTONIC)`. Compare two builds: one where the accessor dispatches through the backend, and one where the accessor calls `bus_read_4` directly.

Report your findings in your lab logbook. On a modern `amd64` CPU, you will likely find the difference is in the single-digit percentage range or smaller. On an older or simpler CPU, it may be larger. Understanding the cost empirically, rather than guessing, is the right way to inform future design decisions.

### Challenge 9: Add a Second FreeBSD Version Target

Take the driver and make it build on both FreeBSD 14.3 and FreeBSD 14.2. Identify one API difference between the two releases that your driver uses, wrap it in `__FreeBSD_version` guards, and confirm the module builds cleanly against both release headers.

Document the difference and your wrapping approach in a short note at the top of the file where the guard lives. This exercise introduces you to the practical side of multi-release compatibility, which you will need if you ever maintain an out-of-tree driver against a range of FreeBSD releases.

### Challenge 10: Design a New Driver from Scratch

Starting from the patterns in this chapter, design a portable driver for a hypothetical device of your choice. The device should have at least two hardware variants (for example, PCI and USB), should have a simple register model, and should be amenable to simulation. Write:

- A one-page design document describing the driver's structure.
- The header file `mydev_backend.h` that defines the backend interface.
- A skeleton implementation of the core that compiles and links with placeholder functions.
- The Makefile with conditional backend selection.

You do not need to implement the full driver. The exercise is to practice the design phase, which is where most of the portability decisions are made. Share your design with a colleague or a mentor and listen for their questions; the questions reveal the parts of the design that were not yet obvious to a fresh reader.

## Troubleshooting and Common Mistakes

Portability refactors fail in characteristic ways. This section catalogues the failures you are likeliest to meet, with concrete symptoms and concrete fixes.

### Symptom: Build Fails with "Undefined Reference" After Splitting a File

A function that was static in the original file is now called from a different file. The compiler does not see it.

**Fix.** Remove the `static` qualifier from the function's definition and add a declaration for it in the appropriate header. If the function should remain private to one file, find the caller in the other file and restructure so that the call crosses a proper interface rather than a private symbol.

### Symptom: Build Fails with "Implicit Function Declaration" After Splitting

A file calls a function whose prototype it has not seen. This is the classic "I moved the definition but forgot the declaration" mistake.

**Fix.** Add `#include "portdrv.h"` or the appropriate header at the top of the calling file. If the header does not declare the function, add the declaration. Treat the prototype as part of the API surface; every function that crosses files must have a declaration in a shared header.

### Symptom: Build Succeeds, but Load Fails with a Dependency Error

The module registration declares a `MODULE_DEPEND` on a module that is not loaded, or loads a version outside the expected range.

**Fix.** Use `kldstat` to see what is loaded. Load the required module first, then load yours. Check that the `MODULE_VERSION` in the dependency matches the range in `MODULE_DEPEND`. If you are in the middle of a refactor and the dependency chain is unstable, temporarily relax the version range or remove the dependency until the refactor settles.

### Symptom: The Driver Loads, but dmesg Shows Only One Backend

The Makefile flags were not set as you expected. The banner reports only the backends whose `#ifdef` was satisfied.

**Fix.** Check the flags on the `make` command line. Look at the actual Makefile expansion:

```sh
make -n
```

The `-n` option prints what `make` would do without executing, and you can see whether `-DPORTDRV_WITH_PCI` actually made it into the compilation command. If not, something in the Makefile conditional is wrong.

### Symptom: Register Reads Return Zero on ARM but Work on x86

You are reaching the device through raw pointer dereference instead of `bus_read_*`.

**Fix.** Replace every raw pointer dereference of device memory with a call to `bus_read_*` or `bus_space_read_*`. On `amd64` the difference is invisible; on ARM it is the difference between working and broken.

### Symptom: The Simulation Backend Works but the PCI Backend Hangs at Attach

The PCI resources were not allocated properly, or the backend-private data structure was not initialised correctly.

**Fix.** Add a `device_printf` trace at each step of the PCI attach function to narrow down where the hang happens. Check that `bus_alloc_resource_any` returned a non-NULL resource. Check that the backend's `attach` callback is called after `sc->sc_backend_priv` is installed; a NULL deref in the callback is easy to cause and obvious once seen.

### Symptom: Build Fails on a Different FreeBSD Release

An API changed between the release you developed on and the release you tried to build on. The compiler complains about a missing function or a mismatched signature.

**Fix.** Bracket the affected code with `#if __FreeBSD_version >= N ... #else ... #endif` where N is the version at which the API took its current form. If the change is used in many places, abstract it into a helper in a compatibility header so the `#if` appears in only one place.

### Symptom: The Driver Panics with "Locking Assertion Failed" After a Backend Split

The core and the backend are disagreeing about which lock is held at the call to the backend method.

**Fix.** Document, in a comment at the top of `portdrv_backend.h`, which lock the core holds at each method call. For each backend operation, either the core holds the softc mutex across the call or it does not, consistently. Audit every backend for conformance. The usual cause is a backend that acquires a lock the core already holds, or vice versa.

### Symptom: The Driver Builds but `make clean` Leaves Stale Object Files

`bsd.kmod.mk` knows how to clean the KMOD's own files but may miss extras you added. The consequence is stale `.o` files that link into the next build and cause odd behaviour.

**Fix.** Delete the build directory and restart:

```sh
rm -f *.o *.ko
make clean
```

Then rebuild. Add the stale files to a `CLEANFILES` variable in the Makefile if they are generated by the build.

### Symptom: Cross-Architecture Build Fails with "Cannot Find `<machine/bus.h>`"

The cross-compile environment is incomplete or the target architecture is not installed.

**Fix.** Follow the FreeBSD handbook on cross-compilation. Typically, you need to install the source tree for the target architecture and run `make TARGET=arm64 TARGET_ARCH=aarch64` from the top of the source tree. Building a single out-of-tree module requires the base system's cross-build support, and the documentation for that is at the top of `/usr/src/Makefile` and in the "Cross-Build" handbook chapter.

### Symptom: A `sysctl` You Just Added Does Not Appear

The sysctl was registered in a file that is not compiled into the module, or the registration code is not called.

**Fix.** Confirm that `portdrv_sysctl.c` is in `SRCS`. Confirm that the sysctl registration is called from the module load or attach path. Add a `device_printf` immediately before the registration call to confirm it executes.

### Symptom: The Driver Works on Real Hardware but Not in `bhyve`

The simulated hardware in `bhyve` exposes slightly different behaviour from the real device. Often this is an MSI versus INTx difference, or a slightly different PCI capability list.

**Fix.** Check the capabilities your driver relies on. Use `pciconf -lvc` on both the host and the guest to see what differs. If a capability your driver needs is missing in the guest, the driver should detect this and degrade gracefully, rather than assuming the capability is always present.

### Symptom: Performance Varies Wildly Between Architectures

You are accessing unaligned data with a direct cast. On `amd64`, unaligned access is cheap; on `arm64`, it can be several times slower if the hardware supports it at all.

**Fix.** Replace direct casts with `memcpy` followed by an endian helper, as shown in Section 5. The resulting code is fast on all architectures and correct on all of them.

### Symptom: `kldunload` Hangs

Something is still using the module. Usually, a callout is still scheduled, an interrupt is still in flight, or a reference count is positive.

**Fix.** Add a `callout_drain` call to your detach path. Add `device_printf` traces at each step of detach to see which step is stuck. Look for open file descriptors on character devices the driver creates; `kldunload` will not complete while a device is open. Use `fstat` to identify which process is holding the device.

### Symptom: Different Behaviour Between `make PORTDRV_WITH_SIM=yes` and `make PORTDRV_WITH_SIM=yes PORTDRV_DEBUG=yes`

Debug code is changing behaviour it should not. Often this is because a debug printf is calling a function that has a side effect, or because the debug path holds a lock longer than the release path.

**Fix.** Review every `PD_DBG` call. If any of them invokes a function rather than just formatting a value, replace the invocation with a pre-computed string. Debug printing must be an observational no-op; if the log statement has side effects, the release behaviour and the debug behaviour will diverge in ways that are hard to trace.

### Symptom: Warning about "Incompatible Pointer Types" After Moving a Function

A function that took a pointer in the original file now takes a slightly different pointer in a new file. Usually the type has drifted between versions of a header, or a typedef has changed subtly.

**Fix.** Open both files and compare the declarations. If the types are genuinely the same, the declaration in a shared header is needed so both files see the same definition. If the types have drifted, pick the correct one and update the caller or the callee.

### Symptom: `bus_dma` Fails with `ENOMEM` on `amd64` but Works on `arm64`

Counter-intuitively, this is usually a `bus_dma_tag_create` configuration issue. The `lowaddr` field was set to a value that does not match the system's actual memory layout.

**Fix.** Check the `lowaddr` and `highaddr` parameters. For a 64-bit device with no limits, use `BUS_SPACE_MAXADDR`. For a 32-bit device, use `BUS_SPACE_MAXADDR_32BIT`. Incorrect addresses produce spurious bounce-buffer allocations that can fail on systems with large memory.

### Symptom: Userland Program Sees Different Data Than the Kernel Log Shows

The `uiomove(9)` call is moving bytes, but the endianness of the values differs between kernel and userland interpretation.

**Fix.** Decide what byte order your character-device interface uses. Document it in a comment on the `cdevsw` struct. Use `htole32`/`le32toh` or similar helpers when values cross the boundary between kernel and userland, just as you would for device boundaries.

### Symptom: The Module Loads but `kldstat` Shows It in a Different Position Than Expected

The module's dependencies are being loaded in an unexpected order. Usually this is a consequence of a missing `MODULE_DEPEND` declaration.

**Fix.** List every module your module depends on with `MODULE_DEPEND`. The kernel resolves the load order automatically; without the declarations, the order is unspecified.

### Symptom: A Debug Build Consumes Much More Memory Than a Release Build

Debug statements are allocating memory on each invocation, or a debug-only data structure is growing without bound.

**Fix.** Review every `PD_DBG` call to ensure none of them perform allocations. If a debug-only data structure must grow, bound its size explicitly. A debug build that triggers out-of-memory conditions is more misleading than helpful.

### Symptom: Tests Pass on the Development Machine but Fail in CI

The CI machine has a different FreeBSD version, a different set of loaded modules, or a different hardware configuration. The difference is exposing a latent bug.

**Fix.** Investigate the specific difference. If the CI environment is correct and the development machine is masking a bug, accept the bug and fix it. If the CI environment is misconfigured, fix the CI environment. Either way, the divergence between development and CI is a signal worth taking seriously.

### Symptom: A Sysctl Can Be Read but Not Written, Even Though `CTLFLAG_RW` Was Set

The sysctl registration used a pointer to a read-only constant, or the handler ignores writes, or the node's parent is read-only.

**Fix.** Confirm the backing variable is writable. Confirm that `CTLFLAG_RW` is actually on the leaf node, not the parent. If a custom handler is installed, ensure it handles the write direction and not only the read direction.

### Symptom: The Driver Panics on `kldunload` After Running for a Long Time

A long-running callout, taskqueue, or workqueue has been freed while still registered, or a resource reference is not released in the right order.

**Fix.** Call `callout_drain` and equivalent drain functions during detach. Ensure every resource allocated during attach is freed during detach, in reverse order. Walk through the attach and detach paths with a piece of paper and track every allocation; this catches the majority of shutdown panics.

### Symptom: Cross-Compiled Module Refuses to Load on the Target

The cross-compiled module's ABI does not match the target kernel's ABI. This is almost always a version mismatch between the build tree and the target.

**Fix.** Ensure the `TARGET` and `TARGET_ARCH` in the cross-compile match the target exactly. If the target runs FreeBSD 14.3-RELEASE, build against the 14.3 source tree, not the 15-CURRENT tree. Module ABI compatibility across major releases is not guaranteed.

### Symptom: A Backend's `attach` Method Is Called Twice on the Same Softc

Either two threads race to attach, or the backend is being re-attached without a detach in between.

**Fix.** Add a state flag in the softc that tracks whether the backend has been attached. The core should refuse to re-attach an already-attached softc. For symmetry, the detach should clear the flag. This catches most duplicate-attach bugs at the logic level, rather than waiting for a resource-exhaustion panic.

### Symptom: Compiler Warns about an Unused Variable in a Conditional Block

The variable is used only when a specific `#ifdef` is active, and the build being compiled has the flag unset.

**Fix.** Move the variable declaration inside the `#ifdef` block if it is only used there. If the variable is used in both branches but the compiler cannot see one of them, add `(void)var;` in the unused branch to tell the compiler the variable is intentionally unused.

### Symptom: The Build Succeeds but `kldxref` Complains About Missing Metadata

The `MODULE_PNP_INFO` declaration is malformed, or the array it points to is the wrong shape. `kldxref(8)` builds an index that maps hardware identifiers to modules, and a malformed `MODULE_PNP_INFO` breaks that index.

**Fix.** Check the arguments to `MODULE_PNP_INFO` against the documentation. The format string must match the layout of the identification array, and the count must be correct. A mismatch is caught at boot when `kldxref` runs, but it is easier to catch at build time by running `kldxref -v` on the directory and inspecting the output.

### Symptom: A Backend That Worked Yesterday Fails Today After a Kernel Update

A kernel API that the backend depends on has changed in the new release. The core is fine; the backend is broken.

**Fix.** Check `UPDATING` in the kernel source tree for any notes about the API that changed. Check the release's `SHARED_LIBS` and module ABI notes. Wrap the affected API call in a `__FreeBSD_version` guard so that the driver supports both the old and new versions. Report the breakage to the backend's maintainer if it is not you.

### Symptom: The Driver Rejects a Device That Should Work

The probe function's matching table is incomplete, or the device ID is being read in the wrong byte order.

**Fix.** Print the vendor and device IDs the probe function is seeing. Compare with the device's datasheet. If the values look byte-swapped, the probe is using the wrong accessor or the wrong mask. If the values look correct but the table does not include them, add the new IDs.

### Symptom: A USB Backend Works with One Device but Not Another of the Same Type

USB devices have descriptors that can vary slightly between firmware revisions of the same product. The backend is matching too strictly on an optional field.

**Fix.** Review the USB matching logic. Match on vendor ID and product ID; ignore release numbers and serial numbers unless they are genuinely significant. The goal is to match the entire product family, not a specific build of it.

### Symptom: After a File Split, Some `static` Functions Can No Longer Be Inlined

Functions that were `static inline` in one file are now called across files, which prevents inlining and can cause a small performance regression.

**Fix.** If the function is small and performance-critical, keep it in a header as a `static inline` so every file that includes the header gets its own copy. If the function is larger, move it to the appropriate `.c` file as a plain function. The compiler can still inline across translation units if link-time optimisation is enabled, but for kernel code that is rarely the case.

### Symptom: `make` Recompiles Everything Even When Only One File Changed

The Makefile does not track header dependencies correctly. Changes to a shared header cause all files that include it to recompile, which is correct. But sometimes the Makefile is too aggressive and recompiles unrelated files.

**Fix.** Check that `bsd.kmod.mk` is generating dependency files (they usually have a `.depend` extension). If they are missing, add `.include <bsd.dep.mk>` or ensure the default include chain runs. For a small driver, full rebuilds are usually acceptable; for a large one, dependency tracking pays for itself.

### Generic Guidance

If you are stuck on a portability problem that does not match any of the above, three habits usually help.

**Narrow the configuration.** If the driver fails only with certain backends enabled, build it with only one backend at a time and find the configuration in which it first breaks.

**Narrow the architecture.** If the driver fails on `arm64` but not on `amd64`, the bug is almost certainly endianness, alignment, or a type-size mismatch. Those are the only three common causes of architecture-specific behaviour in driver code.

**Narrow the function.** Add printfs, add `KASSERT`s, delete code until the smallest remaining driver still fails. Kernel bugs are hard to reason about, but they become tractable once you can trigger them from a tiny example.

**Bisect the change.** If the driver worked in an earlier commit and fails now, run `git bisect`. Kernel bugs are often specific regressions, and bisecting finds the first bad commit in O(log n) steps.

**Read the compiler's warnings, even the annoying ones.** A "set but not used" warning can point to a field that was renamed and forgotten. An "implicit function declaration" warning is a missed include. A "comparison between signed and unsigned" warning can be the source of the exact wraparound bug you are chasing. Compiler warnings are free advice; use them.

## A Retrospective: The Patterns at a Glance

Before the closing note, here is a compact retrospective of the patterns this chapter introduced. Use it as a quick reference when you return to the chapter or when you are reviewing your own code. Each entry is the idea compressed to a sentence, with a pointer back to the section that introduces it in full.

**Accessors over primitives.** Wrap every register access in a driver-local function rather than calling `bus_read_*` directly in the code that cares about the device logic. The wrapper is a single point of change, it names the operation, and it lets you swap the implementation without touching callers. See Section 2.

**Backend interface.** Describe the variable part of the driver as a struct of function pointers. Each bus, hardware variant, or simulation becomes an instance of the struct. The core calls through the struct rather than knowing which backend is present. See Section 3.

**File split by responsibility.** Put core logic in one file, each backend in its own file, and each large subsystem concern (sysctl, ioctl, DMA helpers) in its own file. A reader should be able to guess which file to open from what they are looking for. See Section 4.

**Fixed-width types.** Use `uint8_t` through `uint64_t` for values whose size matters. Avoid `int`, `long`, `unsigned`, and `size_t` when the width is fixed by the device or protocol. See Section 5.

**Endian helpers.** Every multi-byte value that crosses the hardware boundary passes through `htole32`, `le32toh`, or their siblings. No exceptions. See Section 5.

**`bus_space(9)` and `bus_dma(9)`.** Use the architecture-independent APIs for register access and for DMA. Never cast a raw pointer to device memory, and never compute physical addresses directly. See Sections 2 and 5.

**Memory barriers.** Use `bus_barrier` to enforce ordering between device-visible accesses when the order matters. Use `bus_dmamap_sync` to enforce coherency between CPU caches and DMA buffers. See Section 5.

**Conditional compilation with restraint.** Use build-time flags for features that should be compiled in or out. Keep `#ifdef` blocks coarse. Hide guards inside macros when they are used in many places. See Section 6.

**`__FreeBSD_version`.** Use sparingly to guard code that depends on a specific kernel release. Centralise the guards in a compatibility header if they appear more than a few times. See Section 6.

**Cross-BSD wrapping.** Isolate OS-specific APIs behind wrapper macros when cross-BSD support is needed. Keep the core free of OS-specific code. See Section 7.

**Module versioning.** Use `MODULE_VERSION` and `MODULE_DEPEND` to make upgrade and load-order issues visible to the kernel. Bump versions when observable behaviour changes. See Section 8.

**Build matrix.** Maintain a list of supported build configurations. Run a script that builds every configuration before a release. See Section 8.

**Portability README.** Document supported platforms, backends, build options, and limitations in a short readme. Update it whenever any of those change. See Section 8.

**Telemetry.** Expose key counters through a sysctl tree. Bug reports that include sysctl output are easier to triage than reports that do not. See Section 8.

Print this list or keep a copy in your notes. The patterns work because they reinforce one another, and seeing them together is the fastest way to internalise the shape of a portable driver.

## Portability Antipatterns: What to Unlearn

Having seen the patterns that work, it helps to name the patterns that do not. Most driver portability disasters are not spectacular failures of judgement; they are small, comfortable habits that compound. A driver author who copies a working function without understanding it will be tempted, six months later, to copy it again with a small tweak. A driver that has one `#ifdef` for one architecture will, six months later, have five. Each step feels harmless; together they produce code that nobody wants to touch.

This short catalogue names the antipatterns so that you can recognise them in your own code and in code you review. The goal is not to shame; it is to give you the vocabulary to identify the drift early and redirect it.

### The "It Works on My Machine" Driver

A driver that only has one target, one FreeBSD version, and one bus hides portability bugs rather than lacking them. The code may compile and run cleanly for years, but the moment someone else tries to build it on a different architecture or a newer release, every shortcut is exposed at once. The antidote is not to test on every platform from day one; it is to write as if you will one day, so the eventual port is a one-week exercise rather than a three-month one. Fixed-width types, endian helpers, and `bus_read_*` are cheap insurance against the future you cannot yet see.

### The Silent `#ifdef`

The `#ifdef` block with no comment explaining why it is there is almost always a mistake. If the block is guarding truly architecture-specific code, say so, and say what the alternative branch does. If it is guarding a workaround for a bug in a specific chip, say which chip, which bug, and when it might be removed. A driver file with undocumented `#ifdef`s is a file where future-you will not know whether the branch is still needed. Document, or delete.

### The Casual Cast

A cast from a pointer to an integer type, or from an integer of one size to another, is almost always a bug in waiting. On `amd64`, a pointer is 64 bits and a `long` is 64 bits, so the cast happens to work. On an `i386` system, a pointer is 32 bits and `long` is 32 bits, so the cast happens to work. On 32-bit ARM, a pointer is 32 bits but `uint64_t` is, of course, 64 bits; here the cast changes size and triggers a warning only on a subset of builds. Casts that the compiler accepts silently on one platform and warns about on another are precisely the casts you should avoid. If the logic requires a conversion, use `uintptr_t`, which is explicitly designed to hold a pointer, and document why the cast is needed.

### The Optimistic Register

A register read or write that the driver assumes will succeed is an optimistic register. On real hardware, a read from an unmapped address can return `0xFFFFFFFF`, can machine-check the CPU, or can silently produce garbage, depending on the bus and platform. On a simulation backend, the same read returns a known constant. Drivers that never check for the pathological read are drivers that work until one day the hardware is powered off or behind a PCI bus that disconnected due to a hotplug event. Defensive coding at the accessor level (does the return value look sane?) is cheaper than debugging the panic that follows.

### The Ad-Hoc Version Check

`if (major_version >= 14)` sprinkled across the driver is an antipattern. Version checks belong in a compatibility header, not scattered through the code. A single `#define PORTDRV_HAS_NEW_DMA_API 1` in one place, guarded by one `__FreeBSD_version` test, keeps the rest of the driver clean. Scattered version checks are also fragile: when the API changes again, you will forget one of the dozen scattered checks, and the driver will fail in a subtle way on one release.

### The "Temporary" Hack

A comment that says `// TODO: remove before release` or `// FIXME: arm64` is a promise to yourself. Ninety percent of such promises are never kept. Before you write a temporary hack, ask whether there is a way to solve the problem properly in half an hour. Often there is. When there truly is not, the hack should come with a tracking ticket and a calendar reminder. Without those, "temporary" becomes "permanent" on the timescale of a single release cycle.

### The Duplicated Constant

A magic number written once is a design decision. A magic number written twice is a bug waiting for one of the copies to be updated without the other. Constants for register addresses, bit masks, timeouts, and limits should live in one header and be included everywhere they are used. When you catch yourself typing the same number in two places, stop and put it in a `#define`.

### The Growing `struct softc`

A softc that accumulates fields across many commits without any re-organisation is a code smell. It is a sign that every feature added to the driver put its state directly into the softc without asking whether the state belonged somewhere else. When a softc has more than twenty fields, consider grouping related fields into nested structs: `sc->dma.tag`, `sc->dma.map`, `sc->dma.seg` rather than `sc->dma_tag`, `sc->dma_map`, `sc->dma_seg`. The refactor is small, and the resulting structure reads better in logs and in debugger output.

### The Uneven Test Coverage

A driver whose tests only cover the happy path is a driver whose error paths will bite someone in production. Every function that can fail should be testable, and should be tested, in at least the obvious failure mode. The simulation backend makes this cheap; use it to inject failures (out of memory, DMA timeout, device not responding) and assert the core handles them gracefully. A driver that has never been tested against a failing backend is a driver whose error paths are wishful thinking.

### The Gratuitous Abstraction

It is possible to err in the other direction. A driver with a twelve-method backend interface, three layers of wrapping, and an abstract factory for backend instances is likely over-engineered for the concrete need. Abstractions have a cost: they obscure what the code does, they spread knowledge across files, and they make debugging harder. The right level of abstraction is the one that lets you solve today's problem cleanly without making tomorrow's problem meaningfully easier. When in doubt, start with less abstraction and add it when a second concrete use case justifies the cost.

### The Monolithic Commit

A single commit that introduces accessors, splits files, adds a backend interface, and creates a Makefile matrix is impossible to review, impossible to bisect, and impossible to revert cleanly. Split the work: one commit per step, each commit leaving the driver in a buildable, loadable state. Small commits also communicate design intent: the commit message for "introduce accessors" tells a reader exactly what that commit is trying to achieve, whereas a grand commit titled "refactor for portability" tells them nothing useful.

Recognising these antipatterns in your own code is a sign of maturity. The patterns in the main body of the chapter are what you should do; the antipatterns here are what to watch for. When you find one in a driver you wrote last year, do not be hard on yourself; simply fix it. Everyone writes antipatterns at some point. What separates a seasoned author is the speed at which they notice and correct them.

## Portability in the Real World: Three Short Stories

Principles land better when they are tied to consequences. Here are three short stories based on patterns that recur in real FreeBSD development. No single anecdote below is one specific incident; each is a composite of issues that experienced driver authors have seen more than once. The details are illustrative, not verbatim. The lessons are exactly as stated.

### Story 1: The Endian Bug That Slept for Five Years

A driver for a storage controller was written in 2019 on an `amd64` workstation, shipped to a handful of production servers, and ran cleanly for five years. In late 2024, a team migrating to `arm64` servers for power efficiency loaded the same driver on the new hardware. Reads worked. Writes succeeded. Sporadic data corruption appeared a week later, limited to a small fraction of I/O requests.

The bug was a single line in the driver's descriptor setup: a raw struct assignment that deposited an `amd64`-native little-endian value into a ring descriptor that the hardware interpreted in its own byte order. On `amd64`, the hardware byte order happened to match the host byte order, so the raw assignment worked by coincidence. On `arm64`, the kernel runs little-endian too, so the code did not trip immediately, but the hardware in question had a newer firmware revision that changed the descriptor byte order for certain command types. The one byte order that the driver had been relying on by coincidence disappeared, and only a specific workload (streaming writes crossing a descriptor boundary) hit the corruption.

The fix was a one-line change to use `htole32` for every descriptor field. The diagnosis took three weeks, spanning two teams, because nobody had written the first driver expecting it to encounter a second platform or a hardware revision. The lesson is that endianness bugs do not announce themselves the moment they are introduced; they can sleep for years until a change somewhere else wakes them. Writing `htole32` from the start is cheaper than paying for the sleep.

### Story 2: The Conditional Block Nobody Remembered

A network driver that supported both PCIe-attached and embedded FDT-attached variants carried a two-line `#ifdef PCI` block in its attach path. The block enabled an error-handling feature that mattered only on the PCIe variant, because the embedded variant used a different subsystem for the same purpose. The `#ifdef` had no comment.

Three years later, the driver author moved on. The driver acquired a new maintainer. A kernel update changed the semantics of the feature toggled by the `#ifdef`, and the driver started reporting spurious errors on the embedded variant. Nobody knew whether the `#ifdef` was still needed, whether it had ever been needed, or whether it was covering up a bug in either the embedded or the PCIe path. A week of investigation revealed that yes, the `#ifdef` was still correct; it was documenting a real asymmetry between the two paths; but because it had no comment, every new reader had to re-derive that knowledge from scratch.

The fix was to add a three-line comment above the `#ifdef`, citing the subsystem difference and naming the kernel commit that introduced the need for the block. Total cost of the fix: ten minutes. Total cost of the investigation across three years: about forty engineer-hours.

The lesson is that conditional compilation blocks age into mystery very quickly. Every `#ifdef` should have a comment telling a future reader why it is there. The comment pays for itself the first time anyone else touches the file.

### Story 3: The Simulation Backend That Saved a Release

A driver for a custom sensor had a simulation backend added at the end of its first development cycle. The addition took two days of work and was considered a nicety rather than a necessity. The real hardware was rare, expensive, and kept in a lab across the building from the main developer.

Late in the release cycle, a regression appeared: under a specific sequence of user operations, the driver would deadlock on unload. Reproducing the bug on the real hardware required booking the lab, which was in use for validation of a separate product. The developer instead used the simulation backend, reproduced the deadlock within an hour, identified the missing `mtx_unlock` that caused it, and shipped the fix the same day.

The lesson is that simulation backends are not just for testing; they are for reproducing bugs without contending for scarce hardware. The two days invested in the simulation backend saved perhaps a week of schedule pressure and arguably saved the release. The pattern is worth the cost.

These three stories illustrate what the chapter has been arguing abstractly. Endianness bugs sleep. Conditional blocks age. Simulation backends pay back quickly. The specific incidents are illustrative, but the patterns are real, and they repeat across drivers and across years. Knowing them in advance is the difference between writing a driver you will maintain and writing a driver that will embarrass you.

## A Short Closing Note on Discipline

This chapter has asked you to be patient with small, structural decisions. The patterns here, accessors, backend interfaces, file splits, endian helpers, version tags, are not thrilling when you first meet them. They are the kind of thing that a new driver author learns because an older one said to. The younger self reads about them, nods, and writes them down as a checklist.

Reading them as a checklist misses the point. The patterns work because they shape the habits with which you write new code. The first driver you write with accessors in place is also the first driver in which you never forget to check for a NULL resource, because the accessor is the natural place to do the check and you wrote it once. The first driver you structure into core and backends is also the first driver in which adding a feature means touching one file. The first driver you version with `MODULE_VERSION` and `MODULE_DEPEND` is also the first driver whose upgrade path is obvious to your users. The benefits are not in the patterns; they are in the habits the patterns create.

Habit takes time. You will write the next driver faster than you wrote this one, and the one after that faster still, not because the patterns have shortcuts but because your fingers stop reaching for the long way. The cost of portability, measured per driver, decreases sharply after the first. That is why this chapter matters for the rest of your career, and not just for this book.

The great FreeBSD driver authors did not become great by memorising the patterns in this chapter. They became great by applying them every time, even on the small, boring drivers that nobody was going to read. That practice scales. Pick it up early.

### What Success Looks Like

A signpost, since you have come this far. When the patterns in this chapter start to feel automatic, something changes about the way you write drivers. You stop writing `bus_read_4(res, 0x08)` and reach for `portdrv_read_reg(sc, REG_STATUS)` instinctively. You stop wondering which file a new function belongs in; you know. You stop thinking about whether to `htole32` or not; if the value crosses a hardware boundary, it gets the helper, full stop. You stop arguing in code review about whether a flag should be compile-time or runtime, because you have a working heuristic. These are not trivial gains. They are the difference between writing drivers and writing drivers well.

The shift does not happen on any specific day. It happens gradually, over the course of three or four drivers written in this style. One day you notice that a pattern you used to think about is now just what your fingers do. That is the sign that the habit has taken. Keep going.

### A Note on the Path Ahead

Part 7 of this book is titled "Mastery Topics" for a reason. Chapter 29 is the first chapter where the material is about craft rather than technique. Chapters 30 through 38 will continue in this spirit: each chapter addresses a topic that separates a competent driver author from a seasoned one. Virtualisation and containerisation in Chapter 30. Interrupt handling and deferred processing in Chapter 31. Advanced DMA patterns in Chapter 32. And onward.

The patterns in this chapter will reappear throughout those chapters. A virtualisation-aware driver uses the same backend abstraction to add a virtio backend. An interrupt-driven driver uses the same accessors to manage interrupt enable and acknowledge registers. An advanced DMA driver uses the same `bus_dmamap_sync` calls in more sophisticated sequences. Everything you learned here is a foundation for what comes next.

Treat Chapter 29 as the hinge between the driver-subsystem chapters of Part 6 and the mastery chapters of Part 7. You have crossed it now. The rest of the book builds on what you learned here.

## Mini-Glossary of Portability Terms

A short glossary follows, aimed at the reader who wants to revisit the chapter's core vocabulary in one place. Use it as a refresher, not as a replacement for the explanations in the main text.

- **Backend.** An implementation of the driver's core interface for a specific bus, hardware variant, or simulation. A driver can have multiple backends in the same module.
- **Backend interface.** A struct of function pointers that the core calls into. Each backend provides an instance of this struct with its own implementations.
- **Core.** The hardware-independent logic of a driver. Calls into the backend interface but knows nothing about specific buses.
- **Accessor.** A driver-local wrapper around a primitive operation such as `bus_read_4`. Gives the driver a single point of change when the primitive or the backend differs.
- **`bus_space(9)`.** The architecture-independent API for memory-mapped and I/O-port device access. Defined in `/usr/src/sys/sys/bus.h` and the per-architecture `/usr/src/sys/<arch>/include/_bus.h` files.
- **`bus_dma(9)`.** The architecture-independent API for direct memory access by devices. Defined in `/usr/src/sys/sys/bus_dma.h`.
- **Endianness.** The byte order in which a multi-byte value is stored in memory. Handled in FreeBSD through helpers declared in `/usr/src/sys/sys/endian.h`.
- **Alignment.** The requirement that a multi-byte value be stored at an address that is a multiple of its size. Relaxed on `amd64` and `i386`; strictly enforced on some ARM cores.
- **Word size.** The native integer width of a CPU. 32-bit and 64-bit FreeBSD platforms exist. Drivers should use fixed-width types whenever the size matters.
- **Fixed-width type.** A C type whose size is guaranteed: `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, and their signed counterparts. Declared in `/usr/src/sys/sys/types.h` and its includes.
- **`__FreeBSD_version`.** An integer macro that identifies the FreeBSD kernel release the driver is being compiled against. Use to guard code that depends on a specific kernel API version.
- **Kernel options.** Named compile-time flags declared in kernel configuration files. Used to enable or disable optional features. Your driver defines `PORTDRV_*` flags in the Makefile and guards code with `#ifdef PORTDRV_*`.
- **Build matrix.** A table of supported build configurations, along with whether each configuration is expected to compile and whether it has been tested. Validated by a script that iterates over the matrix.
- **Backend versioning.** A `version` field in the backend interface struct that lets the core reject a backend compiled against a mismatched interface.
- **Compatibility wrapper.** A macro or function that abstracts a kernel API difference between FreeBSD releases or between different BSDs. Used sparingly, because each wrapper is a maintenance cost.
- **Simulation backend.** A software-only backend that does not talk to real hardware. Used for testing the core without requiring a physical device.
- **Module dependency.** A declaration, via `MODULE_DEPEND`, that one module requires another at a specific version range. Enforced by the kernel at load time.
- **Subregion.** A named slice of a larger memory-mapped region, carved out with `bus_space_subregion`. Used to give each logical bank of registers its own tag and handle.
- **Memory barrier.** An instruction or compiler directive that enforces ordering between memory operations. Issued via `bus_barrier` or the `atomic_thread_fence_*` family.
- **Bounce buffer.** A temporary buffer used by `bus_dma` when a device cannot reach the physical address of a requested buffer directly. Transparent to the driver; made effective by the cooperation between `bus_dma_tag_create` and `bus_dmamap_sync`.
- **Include graph.** The directed graph of which source or header files include which others. A clean include graph is a sign of a well-structured driver.
- **Compile-time telemetry.** Build-time information (version, build date, enabled backends) embedded into the binary and printed at load time. Useful for debugging and bug reports.
- **API vs. ABI.** The application programming interface is what programmers see; the application binary interface is what linkers see. A change can preserve one and break the other; portability practices must consider both.
- **Stacked backend.** A backend whose implementation wraps another backend, adding logging, tracing, delaying, or other behaviour. Useful for debugging and replay tools.
- **Memory-mapped I/O (MMIO).** A technique in which device registers appear as memory addresses that the CPU can read or write. Accessed through `bus_space` in FreeBSD rather than through raw pointers.
- **Port-mapped I/O (PIO).** An older scheme, common on `i386`, in which device registers are accessed through a separate I/O address space. Also accessed through `bus_space`, which abstracts the difference between MMIO and PIO.
- **DMA segment.** A contiguous physical address range that a device accesses directly. `bus_dma_segment_t` describes one; a DMA transfer may involve multiple segments.
- **Coherency.** The property that a memory location presents the same value to all observers (CPU, DMA engine, caches) at the same time. On some architectures, achieving coherency requires explicit `bus_dmamap_sync` calls.
- **KASSERT.** A kernel-space assertion macro. Panics the kernel when the condition is false; compiled out in release builds. Used to check invariants during development.
- **DEVMETHOD.** A macro in a `device_method_t` array that binds a Newbus method name to an implementation. Terminated by `DEVMETHOD_END`.
- **DRIVER_MODULE.** The macro that ties together a Newbus driver's name, parent bus, driver definition, and optional event callbacks. Usually appears once per bus attachment.
- **`MODULE_PNP_INFO`.** A declaration that describes the device IDs a driver claims, so `devmatch(8)` can auto-load the module when matching hardware appears. Orthogonal to `MODULE_VERSION` and `MODULE_DEPEND`.
- **Loader tunable.** A variable set from `/boot/loader.conf` that a kernel module reads via `TUNABLE_INT` or similar. The most common way to parameterise a driver at boot time.
- **sysctl node.** A named variable exposed through the `sysctl(3)` interface. Can be read-only, writable, or a procedural handler. A portable driver exposes counters and options via sysctl.
- **device hints.** A legacy but still-useful FreeBSD mechanism for supplying per-instance configuration (IRQs, I/O addresses) from `/boot/device.hints`. Parsed by the Newbus framework.
- **Kernel options (`opt_*.h`).** Per-configuration header files generated by the build system, used to guard kernel features with `#ifdef`. Distinct from module-level build flags.
- **Module blacklisting.** A mechanism (via `/boot/loader.conf` or `kldxref`) for preventing a module from being auto-loaded. Useful during debugging.
- **`bus_space_tag_t` and `bus_space_handle_t`.** Opaque types that together identify a memory region. The tag says which kind of space; the handle points into it. Passed as the first two arguments to `bus_space_read_*` and `bus_space_write_*`.
- **Attach path.** The sequence of Newbus calls (`probe`, `attach`, `detach`) that binds a driver to a device instance. Portable drivers isolate bus-specific logic inside this path.
- **Softc lifetime.** The lifetime of a `struct softc` instance, bounded by `attach` (allocation) and `detach` (free). A portable driver treats the softc as the authoritative per-instance state, not a collection of module-global variables.
- **Hot path.** The code path executed on every I/O operation. Performance-critical code. Keep it small and free of unnecessary checks.
- **Cold path.** Code executed rarely, such as during attach, detach, or error recovery. Correctness trumps performance; explicit error handling belongs here.
- **Hotplug.** The ability of a device to be physically added or removed from a running system. PCI hotplug is the main FreeBSD use case; drivers that are expected to support it must handle `detach` correctly at arbitrary points.
- **Refactor.** A structural change to code that preserves its observable behaviour. Portability refactors typically introduce accessors, split files, and introduce backend interfaces; none of these should change what users see.
- **Invariant.** A property that a piece of code guarantees at a given point in execution. Declaring invariants with `KASSERT` makes them checkable during development and self-documenting thereafter.
- **Build configuration.** A combination of Makefile flags, kernel options, and target platform that defines a specific build. A driver's build matrix is the set of configurations it officially supports.
- **Binary drop.** A delivery of a driver as a precompiled `.ko` file rather than as source. Portable binary drops are harder than portable source drops; platform and version compatibility become runtime concerns.
- **Source drop.** A delivery of a driver as source code, typically a tarball or a git repository. Easier to port than a binary drop, because the user's kernel headers and compiler adapt to their platform.
- **Driver lifecycle.** The set of transitions a driver goes through from load to unload: register, probe, attach, operate, detach, unregister. Portable drivers document where each transition is permitted to fail.
- **Migration path.** The documented steps a user follows when upgrading from an older driver version to a newer one. A portable driver keeps migration paths short and reversible.

Keep this glossary nearby as you read Chapter 30 and the chapters that follow. Each term recurs often enough that a quick reference pays for itself.

## Frequently Asked Questions

New driver authors tend to ask the same questions as they work through their first portability refactor. Here are the most common ones, with short, pointed answers. Each answer is a signpost, not an exhaustive treatment; follow the bread crumbs back to the relevant section of the chapter if you want more detail.

**Q: How much of this applies if my driver only needs to run on amd64?**

All of it, except the architecture-specific sections. Even if you never plan to support `arm64`, the discipline of using fixed-width types, endian helpers, and `bus_read_*` costs almost nothing and protects you against bugs that would show up on `amd64` too, just in edge cases. The backend interface and the file split help readability and maintenance even when there is only one backend. If your first driver ever becomes a second driver, you will be glad you started the habit.

**Q: Should every driver have a simulation backend?**

No, but many should. The question to ask is whether the driver can be meaningfully exercised without the real hardware. If yes, a simulation backend lets you run unit tests and catch regressions on CI runners without provisioning the hardware. If no, a simulation backend is extra code with no payoff. Storage drivers, network drivers, and many sensor drivers benefit; a driver for a very specific piece of custom hardware might not.

**Q: Why use a struct of function pointers instead of FreeBSD's `kobj(9)` framework?**

For small drivers, a plain struct is simpler and easier to read. `kobj(9)` is a capable system that adds compile-time method resolution and interface-file processing, which is why Newbus uses it. For a driver with a handful of backends and a simple method set, the plain struct gets you most of the benefit with a fraction of the machinery. If your driver grows large enough that the ceremony of `kobj` pays off, switching is a local refactor.

**Q: My driver has only one backend today. Should I still define a backend interface?**

Probably yes, if the effort is small. A single-entry interface costs almost nothing and lets you add a second backend cheaply later. If the effort is large because the driver is tightly coupled to its current bus, consider refactoring first for separation of concerns, and add the interface when a second backend is actually needed.

**Q: How do I decide where to draw the line between core and backend?**

A practical test: can you imagine writing the same logic for a simulated backend without changing the code at all? If yes, that logic belongs in the core. If the logic inherently mentions PCI, USB, or any specific bus, it belongs in the backend. In doubtful cases, put the logic in the core and see whether the simulation backend can use it unchanged; if not, move it.

**Q: What do I do with a driver that uses a raw `volatile` pointer to device memory?**

Replace every access with `bus_read_*` or `bus_write_*`. This is a one-time refactor and it is the single biggest portability improvement you can make to a driver.

**Q: How often should I re-run my build matrix?**

On every commit to the main branch, if you can. Weekly if you cannot. The longer a regression lives unnoticed, the more work it takes to bisect. Automated matrix validation is cheap; a bisecting sleuth session is expensive.

**Q: Can I use `#ifdef __amd64__` inside a driver source file?**

Sparingly. Prefer machine-specific headers for non-trivial architecture differences. If you genuinely need a small amount of platform-specific code, a short `#if` block is acceptable, but if it grows beyond a few lines, consider moving the code to a separate file and including it via an architecture-selective path or a wrapper header.

**Q: Why does the reference driver's Makefile fall back to SIM if no backend is selected?**

So that a plain `make` produces a loadable module. A driver that refuses to build without a specific flag set is a friction point for new contributors and for CI. The SIM fallback means anyone who just runs `make` gets something they can load and explore, even without any hardware.

**Q: When should I add `MODULE_VERSION` and `MODULE_DEPEND`?**

Always, even for internal drivers. `MODULE_VERSION` is a one-line declaration that tells consumers which release of your driver they have. `MODULE_DEPEND` prevents incompatible combinations from loading. Both are essentially free, and they save hours of debugging when something is mismatched.

**Q: How does a cross-BSD driver handle things like `malloc` that have different signatures?**

Through a per-OS compatibility header that maps a driver-local wrapper (for example, `portdrv_malloc`) onto the correct OS primitive. The core calls `portdrv_malloc`, and the compatibility header contains a branch per OS. The core is never polluted with OS-specific code; only the wrapper header is.

**Q: Is it worth making a production driver cross-BSD compatible if my users are all on FreeBSD?**

Generally no. The upkeep cost is real, and compatibility work that is not exercised tends to rot. The patterns in this chapter still benefit the FreeBSD-only driver, but the explicit wrapper layer for other BSDs should only be introduced when there is a concrete plan to support them.

**Q: How do I test endianness if my only hardware is `amd64`?**

Use QEMU with a big-endian target such as `qemu-system-ppc64`. Boot a FreeBSD big-endian release in the guest, cross-build your driver for `powerpc64`, and run it there. This is the only reliable way to catch endianness bugs short of deploying on real big-endian hardware. Challenge 3 in this chapter gives a lighter-weight approximation using a build flag.

**Q: My driver compiles on FreeBSD 14.3 and fails on 14.2. What is going on?**

An API added in 14.3. Use `__FreeBSD_version` to guard the use of the new API and provide a fallback for the older release. If the new API is essential and cannot be worked around, document the minimum supported version clearly in `README.portability.md`.

**Q: Why does the chapter keep talking about the UART driver?**

Because `/usr/src/sys/dev/uart/` is one of the clearest examples in the FreeBSD tree of a driver with a clean core/backend separation and multiple, well-factored backends. Reading it alongside this chapter is one of the best ways to internalise the patterns, and it is why I reference it so often.

**Q: Are there other FreeBSD drivers that illustrate these patterns well?**

Yes. Beyond `uart(4)`, the `sdhci(4)` family in `/usr/src/sys/dev/sdhci/` is a good study: core logic in `sdhci.c`, with PCI and FDT backends in `sdhci_pci.c` and `sdhci_fdt.c`. The `ahci(4)` driver in `/usr/src/sys/dev/ahci/` also separates its core from its bus attachments cleanly. The `ixgbe` driver in `/usr/src/sys/dev/ixgbe/` shows layered accessors and a tight separation between hardware-specific helpers and the rest of the driver. Reading any of these as a study-alongside source is valuable; picking the one closest to the kind of driver you are writing is even more so.

**Q: My driver already has five hundred lines. Is it too late to refactor for portability?**

No. Five hundred lines is small. Even five thousand lines is refactorable, though the refactor will take a few days. The important thing is to do the refactor incrementally: introduce accessors first, then split files, then introduce the backend interface. Each step leaves the driver working, and you can stop at any point if priorities change.

**Q: What if my hardware has only one physical variant and is unlikely to ever change?**

Then portability across hardware variants is not a concern for you. But portability across architectures, across FreeBSD versions, and between real hardware and a simulated one are still worth the effort. Even a single-variant driver benefits from having a simulation backend for testing and from using the endian helpers so it works on every architecture FreeBSD supports. The patterns here are not only about "what if new hardware comes along"; they are also about making your one driver robust in its current form.

**Q: How do I get review feedback on a portability refactor?**

If you are working on an in-tree driver, send the change to `freebsd-hackers@freebsd.org` or to the relevant subsystem maintainer. For out-of-tree drivers, ask a colleague or a mentor. Either way, break the refactor into small patches: one patch per step. A reviewer can keep up with six small patches; they cannot keep up with one large one. The smaller you make each change, the better the review you will receive.

**Q: Should I rename the driver when I refactor it for portability?**

Usually no. The refactor is an internal change; users do not need to care. The module name, the device node name, and the sysctl tree name should all remain stable so that existing configurations continue to work. The internal file structure is a detail the user never sees.

**Q: How do I justify the time spent on portability to a manager who wants features?**

Talk in terms of concrete future costs. "If we support a second hardware variant later, the current driver will take three weeks to fork; a refactor now takes one week, and any subsequent fork takes two days." "If a customer reports a bug on arm64, the current driver has to be ported before we can even reproduce; the refactored driver would reproduce in a VM." Portability pays for itself in the second year, not the first, and communicating that horizon is how the conversation gets easier.

**Q: Can two drivers share a common library module?**

Yes. FreeBSD supports module-to-module dependencies via `MODULE_DEPEND`. You can build a common library as its own module and have your drivers depend on it. This is how some of the in-tree drivers share helper code. The mechanism is a little fiddly for a small project, but it is the right approach when two drivers genuinely share significant helper code.

**Q: What is the minimum test I should run after a refactor before committing?**

Build. Load. Run the basic use case once. Unload. Rebuild from clean. Re-load. If all these succeed, the refactor is probably safe. If the driver has a test suite, run it. If there is no test suite, the refactor is a good time to write one, because the simulation backend makes tests easy to run.

**Q: How does portability relate to security?**

Portable code is usually more secure than non-portable code, because the discipline that supports portability (clear interfaces, narrow responsibilities, careful type usage, strict input validation) also supports security. A driver that uses `uint32_t` consistently is less likely to overflow. A driver with a clean backend interface is less likely to have ill-defined error paths that leak memory. A driver that is tested on multiple architectures is more likely to have encountered edge cases that expose security bugs. This is not a formal relationship, but it is a real one, and the two concerns reinforce each other more than they conflict.

**Q: How much of this chapter applies to drivers I download from vendors rather than write myself?**

Less directly, but still some. When you review a vendor-provided driver, the patterns in this chapter let you evaluate its quality. A vendor driver that is a single file of five thousand lines with scattered `#ifdef` chains is higher risk than one that is structured into core and backend. When you consume such a driver, you can use the audit questions from Challenge 7 to form an opinion about its maintainability. If the answers are discouraging, factor that into how much you rely on the driver.

**Q: What is the single most important idea in this chapter?**

That the cost of portability is an investment you make once per driver, and that the return on that investment is proportional to how long the driver lives. The specific patterns (backend interfaces, endian helpers, fixed-width types, file splits) are all instances of the same underlying discipline: **separate the details that can change from the logic that should not change, and express that separation in code rather than in comments**. Learn that discipline and you will apply it everywhere, not just in FreeBSD drivers.

**Q: How do I debug a driver that panics only on arm64?**

Start with the panic message itself; it often names the offending function and the kind of fault (alignment, unaligned access, null pointer, invalid memory). Rebuild the driver with debug symbols (`CFLAGS+= -g`), load it on the arm64 target under `kgdb` or `ddb`, and reproduce the panic. Unaligned access faults are the most common arm64-only panic; they almost always mean a struct member was accessed at an offset that is not aligned to its type's natural alignment. Fix these by packing or reordering struct fields, or by using `memcpy` for reads and writes that must be unaligned by design.

**Q: What is the right balance between runtime flexibility and compile-time flexibility?**

A useful rule of thumb: if a choice varies between deployments of the same binary, make it a runtime tunable. If a choice varies between releases of the driver (because a feature is new, or because a platform is not supported), make it a compile-time flag. Tunables let users reconfigure without rebuilding; compile-time flags keep the binary small and sharp. When in doubt, start with compile-time and promote to runtime only when a concrete need arises.

**Q: Is it OK to disable a backend at runtime via a sysctl?**

Yes, but only for backends that support re-initialisation cleanly. Disabling a backend mid-operation can leak resources or leave the driver in an inconsistent state. The safer pattern is to expose the choice at attach time (via a loader tunable) and to treat the runtime sysctl as read-only except during a well-defined maintenance window. If you do expose runtime disable, test it thoroughly, including under I/O load.

**Q: How do I contribute a portability fix upstream to the FreeBSD project?**

If the fix is to an existing in-tree driver, the canonical path is to file a bug in the FreeBSD Bugzilla, attach a patch in unified diff format (or, for larger changes, as `git` commits), and either wait for a committer to pick it up or post the patch to `freebsd-hackers@freebsd.org` for review. Include a clear description of the bug, steps to reproduce, and the platform(s) on which the bug appears. Smaller patches get reviewed faster; if your fix is large, split it into independently reviewable parts.

**Q: Does every driver need a README.portability.md?**

Every driver that ships beyond a single developer's machine benefits from one. The file does not need to be long; even three sections (supported platforms, build configurations, known limitations) are enough to cover the essentials. The file pays for itself the first time a user asks a question that the README answers; you send a link instead of typing the answer again.

**Q: What happens if I forget to call `bus_dmamap_sync` after a DMA?**

On `amd64`, usually nothing visible, because the CPU and DMA engine share the same memory and cache hierarchy. On `arm64` with a device that is not cache-coherent, you will see stale data: the CPU reads its cached copy rather than the memory the device wrote. The bug is architecture-dependent, intermittent, and hard to reproduce without explicit test cases. This is why `bus_dmamap_sync` is mandatory in portable code even when a specific target does not strictly require it.

**Q: Should I use `kobj(9)` or plain function pointers for my backend interface?**

For fewer than five or six methods, plain function pointers in a struct are simpler and easier to debug. For larger interfaces, especially those with inheritance or method overriding, `kobj(9)` starts to pay back the ceremony. If you are unsure, start with function pointers; migrating later is a local refactor, and you will have a clear picture of whether `kobj` is worth it by then.

**Q: What is the best way to learn how an existing driver works?**

Read the code top-down. Start with `DRIVER_MODULE` (the Newbus anchor) and work outward: the attach method, then the methods called from attach, then their callees. Take notes as you go: a one-line description of each function in a plain text file is enough. After an hour or two you will have a functional map of the driver. Depth comes from repeating this exercise across several drivers; no single pass will make you an expert.

**Q: How do I verify that my build matrix actually covers what I think it covers?**

Write a test that fails if a configuration is missing. The simplest form is a shell loop that iterates over the configurations listed in `README.portability.md` and checks that each one has a corresponding entry in the build matrix script. Less rigorously, review the README and the script side by side once a release and confirm they agree. The point is not to be perfect; it is to make the two sources of truth stay aligned over time.

**Q: Can I mix clang-specific and gcc-specific code in a portable driver?**

Ideally no. FreeBSD's base system uses `clang` by default, and writing portable C that compiles cleanly under both compilers is rarely difficult. When a specific feature is only available under one compiler (for example, a specific sanitizer or a compiler-specific attribute), guard it with `#ifdef __clang__` or `#ifdef __GNUC__` and provide a fallback. But such cases should be rare; most of the time, plain C11 works everywhere.

**Q: What do I do when two backends need to share helper code?**

Put the helper in a shared file, say `portdrv_shared.c`, and link both backends against it. Alternatively, put the helper in the core if it genuinely belongs there. Avoid the temptation to copy the helper into each backend; copy-paste is the opposite of portability. If you find yourself wanting to duplicate a function, the function wants to be shared, and the only question is where the shared copy lives.

**Q: How do I version a backend interface that needs to evolve?**

Add a `version` field to the backend struct. The core checks the field at registration time and rejects backends compiled against an older interface. When the interface evolves in a backward-compatible way, bump the minor version; when it evolves in a breaking way, bump the major version and require all backends to be updated. The mechanism is simple and it prevents the class of bug in which a stale backend silently misbehaves because its struct layout has changed.

**Q: What makes a driver "good" beyond the patterns in this chapter?**

Good drivers are reliable, fast, maintainable, and kind to their users. Reliability comes from careful error handling and thorough testing. Speed comes from a clean data path that avoids unnecessary work and locks. Maintainability comes from the patterns in this chapter. Kindness comes from clear error messages, useful logging, and documentation that respects the reader's time. A driver that does well on all four dimensions is a driver worth shipping; a driver that does well on only two is a driver that will age poorly. Aim for all four.

**Q: How do I know when a driver is finished?**

It never is, quite. A driver "in production" is a driver under continuous maintenance, because the hardware world keeps changing around it. A reasonable definition of "ready to ship" is: the driver builds cleanly on every supported configuration, passes the tests you have written for it, survives the reliability soak test, and has a README that reflects its current state. Beyond that, every driver is an ongoing conversation between its author, its users, and the platform. The patterns in this chapter are the shape of that conversation; the driver itself is the record of it.

**Q: What should I read next, after Chapter 30?**

The chapters that follow in Part 7 build on Chapter 29's foundations in specific directions: virtualisation (Chapter 30), advanced interrupt handling (Chapter 31), advanced DMA patterns (Chapter 32), and so on. Read them in order; each one assumes the patterns of this chapter as table stakes. If you want to go wider rather than deeper, read a driver from an area of FreeBSD you have not touched before (storage if you have been doing network, or vice versa), and look for how the patterns of Chapter 29 show up there. Breadth and depth reinforce each other; alternate between them.

## Where the Chapter's Patterns Appear in the Real Tree

A short tour of the FreeBSD source tree, pointing out where you can see the patterns of this chapter applied. Use this as a reading list when you want to practise recognising the structures in production code.

**`/usr/src/sys/dev/uart/`** for backend abstraction. The core in `uart_core.c` is free of bus-specific code. The backends in `uart_bus_pci.c`, `uart_bus_fdt.c`, and `uart_bus_acpi.c` each implement the same interface through different buses. The interface itself lives in `uart_bus.h` as `struct uart_class`. See also `uart_dev_*.c` files for different hardware variants, each a `struct uart_class` instance.

**`/usr/src/sys/dev/sdhci/`** for layered bus and hardware abstraction. The `sdhci.c` core is tall enough that it has its own internal structure, with backends for PCI (`sdhci_pci.c`) and FDT (`sdhci_fdt.c`) plus SoC-specific helpers for specific platforms. The driver illustrates how a mature abstraction accommodates both bus variations and minor hardware variations within a single bus.

**`/usr/src/sys/dev/e1000/`** for layered register accessors. The Intel Gigabit Ethernet driver stacks its register access in four layers (`bus_space_*` primitives, the `E1000_READ_REG`/`E1000_WRITE_REG` macros in `e1000_osdep.h`, the chip-family helpers in the per-generation files, and the driver-facing API in `if_em.c`), as discussed earlier. Reading it alongside Section 2 is a good way to understand when multiple layers are justified.

**`/usr/src/sys/dev/ahci/`** for split by both bus and platform. AHCI supports PCI-attached storage controllers across several generations and across x86 and non-x86 platforms. The driver uses a tight backend interface and per-platform initialisation helpers.

**`/usr/src/sys/dev/virtio/`** for the paravirtual pattern. The virtio transport is itself a portable abstraction, and the drivers that use it illustrate how a backend can be purely virtual. Each virtio driver (network, block, console, and so on) talks to the virtio transport rather than a physical bus. This is the pattern that Chapter 30 will return to.

**`/usr/src/sys/net/if_tuntap.c`** for the simulation pattern applied to a real driver. The `tuntap(4)` driver is not a backend of something else; it is a full driver that synthesises a network interface purely in software. The pattern is useful even in production code.

**`/usr/src/sys/sys/bus.h`** for the `bus_read_*` and `bus_write_*` helpers. The header is instructive to read top to bottom, because it makes clear how much architectural abstraction is layered into what looks like a simple function call.

**`/usr/src/sys/sys/endian.h`** for the endian helpers. A short header that is worth reading in its entirety at least once, because understanding what `htole32` actually expands to on each platform is the best way to remember why you should use it.

**`/usr/src/sys/sys/bus_dma.h`** for the DMA API. Another short header, and the starting point for understanding the `bus_dma` abstraction.

**`/usr/src/sys/modules/*/Makefile`** for real Makefile patterns. Browse at random; almost every subdirectory has one, and each shows a different shape of portability challenge solved by a concrete Makefile.

Reading these files is one of the best ways to internalise the chapter's ideas. Not every file is simple or small, but each one embodies lessons that are hard to convey in the abstract. Treat the tree as a library of examples, not just as a reference.

## A Self-Study Schedule for the Patterns

If you want to take the chapter's lessons further before moving on to Chapter 30, a modest self-study schedule is one way to do it. The suggestions below are sized to fit into a few evenings rather than a week-long course, and each one leaves you with a small, concrete piece of code to keep.

**Week 1: The reference driver end to end.** Build `portdrv` in every backend configuration. Load, test, and unload each one. Walk through the source with the chapter in your other hand, and annotate the code with the section numbers that introduce each pattern. You will finish the week with a marked-up copy of a working, portable driver and a clear mental map of where each pattern appears in practice.

**Week 2: Real-world reading.** Pick one driver from the list in the previous section ("Where the Chapter's Patterns Appear in the Real Tree"). `uart(4)` is a good first choice; `sdhci(4)` is a good second. Read the core file and one backend file. For each pattern this chapter introduced, locate the analogous construct in the real driver and write it down. A few pages of notes from this exercise will do more for your skill than reading another chapter cold.

**Week 3: A small driver of your own.** Write a tiny, original driver from scratch using the chapter's patterns. It can be a pseudo-device, a simulated sensor, or a wrapper around an existing FreeBSD feature exposed as a character device. The specific subject does not matter; what matters is that you use accessors, separate core and backend, structure the Makefile for optional features, and write a `README.portability.md` file. The result will be small and possibly silly, but you will have written a driver with the chapter's patterns from the first line, not retrofitted them.

**Week 4: An audit.** Find any FreeBSD driver, in-tree or out-of-tree, that looks interesting to you. Apply Challenge 7 to it: go through the audit questions and grade the driver. Write your findings as a short memo, as if to the driver's original author. Do not send the memo; write it for practice. The exercise of formalising what you notice about a driver will sharpen your eye.

**After four weeks**, you will have built one driver, read two, written a tiny original, and audited one more. That is not a trivial amount of practice, and it is enough to carry the chapter's patterns into your own long-term work. The schedule is a suggestion, not a prescription; adjust to fit the time you have.

One caveat: do not treat this as a race. The patterns land gradually, and the benefit comes from repeated exposure rather than from any single intense session. A slow, steady schedule, an hour or two per evening, produces more durable understanding than a one-day sprint that covers the same ground.

If you finish the four-week schedule and want to keep going, choose one of the later chapters' topics (interrupts, DMA, or virtio) and apply the same four-week pattern to it: read one, study two, build one, audit one. The method generalises to any topic in FreeBSD kernel development, and once you have done it twice it stops feeling like a schedule at all; it just becomes how you learn.

## A Note on Collaboration and Code Review

Portable drivers are easier to review than non-portable drivers. That is not an accident; it is a consequence of the patterns in this chapter. Accessors make every register access reviewable in isolation. Backend splits make it obvious which parts of the driver are bus-independent and which are not. Fixed-width types and endian helpers let a reviewer verify correctness without running the code. A reviewer who can see the structure can focus on the logic, which is what you actually want from the review.

If you are reviewing a driver written in the style of this chapter, there are a few questions worth having ready at hand. Does every register access go through an accessor? Is the backend interface well defined, with clear method contracts? Are all multi-byte values that cross the hardware boundary passed through the endian helpers? Does the Makefile support the build configurations the README claims? Is the module version bumped when the external behaviour changes? A review that answers these questions is a review that adds value; a review that only checks for formatting is a review that wastes time.

If you are on the receiving end of a review, do not take style comments personally. A reviewer pointing out a missed accessor or a bare `volatile` cast is helping you avoid a bug before it becomes one. Take the feedback, make the change, and thank the reviewer. The fastest path to becoming a better driver author is to be reviewed often by authors who are better than you.

### Code Review Checklists

If you find yourself reviewing many drivers, a short checklist saves time. The one below is compatible with the patterns in this chapter; print it and keep it next to your review environment.

- **Every register access goes through an accessor.** Grep for `bus_read_`, `bus_write_`, and `volatile` in files that are not the accessor file. If there are hits, flag them.
- **Every multi-byte value that crosses the hardware boundary uses endian helpers.** Look at every struct that represents a descriptor, packet, or message. Verify that reads use `le*toh`/`be*toh` and writes use `htole*`/`htobe*`.
- **Fixed-width types are used consistently.** Grep for `int`, `long`, `short`, and `unsigned` in contexts that touch device registers or DMA descriptors. If the code uses a non-fixed-width type, ask whether the size was actually unknown.
- **The backend interface contract is documented.** A reader of the backend header should understand the contract each method fulfils without reading the core source.
- **Conditional compilation blocks have comments.** `#ifdef` blocks without an explanation of why the block exists are a warning sign. Ask the author to add the justification or remove the block.
- **Module version and dependencies are declared.** `MODULE_VERSION` and, where appropriate, `MODULE_DEPEND` are present. The version matches the driver's documented version.
- **The Makefile supports documented build configurations.** The configurations the README mentions actually build when tried. If the README is out of date, say so.
- **Error paths free what they allocated.** Goto-style cleanup, if used, unwinds correctly. Each allocation is balanced by a free.
- **Locking is consistent.** The same data structure is always protected by the same lock, and the core does not call into the backend while holding a lock unless the backend's documentation allows it.
- **Tests exist and pass.** At minimum, a build-matrix script and a smoke test for each backend. Ideally a CI harness that runs them automatically.

A review that covers these items is a review that makes the driver better. Not every review will find issues in every category, but going through the list quickly confirms which categories are clean and which deserve deeper attention.

## A Short Section on Pragmatism

Not every piece of advice in this chapter applies to every driver equally. The chapter has been pitched at the driver that will ship, be maintained for years, run on more than one platform, and be read by more than one author. A throwaway driver written to confirm a hardware quirk does not need any of this. A personal driver written for a single laptop does not need most of it. The patterns are proportional to the expected lifetime and reach of the driver.

How do you decide how much to apply? A few rules of thumb:

- If the driver will only ever be loaded on one machine and will be rewritten when the hardware is replaced, use fixed-width types and endian helpers, and skip the rest. Even a throwaway driver benefits from those two small disciplines.
- If the driver will be shared with colleagues but not shipped, add accessors and a simple Makefile structure. You will not regret it when a colleague asks to build it on their machine.
- If the driver will be shipped to more than one customer, apply everything in Sections 2 through 5. A backend interface, a clean file split, and careful type usage are all in play.
- If the driver will be upstreamed or maintained for five or more years, apply everything in the chapter. This is the driver where the investment pays off most clearly.

Pragmatism means knowing which of these buckets a given driver belongs in and applying the right amount of effort accordingly. Both under-investing and over-investing hurt; the former leaves you with a brittle driver, the latter burns time on abstractions that never pay back. The chapter has shown you what to do when the driver is worth doing well; the judgement about when it is worth doing well is yours.

## Questions to Ask Yourself Before Calling a Driver Done

Before you consider a portable driver complete, walk through the following questions. They are a final pass, a last readiness review for the patterns this chapter introduced. The list reads quickly; going through it honestly takes more time than you expect, because most drivers find at least one item they can improve.

**Would a new reader guess the file layout correctly on the first try?** If someone who has never seen your driver is told that a bug exists in the PCI attach path, would they open the right file immediately? If not, the file split may be muddled or the names may be unhelpful.

**Would the driver compile unchanged on arm64, riscv64, or powerpc64?** You do not need to test on every platform, but the answer should be a confident yes for every platform FreeBSD supports. If you hesitate, the reason for the hesitation is something to fix.

**Can a user replace the hardware with a simulation and run every user-facing code path?** If the simulation backend cannot reach a given path (say, the error recovery code), the test coverage for that path is implicit. Make the simulation rich enough to reach it.

**If you disappeared tomorrow, would the next maintainer know why any given `#ifdef` block exists?** Not just from the conditional itself, but from a comment above it. If any `#ifdef` is unexplained, it is a hole in the driver's institutional memory.

**Does the driver fail loudly when something is wrong, or fail silently?** A driver that silently returns zero when it should return `ENODEV` is harder to debug than one that prints a warning. Quiet failure modes are the ones that surprise people in production.

**Can you name every lock in the driver and say what it protects?** If you cannot produce a one-sentence description of each lock and the data it guards, the locking design is probably less clear than you think. Fix it before you call the driver done.

**Does `kldunload` always succeed?** Run the driver, push some traffic through it, then try to unload. If it hangs, the driver has a reference it is not releasing. Find the reference before shipping.

**Does the driver survive repeated `kldload`/`kldunload` cycles?** Run ten loads and unloads in a loop. If memory usage creeps up, you have a leak. If the tenth unload fails where the first succeeded, you have a stale reference.

**Does the driver handle being loaded before its dependencies?** `MODULE_DEPEND` declarations are the mechanism, but it is worth manually testing that loading your driver from `/boot/loader.conf` (before full userland is up) works as you expect.

**Are the error messages specific enough to be useful in a bug report?** If a user sends you `portdrv0: attach failed`, can you identify the cause? If not, add context: which operation failed, with what code, on what bus.

**Does the Makefile pass a clean build with `-Werror`?** If it does not, something in your code is generating warnings. Warnings are the compiler telling you about code smells; do not silence the signal.

**Have you removed every `printf` added for debugging?** Debug prints left in production code slow the driver down and clutter `dmesg`. Use `if_printf`, `device_printf`, or gated debug macros, and remove everything that is not needed.

Going through this list before every release is a habit worth developing. A driver that passes all twelve questions is a driver that is ready to ship; a driver that fails any of them has an issue that is worth addressing before the release goes out.

## Resources for Further Study

The patterns in this chapter are deep enough that a single reading will not exhaust them. The resources below are the ones most likely to repay the effort of following up.

**The FreeBSD source tree.** No book about FreeBSD drivers can substitute for time spent reading the tree itself. The specific drivers pointed to in this chapter (`uart`, `sdhci`, `ahci`, `e1000`, `virtio`, `tuntap`) are starting points. When you find one you like, read its full commit history. The evolution of a driver often teaches as much as the current snapshot.

**The FreeBSD handbook.** The handbook is not primarily about driver development, but its chapters on the kernel, on jails, and on `bhyve` provide context that informs how drivers get deployed. A portable driver cooperates with these features; reading about them is part of writing for them.

**`sys/sys/bus.h`, `sys/sys/bus_dma.h`, `sys/sys/endian.h`, `sys/sys/systm.h`.** These headers are short enough to read top to bottom in a single sitting. Each one teaches a different portability concern: `bus.h` the bus abstractions, `bus_dma.h` the DMA abstractions, `endian.h` the endian helpers, `systm.h` the general kernel utilities. Read them; mark them up; come back to them.

**The FreeBSD Architecture Handbook.** This is the document to read when you want to understand why things are the way they are. It is less hands-on than this book but complements it by explaining the history and reasoning behind key kernel subsystems.

**The `style(9)` manual page.** Not glamorous, but the single most important reference for what your code should look like. Read it once thoroughly; skim it again every six months. Familiarity with the in-tree style makes your drivers easier for the community to read and review.

**Community forums: `freebsd-hackers@freebsd.org` and `freebsd-arch@freebsd.org`.** These lists are the best places to ask questions about non-trivial driver problems. Search the archives before posting; many questions have been asked before. When you do post, follow the community's conventions: clear subject, self-contained description, specific versions and platforms named.

**The FreeBSD commit log (`git log` in the source tree).** When you want to understand how a specific feature came to be, trace it through the log. You will find design discussion embedded in the commit messages, particularly for large changes. Read widely: not just commits to the driver you are studying but also the kernel changes that drive new driver requirements.

**Your own changes over time.** If you apply the patterns in this chapter across several drivers, keep the drivers in a version control system and read through the history occasionally. Your own evolution as a driver author is as instructive as any external reference; looking back at the choices you made six months ago is one of the best ways to see where you are growing.

These resources will sustain you beyond the boundaries of this book. The book is an introduction and a scaffolding; mastery comes from the continuous work of applying the patterns, reading more code, and refining your own taste in engineering. Pace yourself and stay curious.

## Ten Principles to Carry Forward

A chapter this long deserves a closing compression. The ten principles below are what to remember if nothing else from this chapter sticks. Each one is a distillation of a section; together they are the shape of a portable driver.

**1. Separate what changes from what does not.** The heart of portability is knowing which parts of your driver are likely to vary and isolating them behind an interface. Buses change. Hardware variants change. Architectures change. The driver's core logic should not.

**2. Write through accessors, not through primitives.** Every register access goes through a named function in your driver. The name describes the operation, not the underlying primitive. One day you will be glad you did.

**3. Use fixed-width types at hardware boundaries.** `uint32_t` for a 32-bit register field, `uint64_t` for a 64-bit descriptor word. Never `int` or `long` when the size matters.

**4. Byte-swap at the boundary, and only at the boundary.** Every multi-byte value that crosses from CPU to hardware or vice versa passes through an endian helper. The rest of the code is blissfully unaware of byte order.

**5. Express variation as data, not as conditionals.** A table of backends, a struct of function pointers, a registry indexed by device ID. These are easier to read, test, and extend than sprawling `if-else` chains.

**6. Split files by responsibility, not by size.** The core in one file, each backend in its own file, each large subsystem concern in its own file. File boundaries communicate design intent.

**7. Keep conditional compilation coarse and commented.** A `#ifdef` block with a clear comment is a feature toggle. A sea of nested `#ifdef`s with no comments is a maintenance crisis.

**8. Version everything that can change.** `MODULE_VERSION`, backend interface version fields, `README.portability.md`. When the driver's world changes, its version numbers make the change legible.

**9. Test with simulation, build on a matrix.** A simulation backend exercises your core without hardware. A build matrix catches compile-time portability regressions before they reach users. Both are force multipliers.

**10. Document the invariants, not the syntax.** Comments explain why a choice was made, not what the code does. Code says what; comments say why. The combination is what makes a driver outlive its author.

These ten principles are the chapter. Everything else is elaboration, example, and exercise. Commit them to memory, apply them consistently, and the patterns will feel natural within a year. That is the payoff.

One more note on the principles. They are not a ranking; they are a set. Leaving any one out weakens the others. A driver with perfect endian handling but no file split is still hard to maintain. A driver with a clean file split but no `MODULE_VERSION` is still hard to upgrade. A driver with flawless abstractions but no tests is still fragile under change. The patterns reinforce each other, and missing one undermines the value of the rest. If you must pick a subset (say, because the driver is small or throwaway), pick based on the lifetime and reach you expect, not on which pattern is cheapest today.

The last encouragement is the simplest: start somewhere. Take one driver, one weekend, one pattern from the list, and apply it. Look at what changed. Notice that the driver got better, even if only slightly. Then do it again, the following weekend, with another pattern. After a month of this, the driver will be in better shape than you can remember it, and your sense for what the patterns feel like in practice will be solid. The path to mastery is not dramatic; it is just the patient accumulation of small improvements over time. You now have the map.

## Wrapping Up

Portability is easier to internalise when you can see it on the page, so let us close the chapter with a small, concrete example of what a cross-release change looks like in the real FreeBSD tree. Open `/usr/src/sys/dev/rtsx/rtsx.c`, find `rtsx_set_tran_settings()`, and look at the short block guarded by `#if __FreeBSD_version >= 1300000`. Inside that guard, the driver handles the `MMC_VCCQ` field, which carries the I/O voltage for an MMC card and was added in the FreeBSD 13 series. On older trees the field does not exist; on 14.x it does; and the guard is the only thing that lets a single source file compile cleanly on both. That is portability expressed as a short conditional rather than as two forks of the same driver, and it is exactly the discipline this chapter has been asking you to adopt.

The lesson of that small block is larger than the block itself. Every change between releases, whether it is a new field in a structure, a renamed function, a deprecated macro, or a reorganised header, can be absorbed by the same pattern you just saw: name the variation, isolate it behind a guard or an accessor, and leave a comment that explains why the block is there. The driver keeps working on old and new trees alike, and your energy goes into the logic rather than into maintaining parallel copies of the same file for every release you care about.

A similar story, at a much larger scale, is on display in the Intel Ethernet driver at `/usr/src/sys/dev/e1000/if_em.c`. There, the attach and detach path now goes through the `iflib` framework: `em_if_attach_pre(if_ctx_t ctx)` replaces the bespoke `device_t` attach that this driver once implemented directly, and the `DEVMETHOD` table near the top of the file dispatches `device_probe`, `device_attach`, and `device_detach` through `iflib_device_probe`, `iflib_device_attach`, and `iflib_device_detach`. You are not looking at a small conditional there; you are looking at the result of moving a whole family of NIC drivers onto a shared abstraction so that the per-chip code and the iflib-common code can evolve on different schedules. It is the same instinct as the `rtsx` example, applied at a bigger scale.

If you take one thing from this chapter, take that instinct. Portability is not a property you add at the end of the project, and it is not a special kind of code you write once and forget about. It is a way of sitting down at a source file and asking: what might change, where will it change, and how do I make sure the change lands in one place rather than twenty? Answer that question consistently, and the drivers you write in the coming years will outlive several hardware generations, several kernel releases, and probably several jobs.

## Looking Ahead: Bridge to Chapter 30

You have just refactored a driver for portability. The next chapter, **Virtualisation and Containerization**, turns from the driver's own internals to the environment it runs in. FreeBSD drivers run not only on bare metal but inside `bhyve` guests, under `jail(8)` with VNET, on virtio-backed storage and network, and increasingly inside OCI-style container runtimes built on top of jails.

That question is sharper after Chapter 29 than it was before. You have now learned to write drivers that absorb variations in hardware, bus, and architecture as local changes. Chapter 30 will add a new kind of variation: the environment itself is virtualised. The machine your driver sees may not be a real machine; the devices it probes may be paravirtual; the host may have removed whole classes of capability because the guest is not trusted to use them. You will learn how virtio guest drivers work, how `bhyve`'s `vmm(4)` and `pci_passthru(4)` fit together, how VNET jails isolate network stacks, and how to write drivers that cooperate with those forms of containment without assuming the usual privileges.

You will not be writing a new kind of driver in Chapter 30. You will be learning how the drivers you already know how to write must adapt when the environment around them is virtual, partitioned, or containerised. That is a different kind of step, and it matters the moment you deploy a FreeBSD system in a cloud or in a multi-tenant host.

Before you move on, unload every module you created in this chapter, clean every build directory, and make sure the driver source tree is in a tidy state. Close your lab logbook with a brief note on what worked, what surprised you, and which pattern you expect to reach for most often in your own drivers. Rest your eyes for a minute. Then, when you are ready, turn the page.

## A Final Word: The Long Arc

It is worth stepping back one more time, because chapters about craft are easy to read and hard to apply. The patterns in this book are not a checklist to tick off on a single project; they are a long-running investment in your own capability. The first driver you write using them will feel like a lot of work for modest gain. The fifth driver will feel natural. The tenth driver will be faster and more reliable than any driver you could have written without the patterns, and you will no longer remember that you once found them effortful.

That arc is not unique to FreeBSD driver development. It is the arc of every skilled practice, from carpentry to music to compiler writing. The craft starts as a set of disciplines that feel heavy and deliberate; it becomes, after enough practice, the shape of your attention. When you sit down to write your next driver and find that you have already thought about the backend structure before typing the first line, that is the craft at work.

The chapter ends here, but the practice does not. Keep building. Keep reading. Keep refactoring drivers that are not yet as portable as they could be. The payoff is measurable only over years, which is the exact timescale that FreeBSD drivers tend to live on. You have picked a long game; the patterns in this chapter are the rules that let you play it well.

You have earned the step.
