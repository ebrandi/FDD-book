---
title: "Device Tree and Embedded Development"
description: "Driver development for embedded systems using device tree"
partNumber: 7
partName: "Mastery Topics: Special Scenarios and Edge Cases"
chapter: 32
lastUpdated: "2026-04-20"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 195
---

# Device Tree and Embedded Development

## Introduction

Chapter 31 trained you to look at your driver from the outside, through the eyes of whoever might try to misuse it. The boundaries you learned to watch were invisible to the compiler but very real to the kernel: user space on one side, kernel memory on the other; one thread with privilege, another without; a length field the caller claimed, a length the driver must verify. That chapter was about who is allowed to ask the driver to do something, and what the driver should check before it agrees.

Chapter 32 shifts the perspective entirely. The question stops being *who wants this driver to run* and becomes *how does this driver find its hardware at all*. On the machines we have leaned on so far, the answer was easy enough that you could ignore it. A PCI device announced itself through standard configuration registers. An ACPI-described peripheral appeared in a table the firmware handed to the kernel. The bus did the looking, the kernel probed each candidate, and your driver's `probe()` function only had to look at an identifier and say yes or no. Discovery was mostly someone else's problem.

On embedded platforms that assumption breaks. A small ARM board does not speak PCI, does not carry an ACPI BIOS, and does not have a firmware layer that will hand the kernel a neat table of devices. The SoC has an I2C controller at a fixed physical address, three UARTs at three other fixed addresses, a GPIO bank at a fourth, a timer, a watchdog, a clock tree, a pin multiplexer, and a dozen other peripherals that the hardware designer soldered onto the board in a particular arrangement. Nothing in the silicon announces itself. If the kernel is going to attach drivers to these peripherals, something has to tell the kernel where they are, what they are, and how they relate.

That something is the **Device Tree**, and learning to work with it is what Chapter 32 is about.

Device Tree is not a driver. It is not a subsystem in the sense that `vfs` or `devfs` is a subsystem. It is a *data structure*, a textual hardware description that the bootloader hands to the kernel at boot time, and that the kernel then walks to decide which drivers to run and where to point them. The structure has its own file format, its own compiler, its own conventions, and its own quiet conventions that embedded developers learn over time. A driver written for a Device Tree platform looks almost like the drivers you already know, with a handful of important differences in how it finds its resources. Those differences are the subject of this chapter.

We are also going to widen the map. Until now most of your drivers have run on amd64, the flavour of 64-bit x86 that powers laptops, workstations, and servers. That architecture will not be going anywhere, and your understanding of it will continue to serve you. But FreeBSD runs on more than amd64. It runs on arm64, the 64-bit ARM architecture that powers the Raspberry Pi 4, the Pine64, the HoneyComb LX2, countless industrial boards, and a growing fraction of the cloud. It runs on 32-bit ARM for older Pis, BeagleBones, and embedded appliances. It runs on RISC-V, a newer, open architecture whose first serious FreeBSD support matured over the FreeBSD 13 and 14 cycles. On every one of those architectures outside the PC-like world, Device Tree is how drivers find their hardware. If you want to write drivers that run on anything other than a laptop, you need to know how it works.

The good news is that the shape of a driver does not change much when you cross over. Your probe and attach routines still look like probe and attach routines. Your softc still lives in the same kind of structure. Your lifecycle is still the same lifecycle, with load and unload, detach and cleanup. What changes is the set of helpers you call to discover your hardware's address, to read its interrupt specification, to turn on its clock, to pull its reset line, to request a GPIO pin. Those helpers have a family resemblance once you see a few of them. The FreeBSD source tree uses them in hundreds of places, and by the end of this chapter you will recognise them on sight and know where to look when you need one you have not used before.

The chapter proceeds in eight sections. Section 1 introduces the embedded FreeBSD world and the hardware platforms on which it runs. Section 2 explains what the Device Tree is, how its files are organised, how `.dts` source turns into `.dtb` binary blobs, and what the nodes and properties inside actually mean. Section 3 walks through FreeBSD's support for Device Tree: the `fdt(4)` framework, the `ofw_bus` interfaces, the `simplebus` enumerator, and the Open Firmware helpers that drivers use to read properties. Section 4 is the first concrete driver-writing section. You will see the shape of an FDT-aware driver from probe to attach to detach, with its `ofw_compat_data` table and its calls to `ofw_bus_search_compatible`, `ofw_bus_get_node`, and the property-reading helpers. Section 5 turns to the DTB itself: how to compile a `.dts` with `dtc`, how overlays work, how to add a custom node to an existing board description, and how FreeBSD's build tooling fits together. Section 6 is about debugging, both at boot and at runtime, using `ofwdump(8)`, `dmesg`, and the kernel log to find out why a node did not match. Section 7 brings the pieces together in a practical GPIO driver that takes its pin assignments from Device Tree and toggles an LED. Section 8 is the refactoring section: we will take the driver you built in Section 7, tighten its error handling, expose a sysctl for observability, and talk through what packaging an embedded image looks like.

Along the way we will touch on the peripheral frameworks that every serious embedded driver eventually leans on. The clock framework, regulator framework, and hardware-reset framework under `/usr/src/sys/dev/extres/` are the three big ones; the pin-control framework under `/usr/src/sys/dev/fdt/` is the fourth. The GPIO framework under `/usr/src/sys/dev/gpio/` is your first stop for reading and writing pins. `interrupt-parent` chaining routes IRQs up the interrupt tree to the controller that can actually handle them. Device Tree *overlays*, files with the extension `.dtbo`, let you add or modify nodes at boot without rebuilding the base blob. And at the highest level, on arm64 in particular, a single kernel binary can run on either FDT or ACPI; the mechanism that picks between the two is worth looking at briefly, because it shows how drivers written for both are factored.

One last framing note before we begin. Embedded work can feel intimidating from a distance. The lexicon is unfamiliar, the boards are small and fiddly, the documentation is thinner than on desktop platforms, and the first time a DTB mismatch sends you into a silent boot hang it is easy to lose your nerve. None of that is a reason to stay away. The core skill you are going to build in this chapter, reading a `.dts` file and recognising what is being described, is learnable in a long afternoon. The second skill, writing a driver that matches on a compatible string and walks the properties it needs, is the same kind of driver writing you already know with three or four new helper calls. The third skill, building and loading a DTB and seeing your driver attach, is exactly the kind of feedback loop that makes kernel work enjoyable. By the end of the chapter you will have written, compiled, loaded, and observed an FDT-aware driver on either a real ARM board or an emulated one, and you will have the mental model that lets you read any FreeBSD embedded driver and understand how it finds its hardware.

The rest of the book will continue to treat drivers as drivers, but your toolkit will have grown. Let us begin.

## Reader Guidance: How to Use This Chapter

Chapter 32 sits in a particular place in the book's arc. Earlier chapters assumed a PC-like machine where buses discovered themselves and drivers bound to devices the kernel already knew about. This chapter steps sideways into a world where discovery is a problem the driver has to participate in. That step is easier than it sounds, but it does require a small shift in how you think about hardware, and a willingness to read some new kinds of files.

There are two reading paths, and one optional third path for readers who have embedded hardware at home.

If you choose the **reading-only path**, plan for three to four focused hours. You will finish the chapter with a clear mental model of what Device Tree is, how the kernel uses it, what an FDT-aware driver looks like, and where the important FreeBSD source files live. You will not have typed a driver, but you will be able to read one and understand every helper call you see. For many readers, this is the right stopping point on a first pass.

If you choose the **reading-plus-labs path**, plan for seven to ten hours spread across two or three sessions. The labs are built around a small pedagogical driver called `edled`, short for *embedded LED*. Over the chapter you will write a minimal FDT driver that matches a custom compatible string, grow it into a driver that reads its pin number and timer interval from Device Tree, and finally wrap it in a tidy detach path with a sysctl for runtime observability. You will compile a tiny `.dts` fragment into a `.dtb` overlay, load it with FreeBSD's loader, and watch the driver attach in `dmesg`. None of these steps are long; none require more than a basic familiarity with `make`, `sudo`, and a shell.

If you have access to a Raspberry Pi 3 or 4, a BeagleBone, a Pine64, a Rock64, or a compatible ARM board, you can follow the **reading-plus-labs-plus-hardware path**. In that case the `edled` driver blinks a real LED attached to a real pin, and you get the satisfying experience of writing kernel code that makes a physical thing happen. If you do not have hardware, do not worry. The entire chapter can be followed with a QEMU virtual machine emulating a generic ARM platform, or even with printed simulation on a regular FreeBSD machine. You will not miss any of the conceptual material.

### Prerequisites

You should be comfortable with the FreeBSD driver skeleton from earlier chapters: module init and exit, `probe()` and `attach()`, softc allocation and destruction, `DRIVER_MODULE()` registration, and the basics of `bus_alloc_resource_any()` and `bus_setup_intr()`. If any of those is fuzzy, a short revisit of Chapters 6 through 14 before this chapter will pay off. The helpers in this chapter sit *alongside* the familiar `bus_*` helpers; they do not replace them.

You should also be comfortable with simple FreeBSD system administration: loading and unloading kernel modules with `kldload(8)` and `kldunload(8)`, reading `dmesg(8)`, editing `/boot/loader.conf`, and running a command as root. You will use all of them, but none at any deeper level than earlier chapters have already asked.

No prior experience with embedded hardware is required. If you have never touched an ARM board, the chapter will teach you what you need. If you have used a Raspberry Pi but only as a small Linux machine, the chapter will give you a new perspective on what is going on under its covers.

### Structure and Pacing

Section 1 sets the stage: what embedded FreeBSD looks like in practice, which architectures FreeBSD supports, what kinds of boards you are likely to encounter, and why Device Tree is the answer embedded platforms have settled on. Section 2 is the longest conceptual section: it introduces Device Tree files, their source and binary forms, their node-and-property structure, and the handful of conventions you need to read them fluently. Section 3 brings the conversation back to FreeBSD specifically, introducing the `fdt(4)` framework, the `ofw_bus` interfaces, and the simplebus enumerator. Section 4 is the first driver-writing section; it walks through the canonical shape of an FDT driver from probe to attach to detach. Section 5 teaches you how to build and modify `.dtb` files and how FreeBSD's overlays system works. Section 6 is the debugging section. Section 7 is the substantive worked example: the `edled` GPIO driver. Section 8 is the refactoring and finalising section.

After Section 8 you will find hands-on labs, challenge exercises, a troubleshooting reference, the Wrapping Up review, and the bridge into Chapter 33.

Read the sections in order. They build on each other, and the later sections assume the vocabulary the earlier ones establish. If you are short on time and want the smallest conceptual tour, read Sections 1, 2, and 3, then skim Section 4 for its driver skeleton; that gives you the map of the territory.

### Work Section by Section

Each section covers one coherent piece of the subject. Read one, let it settle, then start the next. If a section ends and a point still feels blurry, pause, re-read the closing paragraphs, and open the cited FreeBSD source file. A quick look at real code often clarifies in thirty seconds what prose can only dance around.

### Keep the Lab Driver Close

The `edled` driver used in the chapter lives under `examples/part-07/ch32-fdt-embedded/` in the book's repository. Each lab directory contains the state of the driver at that step, with its `Makefile`, a brief `README.md`, a matching DTS overlay where relevant, and any supporting scripts. Clone the directory, work in place, and load each version after every change. A kernel module that attaches in `dmesg` and toggles a pin you can observe is the single most concrete feedback loop in embedded work; use it.

### Open the FreeBSD Source Tree

Several sections point to real FreeBSD files. The most useful ones to keep open for this chapter are `/usr/src/sys/dev/fdt/simplebus.c` and `/usr/src/sys/dev/fdt/simplebus.h`, which define the simple FDT bus enumerator every child driver binds to; `/usr/src/sys/dev/ofw/ofw_bus.h`, `/usr/src/sys/dev/ofw/ofw_bus_subr.h`, and `/usr/src/sys/dev/ofw/ofw_bus_subr.c`, which give you the compatibility helpers and property readers; `/usr/src/sys/dev/ofw/openfirm.h`, which declares the lower-level `OF_*` primitives; `/usr/src/sys/dev/gpio/gpioled_fdt.c`, a small real driver you will imitate; `/usr/src/sys/dev/gpio/gpiobusvar.h`, which defines `gpio_pin_get_by_ofw_idx()` and its siblings; and `/usr/src/sys/dev/fdt/fdt_pinctrl.h`, which defines the pinctrl API. Open them when the chapter points you at them. The source is the authority; the book is a guide into it.

### Keep a Lab Logbook

Continue the lab logbook from earlier chapters. For this chapter, log a short note for each lab: which commands you ran, which DTB loaded, what `dmesg` said, which pin you used, and any surprises. Embedded debugging benefits more than most from a paper trail, because the variables that cause a driver not to attach (a missed overlay, a wrong phandle, a swapped pin number) are easy to forget and expensive to re-discover.

### Pace Yourself

The concepts in this chapter are often easier the second time you meet them than the first. The words *phandle*, *ranges*, and *interrupt-parent* can sit uneasily for a while before clicking. If a subsection blurs, mark it, move on, and come back to it. Most readers find Section 2 (the Device Tree format itself) is the hardest of the chapter; after it, Sections 3 and 4 feel easy because they are mostly *driver code* and the shape of driver code is already familiar.

## How to Get the Most Out of This Chapter

A few habits will help you turn the chapter's prose into durable intuition. They are the same habits that help with any new subsystem, tuned for the particulars of embedded work.

### Read Real Source

The single best way to learn to read Device Tree files and FDT-aware drivers is to read real ones. The FreeBSD tree ships several hundred. Pick a peripheral you find interesting, find its driver under `/usr/src/sys/dev/`, open it, and read it. If it has an `ofw_compat_data` table near the top and a `probe()` that calls `ofw_bus_search_compatible`, you are looking at an FDT-aware driver. Notice which properties it reads. Notice how it acquires its resources. Notice what it does and does not do on detach.

The DTS side rewards the same treatment. The FreeBSD tree keeps custom board descriptions under `/usr/src/sys/dts/`, the upstream Linux-derived device trees under `/usr/src/sys/contrib/device-tree/`, and overlays under `/usr/src/sys/dts/arm/overlays/` and `/usr/src/sys/dts/arm64/overlays/`. Open a `.dts` file that describes a board you have heard of, and read it the way you would read code: top down, noticing the hierarchy, the property names, and the commentary.

### Run What You Build

The point of the labs is that they end in something you can observe. When you load a module and nothing happens, that too is information; it usually means the driver did not match, and the chapter will teach you how to find out why. The feedback cycle is the whole point. Do not skip the loading step just because the code compiled.

### Type the Labs

The `edled` driver is small on purpose. Typing it yourself slows you down enough to notice what each line does. The finger-memory of writing FDT boilerplate is worth having. Copy-pasting deprives you of that; resist the temptation even when you are sure you could reproduce the file from memory.

### Follow the Nodes

When you read a Device Tree file or a `dmesg` boot log and you do not recognise a property, follow it. Look at the node's binding documentation in `/usr/src/sys/contrib/device-tree/Bindings/` if one exists. Search the FreeBSD source for the property name to see which drivers care about it. Embedded work is full of small conventions, and each one becomes obvious once you have seen it used in real code.

### Treat `dmesg` as Part of the Manuscript

Almost everything interesting about FDT discovery shows up in `dmesg`, not in the shell. When a driver attaches, when a node is skipped because its status is disabled, when simplebus reports a child with no matching driver, you find those messages in the kernel log and nowhere else. Keep `dmesg -a` or `tail -f /var/log/messages` in a second terminal during labs. Copy relevant lines into your logbook when they teach something non-obvious.

### Break Things on Purpose

Some of the most useful lessons in this chapter come from watching a driver *fail* to attach. A typo in a compatible string, a wrong pin number, a disabled status, a missing overlay will each produce a different flavour of silence in `dmesg`. Working through those failures in a lab environment teaches the recognition you will need when the same failure surprises you in real work. Do not only build drivers that work; break a few on purpose and look at what the kernel says.

### Work in Pairs When You Can

Embedded debugging, like security work, benefits from a second pair of eyes. If you have a study partner, one of you can read the `.dts` while the other reads the driver; you can trade perspectives and compare what you each thought the setup was doing. The conversation tends to catch the small mistakes (a swapped cell count, a misread phandle, an interrupt cell in the wrong position) that slip past a single reader.

### Trust the Iteration

You are not going to memorise every property, every flag, every helper on the first pass. That is fine. What you need on the first pass is the *shape* of the subject: the names of the primitives, the structure of a driver that uses them, the places to look when a concrete question comes up. The identifiers become reflex after you have written one or two FDT drivers; they are not a memorisation exercise.

### Take Breaks

Embedded work, like security work, is cognitively dense. It asks you to hold a description of physical hardware in your head while you read software that tries to describe and control it. Two focused hours, a proper break, and another focused hour is almost always more productive than four hours of grinding through in one sitting.

With those habits in place, let us begin with the broad question: what is embedded FreeBSD, and what does it ask of a driver author that desktop FreeBSD does not?

## Section 1: Introduction to Embedded FreeBSD Systems

The word *embedded* has been used so loosely over the years that it is worth pausing to say what we mean by it inside this book. For our purposes, an embedded system is a computer designed to do a specific job rather than to be a general-purpose machine. A Raspberry Pi running a thermostat control loop is embedded. A BeagleBone running a CNC controller is embedded. A small ARM box running a firewall appliance, a RISC-V development board running a dedicated sensor gateway, a router built around a MIPS SoC in the old days, all of these are embedded. A laptop is not embedded even when it is small. A server is not embedded even when it is stripped down. The word is about purpose and constraint, not size.

From a driver author's perspective, embedded systems share a handful of practical traits that shape the work. The hardware is usually an SoC with many on-chip peripherals rather than a motherboard with plug-in cards. The peripherals are fixed: they cannot be added or removed, because they are literally part of the silicon. There is usually no discoverable bus in the PCI sense; the peripherals sit at known physical addresses and the kernel must be told where they are. Power is constrained, sometimes severely. RAM is limited. Storage is limited. The boot flow is simple, often relying on a bootloader like U-Boot or a small EFI implementation. The user interface, if any, is minimal. The kernel boots, the drivers attach, the one application the machine exists to run comes up, and that is the life of the system.

FreeBSD runs on a growing family of embedded-friendly architectures. Most of this chapter assumes arm64 because it is the most widely used embedded target for FreeBSD today, but most of what you learn applies directly to 32-bit ARM, RISC-V, and, to a lesser extent, older MIPS and PowerPC platforms. The differences between these architectures matter for the compiler and the kernel's lowest-level code, but for driver writing the differences are mostly absent. The same FDT framework, the same `ofw_bus` helpers, the same simplebus enumerator, and the same GPIO and clock APIs work across all of them. A driver you write for a Raspberry Pi today will, with very little modification, build and run on a RISC-V SiFive HiFive Unmatched tomorrow.

### What Embedded FreeBSD Looks Like

Picture a Raspberry Pi 4 running FreeBSD 14.3. The board has a quad-core ARM Cortex-A72 CPU, 4 GB of RAM, a PCIe root complex with a gigabit Ethernet controller and a USB host controller hanging off it, an SD card slot for storage, a Broadcom VideoCore VI GPU, an HDMI output, four USB ports, a 40-pin GPIO header, and on-chip UARTs, SPI buses, I2C buses, pulse-width modulators, and timers. Most of those peripherals sit inside the BCM2711 SoC at fixed memory-mapped addresses. A few, like the USB and the Ethernet, hang off the SoC's internal PCIe controller. The SD card is driven by a host controller inside the SoC that speaks the SD protocol.

When you power the board on, a tiny firmware on the GPU core reads the SD card, finds the FreeBSD EFI bootloader, and hands control to it. The EFI loader reads `/boot/loader.conf`, loads the FreeBSD kernel, loads the Device Tree blob that describes the hardware, loads any overlay blobs that tweak the description, and jumps to the kernel. The kernel boots, attaches a simplebus to the root node of the tree, walks the tree's children, matches drivers to nodes, attaches drivers to their hardware, and the system comes up. By the time you see a login prompt, your filesystem has been mounted from the SD card by the SD host driver, your network interface has been brought up by the Ethernet driver, your USB devices are probed and available, and the ordinary userland tools (`ps`, `dmesg`, `sysctl`, `kldload`) behave exactly as they do on a laptop.

None of that last paragraph would have been true for a PC. On a PC, the firmware is BIOS or UEFI, the peripherals are on PCI, and the kernel does not need a separate blob that describes the hardware because the PCI bus itself describes its children. The arm64 world has none of those luxuries. It has Device Tree instead.

### Why Embedded Platforms Rely on Device Trees

The embedded world chose Device Trees because of the problem space. A small SoC has dozens of on-chip peripherals. Every SoC variant, every board revision, every integration choice the hardware engineer made shapes which peripherals are enabled, which pins they use, how their clocks are wired, and what their interrupt priorities are. There are thousands of distinct SoCs in the wild, and tens of thousands of distinct boards. A single kernel binary cannot afford to have every one of those variations compiled into it. Nor is there a magical bus protocol it could use to *ask* the hardware what is on the board; the hardware does not know, and most of it cannot answer.

The old solution, the one the embedded Linux world used before Device Trees, was called *board files*. Each board had a C source file compiled into the kernel, full of static structures that described the peripherals, their addresses, their interrupts, and their clocks. A kernel intended for five boards had five board files. A kernel intended for fifty boards had fifty, each with its own static description, each requiring the kernel to be rebuilt every time a board's hardware changed. The approach did not scale. Every board revision was a kernel release.

Device Trees are the approach that replaced board files. The core idea is elegant: rather than encode the hardware description in C inside the kernel, encode it in a separate textual file that lives outside the kernel, compile that file into a compact binary blob, and let the bootloader hand the blob to the kernel at boot. The kernel then reads the blob, decides which drivers to attach, and hands each driver the part of the blob that describes its hardware. A single kernel binary can now run on any board whose DTB it is given. A board revision that changes pin assignments or adds a peripheral needs a new DTB, not a new kernel.

The approach originated in the IBM and Apple PowerPC world of the 1990s, where the concept was called *Open Firmware* and the tree was part of the firmware itself. The modern Flattened Device Tree (FDT) format is a descendant, streamlined and reworked to suit bootloaders and kernels that do not carry a whole Forth interpreter the way Open Firmware did. In FreeBSD you will see both names. The framework is called `fdt(4)`, the helpers still live under `sys/dev/ofw/` because of that OpenFirmware lineage, and the functions that read properties are named `OF_getprop`, `OF_child`, `OF_hasprop` for the same reason. When you read *Open Firmware*, *OFW*, and *FDT* in FreeBSD source, they all refer to pieces of the same tradition.

### Architecture Overview: FreeBSD on ARM, RISC-V, and MIPS

FreeBSD supports several embedded-friendly architectures in FreeBSD 14.3. The two most actively used are arm64 (the 64-bit ARM architecture called AArch64 in the ARM documentation) and armv7 (32-bit ARM with hardware floating point, sometimes written `armv7` or `armhf`). RISC-V support has matured over FreeBSD 13 and 14 and now runs on real boards like the SiFive HiFive Unmatched and the VisionFive 2. MIPS support existed for a long time and powered many older routers and embedded appliances; it has been removed from the base system in recent releases, so this chapter will not dwell on it, but the skills you learn for ARM and RISC-V would transfer directly if you ever dropped back to a legacy MIPS platform.

All of those architectures use Device Tree to describe non-discoverable hardware. The boot flows differ in detail, but the shape is the same: some stage-zero firmware starts the CPU, some stage-one bootloader (U-Boot is common; on arm64 an EFI loader is increasingly standard) loads the kernel and the DTB, and the kernel takes over with the tree in hand. On arm64 the FreeBSD EFI loader handles this cleanly, and the tree can come from either a file on disk (`/boot/dtb/*.dtb`) or, on server-class hardware, from the firmware itself. On armv7 boards, U-Boot usually supplies the DTB. On RISC-V the picture is a mix of OpenSBI, U-Boot, and EFI, depending on the board.

None of these differences matter much to a driver author. The tree the kernel sees by the time your driver runs is a Device Tree, the helpers it gives you are `OF_*` and `ofw_bus_*`, and the drivers you write to run against it are portable across architectures that use the same framework.

### Typical Limitations: No ACPI, Limited Buses, Power Constraints

It is worth enumerating the limitations of embedded platforms that shape driver work, so that they do not take you by surprise.

**No ACPI, usually.** ACPI is the firmware-to-OS interface that most PCs use to describe their non-discoverable hardware. It contains tables, a bytecode language called AML, and a long specification. ARM servers sometimes use ACPI, and FreeBSD supports that path on arm64 with the ACPI subsystem, but small and mid-range embedded boards almost always use FDT. A few high-end arm64 systems may ship with *both* ACPI and FDT descriptions and let the firmware pick between them; FreeBSD can handle either, and on arm64 there is a runtime switch that decides which bus to attach. The practical consequence for most embedded drivers is that you write for FDT and not worry about ACPI. We will return to the dual-support case in Section 3.

**No PCI-style discovery for on-SoC peripherals.** A PCI or PCIe device announces itself through vendor and device IDs in a standardised configuration space. The kernel scans the bus, finds the device, and dispatches to a driver that claims the IDs. On-SoC peripherals on an ARM chip do not have that announcement mechanism. The only way the kernel knows a UART is at address `0x7E201000` on a Raspberry Pi 4 is because the DTB says so. This shifts the driver-author's mental model: you do not wait for the bus to hand you a probed device; you wait for simplebus (or the equivalent) to find your node in the tree and dispatch your probe with that node's context.

**Power constraints matter.** An embedded board may run on a battery, or from a small USB adapter, or from a power-over-Ethernet feed. A driver that leaves a clock running when the device is idle, or that keeps a regulator enabled past the peripheral's needs, hurts the entire system. FreeBSD provides clock and regulator frameworks precisely so drivers can turn things off when appropriate. We will touch on them in Sections 3, 4, and 7.

**Limited RAM and storage.** Embedded boards commonly have between 256 MB and 8 GB of RAM, and between a few hundred megabytes and a few tens of gigabytes of storage. A driver that allocates lavishly or prints a screenful of debug output to the console on every event will consume resources the system may not have. Write for the constraints you expect to face.

**A simpler boot flow, for better or worse.** The boot process on an embedded board is usually shorter and less forgiving than on a PC. If the kernel fails to find the root filesystem, you may not have a handy rescue environment. If the DTB is wrong and the right interrupt controller does not attach, the system will hang silently. This is the main reason embedded work benefits from a working debug cable, a serial console, and the habit of making small, testable changes rather than heroic ones.

### Boot Flow: From Firmware to Driver Attachment

To make the moving parts concrete, let us walk through what happens from power-on to the first driver attachment on a representative arm64 board. The details vary by board, but the shape is consistent.

1. Power-on: the first CPU core starts executing code from a boot ROM burned into the SoC. This ROM is beyond your control; its job is to load the next stage.
2. The boot ROM reads a stage-one loader from a fixed location (usually the SD card, eMMC, or SPI flash). On Raspberry Pi boards this is the VideoCore firmware; on generic arm64 boards it is usually U-Boot.
3. The stage-one loader does platform bring-up: it configures DRAM, sets up early clocks, initialises a UART for debug output, and loads the next stage. On boards with FreeBSD installed, that next stage is usually the FreeBSD EFI loader (`loader.efi`).
4. The FreeBSD EFI loader reads its configuration from the ESP partition of the boot medium, consults `/boot/loader.conf`, and loads three things: the kernel itself, a DTB from `/boot/dtb/`, and any overlays listed in the `fdt_overlays` tunable from `/boot/dtb/overlays/`.
5. The loader hands control to the kernel along with pointers to the loaded DTB.
6. The kernel starts. Its machine-dependent startup code parses the DTB to find the memory map, the CPU topology, and the root node of the tree. Based on the top-level `compatible` string of the root node, arm64 decides whether to run the FDT path or the ACPI path.
7. On the FDT path, the kernel attaches an `ofwbus` to the tree root and a `simplebus` to the `/soc` node (or the equivalent on that board). Simplebus walks its children and creates a `device_t` for each node with a valid compatible string.
8. For each of those `device_t`s, the kernel runs the usual newbus probe loop. Every driver that has registered with `DRIVER_MODULE(mydrv, simplebus, ...)` gets a chance to probe. The driver whose `probe()` returns the best match wins and has its `attach()` called. The driver then reads properties, allocates resources, and comes up.
9. The attachment process recurses for nested buses (a `simple-bus` child of `/soc`, an I2C controller with its own child devices, a GPIO bank with its own pin consumers), producing the full device tree visible in `dmesg` and `devinfo -r`.
10. By the time init runs, the devices the system needs to boot (UART, SD host, Ethernet, USB, GPIO) have all attached, and userland comes up.

That sequence is the backdrop against which every driver in this chapter operates. When you write an FDT-aware driver, you are writing the code that runs at step 8 for your particular node. The surrounding machinery runs with or without you; the part you control is the reading of your node and the allocation of your resources.

### Where to See It for Yourself

The fastest way to see a real FDT-based boot is to run FreeBSD 14.3 on a Raspberry Pi 3 or 4. Images are available from the FreeBSD project in the standard arm64 download area, and the setup is well documented. If you do not have the hardware, the second fastest way is to use QEMU's `virt` platform, which emulates a generic arm64 machine with a small set of FDT-described peripherals. The kernel and loader from a normal FreeBSD arm64 release run inside it. A sample QEMU invocation lives in the lab notes later in this chapter.

A third option, useful even on an amd64 workstation, is to read `.dts` files and the FreeBSD drivers that consume them. Open `/usr/src/sys/contrib/device-tree/src/arm/bcm2835-rpi-b.dts` or `/usr/src/sys/contrib/device-tree/src/arm64/broadcom/bcm2711-rpi-4-b.dts`. Follow the node structure top-down. Notice how the top-level node has a `compatible` property that names the board. Notice how children describe CPUs, memory, the clock controller, the interrupt controller, the peripheral bus. Then open `/usr/src/sys/arm/broadcom/bcm2835/` in the FreeBSD tree and look at the drivers that consume those nodes. You will start to see how the descriptions and the code meet.

### Wrapping Up This Section

Section 1 set the scene. Embedded FreeBSD is not an exotic niche inside the project; it is the project on platforms that do not look like PCs. The architecture the embedded work pushes you toward, thinking of hardware as a set of fixed peripherals on a SoC rather than discoverable devices on a bus, is exactly the architecture Device Tree supports. Device Tree is the bridge between the hardware description that the board designer knows and the driver code that the kernel runs. The rest of this chapter is about learning that bridge well enough to walk across it confidently.

In the next section we will slow down and read Device Tree files themselves. Before we can write a driver that consumes them, we need to know how they are structured, what their properties mean, and how a `.dts` source becomes the `.dtb` binary the kernel actually sees.

## Section 2: What Is the Device Tree?

A Device Tree is a textual description of hardware organised as a tree of nodes, with each node representing a device or a bus, and each node carrying named properties that describe its addresses, its interrupts, its clocks, its relationships to other nodes, and anything else a driver might need to know. That description is what the embedded world uses in place of PCI enumeration or ACPI tables. In the embedded FreeBSD world you will spend your life reading, writing, and reasoning about these files. The sooner they become friendly, the easier every subsequent chapter will feel.

The best way to start is to look at one.

### A Minimal Device Tree

Here is the smallest interesting Device Tree source file, in `.dts` form:

```dts
/dts-v1/;

/ {
    compatible = "acme,trivial-board";
    #address-cells = <1>;
    #size-cells = <1>;

    chosen {
        bootargs = "console=ttyS0,115200";
    };

    memory@80000000 {
        device_type = "memory";
        reg = <0x80000000 0x10000000>;
    };

    uart0: serial@10000000 {
        compatible = "ns16550a";
        reg = <0x10000000 0x100>;
        interrupts = <5>;
        clock-frequency = <24000000>;
        status = "okay";
    };
};
```

That is a complete, valid, if tiny Device Tree. It describes a fictional board called `acme,trivial-board` that has 256 MB of RAM at physical address `0x80000000`, and a 16550-compatible UART at `0x10000000` that delivers interrupt number 5 and runs at 24 MHz. Even if none of the syntax is familiar yet, the intent is legible: the file is a hardware description, written in a compact domain-specific format.

Let us unpack it piece by piece.

### The Tree Structure

The first line, `/dts-v1/;`, is a mandatory directive declaring the DTS syntax version. Every FreeBSD DTS file you will ever write or read begins with it. Anything before it is not a valid DTS file; treat it like `#!/bin/sh` at the top of a shell script.

The rest of the file is enclosed in a single block that starts with `/ {` and ends with `};`. That outer block is the **root node** of the tree. Every Device Tree has exactly one root. Its children are peripherals and sub-buses; their children are further peripherals and sub-buses; and so on.

Nodes are identified by **names**. A name like `serial@10000000` consists of a base name (`serial`) and a **unit address** (`10000000`, which is the starting address of the node's first register region written in hexadecimal without the `0x` prefix). The unit address is a convention, not a hard requirement; it exists so that nodes with the same base name can be distinguished (you can have multiple `serial` nodes at different addresses), and it doubles as a readable hint for what the node describes.

A **label**, like `uart0:` in `uart0: serial@10000000`, is a handle that lets other parts of the tree refer to this node. Labels let you write `&uart0` elsewhere in the file to mean *the node labelled `uart0`*. We will use labels in the overlay section.

Nodes contain **properties**. A property is a name followed by an equals sign, followed by a value, followed by a semicolon:

```dts
compatible = "ns16550a";
```

Some properties have no value; they exist only as flags:

```dts
interrupt-controller;
```

The value of a property can be a string (`"ns16550a"`), a list of strings (`"brcm,bcm2711", "brcm,bcm2838"`), a list of integers inside angle brackets (`<0x10000000 0x100>`), a list of references to other nodes (`<&gpio0>`), or a binary byte string (`[01 02 03]`). Most everyday properties are strings, string lists, or integer lists. The tree uses 32-bit *cells* as its fundamental integer unit; the integers inside `<...>` are cells, and 64-bit values are expressed as two consecutive cells (high, low).

Let us go back to the minimal example and read each property with fresh eyes.

### Reading the Minimal Example

The root node has three properties:

```dts
compatible = "acme,trivial-board";
#address-cells = <1>;
#size-cells = <1>;
```

The **`compatible`** property is how the root (and any node) tells the world what it is. It is the single most important property in Device Tree. A driver matches on it. The value is a vendor-prefixed string (`"acme,trivial-board"`) or, more commonly, a list of them in decreasing order of specificity. A Raspberry Pi 4 root node, for example, might be `compatible = "raspberrypi,4-model-b", "brcm,bcm2711";` The first string says "exactly this board"; the second says "in the general family of boards using the BCM2711 chip". Drivers that know about the specific board can match the first; drivers that only know about the chip can match the second. The DTS spec calls this a *compatibility list*, and both FreeBSD and Linux respect it.

The **`#address-cells`** and **`#size-cells`** properties at the root describe how many 32-bit cells a child node uses for its address and size in `reg` properties. At the root of a 32-bit board, both are typically 1. On a 64-bit board with more than 4 GB of addressable memory, they would both be 2. When you see `reg = <0x80000000 0x10000000>;` under the memory node, you know from the parent's cell counts that this is one address cell and one size cell, which means the region is at `0x80000000` with size `0x10000000`. If `#address-cells` were 2, the region would instead be written `reg = <0x0 0x80000000 0x0 0x10000000>;`.

The **`chosen`** node is a special sibling of the root's hardware children. It carries parameters the bootloader wants to pass to the kernel: the boot arguments, the console device, sometimes the initrd location. FreeBSD reads `/chosen/bootargs` and uses it to populate the kernel environment.

The **`memory@80000000`** node describes a region of physical memory. Memory nodes carry `device_type = "memory"` and a `reg` property giving their range. FreeBSD's early boot reads these to build its physical memory map.

The **`serial@10000000`** node is the interesting one. Its `compatible = "ns16550a"` tells the kernel *this is an ns16550a UART*, which is a very common PC-style serial port chip. Its `reg = <0x10000000 0x100>` says *my registers live at physical address `0x10000000` and occupy `0x100` bytes*. Its `interrupts = <5>` says *I deliver interrupt number 5 to my interrupt parent*. Its `clock-frequency = <24000000>` says *my reference clock runs at 24 MHz, so divisors should be computed from this*. Its `status = "okay"` says *I am enabled*; had it said `"disabled"`, the driver would skip this node.

That is essentially what a Device Tree does: it describes each peripheral in a few properties whose meanings are defined by conventions called **bindings**. A binding for a UART tells you what properties a UART node should carry. A binding for an I2C controller tells you what properties an I2C controller node should carry. And so on. The bindings are documented separately, and the FreeBSD tree ships a large library of them under `/usr/src/sys/contrib/device-tree/Bindings/`.

### Source vs Binary: .dts, .dtsi, and .dtb

Device Tree files come in three file types that are easy to confuse at first.

**`.dts`** is the main source form. A `.dts` file describes a whole board or platform and is what you feed to the compiler.

**`.dtsi`** is an include fragment, the `i` standing for *include*. A typical SoC family has a large `.dtsi` describing the SoC itself (its interrupt controller, its clock tree, its on-chip peripherals), and each board that uses that SoC has a small `.dts` that `#include`s the `.dtsi` and then describes the board-specific additions (external devices soldered to the board, pin configuration, chosen node). You will see many `.dtsi` files under `/usr/src/sys/contrib/device-tree/src/arm/` and `/usr/src/sys/contrib/device-tree/src/arm64/`.

**`.dtb`** is the compiled binary form. The kernel and bootloader deal with `.dtb` files, not `.dts` files. A `.dtb` is the output of the `dtc` compiler fed a `.dts` source. It is compact, has no whitespace or comments, and is designed to be parsed by a bootloader in a few kilobytes of code. A `.dtb` file for a Raspberry Pi 4 is typically around 30 KB.

And a fourth, less common type:

**`.dtbo`** is a compiled *overlay*. Overlays are fragments that modify an existing base `.dtb` at load time: they can enable or disable nodes, add new nodes, or change properties. They are the mechanism FreeBSD and many Linux distributions use to let users customise a stock DTB without rebuilding it. `.dtbo` files are compiled from `.dtso` (device-tree-source-overlay) files, and loaded by the bootloader via the `fdt_overlays` tunable. We will meet them in Section 5.

When you work with DTS you are almost always writing either a `.dts` or a `.dtso`. You compile either with the device tree compiler, `dtc`, which in FreeBSD lives in the `devel/dtc` port and is installed as `/usr/local/bin/dtc`. The FreeBSD kernel build system invokes `dtc` through the scripts under `/usr/src/sys/tools/fdt/`, specifically `make_dtb.sh` and `make_dtbo.sh`.

### Nodes, Properties, Addresses, and the Phandle

A few additional concepts recur so often that it is worth naming them once and for all.

**Nodes** are the hierarchical units. Every node has a name, optionally a label, possibly a unit address, and zero or more properties. Nodes nest; a node's children are described inside its enclosing braces.

**Properties** are key-value pairs. The keys are strings. The values are typed by convention: `compatible` is a string list, `reg` is an integer list, `status` is a string, `interrupts` is an integer list whose length depends on the interrupt parent, and so on.

**Unit addresses** in node names (`serial@10000000`) mirror the first cell of the `reg` property. The DTC compiler warns if they disagree. You should write both consistently.

The **`reg`** property describes the memory-mapped register regions of the device. Its format is `<address size address size ...>`, with each address-size pair being a contiguous region. Most simple peripherals have exactly one region. Some have several (a peripheral with a main register area and a separate interrupt register block, for example).

**Address cells and size cells** are the property pair `#address-cells` and `#size-cells` that live on a parent node and describe how `reg` is formatted in its children. A SoC bus with `#address-cells = <1>; #size-cells = <1>;` lets its children use one cell each for address and size. An I2C bus usually has `#address-cells = <1>; #size-cells = <0>;` because an I2C child has an address but no size.

**Interrupts** are described by one or more properties depending on the style used. The older style is `interrupts = <...>;` with the cells interpreted by the interrupt parent's convention. On ARM GIC-based platforms this is a three-cell form: interrupt type, interrupt number, interrupt flags. The newer style, mixed with the old throughout the kernel, is `interrupts-extended = <&gic 0 15 4>;` which explicitly names the interrupt parent. Either way, the cells tell the kernel which hardware interrupt the device raises and under what conditions.

A **phandle** is a unique integer assigned by the compiler to each node. Phandles let other nodes refer to this one. When you write `<&gpio0>`, the compiler substitutes the phandle of the node labelled `gpio0`. When you write `<&gpio0 17 0>`, you are passing three cells: the phandle of `gpio0`, the pin number `17`, and a flags cell `0`. The meaning of the cells after the phandle is defined by the *provider*'s binding. This is the pattern by which GPIO consumers, clock consumers, reset consumers, and interrupt consumers talk to their providers: the first cell names the provider, the following cells say which resource and how.

The **`status`** property is a small but critical one. A node with `status = "okay";` is enabled, and drivers will probe it. A node with `status = "disabled";` is skipped. Overlays often toggle this property to turn a peripheral on or off without removing the node. FreeBSD's `ofw_bus_status_okay()` is the helper that returns true when a node's status is okay.

The **`label`** and **`alias`** mechanisms let you refer to a node by a short name rather than a path. A label like `uart0:` is a file-local handle; an alias defined under the special `/aliases` node (`serial0 = &uart0;`) is a kernel-visible name. FreeBSD makes use of aliases for some devices like the console.

That is most of what you need to read a typical Device Tree. A few more exotic pieces appear in specific bindings (for example, `clock-names` and `clocks` for consumers of the clock framework, `reset-names` and `resets` for hwreset consumers, `dma-names` and `dmas` for DMA engine consumers, `pinctrl-0`, `pinctrl-names` for pin control), but they all follow the same *named index list* pattern.

### A More Realistic Example

To give you a feel for a real SoC-level fragment, here is an abbreviated node from a BCM2711 (Raspberry Pi 4) description. The full file lives under `/usr/src/sys/contrib/device-tree/src/arm/bcm2711.dtsi`.

```dts
soc {
    compatible = "simple-bus";
    #address-cells = <1>;
    #size-cells = <1>;
    ranges = <0x7e000000 0x0 0xfe000000 0x01800000>,
             <0x7c000000 0x0 0xfc000000 0x02000000>,
             <0x40000000 0x0 0xff800000 0x00800000>;
    dma-ranges = <0xc0000000 0x0 0x00000000 0x40000000>;

    gpio: gpio@7e200000 {
        compatible = "brcm,bcm2711-gpio", "brcm,bcm2835-gpio";
        reg = <0x7e200000 0xb4>;
        interrupts = <GIC_SPI 113 IRQ_TYPE_LEVEL_HIGH>,
                     <GIC_SPI 114 IRQ_TYPE_LEVEL_HIGH>;
        gpio-controller;
        #gpio-cells = <2>;
        interrupt-controller;
        #interrupt-cells = <2>;
    };

    spi0: spi@7e204000 {
        compatible = "brcm,bcm2835-spi";
        reg = <0x7e204000 0x200>;
        interrupts = <GIC_SPI 118 IRQ_TYPE_LEVEL_HIGH>;
        clocks = <&clocks BCM2835_CLOCK_VPU>;
        #address-cells = <1>;
        #size-cells = <0>;
        status = "disabled";
    };
};
```

Reading this top-down:

- The `soc` node is the on-chip-peripherals bus. Its `compatible = "simple-bus"` is the magic token that tells FreeBSD to attach the simplebus driver here.
- Its `ranges` property defines the address translation from bus addresses (the "local" addresses the CPU's peripheral interconnect uses internally, starting at `0x7E000000`) to CPU physical addresses (starting at `0xFE000000`). FreeBSD reads this and applies it when mapping child `reg` properties.
- The `gpio` node is the GPIO controller. It claims two interrupts, declares itself a gpio-controller (so other nodes can reference it), and uses two cells per GPIO reference (the first cell is the pin number, the second is a flag word).
- The `spi0` node is the SPI controller at bus address `0x7E204000`. It is `status = "disabled"` in the base description, meaning it does not attach until an overlay enables it.

Every embedded board description looks roughly like this: a tree of on-chip peripherals, each with a compatible string identifying what driver to bind, a `reg` for its memory-mapped registers, an `interrupts` for its IRQ line, and possibly references to clocks, resets, regulators, and pins.

### How the DTB Is Loaded

For completeness, it helps to know how the DTB goes from disk to kernel at boot.

On an arm64 board running FreeBSD 14.3, the typical flow is:

1. The EFI firmware or bootloader reads the FreeBSD EFI loader from the ESP.
2. The FreeBSD loader loads the kernel from `/boot/kernel/kernel` and the DTB from `/boot/dtb/<board>.dtb`. The DTB's filename is selected based on the SoC family.
3. If `/boot/loader.conf` sets `fdt_overlays="overlay1,overlay2"`, the loader reads `/boot/dtb/overlays/overlay1.dtbo` and `/boot/dtb/overlays/overlay2.dtbo`, applies them to the base DTB in memory, and hands the merged result to the kernel.
4. The kernel takes the merged DTB as its authoritative hardware description.

On U-Boot-driven boards (common for armv7) the flow is similar but the loader is U-Boot itself. U-Boot's environment variables `fdt_file` and `fdt_addr` tell it which DTB to load and where to place it. When U-Boot finally does `bootefi` or `booti`, it passes the DTB to the FreeBSD loader or directly to the kernel.

On EFI systems that ship FDT in their firmware (rare for small boards, common for ARM servers that use the Server Base System Architecture), the firmware stores the DTB as an EFI configuration table and the kernel reads it from there.

For a driver author, the details of boot do not matter most of the time. What matters is that by the time your driver's probe is called, the tree has been loaded, parsed, and presented to the kernel; you read it with the same `OF_*` helpers no matter how it got there.

### Wrapping Up This Section

Section 2 introduced Device Tree as a hardware-description language. You have seen a minimal example, you have met the core concepts of nodes and properties, you know the difference between `.dts`, `.dtsi`, `.dtb`, and `.dtbo` files, and you have walked through a realistic fragment of a SoC description. You also know how a DTB reaches the kernel at boot.

What you do *not* yet know is how FreeBSD's kernel consumes the tree once it is loaded: which subsystem attaches to it, which helpers drivers call to read properties, and how a driver's `probe()` and `attach()` find their node. That is Section 3.

## Section 3: FreeBSD's Device Tree Support

FreeBSD handles Device Tree through a framework whose design predates FDT itself. The framework is named after Open Firmware, abbreviated **OFW** throughout the source tree, because the API it grew out of originally served PowerPC Macs and IBM systems that used the real Open Firmware specification. When the ARM world standardised on Flattened Device Tree in the late 2000s, FreeBSD mapped FDT onto the same internal API. A driver in FreeBSD 14.3 therefore calls the same `OF_getprop()` whether it is running on a PowerPC Mac, an ARM board with an FDT blob, or a RISC-V board with an FDT blob. The implementation beneath differs; the interface above is uniform.

This section introduces the pieces of that framework you need to know: the `fdt(4)` interface as it is used in practice, the `ofw_bus` helpers that drivers call on top of the raw `OF_*` primitives, the `simplebus(4)` bus driver that enumerates children, and the property-reading idioms you will use constantly. By the end of the section you will know what code already exists on the FreeBSD side and where it lives; Section 4 will then build a driver on top of it.

### The fdt(4) Framework Overview

`fdt(4)` is the kernel's Flattened Device Tree support. It provides the code that parses the binary `.dtb`, walks it to find nodes, extracts properties, applies overlays, and presents the result through the `OF_*` API. You can think of `fdt(4)` as the lower half and `ofw_bus` as the upper half, with `OF_*` functions spanning both.

The code that implements the FDT side of the OFW interface lives in `/usr/src/sys/dev/ofw/ofw_fdt.c`. It is a specific instance of the `ofw_if.m` kobj interface. When the kernel calls `OF_getprop()`, the call goes through the interface and ends up in the FDT implementation, which walks the flattened blob. On a PowerPC Mac it would end up in the real Open Firmware implementation instead; the drivers above do not need to know or care.

For your purposes as a driver author, you almost never touch `ofw_fdt.c` directly. You use the helpers one layer up.

### OF_*: The Raw Property Readers

The lowest-level API that drivers call is the `OF_*` family of functions declared in `/usr/src/sys/dev/ofw/openfirm.h`. The ones you will use most are a small set.

`OF_getprop(phandle_t node, const char *prop, void *buf, size_t len)` reads a property's raw bytes into a caller-supplied buffer. It returns the number of bytes read on success, or `-1` on failure. The buffer must be large enough for the expected length.

`OF_getencprop(phandle_t node, const char *prop, pcell_t *buf, size_t len)` reads a property whose cells are big-endian and converts them to host byte order as it copies. Almost any property that contains integers should be read with this variant rather than `OF_getprop()`.

`OF_getprop_alloc(phandle_t node, const char *prop, void **buf)` reads a property of unknown length. The kernel allocates a buffer and returns the pointer through the third argument. When you are done, call `OF_prop_free(buf)` to release it.

`OF_hasprop(phandle_t node, const char *prop)` returns non-zero if the named property exists, zero otherwise. Useful for optional properties where the mere presence is meaningful.

`OF_child(phandle_t node)` returns the first child of a node. `OF_peer(phandle_t node)` returns the next sibling. `OF_parent(phandle_t node)` returns the parent. Combined, they let you walk the tree.

`OF_finddevice(const char *path)` returns a phandle for the node at a given path, such as `"/chosen"` or `"/soc/gpio@7e200000"`. Most drivers do not need this because the framework hands them their node already.

`OF_decode_addr(phandle_t dev, int regno, bus_space_tag_t *tag, bus_space_handle_t *hp, bus_size_t *sz)` is a convenience routine used by very early code (serial console drivers, mostly) to set up a bus-space mapping for register `regno` of a given node without going through newbus. Normal drivers use `bus_alloc_resource_any()` instead, which reads the `reg` property via the resource list set up during probe.

These primitives are the foundation. In practice you will call them indirectly through the slightly more convenient `ofw_bus_*` helpers, but the ones above are what those helpers use internally, and it is worth recognising them when you read real driver code.

### ofw_bus: The Compatibility Helpers

The most common thing an FDT-aware driver does is ask: *is this device compatible with something I know how to drive?* FreeBSD provides a small layer of helpers on top of `OF_getprop` to make those checks idiomatic. They live in `/usr/src/sys/dev/ofw/ofw_bus.h` and `/usr/src/sys/dev/ofw/ofw_bus_subr.h`, with their implementations in `/usr/src/sys/dev/ofw/ofw_bus_subr.c`.

The helpers worth knowing, in the order you will meet them:

`ofw_bus_get_node(device_t dev)` returns the phandle associated with a `device_t`. It is implemented as an inline that calls the parent bus's `OFW_BUS_GET_NODE` method. For a child of simplebus, this returns the phandle of the DTS node that gave rise to this device.

`ofw_bus_status_okay(device_t dev)` returns 1 if the node's `status` property is absent, empty, or `"okay"`; 0 otherwise. Every FDT-aware probe should call it at the top to skip disabled nodes.

`ofw_bus_is_compatible(device_t dev, const char *string)` returns 1 if any entry of the node's `compatible` property exactly equals `string`. Short, precise, and the usual tool when a driver only wants one compatible string.

`ofw_bus_search_compatible(device_t dev, const struct ofw_compat_data *table)` walks a driver-provided table and returns the matching entry if any of its compatible strings is in the node's `compatible` list. This is the standard way a driver that supports multiple chips registers its compatibility. The table is an array of entries, each containing a string and a `uintptr_t` cookie the driver can use to remember which chip matched; the table ends with a sentinel entry whose string is `NULL`. We will see the full pattern in Section 4.

`ofw_bus_has_prop(device_t dev, const char *prop)` is a convenience wrapper over `OF_hasprop(ofw_bus_get_node(dev), prop)`.

`ofw_bus_get_name(device_t dev)`, `ofw_bus_get_compat(device_t dev)`, `ofw_bus_get_type(device_t dev)`, and `ofw_bus_get_model(device_t dev)` return the corresponding strings from the node, or `NULL` if absent.

Those are the bread-and-butter helpers. You will see them at the top of almost every FDT-aware driver's probe and attach routines.

### simplebus: The Default Enumerator

The simplebus driver is the piece that makes all of this work in practice. It lives in `/usr/src/sys/dev/fdt/simplebus.c` and its header is at `/usr/src/sys/dev/fdt/simplebus.h`. Simplebus has two jobs.

Its first job is to enumerate children. When the kernel attaches simplebus to a node whose `compatible` includes `"simple-bus"` (or whose `device_type` is `"soc"`, for historical reasons), simplebus walks the node's children, creates a `device_t` for each child that has a `compatible` property, and feeds them into newbus. This is what makes your driver's probe get called at all; simplebus is the bus your driver registers with via `DRIVER_MODULE(mydrv, simplebus, ...)`.

Its second job is to translate child addresses into CPU physical addresses. The parent node's `ranges` property encodes how the bus-local addresses that appear in children's `reg` properties map to CPU physical addresses. Simplebus reads `ranges` in `simplebus_fill_ranges()` and applies it when setting up each child's resource list, so by the time your driver asks for its memory resource, the region is already in CPU physical space.

The central probe code that decides whether simplebus should attach to a given node sits near the top of `/usr/src/sys/dev/fdt/simplebus.c`. Here it is, stripped of comments for brevity:

```c
if (!ofw_bus_status_okay(dev))
    return (ENXIO);

if (ofw_bus_is_compatible(dev, "syscon") ||
    ofw_bus_is_compatible(dev, "simple-mfd"))
    return (ENXIO);

if (!(ofw_bus_is_compatible(dev, "simple-bus") &&
      ofw_bus_has_prop(dev, "ranges")) &&
    (ofw_bus_get_type(dev) == NULL ||
     strcmp(ofw_bus_get_type(dev), "soc") != 0))
    return (ENXIO);

device_set_desc(dev, "Flattened device tree simple bus");
return (BUS_PROBE_GENERIC);
```

That snippet is a compact exemplar of the entire probe style. Test `status`, reject the known exceptions, confirm the node looks like a simple bus, describe the device, and return a probe confidence. Every FDT-aware probe in the tree follows some variation of this shape.

Simplebus registers itself with two parent buses. On the primary ofw root, it registers via `EARLY_DRIVER_MODULE(simplebus, ofwbus, ...)`; and recursively, on itself, via `EARLY_DRIVER_MODULE(simplebus, simplebus, ...)`. The recursion is how nested simple-bus nodes get enumerated: a simplebus parent that encounters a child whose compatible is `"simple-bus"` attaches another simplebus instance to it, which then enumerates *its* children.

For most driver work you do not need to know more about simplebus than that it exists and that you register with it. Your driver's module registration will read `DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);` and everything downstream will happen automatically.

### Mapping the Pieces to a Probe Call

To put the moving parts together, let us trace what happens from the DTB being loaded to your driver's probe being called.

1. The loader hands the kernel a DTB.
2. The kernel's early arm64 code parses the DTB to find memory and CPU information.
3. The `ofwbus0` pseudo-device attaches to the tree root.
4. `ofwbus0` creates a `device_t` for the `/soc` node (or whichever node has `compatible = "simple-bus"`) and dispatches the usual newbus probe loop.
5. The simplebus driver's probe runs, returns `BUS_PROBE_GENERIC`, and is picked.
6. Simplebus's attach walks the children of the `/soc` node and creates a `device_t` for each child. Each child's resource list is populated from its `reg` and `interrupts` properties, translated through the parent's `ranges`.
7. For each child, the newbus probe loop runs. Every driver registered to simplebus gets a chance to probe.
8. Your driver's probe is called. It calls `ofw_bus_status_okay()`, `ofw_bus_search_compatible()`, and (if it matches) returns `BUS_PROBE_DEFAULT`.
9. If your driver wins the probe contest for this node, its attach is called. At that point the `device_t` has a resource list already populated with the node's memory and interrupt information.
10. Your driver calls `bus_alloc_resource_any()` for its memory region and for its interrupt, sets up an interrupt handler if it has one, maps the memory, initialises the hardware, and returns 0 to indicate success.

From the driver author's perspective the first six steps are machinery; steps 7 through 10 are the code you write. The chapter will now zoom in on those four steps.

### Registering a Driver with ofw_bus

When your driver registers with simplebus, it is implicitly opting into the OFW dispatch. The module registration line is:

```c
DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);
```

That tells newbus: *attach the driver described by `mydrv_driver` as a child of simplebus*. Your `device_method_t` array must supply at minimum a `device_probe` and a `device_attach` method. If you have a detach, add `device_detach`. If your driver also implements OFW interface methods (rarely needed at the leaf level), add those too.

On some platforms, and for drivers that want to attach both to simplebus and to the `ofwbus` root (in case there is no simplebus between them and the root), it is common to add a second registration:

```c
DRIVER_MODULE(mydrv, ofwbus, mydrv_driver, 0, 0);
```

This is what `gpioled_fdt.c` does, for example. It covers platforms where the `gpio-leds` node sits directly under the root rather than under a `simple-bus`.

### Writing a Compatibility Table

A driver that supports more than one variant of a chip typically declares a table of compatible strings:

```c
static const struct ofw_compat_data compat_data[] = {
    { "brcm,bcm2711-gpio",   1 },
    { "brcm,bcm2835-gpio",   2 },
    { NULL,                  0 }
};
```

Then in probe:

```c
static int
mydrv_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "My FDT-Aware Driver");
    return (BUS_PROBE_DEFAULT);
}
```

And in attach, the matched entry is available for re-lookup:

```c
static int
mydrv_attach(device_t dev)
{
    const struct ofw_compat_data *match;
    ...
    match = ofw_bus_search_compatible(dev, compat_data);
    if (match == NULL || match->ocd_str == NULL)
        return (ENXIO);

    sc->variant = match->ocd_data; /* 1 for BCM2711, 2 for BCM2835 */
    ...
}
```

The `ocd_data` field is the cookie you defined in the table. It is a plain `uintptr_t`, so you can use it as an integer discriminator, a pointer to a per-variant structure, or whatever fits your driver's needs.

### Reading Properties

Once you have a `device_t`, reading its node's properties is straightforward. A typical pattern:

```c
phandle_t node = ofw_bus_get_node(dev);
uint32_t val;

if (OF_getencprop(node, "clock-frequency", &val, sizeof(val)) <= 0) {
    device_printf(dev, "missing clock-frequency\n");
    return (ENXIO);
}
```

The helpers are `OF_getencprop` for integer properties (with endianness handled), `OF_getprop` for raw buffers, `OF_getprop_alloc` for strings of unknown length. For boolean properties (ones whose presence is the signal and whose value is empty), `OF_hasprop` is the idiom:

```c
bool want_rts = OF_hasprop(node, "uart-has-rtscts");
```

For list properties, `OF_getprop_alloc` or the fixed-buffer variants let you pull the full list and iterate over it.

### Acquiring Resources

Memory and interrupt resources come through the standard `bus_alloc_resource_any()` calls you already know:

```c
sc->mem_rid = 0;
sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
    &sc->mem_rid, RF_ACTIVE);
if (sc->mem_res == NULL) {
    device_printf(dev, "cannot allocate memory\n");
    return (ENXIO);
}

sc->irq_rid = 0;
sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
    &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
```

This is possible because simplebus has already read your node's `reg` and `interrupts` properties, translated them through the parent's `ranges`, and stored them in the resource list. The indices `0`, `1`, `2` refer to the first, second, and third entries of the respective list. A device with multiple `reg` regions will have multiple memory resources at rids `0`, `1`, `2`, and so on.

For anything beyond plain memory and interrupts, you reach into the peripheral frameworks.

### Peripheral Frameworks: Clock, Regulator, Reset, Pinctrl, GPIO

Embedded peripherals usually need more than just a memory region. They need a clock turned on, a regulator enabled, a reset line de-asserted, perhaps pins multiplexed, and sometimes a GPIO to drive a chip-select or an enable. FreeBSD provides a coherent set of frameworks for each.

**Clock framework.** Declared in `/usr/src/sys/dev/extres/clk/clk.h`. A consumer calls `clk_get_by_ofw_index(dev, node, idx, &clk)` to acquire a handle to the N-th clock listed in the node's `clocks` property, or `clk_get_by_ofw_name(dev, node, "fck", &clk)` to acquire the clock whose `clock-names` entry is `"fck"`. With a handle in hand, the consumer calls `clk_enable(clk)` to turn it on, `clk_get_freq(clk, &freq)` to ask its rate, and `clk_disable(clk)` to turn it off on shutdown.

**Regulator framework.** Declared in `/usr/src/sys/dev/extres/regulator/regulator.h`. `regulator_get_by_ofw_property(dev, node, "vdd-supply", &reg)` fetches a regulator by the named property; `regulator_enable(reg)` enables it; `regulator_disable(reg)` disables it.

**Hardware-reset framework.** Declared in `/usr/src/sys/dev/extres/hwreset/hwreset.h`. `hwreset_get_by_ofw_name(dev, node, "main", &rst)` fetches a reset line; `hwreset_deassert(rst)` brings the peripheral out of reset; `hwreset_assert(rst)` puts it back.

**Pin control framework.** Declared in `/usr/src/sys/dev/fdt/fdt_pinctrl.h`. `fdt_pinctrl_configure_by_name(dev, "default")` applies the pin configuration associated with the `pinctrl-names = "default"` slot of the node. Most drivers that need pin control simply call this once from attach.

**GPIO framework.** The consumer side is declared in `/usr/src/sys/dev/gpio/gpiobusvar.h`. `gpio_pin_get_by_ofw_idx(dev, node, idx, &pin)` fetches the N-th GPIO listed in the node's `gpios` property. `gpio_pin_setflags(pin, GPIO_PIN_OUTPUT)` sets its direction. `gpio_pin_set_active(pin, value)` drives its level. `gpio_pin_release(pin)` gives it back.

These frameworks are the reason embedded drivers in FreeBSD are often shorter than their Linux counterparts. You do not have to write the clock tree, the regulator logic, or the GPIO controller: you consume them through a uniform consumer API, and the provider drivers are someone else's problem. The worked example in Section 7 uses the GPIO consumer API end to end.

### Interrupt Routing: A Quick Look at interrupt-parent

Interrupts on FDT platforms use a chained lookup scheme. A node's `interrupts` property gives the raw interrupt specifier, and the node's `interrupt-parent` property (or the nearest ancestor's) names the controller that should interpret it. That controller, in turn, may be a child of another controller (a secondary GIC redistributor, a nested PLIC, an I/O APIC-like bridge on some SoCs), which further routes the interrupt upward until it reaches a top-level controller bound to a real CPU vector.

For a driver author, you do not usually have to think about this chain. The kernel's interrupt resource already lives in your resource list as a cookie that the interrupt controller knows how to interpret, and `bus_setup_intr()` hands the cookie back to the controller when you ask for an IRQ. What matters is that your node has the right `interrupts = <...>;` for its immediate interrupt parent, and that the tree's `interrupt-parent` or `interrupts-extended` chain reaches a real controller. When it does not, your interrupt will be silently dropped at boot.

The internals live in `/usr/src/sys/dev/ofw/ofw_bus_subr.c`, with `ofw_bus_lookup_imap()`, `ofw_bus_setup_iinfo()`, and related helpers. You will probably never call them directly unless you are writing a bus driver.

### A Brief Look at Overlays

We have mentioned overlays several times. The short version is that an overlay is a small DTB fragment that references nodes in the base tree by label (for example, `&i2c0` or `&gpio0`) and adds or modifies properties or children. The loader merges overlays into the base blob before the kernel sees it. We will return to overlays in Section 5 and use them in the worked example in Section 7; for now just note that FreeBSD supports them through the `fdt_overlays` loader tunable and the files under `/boot/dtb/overlays/`.

### ACPI vs FDT on arm64

The arm64 port of FreeBSD supports both FDT and ACPI as discovery mechanisms. Which path the kernel takes is decided during early boot by looking at what the firmware provided. If a DTB was supplied and the top-level `compatible` does not suggest an ACPI path, the kernel attaches the FDT bus. If an ACPI RSDP was supplied and the firmware indicates SBSA compliance, the kernel attaches the ACPI bus. The relevant code is in `/usr/src/sys/arm64/arm64/nexus.c`, which handles both paths; the variable `arm64_bus_method` records which one was chosen.

The practical consequence for driver authors is that a driver written for both mechanisms has to attach to both buses. Drivers that only care about FDT (the majority of small embedded drivers) register with simplebus only. Drivers that serve generic hardware that can show up on either ARM servers (ACPI) or ARM embedded boards (FDT) register with both. The `ahci_generic` driver at `/usr/src/sys/dev/ahci/ahci_generic.c` is one such dual-supporting driver; its source is worth reading when you eventually need to write one. For most of this chapter we will stay on the pure FDT side.

### Wrapping Up This Section

Section 3 gave you the map of FreeBSD's FDT support. You now know where the core code lives, which helpers to use for which job, and how the pieces connect: `fdt(4)` parses the tree, `OF_*` primitives read properties, `ofw_bus_*` helpers wrap the primitives into idiomatic checks, simplebus enumerates the children, and the peripheral frameworks hand you clocks, resets, regulators, pins, and GPIOs.

In the next section we will take those pieces and write a real driver with them. Section 4 walks through the full skeleton of an FDT-aware driver from top to bottom, in enough detail that you could copy the structure into your own project and start filling in the hardware-specific logic.

## Section 4: Writing a Driver for an FDT-Based System

This section walks through the complete shape of an FDT-aware FreeBSD driver. The shape is simple; the reason to lay it out in full is that once you have seen it, every FDT driver in the tree becomes legible. You will start noticing the same patterns in hundreds of files under `/usr/src/sys/dev/` and `/usr/src/sys/arm/`, and each of those drivers becomes one more template you can adapt.

We will build the skeleton in six passes. First the header includes. Then the softc. Then the compatibility table. Then `probe()`. Then `attach()`. Then `detach()` and the module registration. Each pass is short; by the end you will have a complete, compilable minimal driver that prints a message when the kernel matches it to a Device Tree node.

### The Header Includes

FDT drivers lean on a handful of headers from the `ofw` and `fdt` directories on top of the usual kernel and bus headers. A typical set:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/resource.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
```

Nothing exotic. The three `ofw` headers bring in the property readers and the compatibility helpers. If your driver is a consumer of GPIOs, clocks, regulators, or hwreset, add their headers as well:

```c
#include <dev/gpio/gpiobusvar.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/extres/hwreset/hwreset.h>
```

Pinctrl:

```c
#include <dev/fdt/fdt_pinctrl.h>
```

And `simplebus.h` if your driver actually extends simplebus rather than just binding to it as a leaf:

```c
#include <dev/fdt/simplebus.h>
```

Most leaf drivers do not need the simplebus header. Bring it in only when you are implementing a bus-like driver that enumerates children.

### The Softc

A softc for an FDT-aware driver is a plain softc with a few additional fields to track resources and references you acquired through the OFW helpers:

```c
struct mydrv_softc {
    device_t        dev;
    struct resource *mem_res;   /* memory region (bus_alloc_resource) */
    int             mem_rid;
    struct resource *irq_res;   /* interrupt resource, if any */
    int             irq_rid;
    void            *irq_cookie;

    /* FDT-specific state. */
    phandle_t       node;
    uintptr_t       variant;    /* matched ocd_data */

    /* Example: acquired GPIO pin for driving a chip-select. */
    gpio_pin_t      cs_pin;

    /* Example: acquired clock handle. */
    clk_t           clk;

    /* Usual driver state: mutex, buffers, etc. */
    struct mtx      sc_mtx;
};
```

The fields that differ from a PCI or ISA driver are `node`, `variant`, and the consumer handles (`cs_pin`, `clk`, and others). Everything else is standard.

### The Compatibility Table

The compatibility table is the driver's claim on a set of Device Tree nodes. It is conventionally declared file-scope and immutable:

```c
static const struct ofw_compat_data mydrv_compat_data[] = {
    { "acme,trivial-timer",    1 },
    { "acme,fancy-timer",      2 },
    { NULL,                    0 }
};
```

The second field, `ocd_data`, is a `uintptr_t` cookie. I like to use it as an integer discriminator (1 for the basic variant, 2 for the fancy variant); you can also use it as a pointer to a per-variant configuration structure. The table ends with a sentinel entry whose first field is `NULL`.

### The Probe Routine

A canonical probe for an FDT-aware driver:

```c
static int
mydrv_probe(device_t dev)
{

    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, mydrv_compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "ACME Trivial Timer");
    return (BUS_PROBE_DEFAULT);
}
```

Three lines of logic. First, bail out if the node is disabled. Second, bail out if none of our compatible strings match. Third, set a descriptive name and return `BUS_PROBE_DEFAULT`. The exact return value matters when more than one driver might claim the same node; a more specialised driver can return `BUS_PROBE_SPECIFIC` to outrank a generic one, and a generic fallback can return `BUS_PROBE_GENERIC` to let anything better win. For most drivers, `BUS_PROBE_DEFAULT` is correct.

The `ofw_bus_search_compatible(dev, compat_data)` call returns a pointer to the matching entry, or to the sentinel entry if no match was found. The sentinel's `ocd_str` is `NULL`, so testing for `NULL` is the idiomatic way to say *we did not match anything*. Some drivers alternatively save the returned pointer into a local variable and re-use it; we will do that in attach.

### The Attach Routine

Attach is where the real work happens. A canonical FDT attach:

```c
static int
mydrv_attach(device_t dev)
{
    struct mydrv_softc *sc;
    const struct ofw_compat_data *match;
    phandle_t node;
    uint32_t freq;
    int err;

    sc = device_get_softc(dev);
    sc->dev = dev;
    sc->node = ofw_bus_get_node(dev);
    node = sc->node;

    /* Remember which variant we matched. */
    match = ofw_bus_search_compatible(dev, mydrv_compat_data);
    if (match == NULL || match->ocd_str == NULL)
        return (ENXIO);
    sc->variant = match->ocd_data;

    /* Pull any required properties. */
    if (OF_getencprop(node, "clock-frequency", &freq, sizeof(freq)) <= 0) {
        device_printf(dev, "missing clock-frequency property\n");
        return (ENXIO);
    }

    /* Allocate memory and interrupt resources. */
    sc->mem_rid = 0;
    sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
        &sc->mem_rid, RF_ACTIVE);
    if (sc->mem_res == NULL) {
        device_printf(dev, "cannot allocate memory resource\n");
        err = ENXIO;
        goto fail;
    }

    sc->irq_rid = 0;
    sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->irq_rid, RF_ACTIVE | RF_SHAREABLE);
    if (sc->irq_res == NULL) {
        device_printf(dev, "cannot allocate IRQ resource\n");
        err = ENXIO;
        goto fail;
    }

    /* Enable clock, if one is described. */
    if (clk_get_by_ofw_index(dev, node, 0, &sc->clk) == 0) {
        err = clk_enable(sc->clk);
        if (err != 0) {
            device_printf(dev, "could not enable clock: %d\n", err);
            goto fail;
        }
    }

    /* Apply pinctrl default, if any. */
    (void)fdt_pinctrl_configure_by_name(dev, "default");

    /* Initialise locks and driver state. */
    mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

    /* Hook up the interrupt handler. */
    err = bus_setup_intr(dev, sc->irq_res,
        INTR_TYPE_MISC | INTR_MPSAFE, NULL, mydrv_intr, sc,
        &sc->irq_cookie);
    if (err != 0) {
        device_printf(dev, "could not setup interrupt: %d\n", err);
        goto fail;
    }

    device_printf(dev, "variant %lu at %s, clock %u Hz\n",
        (unsigned long)sc->variant, device_get_nameunit(dev), freq);

    return (0);

fail:
    mydrv_detach(dev);
    return (err);
}
```

That is a lot to unpack. Let us walk it.

1. `device_get_softc(dev)` returns the driver's softc, which FreeBSD allocated on your behalf when the driver attached.
2. `ofw_bus_get_node(dev)` returns the phandle for our DT node. We save it on the softc because detach will also want it.
3. We re-run the compat search and record which variant matched.
4. We read a scalar integer property with `OF_getencprop`. The call returns the number of bytes read, `-1` on absence, or some smaller number if the property is too short. We treat anything non-positive as failure.
5. We allocate our memory and IRQ resources. Simplebus has already populated the resource list from the node's `reg` and `interrupts`, so indices 0 and 0 are correct.
6. We try to acquire a clock. This driver treats the clock as optional, so a missing `clocks` property is not fatal. If one is present, we enable it.
7. We apply default pin control.
8. We initialise a driver mutex.
9. We set up the interrupt handler, which will dispatch to `mydrv_intr`.
10. We log a message.
11. On any error, we goto a single cleanup path that calls detach.

The single cleanup path deserves its own discussion. It is an embedded-friendly pattern because embedded drivers acquire many resources from many different frameworks, and attempting to write out cleanup code at every failure point rapidly becomes unreadable. Instead, write a detach that handles partially-initialised softc state, and call it from the failure path. This is the pattern the FreeBSD tree uses consistently; your driver will be easier to read if you follow it.

### The Detach Routine

A compliant detach tears down everything attach might have set up, and does so in the reverse order:

```c
static int
mydrv_detach(device_t dev)
{
    struct mydrv_softc *sc;

    sc = device_get_softc(dev);

    if (sc->irq_cookie != NULL) {
        bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
        sc->irq_cookie = NULL;
    }

    if (sc->irq_res != NULL) {
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
        sc->irq_res = NULL;
    }

    if (sc->mem_res != NULL) {
        bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid,
            sc->mem_res);
        sc->mem_res = NULL;
    }

    if (sc->clk != NULL) {
        clk_disable(sc->clk);
        clk_release(sc->clk);
        sc->clk = NULL;
    }

    if (mtx_initialized(&sc->sc_mtx))
        mtx_destroy(&sc->sc_mtx);

    return (0);
}
```

Two things to notice. First, every teardown step is guarded by a check on whether the resource was actually acquired. The check lets detach run correctly from either a normal unload path (all resources were acquired) or a failed-attach path (only some were). Second, the order is the reverse of acquisition. Interrupt handler off before interrupt resource released. Clock disabled before released. Mutex destroyed last.

### Interrupt Handler

The interrupt handler is a normal FreeBSD interrupt routine. Nothing is FDT-specific about it:

```c
static void
mydrv_intr(void *arg)
{
    struct mydrv_softc *sc = arg;

    mtx_lock(&sc->sc_mtx);
    /* Handle the hardware event. */
    mtx_unlock(&sc->sc_mtx);
}
```

What *is* FDT-specific is the way the interrupt resource was set up in attach. The resource came from the node's `interrupts` property via simplebus, which translated it through the interrupt-parent chain so that by the time your driver called `bus_alloc_resource_any(SYS_RES_IRQ, ...)`, the resource already represented a real hardware interrupt at a real controller.

### Module Registration

The driver's module registration ties the device methods to the driver and registers the driver with a parent bus:

```c
static device_method_t mydrv_methods[] = {
    DEVMETHOD(device_probe,  mydrv_probe),
    DEVMETHOD(device_attach, mydrv_attach),
    DEVMETHOD(device_detach, mydrv_detach),

    DEVMETHOD_END
};

static driver_t mydrv_driver = {
    "mydrv",
    mydrv_methods,
    sizeof(struct mydrv_softc)
};

DRIVER_MODULE(mydrv, simplebus, mydrv_driver, 0, 0);
DRIVER_MODULE(mydrv, ofwbus,   mydrv_driver, 0, 0);
MODULE_VERSION(mydrv, 1);
SIMPLEBUS_PNP_INFO(mydrv_compat_data);
```

The two `DRIVER_MODULE` calls register the driver with both simplebus and the ofwbus root. The latter covers platforms or boards whose node sits directly under the root rather than under an explicit simple-bus. `MODULE_VERSION` declares the driver's version for `kldstat` and dependency tracking. `SIMPLEBUS_PNP_INFO` emits a pnpinfo descriptor that `kldstat -v` can print; it is a small courtesy to the operator but the driver will work without it.

### The Full Skeleton Assembled

Here is the skeleton assembled into a single minimal file that compiles as a kernel module. It does nothing useful; it only demonstrates attachment and logs a message when matched:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

struct fdthello_softc {
    device_t dev;
    phandle_t node;
};

static const struct ofw_compat_data compat_data[] = {
    { "freebsd,fdthello",  1 },
    { NULL,                0 }
};

static int
fdthello_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_str == NULL)
        return (ENXIO);

    device_set_desc(dev, "FDT Hello Example");
    return (BUS_PROBE_DEFAULT);
}

static int
fdthello_attach(device_t dev)
{
    struct fdthello_softc *sc;

    sc = device_get_softc(dev);
    sc->dev = dev;
    sc->node = ofw_bus_get_node(dev);

    device_printf(dev, "attached, node phandle 0x%x\n", sc->node);
    return (0);
}

static int
fdthello_detach(device_t dev)
{
    device_printf(dev, "detached\n");
    return (0);
}

static device_method_t fdthello_methods[] = {
    DEVMETHOD(device_probe,  fdthello_probe),
    DEVMETHOD(device_attach, fdthello_attach),
    DEVMETHOD(device_detach, fdthello_detach),

    DEVMETHOD_END
};

static driver_t fdthello_driver = {
    "fdthello",
    fdthello_methods,
    sizeof(struct fdthello_softc)
};

DRIVER_MODULE(fdthello, simplebus, fdthello_driver, 0, 0);
DRIVER_MODULE(fdthello, ofwbus,   fdthello_driver, 0, 0);
MODULE_VERSION(fdthello, 1);
```

And its matching `Makefile`:

```make
KMOD=	fdthello
SRCS=	fdthello.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

You will find both under `examples/part-07/ch32-fdt-embedded/lab01-fdthello/`. Build them on any FreeBSD system that has the kernel sources installed. The module will only *attach* on a platform that has a Device Tree containing a node with `compatible = "freebsd,fdthello"`, but it will at least compile and load cleanly even on amd64.

### What the Kernel Does Next

When you load that module on an arm64 system whose DTB contains a matching node, the sequence is:

1. The module registers `fdthello_driver` with simplebus and ofwbus.
2. Newbus iterates over every existing device that has `simplebus` or `ofwbus` as a parent and calls the newly registered driver's probe.
3. For each device whose node has `compatible = "freebsd,fdthello"`, the probe returns `BUS_PROBE_DEFAULT`. If no other driver is already attached (or if we outrank it), our attach is called.
4. Attach logs a message; the device is now attached.

When you unload the module, detach runs for each attached instance, then the module is unloaded. In the simple case, `kldunload fdthello` cleans up fully.

### Checking Your Work

Three quick ways to tell whether your driver matched:

1. **`dmesg`** should show a line like:
   ```
   fdthello0: <FDT Hello Example> on simplebus0
   fdthello0: attached, node phandle 0x8f
   ```
2. **`devinfo -r`** should show your device attached somewhere under `simplebus`.
3. **`sysctl dev.fdthello.0.%parent`** should confirm the parent bus.

If your module loads but no devices attach, the probe did not match. The most common causes are a typo in the compatible string, a missing or disabled node, or a node that sits somewhere the simplebus/ofwbus drivers did not reach. Section 6 goes into the debugging in detail.

### A Note on Naming and Vendor Prefixes

Real FreeBSD drivers match on compatibility strings like `"brcm,bcm2711-gpio"`, `"allwinner,sun50i-a64-mmc"`, or `"st,stm32-uart"`. The prefix before the comma is the vendor or community name; the rest is the specific chip or family. The convention is widely respected both in upstream Linux and in FreeBSD. When inventing a new compatible for an experiment (as we did above with `"freebsd,fdthello"`), follow the same vendor-slash-identifier form. Do not invent one-word compatibles; they collide with existing ones and confuse future readers.

### Wrapping Up This Section

Section 4 walked through the shape of an FDT driver. You have seen the headers to include, the softc to define, the compatibility table to declare, the probe and attach and detach routines to write, and the module registration that ties it all together. You have a minimal, complete driver that you could compile and load right now. It does not do much, but its structure is the same structure every FDT-aware driver in FreeBSD uses.

In the next section we turn to the other half of the story. A driver is no use without a Device Tree node to match. Section 5 teaches you how to build and modify `.dtb` files, how FreeBSD's overlay system works, and how to add your own node to an existing board description so that your driver has something to attach to.

## 5. Creating and Modifying Device Tree Blobs

You now have a driver that waits patiently for a Device Tree node with `compatible = "freebsd,fdthello"`. Nothing in the running system provides such a node, so nothing probes. In this section we learn how to change that. We will look at the source-to-binary pipeline, at the overlay mechanism that lets us add nodes without rebuilding the whole `.dtb`, and at the loader tunables that decide which blob the kernel actually sees at boot time.

Creating Device Tree blobs is not a rite of passage for experienced kernel hackers. It is an ordinary editing task. The file is text, the compiler is standard, and the output is a small binary that lives in `/boot`. What makes it feel unfamiliar is only that very few hobbyist projects encounter it. On embedded FreeBSD it is routine.

### The Source-to-Binary Pipeline

Every `.dtb` that boots a FreeBSD system started life as one or more source files. The pipeline is simple:

```text
.dtsi  .dtsi  .dtsi
   \    |    /
    \   |   /
     .dts (top-level source)
       |
       | cpp (C preprocessor)
       v
     .dts (preprocessed)
       |
       | dtc (device tree compiler)
       v
     .dtb (binary blob)
```

The C preprocessor runs first. It expands `#include` directives, macro definitions from header files like `dt-bindings/gpio/gpio.h`, and arithmetic in property expressions. The `dtc` compiler then turns the preprocessed source into the compact flattened format the kernel can parse.

Overlay files go through the same pipeline, except their source file carries the extension `.dtso` and their output carries `.dtbo`. The only real syntactic difference is the magic incantation at the top of overlay source files, which we look at shortly.

FreeBSD's build system wraps this pipeline in two small shell scripts you can study at `/usr/src/sys/tools/fdt/make_dtb.sh` and `/usr/src/sys/tools/fdt/make_dtbo.sh`. They chain `cpp` and `dtc` together, add the right include paths for the kernel's own `dt-bindings` headers, and write the resulting blobs into the build tree. When you `make buildkernel` for an embedded platform, these scripts are what produce the `.dtb` files that end up in `/boot/dtb/` on the installed system.

### Installing the Tools

On FreeBSD, `dtc` is available as a port:

```console
# pkg install dtc
```

The package installs the `dtc` binary together with its companion `fdtdump`, which prints the decoded structure of an existing blob. If you plan to do any overlay work, install both. The FreeBSD base tree also ships a copy of `dtc` under `/usr/src/sys/contrib/device-tree/`, but the port version is easier to reach from userspace.

To check the version:

```console
$ dtc --version
Version: DTC 1.7.0
```

Any version from 1.6 onwards supports overlays. Earlier versions lack the `/plugin/;` directive, so if you inherit an old build environment, upgrade before going further.

### Writing a Standalone .dts File

We will start with a complete, standalone Device Tree source file so the syntax has time to settle before we add overlay complications. Create a file called `tiny.dts` somewhere outside the kernel tree:

```dts
/dts-v1/;

/ {
    compatible = "example,tiny-board";
    model = "Tiny Example Board";
    #address-cells = <1>;
    #size-cells = <1>;

    chosen {
        bootargs = "-v";
    };

    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu0: cpu@0 {
            device_type = "cpu";
            reg = <0>;
            compatible = "arm,cortex-a53";
        };
    };

    memory@0 {
        device_type = "memory";
        reg = <0x00000000 0x10000000>;
    };

    soc {
        compatible = "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        hello0: hello@10000 {
            compatible = "freebsd,fdthello";
            reg = <0x10000 0x100>;
            status = "okay";
        };
    };
};
```

The first line, `/dts-v1/;`, tells `dtc` which version of the source format we are using. Version 1 is the only version in current use, but the directive is still required.

After that, we have the root node, containing a handful of expected children. The `cpus` node describes the processor topology, the `memory@0` node declares a 256 MB region of DRAM at physical address zero, and the `soc` node groups on-chip peripherals under a `simple-bus`. Inside `soc`, our `hello@10000` node provides the Device Tree match for the `fdthello` driver we wrote in Section 4.

A few things are worth noticing here even in this small file.

First, `#address-cells` and `#size-cells` reappear inside the `soc` node. The values a parent sets apply only to that parent's direct children, so every level of the tree that cares about addresses has to declare them. Here the `soc` is using one cell for addresses and one for sizes, which is why `reg = <0x10000 0x100>;` inside `hello@10000` lists exactly two `u32` values.

Second, the `ranges;` property on the `soc` node is empty. An empty `ranges` means "addresses inside this bus match addresses outside it one to one." If the `soc` were, say, mapped at a different base address than its children claim, you would use a longer `ranges` list to express the translation.

Third, `status = "okay"` is explicit here. Without it, every tree implicitly defaults to okay, but many board files set `status = "disabled"` on optional peripherals and expect overlays or board-specific files to flip them on. Get into the habit of checking this property whenever a driver mysteriously fails to probe.

### Compiling a .dts File

Compile the tiny example:

```console
$ dtc -I dts -O dtb -o tiny.dtb tiny.dts
```

The `-I dts` flag tells `dtc` the input is textual source, and `-O dtb` requests a binary blob output. A successful compile prints nothing. A syntax error tells you the file and line.

You can verify the result with `fdtdump`:

```console
$ fdtdump tiny.dtb | head -30
**** fdtdump is a low-level debugging tool, not meant for general use. ****
    Use the fdtput/fdtget/dtc tools to manipulate .dtb files.

/dts-v1/;
// magic:               0xd00dfeed
// totalsize:           0x214 (532)
// off_dt_struct:       0x38
// off_dt_strings:      0x184
// off_mem_rsvmap:      0x28
// version:             17
// last_comp_version:   16
// boot_cpuid_phys:     0x0
// boot_cpuid_phys:     0x0
// size_dt_strings:     0x90
// size_dt_strings:     0x90
// size_dt_structs:     0x14c
// size_dt_structs:     0x14c

/ {
    compatible = "example,tiny-board";
    model = "Tiny Example Board";
    ...
```

That round-trip confirms the blob is valid and parseable. You could now drop it into a QEMU run with `-dtb tiny.dtb` and the kernel would attempt to boot against it. In practice you rarely hand-write a complete `.dts` for a real board. You start with the vendor's own source file (for instance `/usr/src/sys/contrib/device-tree/src/arm64/broadcom/bcm2711-rpi-4-b.dts` for the Raspberry Pi 4) and modify a subset of nodes with an overlay.

### The Role of .dtsi Include Files

The `.dtsi` extension is used for Device Tree *includes*. These files contain tree fragments that are meant to be pulled into another `.dts` or `.dtsi`. The compiler treats them identically to `.dts` files, but the filename suffix tells other humans (and the build system) that the file is not standalone.

A common pattern in modern SoC descriptions is:

```text
arm/bcm283x.dtsi          <- SoC definition
arm/bcm2710.dtsi          <- Family refinement (Pi 3 lineage)
arm/bcm2710-rpi-3-b.dts   <- Specific board top-level file, includes both
```

Each `.dtsi` adds and refines nodes. Labels declared in a lower file can be referenced from a higher file with the `&label` syntax to override properties without rewriting the node. This is the mechanism that makes it possible to support dozens of related boards from a handful of shared SoC descriptions.

### Understanding Overlays

A full `.dts` for a real SBC like the Raspberry Pi 4 is dozens of kilobytes long. If you only want to enable SPI, or add a single GPIO-controlled peripheral, rebuilding the whole blob is wasteful and error-prone. The overlay mechanism exists exactly for this situation.

An overlay is a small, special `.dtb` that targets an existing tree. At load time, the FreeBSD loader merges the overlay into the base tree in memory, producing a combined view that the kernel sees as a single Device Tree. The base `.dtb` on disk is never modified. That means the same overlay can enable a feature across several systems with one copy on each.

The syntax of an overlay source file uses two magic directives at the top:

```dts
/dts-v1/;
/plugin/;
```

After those, the source refers to nodes in the base tree by label. The compiler records the references symbolically, and the loader resolves them at merge time against whatever labels the base tree actually exports. That is why the overlay can be written and compiled independently of the exact base tree it will later be merged with.

Here is a minimal overlay that attaches an `fdthello` node to an existing `soc` bus:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&soc>;
        __overlay__ {
            hello0: hello@20000 {
                compatible = "freebsd,fdthello";
                reg = <0x20000 0x100>;
                status = "okay";
            };
        };
    };
};
```

The outer `compatible` says this overlay is intended for a BCM2711 tree. The loader uses that string to refuse overlays that do not match the current board. Inside we see a single `fragment@0` node. Each fragment targets one existing node in the base tree through the `target` property. The content under `__overlay__` is the set of properties and children that get merged into that target.

In this example, we are adding a `hello@20000` child under whatever node `&soc` resolves to at merge time. The base tree on a Raspberry Pi 4 declares the `soc` label on the top-level SoC bus node, so after the merge the base `soc` will gain a new `hello@20000` child, and our driver's probe will fire.

You can also use overlays to *modify* existing nodes. If you set a property with the same name as an existing one, the overlay value replaces the original. If you add a new property, it simply appears. If you add a new child, it is grafted on. The mechanism is additive except for property value replacement.

### Compiling and Deploying Overlays

Build the overlay with:

```console
$ dtc -I dts -O dtb -@ -o fdthello.dtbo fdthello-overlay.dts
```

The `-@` flag tells the compiler to emit the symbolic label information needed at merge time. Without it, overlays that reference labels fail silently or produce unhelpful errors.

On a running FreeBSD system, overlays live under `/boot/dtb/overlays/`. The filename needs to end in `.dtbo` by convention. The loader looks in `/boot/dtb/overlays` by default; the path can be overridden through loader tunables if you want to stage overlays elsewhere.

To tell the loader which overlays to apply, add a line to `/boot/loader.conf`:

```ini
fdt_overlays="fdthello,sunxi-i2c1,spigen-rpi4"
```

The value is a comma-separated list of overlay basenames without the `.dtbo` extension. Order matters only when overlays interact with each other. On boot, the loader reads the list, loads each overlay, merges them into the base tree in sequence, and hands the combined blob to the kernel.

A good sanity check is to watch the loader output on a serial console or HDMI screen. When `fdt_overlays` is set, the loader prints a line like:

```text
Loading DTB overlay 'fdthello' (0x1200 bytes)
```

If the file is missing or the target does not match, the loader prints a warning and continues. Your driver then fails to probe because the overlay never applied. Checking the loader's console output is the fastest way to catch this kind of failure.

### A Walkthrough: Adding a Node to a Raspberry Pi 4 Tree

Let us put the machinery together in a realistic scenario. Imagine you are bringing up a custom daughterboard for the Raspberry Pi 4. It contains a single GPIO-controlled indicator LED on GPIO18 of the Pi's header. You want FreeBSD to drive the LED through your own `edled` driver (which we build in Section 7). You need a Device Tree node.

First, you look up what the Pi 4's base `.dtb` already declares. On a running Pi with FreeBSD installed, `ofwdump -ap | less` or `fdtdump /boot/dtb/broadcom/bcm2711-rpi-4-b.dtb | less` gives you the full tree. Your main interest is the `soc` and `gpio` nodes, where you see a label `gpio = <&gpio>;` exported from the GPIO controller.

Next, you write the overlay source:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            edled0: edled@0 {
                compatible = "example,edled";
                status = "okay";
                leds-gpios = <&gpio 18 0>;
                label = "daughter-indicator";
            };
        };
    };
};
```

We use `target-path` instead of `target` here because the target is an existing path rather than a label. Both forms are valid; `target` takes a phandle reference, `target-path` takes a string.

The `leds-gpios` property is the conventional way to describe a GPIO reference in Device Tree. It is a phandle to the GPIO controller, followed by the GPIO number on that controller, followed by a flag word. Value `0` for the flag means active-high; `1` means active-low. Pinmux usually needs no explicit mention on the Pi because the Broadcom GPIO controller handles both direction and function through the same register set.

Compile and install the overlay:

```console
$ dtc -I dts -O dtb -@ -o edled.dtbo edled-overlay.dts
$ sudo cp edled.dtbo /boot/dtb/overlays/
```

Add the overlay to the loader configuration:

```console
# echo 'fdt_overlays="edled"' >> /boot/loader.conf
```

Reboot. On the way up, the loader prints its overlay-load line, the base DT gains the new `edled@0` node under `/soc`, the `edled` driver's probe fires, and the daughterboard LED comes up under software control.

### Inspecting the Result

Once the kernel is running, three tools verify that everything landed where it should:

```console
# ofwdump -p /soc/edled@0
```

prints the properties of the newly added node.

```console
# sysctl dev.edled.0.%parent
```

confirms the driver attached and shows its parent bus.

```console
# devinfo -r | less
```

displays the full device tree as FreeBSD sees it, with your driver in place.

If any of these disagree with the overlay content, Section 6 helps you diagnose the cause.

### Troubleshooting Build Failures

Most DT build errors fall into a small number of categories.

**Unresolved references.** If the overlay refers to a label like `&gpio` that is not exported by the base tree, the loader prints `no symbol for <label>` and refuses to apply the overlay. Fix by using `target-path` with an absolute path instead, or by rebuilding the base `.dtb` with `-@` to include its symbols.

**Syntax errors.** These show up as `dtc` errors with a line number. Common culprits are missing semicolons at the end of property assignments, unbalanced braces, and property values that mix unit types (for example, a mix of angle-bracket integers and quoted strings on the same line).

**Cell-count mismatches.** If the parent declares `#address-cells = <2>` and a child's `reg` gives only one cell, the compiler tolerates it but the kernel parses the value wrong. `ofwdump -p node` and careful reading of the parent's cell counts usually reveal the mismatch.

**Duplicate node names.** Two nodes at the same level cannot share a name-plus-unit-address. The compiler flags this, but overlays that try to add a new node whose name collides with an existing one produce a cryptic merge failure at boot. Choose unique names or target a different path.

### The Kernel's dtb Loading Process

For context, it helps to know what happens to the final merged blob after the loader hands it off.

On arm64 and several other platforms, the loader places the blob at a fixed physical address and passes the pointer to the kernel in its boot argument block. The earliest kernel code, in `/usr/src/sys/arm64/arm64/machdep.c`, validates the magic number and size, maps the blob into kernel virtual memory, and registers it with the FDT subsystem. By the time Newbus begins attaching devices, the blob is fully parsed and the OFW API can walk it.

On amd64 embedded systems (rare but they exist), the flow is similar: UEFI passes the blob through a configuration table, the loader discovers it, and the kernel consumes it through the same FDT API.

The blob is read-only from the kernel's perspective. You never modify it at runtime. If a property value needs to change, the right place to change it is in the source, not in a live tree.

### Wrapping Up This Section

Section 5 taught you how to move between Device Tree source and Device Tree binary, how overlays target an existing tree, and how `fdt_overlays` wires the whole thing into the FreeBSD boot process. You can now write a `.dts`, compile it, place the result under `/boot/dtb/overlays/`, list it in `loader.conf`, and watch the kernel pick up your node. The driver you wrote in Section 4 now has something to attach to.

In Section 6 we turn the lens around and look at tools for inspecting what actually landed in the kernel. When things go wrong, as they will, good observation is the shortest path back to a working system.

## 6. Testing and Debugging FDT Drivers

Every driver you write will, at some point, fail to probe. The Device Tree will look right in the source, your compatibility string will read correctly, and `kldload` will complete without complaint, but `dmesg` will be silent. Debugging this class of failure is its own skill, and the sooner you build the habit the less time each problem will cost you.

This section covers the tools and techniques for inspecting the running Device Tree, for diagnosing probe failures, for observing attach behavior in detail, and for tracking down unload problems. Much of the material here is applicable to bus drivers, peripheral drivers, and pseudo-devices alike. The one thing that is truly FDT-specific is the set of tools for reading the tree itself.

### The ofwdump(8) Tool

On a running FreeBSD system, `ofwdump` is your primary window into the Device Tree. It prints nodes and properties from the in-kernel tree, so what it shows is exactly what the drivers see during probe. If the tree is wrong in the kernel, it will be wrong in `ofwdump`, which saves you from compiling and booting again just to check an edit.

The simplest invocation prints the whole tree:

```console
# ofwdump -a
```

Pipe through `less` on any non-trivial system; the output runs thousands of lines.

A more focused run dumps one node and its properties:

```console
# ofwdump -p /soc/hello@10000
Node 0x123456: /soc/hello@10000
    compatible:  freebsd,fdthello
    reg:         00 01 00 00 00 00 01 00
    status:      okay
```

The `-p` flag prints properties alongside the node name. Integer values come out as byte strings because `ofwdump` cannot, in general, know how many cells a property is supposed to have. You interpret the bytes using the parent's `#address-cells` and `#size-cells`.

To read one specific property:

```console
# ofwdump -P compatible /soc/hello@10000
```

Add `-R` to recurse into children of the given node. Add `-S` to print phandles and `-r` to print raw binary if you want to pipe the data into another tool.

Get comfortable with `ofwdump`. When someone says "check the tree," this is the tool they mean.

### Reading the Raw Blob Through sysctl

FreeBSD exposes the unmerged base blob through a sysctl:

```console
# sysctl -b hw.fdt.dtb | fdtdump
```

The `-b` flag tells sysctl to print raw binary; piping into `fdtdump` decodes it. This is useful when you suspect an overlay has altered the tree and you want to compare the pre-merge blob with the post-merge view. `ofwdump` shows the post-merge view; `hw.fdt.dtb` shows the pre-merge base.

### Confirming FDT Mode on arm64

FreeBSD does not expose a dedicated sysctl that says "you are running on FDT" or "you are running on ACPI." The decision is made very early in boot by the kernel variable `arm64_bus_method`, and the easiest way to observe it from user space is by looking at the device tree `dmesg` prints during startup. A machine that chose the FDT path shows a root line like:

```text
ofwbus0: <Open Firmware Device Tree>
simplebus0: <Flattened device tree simple bus> on ofwbus0
```

followed by the rest of the FDT children. A machine that chose the ACPI path shows `acpi0: <...>` instead, and you will never see an `ofwbus0` line.

On a live system you can also run `devinfo -r` and look for `ofwbus0` in the hierarchy, or confirm that the sysctl `hw.fdt.dtb` is present. That sysctl is only registered when a DTB was parsed at boot, so its existence is itself a signal:

```console
# sysctl -N hw.fdt.dtb 2>/dev/null && echo "FDT is active" || echo "ACPI or neither"
```

The `-N` flag asks sysctl only for the name, so it succeeds without printing the blob's bytes.

On boards that support both mechanisms, the mechanism that chooses between them is the loader tunable `kern.cfg.order`. Setting `kern.cfg.order="fdt"` in `/boot/loader.conf` forces the kernel to try FDT first and fall back to ACPI only if no DTB is found; `kern.cfg.order="acpi"` does the opposite. On x86 platforms, `hint.acpi.0.disabled="1"` disables the ACPI attachment entirely and is sometimes useful when the firmware is misbehaving. Section 3 covered this duality in more detail; if you ever find yourself staring at an FDT driver that refuses to attach on an ARM server platform, one of the first things to verify is which bus method the kernel actually chose.

### Debugging a Probe That Does Not Fire

The most common symptom is silence: the module loads, `kldstat` shows it, but no device attaches. The probe either never ran or returned `ENXIO`. Walk through the following checklist.

**1. Is the node present in the kernel's tree?**

```console
# ofwdump -p /soc/your-node
```

If the node is missing, your overlay did not apply. Review the loader's output at boot time. Re-check `/boot/loader.conf` for a `fdt_overlays=` line. Confirm the `.dtbo` file is in `/boot/dtb/overlays/`. Rebuild the overlay if you suspect a stale copy.

**2. Is the status property set to okay?**

```console
# ofwdump -P status /soc/your-node
```

A value of `"disabled"` keeps the node from being probed. Base board files often declare optional peripherals as disabled and leave it to overlays to enable them.

**3. Is the compatible string exactly what the driver expects?**

A typo in the overlay or the driver's compat table is the single most common cause of probe failure. Compare them character by character:

```console
# ofwdump -P compatible /soc/your-node
```

Against the matching line in the driver:

```c
{"freebsd,fdthello", 1},
```

If even the vendor prefix differs (for example `free-bsd,` vs `freebsd,`), no match occurs.

**4. Does the parent bus support probing?**

FDT drivers attach to `simplebus` or `ofwbus`. If your node's parent is something else (say, an `i2c` bus node), your driver must register with that parent instead. Check the parent by looking one level up in `ofwdump`.

**5. Is the driver ranked below another driver that matched?**

If a more general driver returned `BUS_PROBE_GENERIC` first, your new driver needs to return something stronger, like `BUS_PROBE_DEFAULT` or `BUS_PROBE_SPECIFIC`. `devinfo -r` shows which driver actually attached.

### Adding Temporary Debug Output

When none of the above reveals the cause, add `device_printf` calls to `probe` and `attach` to watch the flow directly. In `probe`:

```c
static int
fdthello_probe(device_t dev)
{
    device_printf(dev, "probe: node=%ld compat=%s\n",
        ofw_bus_get_node(dev),
        ofw_bus_get_compat(dev) ? ofw_bus_get_compat(dev) : "(none)");

    if (!ofw_bus_status_okay(dev)) {
        device_printf(dev, "probe: status not okay\n");
        return (ENXIO);
    }

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0) {
        device_printf(dev, "probe: compat mismatch\n");
        return (ENXIO);
    }

    device_set_desc(dev, "FDT Hello Example");
    return (BUS_PROBE_DEFAULT);
}
```

This prints on every probe call, so expect noise. Remove the prints before shipping. The point is the transient visibility into what `ofw_bus_*` helpers are returning.

In `attach`, print the resource rids and addresses you allocated:

```c
device_printf(dev, "attach: mem=%#jx size=%#jx\n",
    (uintmax_t)rman_get_start(sc->mem),
    (uintmax_t)rman_get_size(sc->mem));
```

This confirms that `bus_alloc_resource_any` handed back a valid range. A probe that matches but an attach that crashes here usually means `reg` in the DT is wrong.

### Observing Attach Order and Dependencies

On embedded systems, attach order is not always intuitive. A GPIO-consuming driver has to wait for the GPIO controller to attach first. If your driver tries to acquire a GPIO line before the controller is ready, `gpio_pin_get_by_ofw_idx` returns `ENXIO` and your attach fails. FreeBSD handles the ordering through explicit dependencies expressed at driver registration time, and through the `interrupt-parent` walk for interrupt trees.

Use `devinfo -rv` to watch the order:

```console
# devinfo -rv | grep -E '(gpio|edled|simplebus)'
```

If `edled` appears before `gpio`, something in the ordering needs fixing. The usual fix is a `MODULE_DEPEND` line in the consumer driver:

```c
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
```

This forces `gpiobus` to load first, ensuring the GPIO controller is available when `edled` attaches.

### Debugging Detach and Unload

Detach debugging is easier than probe debugging because detach runs while the system is up and `printf` output reaches `dmesg` immediately. The two problems you are most likely to meet are:

**Unload returns EBUSY.** Some resource is still held by the driver. The common cause is a GPIO pin or interrupt handle that was not released. Audit every `_get_` call and make sure there is a matching `_release_` in detach.

**Unload succeeds but the module reattaches on next `kldload`.** This is almost always because detach left a softc field pointing to freed memory, and the second attach followed that pointer. Treat detach as a careful teardown of everything attach built, in reverse order.

A useful trick is to add:

```c
device_printf(dev, "detach: entered\n");
...teardown...
device_printf(dev, "detach: complete\n");
```

If the second line never appears, something in detach hung or panicked.

### Using DTrace for Deeper Visibility

For more sophisticated investigations, DTrace can trace `device_probe` and `device_attach` across the whole kernel without touching the driver source. A one-liner that shows every attach call:

```console
# dtrace -n 'fbt::device_attach:entry { printf("%s", stringof(args[0]->softc)); }'
```

The output is noisy during boot, but running it interactively while `kldload`-ing your driver filters it naturally. DTrace usage is beyond the scope of this chapter, but knowing it exists is worth the half-page it would take to set up.

### Testing on QEMU

Not every reader has a Raspberry Pi or a BeagleBone to test against. QEMU can emulate an arm64 virt machine, boot FreeBSD on it, and let you load drivers and overlays without any real hardware. The virt machine uses its own Device Tree, which QEMU generates automatically; your overlay can target that tree in exactly the same way it would target a real board. The only caveat is that GPIOs and similar low-level peripherals are limited or absent on the virt machine. For pure DT-and-module experimentation it is perfectly adequate.

The basic invocation looks like this:

```console
$ qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a72 \
    -m 2G \
    -kernel /path/to/kernel \
    -drive if=virtio,file=disk.img \
    -serial mon:stdio \
    -append "console=comconsole"
```

Once the system is up, `kldload` your module and watch for probe messages on the serial console.

### When to Stop Debugging and Rebuild

Sometimes a bug is easier to fix by tearing the driver down and rebuilding from a known-good skeleton. The `fdthello` example in Section 4 is exactly that skeleton. If you find yourself chasing a probe failure for more than an hour, copy `fdthello`, rename it, add your compat string, and verify the trivial case attaches. Then port the real functionality over one piece at a time. You will almost always find the bug in the process.

### Wrapping Up This Section

Section 6 armed you with the tools and habits of an embedded driver debugger. You have `ofwdump` for the tree, `hw.fdt.dtb` for the raw blob, `devinfo -r` for the attached-device view, `MODULE_DEPEND` for ordering, and `device_printf` for ad-hoc visibility. You also have a mental checklist for the common probe and detach failures.

Section 7 now puts all of the chapter's theory into a single worked example: a GPIO-backed LED driver that you build, compile, load, and drive from a `.dts` overlay. If you have worked through this chapter sequentially, the example will feel like a straightforward synthesis of the pieces we have already seen.

## 7. Practical Example: GPIO Driver for an Embedded Board

This section walks through the full construction of a small but real driver called `edled` (embedded LED). The driver:

1. Matches a Device Tree node with `compatible = "example,edled"`.
2. Acquires a GPIO pin listed in the node's `leds-gpios` property.
3. Exposes a `sysctl` knob the user can toggle to set the LED state.
4. Releases the GPIO cleanly on detach.

The driver is deliberately minimal. Once it works, you will be able to adapt it to drive anything that sits behind a single GPIO, and the patterns scale up when you need to handle multiple pins, interrupts, or more elaborate peripherals.

### What You Need

To follow along you need:

- A FreeBSD system running kernel 14.3 or later.
- The kernel sources installed under `/usr/src`.
- `dtc` from the `devel/dtc` port or similar.
- A board with at least one free GPIO pin and an LED (or you can test the sysctl toggle without a real LED; the driver still attaches and logs state changes).

If you are on a Raspberry Pi 4 running FreeBSD, GPIO 18 is a convenient choice because it does not collide with the default console or SD card controller. A Pi 3 or Pi Zero 2 works the same way with adjusted GPIO numbers. On a BeagleBone Black, pick any of the many free pins on the 46-way header.

### The Overall File Layout

We will produce five files:

```text
edled.c            <- C source
Makefile           <- Kernel module Makefile
edled.dts          <- DT overlay source
edled.dtbo         <- Compiled overlay (output)
README             <- Notes for the reader
```

The corresponding repository layout under the `examples/` tree is:

```text
examples/part-07/ch32-fdt-embedded/lab04-edled/
    edled.c
    edled.dts
    Makefile
    README.md
```

You can copy the files out of that tree once you reach the lab section at the end of the chapter.

### The Softc

Every driver instance needs a small state block. `edled`'s softc holds:

- The device handle itself.
- The GPIO pin descriptor.
- The current on/off state.
- The sysctl oid for toggling.

```c
struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
};
```

`gpio_pin_t` is defined in `/usr/src/sys/dev/gpio/gpiobusvar.h`. It is an opaque handle that carries the GPIO controller reference, the pin number, and the active-high/active-low flag. You never dereference it directly; you pass it to `gpio_pin_setflags`, `gpio_pin_set_active`, and `gpio_pin_release`.

### Headers

The top of `edled.c` pulls in the definitions we need:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>
```

Compared with the `fdthello` skeleton in Section 4, we add `<sys/sysctl.h>` for the knob and `<dev/gpio/gpiobusvar.h>` for GPIO consumer APIs.

### The Compatibility Table

A tiny table with one entry is all this driver needs:

```c
static const struct ofw_compat_data compat_data[] = {
    {"example,edled", 1},
    {NULL,            0}
};
```

A real project would pick a vendor prefix it owns. Using `"example,"` flags the compatible as illustrative. When you ship a product, replace it with your company or project prefix.

### The Probe

Probe uses the same pattern as `fdthello`:

```c
static int
edled_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);

    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);

    device_set_desc(dev, "Example embedded LED");
    return (BUS_PROBE_DEFAULT);
}
```

There is nothing new here. The only reason to copy the probe verbatim from Section 4 is to emphasize how repetitive this step is across drivers; the meaningful differences between drivers almost always live in attach, detach, and the operations layer.

### The Attach

Attach is where the real work happens. We allocate and initialize the softc, acquire the GPIO pin, configure it as an output, set it to "off," publish the sysctl, and print a confirmation.

```c
static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        return (error);
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, GPIO pin acquired, state=0\n");
    return (0);
}
```

There are several things worth examining in this code.

The call `gpio_pin_get_by_ofw_property(dev, node, "leds-gpios", &sc->sc_pin)` parses the `leds-gpios` property from the DT node, resolves the phandle to the GPIO controller, consumes the pin number, and produces a ready-to-use handle. If the controller has not yet attached, this call returns `ENXIO`, which is why we express a `MODULE_DEPEND` on `gpiobus` during registration.

`gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT)` configures the pin direction. Other valid flags include `GPIO_PIN_INPUT`, `GPIO_PIN_PULLUP`, and `GPIO_PIN_PULLDOWN`. You can combine them, for example `GPIO_PIN_INPUT | GPIO_PIN_PULLUP`.

`gpio_pin_set_active(sc->sc_pin, 0)` sets the pin to its inactive level. "Active" here takes polarity into account, so on a pin configured active-low, a value of `1` drives the line low and `0` drives it high. The DT flag cell we discussed earlier is what determines this.

`SYSCTL_ADD_PROC` creates a node at `dev.edled.<unit>.state` whose handler is our own `edled_sysctl_state` function. The `CTLFLAG_NEEDGIANT` flag is appropriate for a small driver that does not yet have proper locking; a production driver would use a dedicated mutex and drop the Giant flag.

If any step fails, we release whatever we have already acquired and return the error. Leaking a GPIO pin on the error path would prevent other drivers from ever using the same line.

### The Sysctl Handler

The sysctl handler reads or writes the LED state:

```c
static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val = sc->sc_on;
    int error;

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    return (error);
}
```

`SYSCTL_HANDLER_ARGS` expands to the standard sysctl handler signature. We read the current value into a local, call `sysctl_handle_int` to do the user-space copy, and, if the user supplied a new value, we sanity-check it and apply it through the GPIO API. The current state is kept in the softc so a read without a write returns the last value we set.

### The Detach

Detach must release everything attach acquired, in reverse order:

```c
static int
edled_detach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);

    if (sc->sc_pin != NULL) {
        (void)gpio_pin_set_active(sc->sc_pin, 0);
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    device_printf(dev, "detached\n");
    return (0);
}
```

We turn the LED off before releasing the pin. Leaving it on across a module unload is rude to the next driver; worse, the pin is released while asserted, and whatever it is driving stays on until something else reclaims the line. The sysctl context is owned by the newbus system through `device_get_sysctl_ctx`, so we do not free the oid explicitly; newbus tears it down for us.

### The Method Table and Driver Registration

Nothing surprising here:

```c
static device_method_t edled_methods[] = {
    DEVMETHOD(device_probe,  edled_probe),
    DEVMETHOD(device_attach, edled_attach),
    DEVMETHOD(device_detach, edled_detach),
    DEVMETHOD_END
};

static driver_t edled_driver = {
    "edled",
    edled_methods,
    sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
```

The only addition compared with `fdthello` is `MODULE_DEPEND(edled, gpiobus, 1, 1, 1)`. The three integer arguments are the minimum, preferred, and maximum version of `gpiobus` that `edled` can tolerate. A value triplet of `1, 1, 1` means "any version at or above 1." In practice this is almost always what you want.

### The Full Source

Putting it all together:

```c
/*
 * edled.c - Example Embedded LED Driver
 *
 * Demonstrates a minimal FDT-driven GPIO consumer on FreeBSD 14.3.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/gpio/gpiobusvar.h>

struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
};

static const struct ofw_compat_data compat_data[] = {
    {"example,edled", 1},
    {NULL,            0}
};

static int edled_sysctl_state(SYSCTL_HANDLER_ARGS);

static int
edled_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);
    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);
    device_set_desc(dev, "Example embedded LED");
    return (BUS_PROBE_DEFAULT);
}

static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        return (error);
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        gpio_pin_release(sc->sc_pin);
        return (error);
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_NEEDGIANT,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, GPIO pin acquired, state=0\n");
    return (0);
}

static int
edled_detach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);

    if (sc->sc_pin != NULL) {
        (void)gpio_pin_set_active(sc->sc_pin, 0);
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    device_printf(dev, "detached\n");
    return (0);
}

static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val = sc->sc_on;
    int error;

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    return (error);
}

static device_method_t edled_methods[] = {
    DEVMETHOD(device_probe,  edled_probe),
    DEVMETHOD(device_attach, edled_attach),
    DEVMETHOD(device_detach, edled_detach),
    DEVMETHOD_END
};

static driver_t edled_driver = {
    "edled",
    edled_methods,
    sizeof(struct edled_softc)
};

DRIVER_MODULE(edled, simplebus, edled_driver, 0, 0);
DRIVER_MODULE(edled, ofwbus,    edled_driver, 0, 0);
MODULE_DEPEND(edled, gpiobus, 1, 1, 1);
MODULE_VERSION(edled, 1);
```

Around 140 lines of C, including headers and blank lines. That is a working, production-shaped FDT GPIO driver.

### The Makefile

As with every kernel module in this book, the Makefile is trivial:

```makefile
KMOD=   edled
SRCS=   edled.c

SYSDIR?= /usr/src/sys

.include <bsd.kmod.mk>
```

`bsd.kmod.mk` handles the rest. Typing `make` in the directory produces `edled.ko` and `edled.ko.debug`.

### The Overlay Source

The companion `.dts` overlay looks like this (tuned for a Raspberry Pi 4; adjust for your board):

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/soc";
        __overlay__ {
            edled0: edled@0 {
                compatible = "example,edled";
                status = "okay";
                leds-gpios = <&gpio 18 0>;
                label = "lab-indicator";
            };
        };
    };
};
```

Compile it with:

```console
$ dtc -I dts -O dtb -@ -o edled.dtbo edled.dts
```

and copy to `/boot/dtb/overlays/`.

### Building and Loading

On the target system, put all four files in a scratch directory, then:

```console
$ make
$ sudo cp edled.dtbo /boot/dtb/overlays/
$ sudo sh -c 'echo fdt_overlays=\"edled\" >> /boot/loader.conf'
$ sudo reboot
```

After the reboot, you should see:

```console
# dmesg | grep edled
edled0: <Example embedded LED> on simplebus0
edled0: attached, GPIO pin acquired, state=0
```

If you plugged an LED into GPIO 18, it is currently off. Verify with:

```console
# sysctl dev.edled.0.state
dev.edled.0.state: 0
```

Turn it on:

```console
# sysctl dev.edled.0.state=1
dev.edled.0.state: 0 -> 1
```

The LED lights. Turn it off:

```console
# sysctl dev.edled.0.state=0
dev.edled.0.state: 1 -> 0
```

Done. You have a functioning embedded driver end to end: from Device Tree source through kernel module through user-space control.

### Inspecting the Resulting Device

A few useful queries to confirm the driver is well integrated:

```console
# devinfo -r | grep -A1 simplebus
# sysctl dev.edled.0
# ofwdump -p /soc/edled@0
```

The first shows your driver in the Newbus tree. The second lists all sysctls registered by your driver. The third confirms the DT node has the expected properties.

### Pitfalls Worth Calling Out

The following mistakes are classic when writing your first GPIO-consumer driver. Each is easy to make and, once you know to look for it, easy to avoid.

**Forgetting to release the pin in detach.** `kldunload` succeeds, but the pin stays tied up. The next load reports "pin busy." Always match every `gpio_pin_get_*` with a `gpio_pin_release` in detach.

**Reading the DT property before the parent bus has attached.** The `leds-gpios` parse returns `ENXIO`. The fix is to ensure the GPIO controller module loads first, via `MODULE_DEPEND`. During boot this happens automatically because the static kernel has both resident; during hand-loaded experiments with `kldload`, you may need to load `gpiobus` explicitly first.

**Getting the active flag wrong.** On boards that wire the LED so the GPIO sinks current (LED between `3V3` and the pin), "on" corresponds to a low output. In that case, `leds-gpios = <&gpio 18 1>` is correct and `gpio_pin_set_active(sc->sc_pin, 1)` will drive the pin low, lighting the LED. If the LED behaves inversely, flip the flag.

**Sysctls that change state without a lock.** This driver uses `CTLFLAG_NEEDGIANT` as a shortcut. In a real driver, you allocate a `struct mtx`, take it in the sysctl handler around the GPIO call, and publish the sysctl without the Giant flag. For a single-GPIO LED it does not matter much in practice, but the pattern is important once you extend the driver to handle interrupts or shared state.

### Wrapping Up This Section

Section 7 delivered on the promise of the chapter. You have built a complete FDT-driven GPIO consumer, deployed it through an overlay, loaded it on a running system, and exercised it from user space. The components you used, compat tables, OFW helpers, resource acquisition through consumer frameworks, sysctl registration, newbus registration, are the same components every embedded driver in FreeBSD relies on.

Section 8 looks at how to turn a working driver like `edled` into a robust one. Working is the first milestone. Robust is the one that earns a driver its place in the kernel tree.

## 8. Refactoring and Finalizing Your Embedded Driver

The driver in Section 7 works. You load the module, flip a sysctl, and the LED behaves. That is a real accomplishment, and if the goal is a one-off experiment on a bench, you can stop there. For anything more serious, a working driver needs to be turned into a *finished* driver: one that can be read by a stranger, audited by a reviewer, and trusted in a system that stays up for months.

This section walks through the refactoring passes that take `edled` from working to finished. The changes are not about making it do more. They are about making it right. The same process applies to any driver you write, including drivers you adapt from other projects or port from Linux.

### What Refactoring Means Here

"Refactor" is one of those words that often covers whatever the speaker feels like changing. For the purposes of this section, refactoring means:

1. Removing latent bugs that happen not to fire in the happy path.
2. Adding the locking and error paths a production driver requires.
3. Improving names, layout, and comments so the next reader does not need to guess.
4. Moving infrastructure out of attach into helpers when the body has grown too long.

Nothing here changes the external behavior of the driver. The sysctl still reads and writes the same integer, the LED still turns on and off, and the DT binding does not move. What changes is how reliable the driver is when something unexpected happens.

### Pass One: Tighten the Attach Error Paths

The original attach function grew a cluster of error handlers that each call `gpio_pin_release` and return. That works, but it duplicates cleanup. A cleaner shape uses a single exit block with labels:

```c
static int
edled_attach(device_t dev)
{
    struct edled_softc *sc = device_get_softc(dev);
    phandle_t node = ofw_bus_get_node(dev);
    int error;

    sc->sc_dev = dev;
    sc->sc_on = 0;

    error = gpio_pin_get_by_ofw_property(dev, node,
        "leds-gpios", &sc->sc_pin);
    if (error != 0) {
        device_printf(dev, "cannot get GPIO pin: %d\n", error);
        goto fail;
    }

    error = gpio_pin_setflags(sc->sc_pin, GPIO_PIN_OUTPUT);
    if (error != 0) {
        device_printf(dev, "cannot set pin flags: %d\n", error);
        goto fail;
    }

    error = gpio_pin_set_active(sc->sc_pin, 0);
    if (error != 0) {
        device_printf(dev, "cannot set pin state: %d\n", error);
        goto fail;
    }

    sc->sc_oid = SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
        SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
        OID_AUTO, "state",
        CTLTYPE_INT | CTLFLAG_RW,
        sc, 0, edled_sysctl_state, "I", "LED state (0=off, 1=on)");

    device_printf(dev, "attached, state=0\n");
    return (0);

fail:
    if (sc->sc_pin != NULL) {
        gpio_pin_release(sc->sc_pin);
        sc->sc_pin = NULL;
    }
    return (error);
}
```

The `goto fail` pattern is idiomatic FreeBSD kernel style. It collapses the cleanup logic into one place so it is impossible for a future edit to leak a resource by forgetting one of several identical `release` calls.

### Pass Two: Add Proper Locking

`CTLFLAG_NEEDGIANT` was a shortcut. The right approach is a per-softc mutex held around the hardware access:

```c
struct edled_softc {
    device_t        sc_dev;
    gpio_pin_t      sc_pin;
    int             sc_on;
    struct sysctl_oid *sc_oid;
    struct mtx      sc_mtx;
};
```

Initialize the mutex in attach:

```c
mtx_init(&sc->sc_mtx, device_get_nameunit(dev), "edled", MTX_DEF);
```

Destroy it in detach:

```c
mtx_destroy(&sc->sc_mtx);
```

Take it in the sysctl handler around the hardware call:

```c
static int
edled_sysctl_state(SYSCTL_HANDLER_ARGS)
{
    struct edled_softc *sc = arg1;
    int val, error;

    mtx_lock(&sc->sc_mtx);
    val = sc->sc_on;
    mtx_unlock(&sc->sc_mtx);

    error = sysctl_handle_int(oidp, &val, 0, req);
    if (error != 0 || req->newptr == NULL)
        return (error);

    if (val != 0 && val != 1)
        return (EINVAL);

    mtx_lock(&sc->sc_mtx);
    error = gpio_pin_set_active(sc->sc_pin, val);
    if (error == 0)
        sc->sc_on = val;
    mtx_unlock(&sc->sc_mtx);

    return (error);
}
```

Notice that we drop the mutex around `sysctl_handle_int`. That call may copy data to or from user space, which can sleep, and you do not hold a mutex across a sleep. The value we hand to `sysctl_handle_int` is a local copy, so the drop is safe.

Remove `CTLFLAG_NEEDGIANT` from the `SYSCTL_ADD_PROC` call. With a real lock in place, Giant is no longer needed.

### Pass Three: Handle the Power Rail Explicitly

On many real peripherals, the driver is responsible for turning on the power rail and the reference clock before touching the device. FreeBSD provides consumer APIs under `/usr/src/sys/dev/extres/regulator/` and `/usr/src/sys/dev/extres/clk/` for exactly this purpose. Even though a discrete LED does not need a regulator, more serious peripherals (say, a SPI-connected accelerometer) do. To keep `edled` a useful teaching template, we show how the machinery slots in.

Under a hypothetical DT node:

```dts
edled0: edled@0 {
    compatible = "example,edled";
    status = "okay";
    leds-gpios = <&gpio 18 0>;
    vled-supply = <&ldo_led>;
    clocks = <&clks 42>;
    label = "lab-indicator";
};
```

Two extra properties: `vled-supply` references a regulator phandle, and `clocks` references a clock phandle. Attach picks them up like this:

```c
#include <dev/extres/clk/clk.h>
#include <dev/extres/regulator/regulator.h>

struct edled_softc {
    ...
    regulator_t     sc_reg;
    clk_t           sc_clk;
};

...

    error = regulator_get_by_ofw_property(dev, node, "vled-supply",
        &sc->sc_reg);
    if (error == 0) {
        error = regulator_enable(sc->sc_reg);
        if (error != 0) {
            device_printf(dev, "cannot enable regulator: %d\n",
                error);
            goto fail;
        }
    } else if (error != ENOENT) {
        device_printf(dev, "regulator lookup failed: %d\n", error);
        goto fail;
    }

    error = clk_get_by_ofw_index(dev, node, 0, &sc->sc_clk);
    if (error == 0) {
        error = clk_enable(sc->sc_clk);
        if (error != 0) {
            device_printf(dev, "cannot enable clock: %d\n", error);
            goto fail;
        }
    } else if (error != ENOENT) {
        device_printf(dev, "clock lookup failed: %d\n", error);
        goto fail;
    }
```

Detach releases them in reverse order:

```c
    if (sc->sc_clk != NULL) {
        clk_disable(sc->sc_clk);
        clk_release(sc->sc_clk);
    }
    if (sc->sc_reg != NULL) {
        regulator_disable(sc->sc_reg);
        regulator_release(sc->sc_reg);
    }
```

The `ENOENT` check is important. If the DT does not declare a regulator or a clock, `regulator_get_by_ofw_property` and `clk_get_by_ofw_index` return `ENOENT`. A driver that supports multiple boards, some with and some without a dedicated rail, treats `ENOENT` as "not needed here" rather than as a fatal error.

### Pass Four: Pinmux Setup

On SoCs where GPIO pins can be repurposed as UART, SPI, I2C, or other functions, the pin multiplexing controller has to be programmed before the driver can use a pin. FreeBSD handles this through the `pinctrl` framework at `/usr/src/sys/dev/fdt/fdt_pinctrl.h`. A Device Tree node requesting a specific configuration uses the `pinctrl-names` and `pinctrl-N` properties:

```dts
edled0: edled@0 {
    compatible = "example,edled";
    pinctrl-names = "default";
    pinctrl-0 = <&edled_pins>;
    ...
};

&pinctrl {
    edled_pins: edled_pins {
        brcm,pins = <18>;
        brcm,function = <1>;  /* GPIO output */
    };
};
```

In attach, call:

```c
fdt_pinctrl_configure_by_name(dev, "default");
```

before any pin access. The framework walks the `pinctrl-0` handle, finds the referenced node, and applies its settings through the SoC-specific pinctrl driver.

The LED example does not strictly need pinmux because the Broadcom GPIO driver configures pins as part of `gpio_pin_setflags`, but on OMAP, Allwinner, and many other SoCs it is essential. Include the pattern in your teaching template so readers see where it fits.

### Pass Five: Style and Naming Audit

Give the final source a slow read. Things to check:

- **Consistent naming.** All functions start with `edled_`, all fields with `sc_`, all constants in uppercase. A stranger reading the source should never wonder which driver a symbol belongs to.
- **No dead code.** Remove any `device_printf` or stub function that was useful during bring-up and has no production purpose.
- **No magic numbers.** If you write `sc->sc_on = 0` in ten places, define an enum or at least a `#define EDLED_OFF 0`.
- **Short comments only where the code's intent is not obvious.** Attempting to add a docstring to every function tends to clutter FreeBSD sources; brevity is the house style.
- **Correct include order.** By convention, `<sys/param.h>` comes first, followed by other `<sys/...>` headers, then `<machine/...>`, then subsystem-specific headers like `<dev/ofw/...>`.
- **Line length.** Stick to 80-column lines. Long function calls use the FreeBSD indent style for continuation.
- **License header.** Every FreeBSD source file opens with the project's BSD-style license block. For out-of-tree drivers, include your own copyright and license notice.

### Pass Six: Static Analysis

Run the compiler with warnings turned up:

```console
$ make CFLAGS="-Wall -Wextra -Werror"
```

Fix every warning. Warnings indicate either a real bug or an unclear piece of code. In both cases the fix improves the driver.

Consider running scan-build:

```console
$ scan-build make
```

`scan-build` is part of the llvm clang analyzer. It catches null-pointer dereferences and use-after-free bugs the compiler misses.

### Pass Seven: Documentation

A driver is not finished until it can be understood without reading the code. Write a one-page README that covers:

- What the driver does.
- Which DT binding it expects.
- Which module dependencies it has.
- Any known limitations or board-specific notes.
- How to build, load, and exercise it.

Include a short man page for the companion-material tree, too. Even a stub `edled(4)` page is valuable; you can refine it later.

### Packaging and Distribution

Out-of-tree drivers live in a few canonical places:

- As an unofficial `devel/` port, for users to install on top of FreeBSD.
- As a GitHub repository following the FreeBSD project's conventional layout.
- As a `.tar.gz` archive posted alongside a README and an INSTALL file.

The FreeBSD ports tree welcomes driver packages that are known to be stable. Filing a `devel/edled-kmod` port once the driver has some miles on it is a reasonable goal.

If your driver is general enough to benefit other users, consider contributing it upstream. The review process is careful but positive, and the `freebsd-drivers@freebsd.org` mailing list is the natural starting point.

### Reviewing Against Real FreeBSD Drivers

Once `edled` is tight, compare it against `/usr/src/sys/dev/gpio/gpioled_fdt.c`, which is the driver that inspired the example. The real driver is a bit larger because it supports multiple LEDs per parent node, but its overall shape matches yours. Note how it:

- Uses `for (child = OF_child(leds); child != 0; child = OF_peer(child))` to iterate DT children.
- Calls `OF_getprop_alloc` to read a variable-length label string.
- Registers with both `simplebus` and `ofwbus` through `DRIVER_MODULE`.
- Declares its DT binding through `SIMPLEBUS_PNP_INFO` so device-id matching works with `devmatch(8)`.

Reading real drivers in detail after finishing your own is one of the most productive things you can do in this field. You will find techniques you have never seen, and you will recognize patterns you now understand from the inside.

### Wrapping Up This Section

Section 8 walked through the finishing passes every driver needs. Error paths tightened, locking corrected, power and clock handling made explicit, pinmux considered, style audited, analysis run, documentation written. What you have now is no longer an experiment; it is a driver you could hand to someone else with a straight face.

At this point the chapter's technical material is complete. The remaining pieces are the hands-on labs that let you run everything yourself, the challenge exercises that stretch what you have learned, a short list of common mistakes to watch for, and the wrap-up that closes the loop back to the broader arc of the book.

## 9. Reading Real FreeBSD FDT Drivers

We have built `fdthello` and `edled`, two drivers that exist for the purpose of teaching. They are real in the sense that you can load them on a FreeBSD system and see them attach, but they are small, and they do not carry the accumulated wisdom of drivers that have lived in the tree for years and been touched by dozens of contributors. To complete your apprenticeship as an FDT driver writer, you need to read drivers that did not start as teaching material.

This section picks a few drivers out of `/usr/src/sys` and walks through what they show. The goal is not to have you memorize their source, it is to build the habit of reading real code as a primary learning source. The book's teaching examples will fade from memory within months; real driver reading is a skill you can use for the rest of your career.

### gpioled_fdt.c: A Close Relative of edled

Our `edled` driver was deliberately modeled on `/usr/src/sys/dev/gpio/gpioled_fdt.c`. Reading the real thing with `edled` in mind makes the comparison instructive. The real driver is about 150 lines, almost the same size as ours, but handles several details we chose to simplify.

The driver's compatibility table lists a single entry:

```c
static struct ofw_compat_data compat_data[] = {
    {"gpio-leds", 1},
    {NULL,        0}
};
```

Notice that `gpio-leds` is an unprefixed string. This reflects a longstanding community binding that predates the current convention of vendor prefixes. New bindings should always use a prefix, but established ones remain as they are for the sake of compatibility.

The probe is almost identical to ours:

```c
static int
gpioled_fdt_probe(device_t dev)
{
    if (!ofw_bus_status_okay(dev))
        return (ENXIO);
    if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
        return (ENXIO);
    device_set_desc(dev, "OFW GPIO LEDs");
    return (BUS_PROBE_DEFAULT);
}
```

The attach function is where the drivers diverge. `gpioled_fdt.c` supports multiple LEDs per DT node, following the `gpio-leds` binding that lists each LED as a child node of a single parent. The pattern is:

```c
static int
gpioled_fdt_attach(device_t dev)
{
    struct gpioled_softc *sc = device_get_softc(dev);
    phandle_t leds, child;
    ...

    leds = ofw_bus_get_node(dev);
    sc->sc_dev = dev;
    sc->sc_nleds = 0;

    for (child = OF_child(leds); child != 0; child = OF_peer(child)) {
        if (!OF_hasprop(child, "gpios"))
            continue;
        ...
    }
}
```

`OF_child` and `OF_peer` are the classic walkers for iterating Device Tree children. `OF_child(parent)` returns the first child or zero. `OF_peer(node)` returns the next sibling or zero when you hit the end. This two-line iteration idiom is the backbone of every driver that processes a variable number of sub-entries.

Inside the loop, the driver reads per-LED properties:

```c
    name = NULL;
    len = OF_getprop_alloc(child, "label", (void **)&name);
    if (len <= 0) {
        OF_prop_free(name);
        len = OF_getprop_alloc(child, "name", (void **)&name);
    }
```

`OF_getprop_alloc` allocates memory for a property and returns the length. The caller is responsible for releasing the buffer with `OF_prop_free`. Notice the fallback: if there is no `label` property, the driver uses the node's `name` instead. This kind of graceful fallback is worth noting; it makes drivers more tolerant of binding variations.

Each GPIO is then acquired through a `gpio_pin_get_by_ofw_idx` call with an explicit index of zero, since each LED's own `gpios` property is indexed starting from zero within that child's scope. The driver calls `gpio_pin_setflags(pin, GPIO_PIN_OUTPUT)` and registers each LED with the `led(4)` framework so it shows up as `/dev/led/<name>` in user space.

### DRIVER_MODULE Registration

The module registration lines look like this:

```c
static driver_t gpioled_driver = {
    "gpioled",
    gpioled_methods,
    sizeof(struct gpioled_softc)
};

DRIVER_MODULE(gpioled, ofwbus,    gpioled_driver, 0, 0);
DRIVER_MODULE(gpioled, simplebus, gpioled_driver, 0, 0);
MODULE_VERSION(gpioled, 1);
MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1);
SIMPLEBUS_PNP_INFO(compat_data);
```

Two additions stand out. `MODULE_DEPEND(gpioled, gpiobus, 1, 1, 1)` we already saw. The new line is `SIMPLEBUS_PNP_INFO(compat_data)`. This macro expands into a set of module metadata that tools like `devmatch(8)` use to decide which driver to auto-load for a given DT node. The argument is the same `compat_data` table the probe uses, so there is only one source of truth.

When you write production-grade drivers, include `SIMPLEBUS_PNP_INFO` so that auto-loading works. Without it, your driver will not be picked up automatically and the user has to add it to `loader.conf` explicitly.

### What to Take Away from gpioled_fdt.c

Read it alongside `edled` and you will see:

- How to iterate multiple children in a DT node.
- How to fall back between property names.
- How to use `OF_getprop_alloc` and `OF_prop_free` for variable-length strings.
- How to register with the `led(4)` framework as well as with Newbus.
- How to declare PNP info for auto-matching.

These are five patterns that appear over and over in FreeBSD drivers. Having seen them once in a real source file, you will recognize them instantly in the next driver you open.

### bcm2835_gpio.c: A Bus Provider

`edled` and `gpioled_fdt.c` are GPIO consumers. They *use* GPIO pins provided by another driver. The driver that *provides* those pins on the Raspberry Pi is `/usr/src/sys/arm/broadcom/bcm2835/bcm2835_gpio.c`. Reading it shows the other side of the transaction.

The driver's attach does considerably more than ours:

- Allocates the MMIO resource for the GPIO controller's register block.
- Allocates all of the interrupt resources (the BCM2835 routes two interrupt lines per bank).
- Initializes a mutex and a driver data structure that tracks per-pin state.
- Registers a GPIO bus child so consumers can attach to it.
- Registers pinmux functions for all the multiplex-capable pins.

The most important thing to notice, from our perspective as consumers, is the way it exposes itself. Deep in the attach:

```c
if ((sc->sc_busdev = gpiobus_attach_bus(dev)) == NULL) {
    device_printf(dev, "could not attach GPIO bus\n");
    return (ENXIO);
}
```

`gpiobus_attach_bus(dev)` is what creates the gpiobus instance that consumers subsequently probe against. Without this call, no consumer driver could ever acquire a pin, because there would be no bus to resolve phandles against.

At the bottom of the file, `DEVMETHOD` entries map GPIO bus methods to the driver's own functions:

```c
DEVMETHOD(gpio_pin_set,    bcm_gpio_pin_set),
DEVMETHOD(gpio_pin_get,    bcm_gpio_pin_get),
DEVMETHOD(gpio_pin_toggle, bcm_gpio_pin_toggle),
DEVMETHOD(gpio_pin_getcaps, bcm_gpio_pin_getcaps),
DEVMETHOD(gpio_pin_setflags, bcm_gpio_pin_setflags),
```

These are the functions our consumer ends up calling, indirectly, every time it does `gpio_pin_set_active`. The consumer API in `gpiobusvar.h` is a thin layer over this DEVMETHOD table.

### ofw_iicbus.c: A Bus That Is Both Parent and Child

Many I2C controllers are attached as children of `simplebus` (their DT parent) and then themselves act as the parent of individual I2C device drivers. `/usr/src/sys/dev/iicbus/ofw_iicbus.c` is a good example to skim. It shows how a driver can both:

- Probe and attach on its own DT node like any FDT driver.
- Register its own child devices from the DT children of its node.

The iteration over children uses the same `OF_child`/`OF_peer` idiom, but for each child it creates a new Newbus device with `device_add_child`, sets up its own OFW metadata, and relies on Newbus to run the probe for a driver that can handle it (for example, a temperature sensor or an EEPROM).

Reading this driver gives you a feel for how bus-and-consumer relationships cascade. The FDT is a tree; so is the Newbus hierarchy. A driver in the middle of the tree plays both parent and child roles.

### ofw_bus_subr.c: The Helpers Themselves

When you find yourself constantly looking up what exactly `ofw_bus_search_compatible` does, the answer is in `/usr/src/sys/dev/ofw/ofw_bus_subr.c`. Reading the helpers you call is an underrated way to understand what your driver is really doing.

A short tour of the helpers you will meet most often:

- `ofw_bus_is_compatible(dev, str)` returns true if the node's `compatible` list contains `str`. It iterates through all entries in the compatible list, not just the first.
- `ofw_bus_search_compatible(dev, table)` walks the same compatible list against the entries in a `struct ofw_compat_data` table and returns a pointer to the matching entry (or to a sentinel).
- `ofw_bus_status_okay(dev)` checks the `status` property. Missing status defaults to okay; `"okay"` or `"ok"` is okay; anything else (`"disabled"`, `"fail"`) is not.
- `ofw_bus_has_prop(dev, prop)` tests existence without reading.
- `ofw_bus_parse_xref_list_alloc` and related helpers read phandle reference lists (the format used by `clocks`, `resets`, `gpios`, etc.) and return an allocated array the caller must free.

Reading these helpers confirms that there is nothing magical in the system. They are readable C code that walks the same blob the kernel parsed at boot.

### simplebus.c: The Driver That Runs Your Driver

If you want to understand why your FDT driver actually gets probed, open `/usr/src/sys/dev/fdt/simplebus.c`. The probe and attach of `simplebus` itself are short and, once you know what to look for, surprisingly concrete.

`simplebus_probe` checks that the node has `compatible = "simple-bus"` (or is an SoC-class node) and that it has no parent-specific peculiarities. `simplebus_attach` then walks the node's children, creates a new device for each, parses each child's `reg` and interrupts, and calls `device_probe_and_attach` on the new device. That last call is what kicks off your driver's probe.

The key lines are something like:

```c
for (node = OF_child(parent); node > 0; node = OF_peer(node)) {
    ...
    child = simplebus_add_device(bus, node, 0, NULL, -1, NULL);
    if (child == NULL)
        continue;
}
```

This iteration is what turns a tree of DT nodes into a tree of Newbus devices. Every FDT driver in existence enters the system through this loop.

Reading `simplebus.c` demystifies the "why does my driver get called" question. You see, in plain C, exactly how the kernel walks from the blob in memory to a call into your driver's probe. If you ever need to troubleshoot why your probe is not running, the first step is to instrument this file with `device_printf` at the right spot.

### A Survey of Drivers Worth Reading

Beyond the specific drivers above, here is a short list of FDT drivers in `/usr/src/sys` that are worth your time as study subjects. Each is representative of a pattern you are likely to encounter.

- `/usr/src/sys/dev/gpio/gpioiic.c`: A driver that implements an I2C bus on top of GPIO pins. Shows bit-banging patterns.
- `/usr/src/sys/dev/gpio/gpiokeys.c`: Consumes GPIO inputs as a keyboard. Shows interrupt handling from GPIOs.
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: A platform-independent UART driver with FDT hooks. Shows how a generic driver can accept FDT attach paths alongside other bus types.
- `/usr/src/sys/dev/sdhci/sdhci_fdt.c`: A large FDT driver for SD host controllers. Shows how production drivers handle clocks, resets, regulators, and pinmux together.
- `/usr/src/sys/arm/allwinner/aw_gpio.c`: A complete modern GPIO controller for the Allwinner SoC family. Worth comparing with `bcm2835_gpio.c` to see two takes on the same problem.
- `/usr/src/sys/arm/freescale/imx/imx_gpio.c`: The i.MX6/7/8 GPIO driver, another well-maintained reference.
- `/usr/src/sys/dev/extres/syscon/syscon.c`: A "system controller" pseudo-bus that exposes shared register blocks to multiple drivers. Useful to see how FreeBSD handles DT patterns that do not fit cleanly into "one node, one driver."

You do not need to read these cover to cover. A healthy habit is to pick one every week or two, skim the structure, and then focus on whichever small detail catches your interest. Over time, these readings will build a library in your head of real code you have seen work.

### Using grep as a Study Tool

When you find a new function in a driver you are reading and you are not sure what it does, a good first step is:

```console
$ grep -rn "function_name" /usr/src/sys | head
```

This shows every place the function is defined and called. Often the declaration in a header is enough, combined with two or three representative call sites, to understand what the function is for. This beats searching the web, which returns outdated documentation and half-remembered forum posts.

For a particular DT binding, the same trick works:

```console
$ grep -rn '"gpio-leds"' /usr/src/sys
```

The output tells you every file that references that compat string, including the driver that implements it, overlays that use it, and tests that exercise it.

### Wrapping Up This Section

Section 9 gave you a reading list and a method. Real FreeBSD drivers are the richest resource available, and learning to read them effectively is a skill as important as writing your own. The drivers on the list above show the patterns our teaching examples simplified. They are the ones to reach for when you are stuck, when you need inspiration, and when you want to know how a production-quality driver handles the corner cases your own code has not encountered yet.

The remaining material in the chapter is practical: the labs you can run on your own hardware, the challenge exercises that stretch the teaching driver, the troubleshooting reference, and the closing bridge to the next chapter.

## 10. Interrupt Plumbing in FDT-Based Systems

We have mostly treated interrupts as opaque. In this section we open the box and look at how Device Tree describes interrupt connectivity, how FreeBSD's interrupt framework (`intrng`) consumes that description, and how a driver asks for an IRQ that will actually fire when the hardware wants attention.

The reason this subject deserves its own section is that interrupt wiring on modern SoCs can get complicated. Simple platforms have one controller, one set of lines, and a flat assignment. Complex platforms have a root controller, several subsidiary controllers that multiplex wider IRQ sources into narrower outputs, and pin-based controllers (like GPIOs-as-interrupts) whose lines chain through the multiplex tree. A driver writer who understands the chain can debug strange interrupt failures in minutes; one who does not can spend hours checking the wrong things.

### The Interrupt Tree

The Device Tree expresses interrupts as a separate logical tree that runs alongside the main address tree. Each node has an interrupt parent (its controller), and the tree ascends through controllers until it reaches the root controller that the CPU actually takes exceptions from.

Three properties describe the tree:

- **`interrupts`**: The interrupt description for this node. Its format depends on the controller it attaches to.
- **`interrupt-parent`**: A phandle to the controller, if it is not the closest ancestor that is already an interrupt controller.
- **`interrupt-controller`**: An empty-valued property that marks a node as a controller. A consumer's `interrupt-parent` must point at such a node.

An example fragment:

```dts
&soc {
    gic: interrupt-controller@10000 {
        compatible = "arm,gic-v3";
        interrupt-controller;
        #interrupt-cells = <3>;
        reg = <0x10000 0x1000>, <0x11000 0x20000>;
    };

    uart0: serial@20000 {
        compatible = "arm,pl011";
        reg = <0x20000 0x100>;
        interrupts = <0 42 4>;
        interrupt-parent = <&gic>;
    };
};
```

The GIC (Generic Interrupt Controller, the standard arm64 root controller) declares `#interrupt-cells = <3>`. Every device that attaches to it must provide three cells in its `interrupts` property. For a GICv3, the three cells are *type, number, flags*: `<0 42 4>` means "shared peripheral interrupt 42, level-triggered, high."

If `interrupt-parent` is omitted, the parent is the nearest ancestor with an `interrupt-parent` or with the `interrupt-controller` property set. This chain can be non-obvious when drivers sit several levels deep.

### Interrupt-Parent Chaining

Consider a more realistic example. On a BCM2711 (Raspberry Pi 4), the GPIO controller is its own interrupt controller: it aggregates interrupts from individual pins into a handful of outputs that feed into the GIC. A button wired to a GPIO pin appears in the DT like this:

```dts
&gpio {
    button_pins: button_pins {
        brcm,pins = <23>;
        brcm,function = <0>;       /* GPIO input */
        brcm,pull = <2>;           /* pull-up */
    };
};

button_node: button {
    compatible = "gpio-keys";
    pinctrl-names = "default";
    pinctrl-0 = <&button_pins>;
    key_enter {
        label = "enter";
        linux,code = <28>;
        gpios = <&gpio 23 0>;
        interrupt-parent = <&gpio>;
        interrupts = <23 3>;       /* edge trigger */
    };
};
```

Two properties name the parent controller: `gpios = <&gpio ...>` names the GPIO controller as the pin provider, and `interrupt-parent = <&gpio>` names the same controller as the interrupt provider. The two roles are distinct and must be stated independently.

The GPIO controller then aggregates its interrupt lines and reports them to the GIC. Inside the GPIO driver, when an interrupt arrives from the GIC, it identifies which pin fired and dispatches the event to whichever driver registered a handler for that pin's IRQ resource.

When your driver requests an interrupt for this node, FreeBSD walks the chain: button driver asks for IRQ, the GPIO controller's intrng logic assigns a virtual IRQ number, and eventually the kernel arranges for the GIC's upstream IRQ to call into the GPIO driver's dispatcher which in turn calls the button driver's handler. You do not write any of this plumbing yourself; you just request the IRQ and handle it.

### The intrng Framework

FreeBSD's `intrng` (interrupt next-generation) subsystem is what unifies all of this. An interrupt controller implements the `pic_*` methods:

```c
static device_method_t gpio_methods[] = {
    ...
    DEVMETHOD(pic_map_intr,      gpio_pic_map_intr),
    DEVMETHOD(pic_setup_intr,    gpio_pic_setup_intr),
    DEVMETHOD(pic_teardown_intr, gpio_pic_teardown_intr),
    DEVMETHOD(pic_enable_intr,   gpio_pic_enable_intr),
    DEVMETHOD(pic_disable_intr,  gpio_pic_disable_intr),
    ...
};
```

`pic_map_intr` is the one that reads the DT property and returns an internal representation of the IRQ. `pic_setup_intr` attaches a handler. The remaining methods control masking and acknowledgement.

A consumer driver never calls these directly. It calls `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)`, and Newbus, together with the OFW resource code, walks the DT and the intrng framework to resolve the IRQ.

### Requesting an IRQ in Practice

The full shape of interrupt handling in an FDT driver looks like this:

```c
struct driver_softc {
    ...
    struct resource *irq_res;
    void *irq_cookie;
    int irq_rid;
};

static int
driver_attach(device_t dev)
{
    struct driver_softc *sc = device_get_softc(dev);
    int error;

    sc->irq_rid = 0;
    sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
        &sc->irq_rid, RF_ACTIVE);
    if (sc->irq_res == NULL) {
        device_printf(dev, "cannot allocate IRQ\n");
        return (ENXIO);
    }

    error = bus_setup_intr(dev, sc->irq_res,
        INTR_TYPE_MISC | INTR_MPSAFE,
        NULL, driver_intr, sc, &sc->irq_cookie);
    if (error != 0) {
        device_printf(dev, "cannot setup interrupt: %d\n", error);
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
        return (error);
    }
    ...
}
```

The RID for interrupts starts at zero and increments for each IRQ in the node's `interrupts` list. A node with two IRQs would use RIDs 0 and 1 in succession.

`bus_setup_intr` registers the handler. The fourth argument is a filter function (runs in interrupt context); the fifth is a thread handler (runs in a dedicated kernel thread). You pass `NULL` for the one you are not using. The `INTR_MPSAFE` flag tells the framework that the handler does not require the Giant lock.

Teardown in detach:

```c
static int
driver_detach(device_t dev)
{
    struct driver_softc *sc = device_get_softc(dev);

    if (sc->irq_cookie != NULL)
        bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
    if (sc->irq_res != NULL)
        bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
            sc->irq_res);
    return (0);
}
```

Failure to `bus_teardown_intr` is a classic unload bug: the IRQ stays connected to freed memory, and the next fire time the kernel panics.

### Filter Versus Thread Handlers

The distinction between filter and thread handlers is one of the topics that new kernel developers often find confusing. A short primer helps.

A *filter* runs in the context of the interrupt itself, at high IPL, with strict constraints on what it can call. It cannot sleep, cannot allocate memory, cannot acquire a regular sleep-capable mutex. It can only acquire spin mutexes. Its purpose is to decide whether the interrupt is for this device, acknowledge the hardware condition, and either handle the event trivially or schedule a thread handler to do the rest.

A *thread* handler runs in a dedicated kernel thread. It can sleep, allocate, and take sleep-capable locks. Many drivers do all their work in a thread handler and leave the filter empty.

For a driver as simple as `edled`, we never handle an interrupt. If we extended it to handle a pushbutton, we would start with a thread handler and only introduce a filter when profiling showed it necessary.

### Edge Versus Level Triggering

The third cell of a GIC `interrupts` triple is the trigger type. Common values:

- `1`: rising edge
- `2`: falling edge
- `3`: either edge
- `4`: active high level
- `8`: active low level

GPIO-as-interrupt nodes use a different cell count (typically two) and a similar encoding. The choice matters. Edge-triggered interrupts fire once per transition; level-triggered interrupts keep firing as long as the line is asserted. A driver that acknowledges too late on a level-triggered line can end up in an interrupt storm.

The DT binding documentation for each controller specifies the exact cell count and flag semantics. When in doubt, grep in `/usr/src/sys/contrib/device-tree/Bindings/` for the controller family.

### Debugging Interrupts That Do Not Fire

The symptoms of misconfigured interrupts are usually clear: the hardware works the first time, and subsequent interrupts never come; or the system boots but the driver's handler never runs.

Checks, in order:

1. **Does `vmstat -i` show the interrupt being counted?** If yes, the hardware is asserting but the driver is not acknowledging it. Check your filter or thread handler.
2. **Does the DT's `interrupts` match the controller's expected format?** Cell count and values are the usual culprits.
3. **Is `interrupt-parent` pointing at the right controller?** If a pin-based controller is the source but the DT says the GIC, the request will fail because the GIC's cell format does not match.
4. **Did `bus_setup_intr` return zero?** If not, read the error code. `EINVAL` usually means the IRQ resource was not fully mapped; `ENOENT` means the IRQ number is not claimed by any controller.

The `intr_event_show` DTrace probe can help in advanced debugging, but the four-step check above catches most problems without DTrace.

### Real Example: gpiokeys.c

`/usr/src/sys/dev/gpio/gpiokeys.c` is worth reading as a working example of a GPIO-consuming driver that uses interrupts. For each child node, it acquires a pin, configures it as an input, and hooks an interrupt through `gpio_alloc_intr_resource` and `bus_setup_intr`. The filter is very short: it just calls `taskqueue_enqueue` on a work item. The actual key processing runs on the kernel's taskqueue, not in interrupt context.

This is a clean pattern for a small interrupt-driven driver. A filter that just signals, a worker that does the job. When you need to implement something similar for a custom board peripheral, the gpiokeys driver is a good template.

### Wrapping Up This Section

Section 10 unwrapped the interrupt machinery that our earlier examples kept hidden. You now know how the Device Tree describes interrupt connectivity, how FreeBSD's intrng resolves an IRQ request into a concrete handler registration, how filter and thread handlers divide work, and how to debug the class of failures that misconfigured interrupts produce.

The chapter's technical coverage is now truly complete. The labs, exercises, and troubleshooting material follow.

## Hands-on Labs

Nothing in this chapter will stick without actually running it. The labs that follow are arranged in order of difficulty. Lab 1 is a warm-up you can complete on any FreeBSD system with kernel sources, even a generic amd64 laptop in QEMU. Lab 2 introduces overlays, which means you will want an arm64 target, either real (Raspberry Pi 4, BeagleBone, Pine64) or emulated. Lab 3 is a debugging exercise in which you will deliberately break a DT and learn the symptoms. Lab 4 builds the full `edled` driver and drives an LED through it.

All lab files are published under `examples/part-07/ch32-fdt-embedded/`. Each lab has its own subdirectory with a `README.md` and all the sources you need. The following text is self-contained so you can work directly from the book, but the example tree is there as a safety net when you want to diff your work against a known-good reference.

### Lab 1: Build and Load the fdthello Skeleton

**Goal:** Compile the minimal FDT-aware driver from Section 4, load it on a running FreeBSD system, and confirm it registers with the kernel even when no matching DT node exists.

**What you will learn:**

- How a kernel module Makefile works.
- How `kldload` and `kldunload` interact with module registration.
- How Newbus runs probes the moment a driver is introduced.

**Steps:**

1. Create a scratch directory named `lab01-fdthello` on a FreeBSD 14.3 system with kernel sources installed.

2. Save the full `fdthello.c` source listing from Section 4 in that directory.

3. Save a Makefile with the following content:

   ```
   KMOD=   fdthello
   SRCS=   fdthello.c

   SYSDIR?= /usr/src/sys

   .include <bsd.kmod.mk>
   ```

4. Build the module:

   ```
   $ make
   ```

   A clean build produces `fdthello.ko` and `fdthello.ko.debug` in the current directory.

5. Load the module:

   ```
   # kldload ./fdthello.ko
   ```

   On a system without a matching DT node, no probe succeeds. That is expected. The module is resident, but no `fdthello0` device appears.

6. Verify that the module is loaded:

   ```
   # kldstat -m fdthello
   ```

7. Unload:

   ```
   # kldunload fdthello
   ```

**Expected outcome:**

The build completes without warnings. The module loads and unloads cleanly. `kldstat` shows `fdthello.ko` between the two steps.

**If you hit a snag:**

- **`kldload` reports "module not found":** Make sure you passed `./fdthello.ko` with the leading `./` so `kldload` does not try the system module path.
- **The build fails with "no such file `bsd.kmod.mk`":** Install `/usr/src` via `pkgbase` or checkout from git.
- **The build fails because kernel symbols are missing:** Confirm `/usr/src/sys` matches the running kernel's version. A mismatch between running kernel and source tree is the usual cause.

The starter files live at `examples/part-07/ch32-fdt-embedded/lab01-fdthello/`.

### Lab 2: Build and Deploy an Overlay

**Goal:** Add a DT node that matches the `fdthello` driver, deploy it as an overlay on an arm64 FreeBSD board, and watch the driver attach.

**What you will learn:**

- How to write an overlay source file.
- How `dtc -@` produces overlay-ready `.dtbo` output.
- How the FreeBSD loader applies overlays through `fdt_overlays` in `loader.conf`.
- How to verify that an overlay landed correctly.

**Steps:**

1. On a running arm64 FreeBSD system (Raspberry Pi 4 is the reference target), install `dtc`:

   ```
   # pkg install dtc
   ```

2. In a scratch directory, save the following overlay source as `fdthello-overlay.dts`:

   ```
   /dts-v1/;
   /plugin/;

   / {
       compatible = "brcm,bcm2711";

       fragment@0 {
           target-path = "/soc";
           __overlay__ {
               hello@20000 {
                   compatible = "freebsd,fdthello";
                   reg = <0x20000 0x100>;
                   status = "okay";
               };
           };
       };
   };
   ```

3. Compile the overlay:

   ```
   $ dtc -I dts -O dtb -@ -o fdthello.dtbo fdthello-overlay.dts
   ```

4. Copy the result to the loader's overlay directory:

   ```
   # cp fdthello.dtbo /boot/dtb/overlays/
   ```

5. Edit `/boot/loader.conf` (create it if missing) to include:

   ```
   fdt_overlays="fdthello"
   ```

6. Copy your `fdthello.ko` built in Lab 1 to `/boot/modules/`:

   ```
   # cp /path/to/fdthello.ko /boot/modules/
   ```

7. Ensure `fdthello_load="YES"` is in `/boot/loader.conf`:

   ```
   fdthello_load="YES"
   ```

8. Reboot:

   ```
   # reboot
   ```

9. After reboot, confirm:

   ```
   # dmesg | grep fdthello
   fdthello0: <FDT Hello Example> on simplebus0
   fdthello0: attached, node phandle 0x...
   ```

**Expected outcome:**

The driver attaches at boot and its message shows up in `dmesg`. `ofwdump -p /soc/hello@20000` prints the node properties.

**If you hit a snag:**

- **Loader prints "error loading overlay":** Usually the `.dtbo` file is missing or in the wrong directory. Confirm it is under `/boot/dtb/overlays/` and named with a `.dtbo` extension.
- **The driver does not attach:** Use the checklist from Section 6: node present, status okay, compatible exact, parent `simplebus`.
- **You are on a non-Pi board:** Change the top-level `compatible` in the overlay to match your board's base compatible. `ofwdump -p /` shows the current value.

The starter files live at `examples/part-07/ch32-fdt-embedded/lab02-overlay/`.

### Lab 3: Debug a Broken Device Tree

**Goal:** Given a deliberately broken overlay, identify three distinct failure modes and fix each one.

**What you will learn:**

- How to use `dtc`, `fdtdump`, and `ofwdump` to read a blob.
- How to correlate the tree content with kernel probe behavior.
- How to use `device_printf` breadcrumbs to diagnose a probe mismatch.

**Steps:**

1. Copy the following broken overlay into `lab03-broken.dts`:

   ```
   /dts-v1/;
   /plugin/;

   / {
       compatible = "brcm,bcm2711";

       fragment@0 {
           target-path = "/soc";
           __overlay__ {
               hello@20000 {
                   compatible = "free-bsd,fdthello";
                   reg = <0x20000 0x100>;
                   status = "disabled";
               };
           };
       };

       fragment@1 {
           target-path = "/soc";
           __overlay__ {
               hello@30000 {
                   compatible = "freebsd,fdthello";
                   reg = <0x30000>;
                   status = "okay";
               };
           };
       };
   };
   ```

2. Compile and install the overlay:

   ```
   $ dtc -I dts -O dtb -@ -o lab03-broken.dtbo lab03-broken.dts
   # cp lab03-broken.dtbo /boot/dtb/overlays/
   ```

3. Edit `/boot/loader.conf` to load this overlay instead of `fdthello`:

   ```
   fdt_overlays="lab03-broken"
   ```

4. Reboot. Observe:

   - No `fdthello0` device attaches.
   - `dmesg` may be silent or may show an FDT parsing warning about `hello@30000`.

5. Diagnose. Use the following techniques in order:

   **a) Compare the compatible strings:**

   ```
   # ofwdump -P compatible /soc/hello@20000
   # ofwdump -P compatible /soc/hello@30000
   ```

   The first prints `free-bsd,fdthello`, which the driver does not match. The hyphen after `free` is the typo. Fix is to correct the string to `freebsd,fdthello`.

   **b) Check the status:**

   ```
   # ofwdump -P status /soc/hello@20000
   ```

   Returns `disabled`. Even if the compatible were right, the driver would still skip this node. Fix is to set `status = "okay"`.

   **c) Check the reg property:**

   ```
   # ofwdump -P reg /soc/hello@30000
   ```

   Notice that `reg` has only one cell where the parent expects address plus size. Under parents with `#address-cells = <1>` and `#size-cells = <1>`, `reg` must have two cells. The driver attaches, but if it ever allocates the resource, it will misread the size as whatever garbage follows. Fix is `reg = <0x30000 0x100>;`.

6. Apply the fixes, recompile, reinstall the overlay, and reboot. The driver should attach to one or both of the hello nodes.

**Expected outcome:**

After all three fixes, `dmesg | grep fdthello` shows two attached devices, `hello@20000` and `hello@30000`, each reported through `simplebus`.

**If you hit a snag:**

- **ofwdump reports "no such node":** The overlay did not apply. Check the loader's output for an overlay load message, check the `.dtbo` is where the loader expects it.
- **Only one hello device attaches:** One of the three bugs is still present.
- **The kernel panics:** You are almost certainly reading past the end of `reg` because its cell count is still wrong. Revert to the known-good overlay while you diagnose.

The starter files live at `examples/part-07/ch32-fdt-embedded/lab03-debug-broken/`.

### Lab 4: Build the edled Driver End to End

**Goal:** Construct the full `edled` driver from Section 7, compile it, attach it through a DT overlay to GPIO18 on a Raspberry Pi 4, and toggle the LED from user space.

**What you will learn:**

- How to integrate GPIO resource acquisition into an FDT driver.
- How to expose a sysctl that drives hardware.
- How to verify the driver on a running system using `dmesg`, `sysctl`, `ofwdump`, and `devinfo -r`.

**Steps:**

1. Wire an LED between GPIO18 (pin 12 on the header) and ground through a 330-ohm resistor. If you do not have physical hardware, you can still proceed; the driver attaches and toggles its logical state, but there is nothing to light up.

2. In a scratch directory, save:

   - `edled.c` from the full listing in Section 7.
   - `Makefile` with `KMOD=edled`, `SRCS=edled.c`, `SYSDIR?=/usr/src/sys`, and `.include <bsd.kmod.mk>`.
   - `edled.dts` overlay source from Section 7.

3. Build the module:

   ```
   $ make
   ```

4. Compile the overlay:

   ```
   $ dtc -I dts -O dtb -@ -o edled.dtbo edled.dts
   ```

5. Install:

   ```
   # cp edled.ko /boot/modules/
   # cp edled.dtbo /boot/dtb/overlays/
   ```

6. Edit `/boot/loader.conf`:

   ```
   edled_load="YES"
   fdt_overlays="edled"
   ```

7. Reboot.

8. Confirm attach:

   ```
   # dmesg | grep edled
   edled0: <Example embedded LED> on simplebus0
   edled0: attached, GPIO pin acquired, state=0
   ```

9. Exercise the sysctl:

   ```
   # sysctl dev.edled.0.state
   dev.edled.0.state: 0

   # sysctl dev.edled.0.state=1
   dev.edled.0.state: 0 -> 1
   ```

   The LED lights. Read back and confirm:

   ```
   # sysctl dev.edled.0.state
   dev.edled.0.state: 1
   ```

10. Turn the LED off and unload the driver:

    ```
    # sysctl dev.edled.0.state=0
    # kldunload edled
    ```

**Expected outcome:**

The driver loads, attaches, toggles the LED, and unloads without leaving resources in use. `gpioctl -l` shows the pin returned to the unconfigured state after unload.

**If you hit a snag:**

- **dmesg shows "cannot get GPIO pin":** The GPIO controller module has not attached yet. Verify `gpiobus` is loaded: `kldstat -m gpiobus`. If it is not, `kldload gpiobus` before retrying.
- **The LED does not light:** Check the polarity. If the flag in the DT is `0` (active high), the pin drives 3.3V when active. If the LED's cathode goes to the pin, you want `1` (active low).
- **kldunload fails with EBUSY:** Some process still has `dev.edled.0` open or the driver's detach path left a resource acquired. Audit detach.

The starter files live at `examples/part-07/ch32-fdt-embedded/lab04-edled/`.

### Lab 5: Extend edled to Consume a GPIO Interrupt

**Goal:** Modify the `edled` driver from Lab 4 so that a second GPIO, configured as an input with a pull-up resistor, becomes an interrupt source. When the pin is grounded (a push button pulling it low), the handler toggles the LED.

**What you will learn:**

- How to acquire a GPIO interrupt resource through `gpio_alloc_intr_resource`.
- How to set up a thread-context handler with `bus_setup_intr`.
- How to coordinate the interrupt path and the sysctl path through shared state.
- How to tear down an interrupt handler cleanly in detach.

**Steps:**

1. Start from `edled.c` from Lab 4. Copy it to `edledi.c` in a fresh scratch directory.

2. Add a second GPIO to the softc and to the DT binding. The new DT node looks like:

   ```
   edledi0: edledi@0 {
       compatible = "example,edledi";
       status = "okay";
       leds-gpios = <&gpio 18 0>;
       button-gpios = <&gpio 23 1>;
       interrupt-parent = <&gpio>;
       interrupts = <23 3>;
   };
   ```

   The button uses GPIO 23, wired with the usual pushbutton arrangement: one leg to the pin, the other to ground, with a pull-up to 3.3V.

3. Update the compat string table to `"example,edledi"` so the driver matches the new binding.

4. In the softc, add:

   ```c
   gpio_pin_t      sc_button;
   struct resource *sc_irq;
   void            *sc_irq_cookie;
   int             sc_irq_rid;
   ```

5. In attach, after acquiring the LED pin, acquire the button pin and its interrupt:

   ```c
   error = gpio_pin_get_by_ofw_property(dev, node,
       "button-gpios", &sc->sc_button);
   if (error != 0) {
       device_printf(dev, "cannot get button pin: %d\n", error);
       goto fail;
   }

   error = gpio_pin_setflags(sc->sc_button,
       GPIO_PIN_INPUT | GPIO_PIN_PULLUP);
   if (error != 0) {
       device_printf(dev, "cannot configure button: %d\n", error);
       goto fail;
   }

   sc->sc_irq_rid = 0;
   sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
       &sc->sc_irq_rid, RF_ACTIVE);
   if (sc->sc_irq == NULL) {
       device_printf(dev, "cannot allocate IRQ\n");
       goto fail;
   }

   error = bus_setup_intr(dev, sc->sc_irq,
       INTR_TYPE_MISC | INTR_MPSAFE,
       NULL, edledi_intr, sc, &sc->sc_irq_cookie);
   if (error != 0) {
       device_printf(dev, "cannot setup interrupt: %d\n", error);
       goto fail;
   }
   ```

6. Write the interrupt handler:

   ```c
   static void
   edledi_intr(void *arg)
   {
       struct edled_softc *sc = arg;

       mtx_lock(&sc->sc_mtx);
       sc->sc_on = !sc->sc_on;
       (void)gpio_pin_set_active(sc->sc_pin, sc->sc_on);
       mtx_unlock(&sc->sc_mtx);
   }
   ```

   This is a thread handler (passed as the fifth argument to `bus_setup_intr`, with `NULL` as the fourth for the filter). It is safe to take a mutex and to call into the GPIO framework.

7. In detach, add teardown in reverse order:

   ```c
   if (sc->sc_irq_cookie != NULL)
       bus_teardown_intr(dev, sc->sc_irq, sc->sc_irq_cookie);
   if (sc->sc_irq != NULL)
       bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
           sc->sc_irq);
   if (sc->sc_button != NULL)
       gpio_pin_release(sc->sc_button);
   ```

8. Rebuild, redeploy the overlay, reboot, and press the button.

**Expected outcome:**

Each press toggles the LED. The sysctl still works for programmatic control. The driver unloads cleanly.

**If you hit a snag:**

- **Interrupt never fires:** Confirm the pull-up is actually pulling the pin high when idle; check the DT trigger cell (3 = either edge); look at `vmstat -i` to see if any IRQ is being counted for your device.
- **Repeated interrupts on a single press (bouncing):** Mechanical buttons bounce. A simple software debounce can be done by ignoring interrupts within a short window after the first. Use `sbintime()` and a state field in the softc.
- **kldunload fails with EBUSY:** You missed a `bus_teardown_intr` or a `gpio_pin_release`.

The starter files live at `examples/part-07/ch32-fdt-embedded/lab05-edledi/`.

### After the Labs

At the end of Lab 4 you have run through the whole arc of embedded driver work: source, overlay, kernel module, user-space access, teardown. The remaining sections of the chapter give you ways to stretch what you have built and a last look at common pitfalls.

## Challenge Exercises

The exercises below go beyond the guided labs. They do not include step-by-step instructions, because the point is for you to apply what you have learned to unstructured problems. If you get stuck, the reference material in Sections 5 through 8 and the real drivers under `/usr/src/sys/dev/gpio/` and `/usr/src/sys/dev/fdt/` are your strongest resources.

### Challenge 1: Multiple LEDs Per Node

Modify `edled` to accept a DT node that declares several GPIOs, like the real `gpioled_fdt.c` does. The binding should look like:

```dts
edled0: edled@0 {
    compatible = "example,edled-multi";
    led-red  = <&gpio 18 0>;
    led-amber = <&gpio 19 0>;
    led-green = <&gpio 20 0>;
};
```

Expose one sysctl per LED: `dev.edled.0.red`, `dev.edled.0.amber`, `dev.edled.0.green`. Each should behave independently.

*Hint:* Walk the DT properties in attach. For a cleaner structure, store an array of pin handles in the softc and iterate over it in both attach and detach. `gpio_pin_get_by_ofw_property` takes the property name as its third argument, so the same driver can handle different property names with a small lookup table.

### Challenge 2: Support a Blink Timer

Extend `edled` with a second sysctl `dev.edled.0.blink_ms` that, when set to a non-zero value, starts a kernel callout that toggles the pin every `blink_ms` milliseconds. Writing `0` stops the blink and leaves the LED in its current state.

*Hint:* Use `callout_init_mtx` to associate the callout with the per-softc mutex, and `callout_reset` to schedule it. Remember to `callout_drain` in detach so the system does not leave a scheduled event pointing at freed memory.

### Challenge 3: Generalize to an Arbitrary GPIO Output

Rename and generalize `edled` into `edoutput`, a driver that can drive any GPIO output line with a sysctl interface. Accept a `label` property from the DT and use it as part of the sysctl path so multiple instances do not collide. Add a `dev.edoutput.0.pulse_ms` sysctl that drives the line active for the given number of milliseconds and then returns to inactive.

*Hint:* `device_get_unit(dev)` gives you the unit number; use `device_get_nameunit(dev)` for a combined name+unit string when you need one.

### Challenge 4: Consume an Interrupt

If your board exposes a button wired to a GPIO input (or you can use a pull-up resistor and a jumper wire as a one-shot simulation), modify the driver to watch an input pin for edge transitions and log them through `device_printf`. You will need to acquire the IRQ resource with `bus_alloc_resource_any(dev, SYS_RES_IRQ, ...)`, set up the interrupt handler with `bus_setup_intr`, and release it cleanly in detach.

*Hint:* Look at `/usr/src/sys/dev/gpio/gpiokeys.c` as a reference for a driver that consumes GPIO-triggered interrupts from FDT.

### Challenge 5: Produce a Custom Device Tree for QEMU

Write a complete `.dts` for a hypothetical embedded board that includes:

- A single ARM Cortex-A53 core.
- 256 MB of RAM.
- One simplebus.
- One UART at a chosen address.
- One `edled` node under simplebus, referencing a GPIO controller.
- A minimal GPIO controller node you invent.

Compile the result, boot a FreeBSD arm64 kernel against it under QEMU with `-dtb`, and watch the driver attach. The GPIO controller will fail because nothing drives the invented hardware, but you will see the full path from DT to Newbus with your own source.

*Hint:* Use `/usr/src/sys/contrib/device-tree/src/arm64/arm/juno.dts` as a structural reference.

### Challenge 6: Port a Real Driver's DT Binding to a New Board

Pick any existing FreeBSD FDT driver (for example, `sys/dev/iicbus/pmic/act8846.c`), read its DT binding by studying the driver source, and write a complete DT fragment that would attach it on a Raspberry Pi 4. You do not need to actually run the driver; the exercise is in reading the binding from the source and producing a correct `.dtsi` fragment.

*Hint:* Read the driver's compat table, its probe, and any `of_` calls to discover which properties it expects. Vendor kernel source trees often document DT bindings in comments at the top of the file.

### Challenge 7: Hand-Write a Full DT for an Embedded Target Under QEMU

Challenge 5 invited you to write a partial DT for a hypothetical board. Challenge 7 goes further: write a complete `.dts` for a QEMU arm64 target that you define entirely yourself. Include memory, timer, a PL011 UART, a GIC, one PL061-style GPIO controller, and one instance of a peripheral of your own design. Boot an unmodified FreeBSD arm64 kernel under QEMU with your `.dtb`. Verify that the console comes up on your chosen UART and that the kernel's device probe walks the tree.

*Hint:* The `qemu-system-aarch64` command supports `-dtb` plus `-machine virt,dumpdtb=out.dtb` to emit a reference DT you can study and adapt.

### Challenge 8: Implement a Simple MMIO Peripheral Driver

Write a driver for a hypothetical MMIO peripheral you also simulate under QEMU. The peripheral exposes one 32-bit register at a fixed address. Reading the register returns a free-running counter; writing zero resets the counter. Your driver should expose one sysctl `dev.counter.0.value` that reads and writes this register. Verify that `bus_read_4` and `bus_write_4` work as expected. Simulate the hardware by writing a small QEMU device model, or by repurposing an existing simulated region whose value you can observe.

*Hint:* `bus_read_4(sc->mem, 0)` returns the u32 at offset 0 within your allocated memory resource. The bus_space(9) man page is the authoritative reference.

### After the Challenges

These exercises are intentionally open-ended. If you finish any one of them, you have internalized the material of this chapter. If you find yourself wanting more, `/usr/src/sys/dev/` has dozens of FDT drivers at all sizes. Reading a driver a week is one of the best habits an embedded FreeBSD developer can build.

## Common Mistakes and Troubleshooting

This section is a concentrated reference for the issues that most often trip up writers of FDT drivers. Everything here has already been mentioned in Sections 3 through 8, but collecting the points in one place gives you something you can skim when a particular symptom appears.

### Module Loads but No Device Attaches

Most probe failures come down to one of five causes:

1. **Typo in `compatible`.** Either in the DT source or in the driver's compat table. The string must match byte for byte.
2. **Node has `status = "disabled"`.** Either fix the base tree or write an overlay that sets status to okay.
3. **Wrong parent bus.** If the node sits under an I2C or SPI controller node, the driver must register with that controller's driver type, not with `simplebus`.
4. **Overlay did not apply.** Check the loader output at boot for an error message. Confirm the `.dtbo` is in `/boot/dtb/overlays/` and listed in `loader.conf`.
5. **Driver outranked by another driver.** Use `devinfo -r` to see which driver actually attached. Increase the probe's return value (`BUS_PROBE_DEFAULT` is the common baseline; `BUS_PROBE_SPECIFIC` signals a more exact match).

### Overlay Fails to Apply at Boot

The loader prints its attempts on the console. Watch for lines like:

```text
Loading DTB overlay 'edled' (0x1200 bytes)
```

If that line is missing, the loader either did not find the file or skipped it. Possible causes:

- Filename ends in `.dtbo` but `fdt_overlays` misspells it.
- File is in the wrong directory. The default is `/boot/dtb/overlays/`.
- Base blob and overlay disagree on the top-level `compatible`. The loader refuses to apply an overlay whose top-level compatible does not match the base.
- The `.dtbo` was compiled without `-@` and references labels the loader cannot resolve.

### Cannot Allocate Resources

If attach calls `bus_alloc_resource_any(dev, SYS_RES_MEMORY, ...)` and it returns `NULL`, the most likely causes are:

- `reg` missing or malformed in the DT node.
- Cell count mismatch between the node's `reg` and the parent's `#address-cells`/`#size-cells`.
- Another driver already claimed the same region.
- The parent bus's `ranges` does not cover the requested address.

Print the resource start and size in attach while debugging:

```c
if (sc->mem == NULL) {
    device_printf(dev, "cannot allocate memory resource (rid=%d)\n",
        sc->mem_rid);
    goto fail;
}
device_printf(dev, "memory at %#jx len %#jx\n",
    (uintmax_t)rman_get_start(sc->mem),
    (uintmax_t)rman_get_size(sc->mem));
```

### GPIO Acquisition Fails

`gpio_pin_get_by_ofw_*` returns `ENXIO` when:

- The GPIO controller referenced in the DT property has not attached yet.
- The pin number is out of range for that controller.
- The phandle in the DT is wrong.

The first cause is by far the most common. The fix is `MODULE_DEPEND(your_driver, gpiobus, 1, 1, 1)` so the dynamic loader brings up `gpiobus` first.

### Interrupt Handler Does Not Fire

If the hardware should raise an interrupt and nothing happens:

- Confirm the DT `interrupts` property is correct. The format depends on the parent interrupt controller's `#interrupt-cells`.
- Confirm `bus_alloc_resource_any(SYS_RES_IRQ, ...)` returned a valid resource.
- Confirm `bus_setup_intr` returned zero.
- Confirm your handler's return value is `FILTER_HANDLED` or `FILTER_STRAY` for a filter, or `FILTER_SCHEDULE_THREAD` if you are using a threaded handler.

Use `vmstat -i` to see whether your interrupt is being counted. If the count stays at zero, the interrupt is not even being routed to your handler.

### Unload Returns EBUSY

Detach forgot to release something. Walk your attach carefully and confirm every `_get_` has a matching `_release_` call in detach. Common culprits are:

- GPIO pins retrieved with `gpio_pin_get_by_*`.
- Interrupt handlers set up with `bus_setup_intr`.
- Clock handles from `clk_get_by_*`.
- Regulator handles from `regulator_get_by_*`.
- Memory resources from `bus_alloc_resource_any`.

Print a breadcrumb on each release:

```c
device_printf(dev, "detach: releasing GPIO pin\n");
gpio_pin_release(sc->sc_pin);
```

If the breadcrumbs stop before the expected end, the resource that goes uncleared is the one after the last printed line.

### Panic During Boot

Panics that fire during FDT parsing usually mean the blob itself is malformed, or a driver is dereferencing an `OF_getprop` result it did not check. Two safeguards:

- Always check the return value of `OF_getprop`, `OF_getencprop`, and `OF_getprop_alloc`. A missing property returns `-1` or `ENOENT`; treating it as present leads to reads of whatever is next on the stack.
- Use `OF_hasprop(node, "prop")` before calling `OF_getprop` when a property is optional.

### DT Compile Errors

`dtc` error messages are reasonably clear. A few patterns to recognize:

- **`syntax error`**: Missing semicolon, unbalanced brace, or bad property value syntax.
- **`Warning (simple_bus_reg)`**: A node under `simple-bus` has `reg` but no `ranges` translation, or its `reg` does not match the parent's cell counts.
- **`FATAL ERROR: Unable to parse input tree`**: The file is syntactically broken at a coarse level. Check for missing `/dts-v1/;` or mis-quoted strings.
- **`ERROR (phandle_references): Reference to non-existent node or label`**: An `&label` reference that the compiler cannot resolve. This is where `-@` matters; without it, overlays that depend on base-tree labels cannot be validated.

### The Kernel Sees the Wrong Hardware

If your driver attaches but reads garbage from registers:

- Double-check the `reg` value in the DT.
- Confirm `#address-cells` and `#size-cells` match what you expect at the parent's level.
- Use `hexdump /dev/mem` style techniques only if you are certain the address is safe; reading the wrong MMIO range can hang the bus.

### You Changed the Driver But Nothing Changed

Double-check:

- Did you run `make` after editing?
- Did you copy the new `.ko` to `/boot/modules/` (or load the local one explicitly)?
- Did you unload the old module before loading the new one? `kldstat -m driver` shows currently resident modules.

A simple habit that avoids the last pitfall is to always `kldunload` explicitly before `kldload`, or to use `kldload -f` to force replacement.

### Quick Reference: Most-Used OFW Calls

For convenience, here is a compact table of the OFW and `ofw_bus` helpers that FDT drivers use most often. Each is declared in either `<dev/ofw/openfirm.h>` or `<dev/ofw/ofw_bus_subr.h>`.

| Call                                         | What it does                                                   |
|----------------------------------------------|---------------------------------------------------------------|
| `ofw_bus_get_node(dev)`                      | Return the phandle of this device's DT node.                 |
| `ofw_bus_get_compat(dev)`                    | Return the first compatible string of the node, or NULL.     |
| `ofw_bus_get_name(dev)`                      | Return the node's name part (before the '@').                |
| `ofw_bus_status_okay(dev)`                   | True if status is missing, "okay", or "ok".                  |
| `ofw_bus_is_compatible(dev, s)`              | True if one of the compatible entries matches s.             |
| `ofw_bus_search_compatible(dev, tbl)`        | Return the matching entry in a compat_data table.            |
| `ofw_bus_has_prop(dev, s)`                   | True if the property is present on the node.                 |
| `OF_getprop(node, name, buf, len)`           | Copy raw property bytes.                                     |
| `OF_getencprop(node, name, buf, len)`        | Copy property, byte-swapping u32 cells to host endianness.   |
| `OF_getprop_alloc(node, name, bufp)`         | Allocate and return property; caller frees with OF_prop_free.|
| `OF_hasprop(node, name)`                     | Return non-zero if the property exists.                      |
| `OF_child(node)`                             | First child phandle, or 0.                                   |
| `OF_peer(node)`                              | Next sibling phandle, or 0.                                  |
| `OF_parent(node)`                            | Parent phandle, or 0.                                        |
| `OF_finddevice(path)`                        | Look up a node by absolute path.                             |

### Quick Reference: Most-Used Peripheral Calls

From `<dev/gpio/gpiobusvar.h>`:

| Call                                            | What it does                                              |
|-------------------------------------------------|-----------------------------------------------------------|
| `gpio_pin_get_by_ofw_idx(dev, node, idx, &pin)` | Acquire pin by `gpios` index.                             |
| `gpio_pin_get_by_ofw_name(dev, node, n, &pin)`  | Acquire pin by named reference (e.g., `led-gpios`).       |
| `gpio_pin_get_by_ofw_property(dev, n, p, &pin)` | Acquire pin from named DT property.                       |
| `gpio_pin_setflags(pin, flags)`                 | Configure direction, pulls, and similar.                  |
| `gpio_pin_set_active(pin, val)`                 | Drive output to active or inactive state.                 |
| `gpio_pin_get_active(pin, &val)`                | Read current input or output level.                       |
| `gpio_pin_release(pin)`                         | Return pin to the unowned pool.                           |

From `<dev/extres/clk/clk.h>`:

| Call                                      | What it does                                                   |
|-------------------------------------------|---------------------------------------------------------------|
| `clk_get_by_ofw_index(dev, node, i, &c)`  | Acquire nth clock listed in `clocks` property.               |
| `clk_get_by_ofw_name(dev, node, n, &c)`   | Acquire clock by name.                                        |
| `clk_enable(c)` / `clk_disable(c)`        | Gate the clock on or off.                                     |
| `clk_get_freq(c, &f)`                     | Read current frequency in Hz.                                 |
| `clk_release(c)`                          | Release clock handle.                                         |

From `<dev/extres/regulator/regulator.h>`:

| Call                                                  | What it does                                         |
|-------------------------------------------------------|-----------------------------------------------------|
| `regulator_get_by_ofw_property(dev, node, p, &r)`     | Acquire regulator from DT property.                |
| `regulator_enable(r)` / `regulator_disable(r)`        | Turn rail on or off.                               |
| `regulator_set_voltage(r, min, max)`                  | Ask for voltage within min..max range.             |
| `regulator_release(r)`                                | Release regulator handle.                          |

From `<dev/extres/hwreset/hwreset.h>`:

| Call                                         | What it does                                              |
|----------------------------------------------|-----------------------------------------------------------|
| `hwreset_get_by_ofw_name(dev, node, n, &h)`  | Acquire reset line by name.                               |
| `hwreset_get_by_ofw_idx(dev, node, i, &h)`   | Acquire reset line by index.                              |
| `hwreset_assert(h)` / `hwreset_deassert(h)`  | Put peripheral into or out of reset.                      |
| `hwreset_release(h)`                         | Release reset handle.                                     |

From `<dev/fdt/fdt_pinctrl.h>`:

| Call                                          | What it does                                             |
|-----------------------------------------------|----------------------------------------------------------|
| `fdt_pinctrl_configure_by_name(dev, name)`    | Apply the named pinctrl state.                           |
| `fdt_pinctrl_configure_tree(dev)`             | Apply `pinctrl-0` recursively to children.               |
| `fdt_pinctrl_register(dev, mapper)`           | Register a new pinctrl provider.                         |

Print this table, pin it near your workstation, and refer to it whenever you start a new driver. The calls become second nature within a few projects.

### Summary Checklist

Before declaring a driver finished, run through this list:

- [ ] Module builds cleanly with no warnings under `-Wall -Wextra`.
- [ ] `kldload` produces the expected attach message.
- [ ] `kldunload` succeeds without EBUSY.
- [ ] Repeating load-unload cycles a dozen times does not leak resources.
- [ ] `devinfo -r` shows the driver in the expected position in the tree.
- [ ] sysctls are present, readable, and writable where intended.
- [ ] All DT properties the driver cares about are documented in the README.
- [ ] A companion overlay source compiles with `dtc -@`.
- [ ] A fresh reader could read the source and understand what it does.

## Glossary of Device Tree and Embedded Terms

This glossary collects the terms this chapter uses, defined briefly so you can check a term on first encounter without searching the prose. Cross-references to the relevant section appear in parentheses where helpful.

**ACPI**: Advanced Configuration and Power Interface. An alternative to FDT used by PC-class and some arm64 servers. A FreeBSD kernel picks one or the other at boot. (Section 3.)

**amd64**: FreeBSD's 64-bit x86 architecture. Usually uses ACPI rather than FDT, though FDT can be used in specialized embedded x86 cases.

**arm64**: FreeBSD's 64-bit ARM architecture. Uses FDT by default on embedded boards; uses ACPI on SBSA-compliant servers.

**Bindings**: Documented conventions for how a peripheral's properties are spelled in Device Tree. For instance, the `gpio-leds` binding documents what properties an LED controller's DT node should carry.

**Blob**: Informal term for a `.dtb` file, since it is an opaque binary chunk from the perspective of anything other than an FDT parser.

**BSP**: Board Support Package. The collection of files (kernel config, Device Tree, loader hints, sometimes drivers) needed to run an operating system on a specific board.

**Cell**: A 32-bit big-endian value that is the atomic unit of a DT property. Property values are sequences of cells.

**Compatible string**: The identifier a driver's probe matches against, stored in the `compatible` property of a DT node. Usually has vendor-prefix-slash-model form: `"brcm,bcm2711-gpio"`.

**Compat data table**: An array of `struct ofw_compat_data` entries that a driver walks in probe via `ofw_bus_search_compatible`. (Section 4.)

**dtb**: Compiled Device Tree binary. Output of `dtc`; the format the kernel parses at boot.

**dtbo**: Compiled Device Tree overlay. A small binary that the loader merges into the main `.dtb` before handing it to the kernel.

**dtc**: The Device Tree Compiler. Translates source to binary.

**dts**: Device Tree Source. Text input to `dtc`.

**dtsi**: Device Tree Source Include. A source fragment meant to be `#include`-d.

**Edge triggered**: An interrupt that fires on a level transition (rising, falling, or both). Contrast with level-triggered.

**FDT**: Flattened Device Tree. The binary format and framework FreeBSD uses for DT-based systems. Also used colloquially to refer to the whole concept.

**fdt_overlays**: A loader.conf tunable that lists overlay names to apply at boot.

**fdtdump**: A utility that decodes a `.dtb` file to a readable approximation of its source.

**Fragment**: A top-level entry in an overlay that names a target and declares the content to merge. (Section 5.)

**GPIO**: General-Purpose Input/Output. A programmable digital pin that can drive or read a line.

**intrng**: FreeBSD's interrupt next-generation framework. Unifies interrupt controllers and consumers. (Section 10.)

**kldload**: The command that loads a kernel module into a running system.

**kldunload**: The command that unloads a loaded kernel module.

**Level triggered**: An interrupt that asserts as long as a condition is true. Must be cleared at the source to stop firing.

**Loader**: FreeBSD's boot loader. Reads configuration, loads modules, merges DT overlays, hands control to the kernel.

**MMIO**: Memory-Mapped IO. A hardware register set exposed through a range of physical addresses.

**Newbus**: FreeBSD's device driver framework. Every driver registers into a tree of Newbus parent-child relationships.

**Node**: A point in the Device Tree. Has a name (and optional unit address) and a set of properties.

**OFW**: Open Firmware. A historical standard that FreeBSD's FDT code reuses the API of.

**ofwbus**: FreeBSD's top-level bus for Open Firmware-derived device enumeration.

**ofwdump**: A userspace utility that prints nodes and properties from the running kernel's DT. (Section 6.)

**Overlay**: A partial `.dtb` that modifies an existing tree. Targets nodes by label or path and merges content under them. (Section 5.)

**phandle**: Phantom handle. A 32-bit integer identifier for a DT node, used to cross-reference nodes within the tree.

**pinctrl**: Pin control framework. Handles multiplexing of SoC pins between their possible functions. (Section 8.)

**PNP info**: Metadata a driver publishes to identify the DT compatible strings it supports. Used by `devmatch(8)` for auto-loading. (Section 9.)

**Probe**: The driver method that inspects a candidate device and reports whether it can drive it. Returns a strength score or an error.

**Property**: A named value in a DT node. Values can be strings, cell lists, or opaque byte strings.

**Reg**: A property listing one or more (address, size) pairs that describe the MMIO ranges the peripheral occupies.

**Root controller**: The topmost interrupt controller. On arm64 systems, usually a GIC.

**SBC**: Single-Board Computer. An embedded board with CPU, memory, and peripherals on one PCB. Examples: Raspberry Pi, BeagleBone.

**SIMPLEBUS_PNP_INFO**: A macro that exports a driver's compat table as module metadata. (Section 9.)

**Simplebus**: The FreeBSD driver that probes DT children whose parent has `compatible = "simple-bus"`. It turns DT nodes into Newbus devices.

**softc**: Short for "soft context." A per-device state structure allocated by Newbus and passed to driver methods. (Section 4.)

**SoC**: System on Chip. An integrated circuit containing CPU, memory controller, and many peripheral blocks.

**Status**: A property on a DT node that indicates whether the device is enabled (`"okay"`) or not. Missing status defaults to okay.

**sysctl**: FreeBSD's system control interface. A driver can publish tunable parameters that userspace reads and writes.

**Target**: In an overlay, the node in the base tree that a fragment modifies.

**Unit address**: The numeric part after the `@` in a node name. Indicates where in the parent's address space the node lives.

**Vendor prefix**: The part of a compatible string before the comma. Identifies the organization responsible for the binding.

## Frequently Asked Questions

These are questions that come up repeatedly when people first write FDT drivers for FreeBSD. Most are already answered somewhere in the chapter; the FAQ format just puts the short answer in one place.

**Do I need to know ARM assembly to write an FDT driver?**

No. The whole point of the driver framework is that you work in C against a uniform API. You might read disassembly if you are debugging a very low-level crash, but that is the exception, not the rule.

**Can I write FDT drivers on amd64, or do I need arm64 hardware?**

You can develop on amd64 and cross-build for arm64. You can also run arm64 FreeBSD under QEMU on an amd64 host, which is the most common workflow for anyone who does not want to wait on a Pi to reboot. For the final validation, you eventually want real hardware or a faithful emulator, but day-to-day iteration fits on a laptop.

**What is the difference between simplebus and ofwbus?**

`ofwbus` is the top-level root bus for Open Firmware-derived device enumeration. `simplebus` is a generic bus driver that covers DT nodes compatible with `"simple-bus"` and similar simple enumeration. Most of your drivers will register with both; `ofwbus` handles the root and specialized cases, `simplebus` handles the overwhelming majority of peripheral buses.

**Why does my driver need to register with both ofwbus and simplebus?**

Some nodes appear under `simplebus`, some directly under `ofwbus` (particularly on systems where the tree structure is unusual). Registering with both means the driver attaches wherever the node happens to end up.

**Why is my overlay not applying?**

Run through the checklist in Section 6. The most common cause, in order: wrong filename or directory, typo in `fdt_overlays`, base compatible mismatch, missing `-@` at overlay compile time.

**Can a driver span multiple DT nodes?**

Yes. A single driver instance usually matches one node, but the attach function can walk children or phandle references to collect state from several nodes. See Section 9's discussion of `gpioled_fdt.c` for the children case.

**How do I handle a device that has both ACPI and FDT descriptions?**

Write two compat paths. Most large FreeBSD drivers that support both platforms do exactly this: separate probe functions register with each bus, and the shared code lives in the common attach. Look at `sdhci_acpi.c` and `sdhci_fdt.c` for a worked example.

**What about DT bindings I do not find in the FreeBSD tree?**

The source of truth for DT bindings is the upstream device-tree specification plus the Linux kernel's documentation. FreeBSD uses the same bindings where practical. If you need a binding FreeBSD does not yet support, you can typically port the relevant Linux driver or write a native FreeBSD driver that consumes the same binding.

**Do I need to modify the kernel to add a new driver?**

No. Out-of-tree modules compile against `/usr/src/sys` headers and load at runtime. You only edit the kernel tree itself when you contribute a driver upstream or when you need to change a generic piece of infrastructure.

**How do I cross-compile for arm64 from an amd64 host?**

Use the cross toolchain included in FreeBSD's build system:

```console
$ make TARGET=arm64 TARGET_ARCH=aarch64 buildworld buildkernel
```

This builds a full arm64 system image on your amd64 workstation. Module-only builds follow the same pattern with narrower targets.

**Is there a way to auto-load my driver when a matching DT node appears?**

Yes, through `devmatch(8)` and the `SIMPLEBUS_PNP_INFO` macro. Declare your compat table, include `SIMPLEBUS_PNP_INFO(compat_data)` in the driver source, and `devmatch` picks it up.

**Can I use C++ for an FDT driver?**

No. FreeBSD kernel code is strictly C (and a very small amount of assembly). Other languages are not supported, and the kernel API assumes C conventions.

**How do I debug a crash during DT parsing at boot?**

Early crashes are hard. The usual techniques: enable verbose boot messages (`-v` in `loader.conf`), compile the kernel with `KDB` and `DDB`, use a serial console, and attach a JTAG debugger if you have one. Also consider adding temporary `printf` statements directly in the FDT parsing code under `/usr/src/sys/dev/ofw/`.

**Do modules for drivers that share FDT bindings interfere with each other?**

No. Each module registers its compat table, and the probe strength of matching drivers decides which one wins. If two drivers claim the same compatible with the same strength, the order they were loaded determines the winner. Give each driver a distinctive strength to avoid surprises.

**How do I preserve state across kldload/kldunload cycles?**

You cannot. A module that unloads loses all its state. If your driver needs persistence, write to a file, a sysctl's tunable form, a kernel tunable, or a location in NVRAM or EEPROM that outlives the module. For debugging, printing state to `dmesg` before unload and parsing it back after the next load is a workable shortcut.

**Is the `compatible` list order significant?**

Yes. A node may list multiple compatible strings, from most specific to least specific. `compatible = "brcm,bcm2711-gpio", "brcm,bcm2835-gpio";` declares that the node is primarily a 2711-variant but is compatible with the older 2835 binding as a fallback. A driver that claims the 2835 compatible will match this node if no driver for 2711 is loaded. The order lets firmware describe a device in multiple levels of detail so newer kernels can take advantage of refinements without breaking older ones.

**Why does FreeBSD sometimes use different property names than Linux?**

Most DT properties are shared, but a few are intentionally different where FreeBSD's behavior diverges from Linux's expectations. When you port a driver, read the FreeBSD side's existing bindings carefully; silently assuming the Linux semantics is a common source of porting bugs.

**What is the relationship between Newbus and intrng?**

Newbus handles device probe, attach, and resource allocation. intrng handles interrupt controller registration and IRQ routing. They interoperate: Newbus resource allocation for `SYS_RES_IRQ` goes through intrng to find the right controller, and intrng dispatches interrupts back into the driver handler that Newbus registered.

## Wrapping Up

This chapter took you from "FreeBSD is a general-purpose operating system" to "I can write a driver that fits into an embedded system's Device Tree." The two tasks are not the same. The first is about using the kernel you already know; the second is about understanding a whole new vocabulary for describing the hardware the kernel runs on.

We began by looking at what embedded FreeBSD actually is: a lean, capable system running on SBCs, industrial boards, and purpose-built hardware. We saw how those systems describe themselves not through PCI enumeration or ACPI tables but through a static Device Tree that the firmware hands to the kernel at boot.

We then learned the Device Tree language itself: nodes with names and unit addresses, properties with cell-structured values, phandles for cross-references, and the `/plugin/;` overlay syntax that lets us add or modify nodes without rebuilding the base blob. The language takes an afternoon to get used to; the habits it builds, about thinking of hardware as a parented, addressed, typed tree, last for a career.

With the language in hand, we looked at FreeBSD's machinery for consuming a DT. The `fdt(4)` subsystem loads the blob. The OFW API walks it. The `ofw_bus_*` helpers expose the walk in terms of compat strings and status checks. The `simplebus(4)` driver enumerates children. And the consumer frameworks (`clk`, `regulator`, `hwreset`, `pinctrl`, GPIO) all integrate with DT phandle references so that drivers can acquire their resources through a uniform pattern.

Armed with the machinery, we built a driver. First `fdthello`, the minimal skeleton, which showed the required shape of an FDT driver in its purest form. Then `edled`, a complete GPIO-driven LED driver that illustrates attach, detach, sysctl, and proper resource management. Along the way we looked at how to compile overlays, deploy them through `loader.conf`, inspect the tree at runtime with `ofwdump`, and debug the class of failures that embedded drivers uniquely exhibit.

Finally, we took `edled` through the refactoring passes that turn a working driver into a finished one: tightened error paths, a real mutex, optional power and clock handling, pinctrl awareness, a style audit. This is the work that distinguishes a toy from a driver you would be comfortable running in production.

The chapter's labs gave you a chance to run everything yourself. The challenge exercises gave you room to extend. The troubleshooting section gave you somewhere to look when things break.

### What You Should Be Able to Do Now

If you worked through the labs and read the explanations carefully, you can now:

- Read an unfamiliar `.dts` and explain what hardware it describes.
- Write a new `.dts` or `.dtso` that adds a peripheral to an existing board.
- Compile that source into a binary and deploy it to a running system.
- Write an FDT-aware FreeBSD driver from scratch.
- Acquire memory, IRQ, GPIO, clock, and regulator resources through the standard consumer APIs.
- Debug the probe-did-not-fire, attach-failed, and detach-stuck classes of problems.
- Identify when a board is running in FDT mode versus ACPI mode on arm64, and what that means for your driver.

### What This Chapter Leaves Implicit

There are three topics the chapter leaves for other sources. They are not book-shaped; they are reference-shaped, and the book would bog down if it tried to cover them fully.

First, the full DT binding specification. We covered the properties you are most likely to use. The complete binding catalog, maintained upstream by the device-tree community, is browsable at the online documentation for Linux kernel's `Documentation/devicetree/bindings/` tree. FreeBSD follows most of those bindings, with exceptions noted in `/usr/src/sys/contrib/device-tree/Bindings/` for the subset FreeBSD relies on.

Second, interrupt routing on complex SoCs. We touched on `interrupt-parent` chaining. A board with multiple GIC-style controllers, pinctrl-managed interrupts, and nested gpio-as-interrupt nodes can get intricate. The FreeBSD `intrng` subsystem is where to look when the simple case is no longer enough.

Third, the FDT support for peripherals that do not go through simplebus: USB phys, clock trees on SoCs with hierarchical PLLs, voltage scaling for DVFS. Each is its own subtopic. The book's appendix points at the canonical sources.

### Key Takeaways

If the chapter can be distilled into a single page, here are the points worth remembering:

1. **Embedded systems describe themselves through Device Tree, not through runtime enumeration.** The blob the firmware hands the kernel is the authoritative description of what hardware is present.

2. **A driver matches through compatible strings.** Your probe consults a compat table, which compares against the node's `compatible` property. Get this string exactly right.

3. **`simplebus(4)` is the enumerator.** Every FDT driver's parent is either `simplebus` or `ofwbus`. Register with both at module load time.

4. **Resources come from the framework.** `bus_alloc_resource_any` for MMIO and IRQ, `gpio_pin_get_by_ofw_*` for GPIO, and the matching `clk_*`, `regulator_*`, `hwreset_*` calls for power and reset. Release each in detach.

5. **Overlays modify the tree without rebuilding it.** A small `.dtbo` placed under `/boot/dtb/overlays/` and listed in `fdt_overlays` is a clean way to add or enable peripherals on a given board.

6. **Interrupt wiring follows a parent chain.** A node's `interrupt-parent` and `interrupts` properties connect through intrng to the root controller. Understanding the chain is essential for debugging silent interrupts.

7. **Real drivers are the best reference.** `/usr/src/sys/dev/gpio/`, `/usr/src/sys/dev/fdt/`, and each SoC's platform tree contain dozens of FDT drivers that demonstrate every pattern you are likely to need.

8. **Error paths and teardown matter.** A driver that loads and works once is the easy case. A driver that loads, unloads, and reloads cleanly a hundred times is the robust case.

9. **The tooling is simple but effective.** `dtc`, `ofwdump`, `fdtdump`, `devinfo -r`, `sysctl dev.<name>.<unit>`, and `kldstat` together cover almost every inspection you need.

10. **FDT is just one of several hardware-description systems.** On arm64 servers, ACPI takes the same role. The driver patterns you learned here transfer, but the match layer changes.

### Before You Move On

Before you treat this chapter as completed and move to Chapter 33, take a moment to verify the following. These are the kinds of checks that separate a learner who has seen the material from a learner who owns it.

- You can sketch, without looking, the skeleton of an FDT-aware driver, including the compat table, probe, attach, detach, method table, and `DRIVER_MODULE` registration.
- You can take a paragraph of DT source and narrate what it describes, node by node, property by property.
- You can distinguish a phandle from a path reference and explain when you would use each.
- You can explain why `MODULE_DEPEND(your_driver, gpiobus, 1, 1, 1)` matters and what goes wrong without it.
- You know which loader tunable controls overlay loading and where the `.dtbo` files live.
- Given a probe that does not fire, you have a mental checklist of four or five things to verify before reaching for a debugger.
- You know what `SIMPLEBUS_PNP_INFO` does and why it matters for production drivers.
- You can name at least three real FDT drivers in `/usr/src/sys` and describe, in a sentence each, what they demonstrate.

If any of these still feel shaky, loop back to the relevant section and re-read it. The chapter's material compounds; Chapter 33 will assume you have internalized most of what is here.

### A Note on Sustained Practice

Embedded FreeBSD rewards regular practice. The first time you read a `.dts` for a real board, it looks overwhelming. The tenth time, you skim for the nodes you care about. The hundredth time, you edit in place. If you have a spare SBC on your desk, put it to work. Pick a sensor, write a driver, add a line to a loader.conf. The skill compounds. Every small driver you finish is a template for the next one, and the patterns carry across boards and vendors.

### Connecting Back to the Rest of the Book

Chapter 32 sits late in Part 7, and the material here rests on layers you met earlier. A short tour of what you now see in a new light:

From Part 1 and Part 2, the kernel module skeleton, the `DRIVER_MODULE` registration, and the `kldload`/`kldunload` lifecycle. The shape of an FDT driver is the same shape, with a different probe strategy.

From Part 3, the Newbus framework. `simplebus` is a bus driver that happens to source its children from a Device Tree rather than from a bus protocol. Every Newbus pattern you learned earlier applies, unchanged, in this context.

From Part 4, the driver-to-userspace interfaces: `cdev`, `sysctl`, `ioctl`. Our `edled` driver used sysctl as its control interface; in a larger project it might add a character device or even a netlink socket.

From Part 5 and Part 6, the practical chapters on concurrency, testing, and kernel debugging. Those tools all apply to embedded drivers in full. The only difference is that the hardware is often harder to reach, so the tools matter more.

From Part 7, Chapter 29 on 32-bit platforms and Chapter 30 on virtualization both touch on embedded angles. Chapter 31's security chapter applies directly: a driver on an embedded device is often exposed to whatever runs in the product's userspace, and the same defensive patterns apply.

The picture after Chapter 32 is that you have a complete starter toolkit for most classes of FreeBSD device work. You can write character drivers, bus drivers, network drivers, and now FDT-based embedded drivers. The remaining chapters of Part 7 refine and finalize those skills.

### Looking Ahead

The next chapter, Chapter 33, turns from "does it work" to "how well does it work." Performance tuning and profiling is the art of measuring what the kernel actually does, under real load, with real data. On an embedded system, the question has extra force. The hardware is usually small, the workload is often fixed, and the margin between "runs well" and "runs badly" is measured in microseconds. In Chapter 33 we look at the tools FreeBSD gives you to measure: `hwpmc`, `pmcstat`, DTrace, flame graphs, and the kernel's own performance-counter probes. We also look at the mental model you need to interpret what the tools say, and at the pitfalls of tuning the wrong thing.

Beyond Chapter 33, Chapters 34 through 38 complete the mastery arc: coding style and readability, contributing to FreeBSD, porting drivers between platforms, documenting your work, and a capstone project that brings several earlier threads together. Each builds on what you have done here. The `edled` driver you wrote in this chapter, extended through the challenge exercises, is a perfectly reasonable candidate to carry forward into those final chapters as a running example.

The driver work is done. The measurement work begins.

### A Parting Word

One last encouragement before we close. Embedded FreeBSD has a quiet, steady community. The boards are inexpensive, the source is open, the mailing lists are patient. If you stick with it, you will find that each driver you write teaches you something the next one needs. The `edled` driver in this chapter is a small thing, yet in writing it you touched the FDT parser, the OFW API, the GPIO consumer framework, the Newbus tree, and the sysctl mechanism. That is not nothing. It is the backbone of every FDT driver in the tree, exercised in miniature.

Keep the practice up. The field rewards curiosity with steady progress. The next chapter gives you the tools to make sure that progress is progress in the right direction.

When you eventually sit down in front of an unfamiliar board, the view will not feel unfamiliar anymore. You will know to ask for its `.dts`, to skim the peripherals listed under `soc`, to check which GPIO controller hosts the pin you care about, to look up the compatible string you need to match. The habits you have just built travel with you. They apply to a Raspberry Pi, to a BeagleBone, to a custom industrial board, to a QEMU virt machine. They apply in ten years from now on boards that do not yet exist, because the underlying language of hardware description is stable.

That portability, in the end, is what the Device Tree was designed for, and what this chapter was designed to teach. Welcome to embedded FreeBSD.

With Chapter 32 in hand, your toolkit is nearly complete. On to Chapter 33, and to the measurement work that will tell you whether the driver you just wrote is as efficient as it needs to be.

Good reading, and good driver writing.
