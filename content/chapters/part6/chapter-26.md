---
title: "USB and Serial Drivers"
description: "Chapter 26 opens Part 6 by teaching transport-specific driver development through USB and serial devices. It explains what makes USB and serial drivers different from the generic character drivers built earlier in the book; introduces the USB mental model (host and device roles, device classes, descriptors, interfaces, endpoints, and the four transfer types); introduces the serial mental model (UART hardware, RS-232-style framing, baud rate, parity, flow control, and the FreeBSD tty discipline); walks through the organisation of FreeBSD's USB subsystem and the registration idioms that drivers use to attach to `uhub`; shows how a USB driver sets up bulk, interrupt, and control transfers through `usbd_transfer_setup` and handles them in callbacks that follow the `USB_GET_STATE` state machine; explains how a USB driver may expose a user-visible `/dev` interface through `usb_fifo` or a custom `cdevsw`; contrasts FreeBSD's two serial worlds, the `uart(4)` subsystem for real UART hardware and the `ucom(4)` framework for USB-to-serial bridges; teaches how baud rate, parity, stop bits, and RTS/CTS flow control are carried through `struct termios` and programmed into hardware; and shows how to test USB and serial driver behaviour without ideal physical hardware using `nmdm(4)`, `cu(1)`, `usb_template(4)`, QEMU USB redirection, and the existing kernel's own loopback facilities. The `myfirst` driver gains a new transport-specific sibling, `myfirst_usb`, at version 1.9-usb, which probes a vendor/product identifier pair, attaches on device insertion, sets up one bulk-in and one bulk-out transfer, echoes received bytes back through a /dev node, and unwinds cleanly on hot-unplug. The chapter prepares the reader for Chapter 27 (storage and the VFS layer) by establishing the two mental models a reader will reuse everywhere in Part 6: a transport is a protocol plus a lifecycle, and a FreeBSD transport-specific driver is a Newbus driver whose resources happen to be bus endpoints rather than PCI BARs."
partNumber: 6
partName: "Writing Transport-Specific Drivers"
chapter: 26
lastUpdated: "2026-04-19"
status: "complete"
author: "Edson Brandi"
reviewer: "TBD"
translator: "TBD"
estimatedReadTime: 300
---

# USB and Serial Drivers

## Introduction

Chapter 25 closed Part 5 with a driver that the rest of the system could talk to. The `myfirst` driver at version `1.8-maintenance` had a rate-limited logging macro, a careful errno vocabulary, loader tunables and writable sysctls, a three-way version split, a labelled-goto cleanup chain in attach and detach, a clean modular source layout, `MODULE_DEPEND` and `MODULE_PNP_INFO` metadata, a `MAINTENANCE.md` document, a `shutdown_pre_sync` event handler, and a regression script that could load and unload the driver a hundred times without leaking a single resource. What the driver did not have was any contact with real hardware. The character device backed a buffer in kernel memory. The sysctl counters tracked operations against that buffer. The `MYFIRSTIOC_GETCAPS` ioctl announced capabilities that were implemented entirely in software. Everything the driver did, it did without ever reading a byte off a wire.

Chapter 26 begins the step outward. Instead of serving a buffer in RAM, the driver will attach to a real bus and service a real device. The bus will be the Universal Serial Bus, because USB is the most approachable transport in FreeBSD: it is ubiquitous, its subsystem is extremely well organised, the kernel interface is designed around a small handful of structures and macros, and every FreeBSD developer already has a dozen USB devices on their desk. After USB, the chapter pivots to the subject that historically preceded USB and still lives alongside it everywhere from debug consoles to GPS modules: the serial port, in its classical form as a UART-driven RS-232 interface and in its modern form as a USB-to-serial bridge. By the end of the chapter, the `myfirst` driver family has grown a new transport-specific sibling, `myfirst_usb`, at version `1.9-usb`. That sibling knows how to attach to a real USB device, how to set up a bulk-in and a bulk-out transfer, how to echo received bytes through a `/dev` node, and how to survive the device being yanked out of the port while the driver is in use.

Chapter 26 is the opening chapter of Part 6. Part 6 is organised around the observation that up to this point the book has been teaching the parts of FreeBSD driver development that are *transport-neutral*: the Newbus model, the character device interface, synchronisation, interrupts, DMA, power management, debugging, integration, and maintenance. All of those disciplines apply to every driver regardless of how the device is attached. Part 6 shifts the focus. USB, storage, and networking each have their own bus, their own lifecycle, their own data-flow pattern, and their own idiomatic way of integrating with the rest of the kernel. The disciplines you have built in Parts 1 through 5 carry over unchanged; what is new is the shape of the interface between your driver and the specific subsystem it plugs into. Chapter 26 teaches that shape for USB and for serial devices. Chapter 27 teaches it for storage devices and the VFS layer. Chapter 28 teaches it for network interfaces. Each of the three chapters is structurally parallel: the transport is introduced, the subsystem is mapped, a minimal driver is built, and the reader is shown how to test without the specific hardware everyone happens not to have.

There is a deliberate pairing of USB and serial in this chapter. The two topics sit together because they are both first-class citizens of the same larger mental model: a transport is a *protocol plus a lifecycle*, and the driver is the piece of code that carries data across the protocol boundary and keeps the lifecycle consistent with the kernel's view of the device. USB is a protocol with a rich four-transfer-type vocabulary and a hot-plug lifecycle. A UART is a protocol with a much simpler byte-framing vocabulary and a statically-attached lifecycle. Studying them together makes the pattern visible. A student who has seen the USB callback state machine and the UART interrupt handler side by side understands that "FreeBSD's driver model" is not a single shape but a family of shapes, each one adapted to the demands of its own transport.

The second reason to pair USB and serial is historical and practical. A very large number of what the operating system calls "USB devices" are in fact serial ports in disguise. The FTDI FT232R chip, the Prolific PL2303, the Silicon Labs CP210x, and the WCH CH340 all expose a standard serial-port API to user space, but physically they sit on the USB bus. FreeBSD handles that with the `ucom(4)` framework: a USB driver registers callbacks with `ucom`, and `ucom` produces the user-visible `/dev/ttyU0` and `/dev/cuaU0` device nodes and arranges for the termios-aware line discipline to operate correctly on top of a USB bulk-in and bulk-out pair. The reader who is about to write a USB driver is likely, sooner or later, to write a USB-to-serial driver, and that driver will be an intersection of the two worlds the chapter introduces. Putting the material into a single chapter makes the intersection visible.

A third reason is pedagogical. The `myfirst` driver so far has been a pseudo-device. The transition to real hardware is a conceptual step, not just a coding step. Many readers will find their first attempt at a hardware-backed driver unsettling: interrupts arrive without asking, transfers can stall or time out, the device can be unplugged mid-operation, and the kernel has opinions about how fast you are allowed to respond. USB is the friendliest possible introduction to that world because the USB subsystem does an unusually large amount of work on the driver's behalf. Setting up a bulk transfer in USB is not the same kind of problem as setting up a DMA ring on a PCI Express NIC. The USB core manages the low-level DMA bookkeeping; your driver works at the level of "tell me when this transfer completes". Learning the USB pattern first makes the later hardware chapters (storage, networking, embedded buses in Part 7) less intimidating because by then the basic shape of a transport-specific driver is familiar.

The `myfirst` driver's path through this chapter is concrete. It picks up at version `1.8-maintenance` from the end of Chapter 25. It adds a new source file, `myfirst_usb.c`, compiled into a new kernel module, `myfirst_usb.ko`. The new module declares itself dependent on `usb`, lists a single vendor and product identifier in its match table, probes and attaches on hot-plug, allocates one bulk-in and one bulk-out transfer, exposes a `/dev/myfirst_usb0` node, echoes incoming bytes to the kernel log and copies them back out on a read, handles detach cleanly when the cable is pulled, and carries forward every Chapter 25 discipline without exception. The labs exercise each piece in turn. By the end of the chapter, there is a second driver in the family, a new source layout to accommodate it, and a working example of a FreeBSD USB driver that the reader has typed themselves.

Because this is also a chapter about serial devices, the chapter spends time on the serial half of its scope even though `myfirst_usb` itself is not a serial driver. The serial material teaches how `uart(4)` is laid out, how `ucom(4)` fits in, how termios carries baud rate and parity and flow control from user space down to hardware, and how to test serial interfaces without physical hardware using `nmdm(4)`. The serial material does not build a new UART hardware driver from scratch. Writing a UART hardware driver is a specialised undertaking that is almost never the right choice in a modern environment: the existing `ns8250` driver in the base system already handles every PC-compatible serial port, every common PCI serial card, and the ARM PL011 that most virtualised platforms present. The chapter teaches the serial subsystem at the level the reader actually needs: how it is organised, how to read existing drivers, how termios reaches a driver's `param` method, how to use the subsystem from user space, and what to do when the goal is a USB-to-serial driver (the common case) rather than a new hardware driver (the rare case).

The rhythm of Chapter 26 is the rhythm of pattern recognition. The reader will leave the chapter knowing what a USB driver looks like, what a serial driver looks like, where the two overlap, where they differ from the pseudo-drivers of Parts 2 through 5, and how to test both without a lab full of adapters. Those are the foundations of transport-specific driver development. Chapter 27 will then apply the same discipline to storage, and Chapter 28 will apply it to networking, each time taking the same general pattern and bending it to a new transport's rules.

### Where Chapter 25 Left the Driver

A short checkpoint before the new work starts. Chapter 26 extends the driver family produced at the end of Chapter 25, tagged as version `1.8-maintenance`. If any of the items below is uncertain, return to Chapter 25 and resolve it before starting this chapter, because the new material assumes every Chapter 25 primitive is working and every habit is in place.

- Your driver source matches Chapter 25 Stage 4. `myfirst.ko` compiles cleanly, identifies itself as `1.8-maintenance` in `kldstat -v`, and carries the full `MYFIRST_VERSION`, `MODULE_VERSION`, and `MYFIRST_IOCTL_VERSION` triple.
- The source layout is split: `myfirst_bus.c`, `myfirst_cdev.c`, `myfirst_ioctl.c`, `myfirst_sysctl.c`, `myfirst_debug.c`, `myfirst_log.c`, with `myfirst.h` as the shared private header.
- The rate-limited log macro `DLOG_RL` is in place and tied to a `struct myfirst_ratelimit` inside the softc.
- The `goto fail;` cleanup chain in `myfirst_attach` is working and exercised by a deliberate failure lab.
- The regression script passes a hundred consecutive `kldload`/`kldunload` cycles with no residual OIDs, no orphaned cdev nodes, and no leaked memory.
- Your lab machine runs FreeBSD 14.3 with `/usr/src` on disk, a debug kernel with `INVARIANTS`, `WITNESS`, `WITNESS_SKIPSPIN`, `DDB`, `KDB`, `KDB_UNATTENDED`, `KDTRACE_HOOKS`, and `DDB_CTF`, and a VM snapshot at the `1.8-maintenance` state you can revert to.

That driver, those files, and those habits are what Chapter 26 extends. The additions introduced in this chapter live almost entirely in a new file, `myfirst_usb.c`, which becomes a second kernel module sharing the same conceptual family as `myfirst.ko` but building a separate `myfirst_usb.ko`. The chapter's labs exercise each stage of the new module: probe, attach, transfer setup, callback handling, /dev exposure, and detach. The chapter does not modify `myfirst.ko` itself; the existing driver remains a reference implementation of Parts 2 through 5, and the new driver is its USB-transport sibling.

### What You Will Learn

By the end of this chapter you will be able to:

- Explain what makes a transport-specific driver different from the pseudo-drivers built in Parts 2 through 5, and name the three broad categories of work a transport-specific driver has to add to its Newbus foundation: matching rules, transfer mechanics, and lifecycle hot-plug handling.
- Describe the USB mental model at the level needed to write a driver: host versus device roles, hubs and ports, device classes (CDC, HID, Mass Storage, Vendor), the descriptor hierarchy (device, configuration, interface, endpoint), the four transfer types (control, bulk, interrupt, isochronous), and the hot-plug lifecycle.
- Read the output of `usbconfig` and `dmesg` for a USB device and identify its vendor identifier, product identifier, interface class, endpoint addresses, endpoint types, and packet sizes.
- Describe the serial mental model at the level needed to write a driver: the UART as a shift register with a baud generator, RS-232 framing, start and stop bits, parity, hardware flow control via RTS and CTS, software flow control via XON and XOFF, and the relationship between `struct termios`, `tty(9)`, and the driver's `param` callback.
- Explain the difference between FreeBSD's `uart(4)` subsystem for real UART hardware and the `ucom(4)` framework for USB-to-serial bridges, and name the two worlds a serial-driver author must never confuse.
- Write a USB device driver that attaches to `uhub`, declares a `STRUCT_USB_HOST_ID` match table, implements `probe` and `attach` and `detach` methods, uses `usbd_transfer_setup` to configure a bulk-in and a bulk-out transfer, and unwinds cleanly through a labelled-goto chain.
- Write a USB transfer callback that follows the `USB_GET_STATE` state machine, handles `USB_ST_SETUP` and `USB_ST_TRANSFERRED` correctly, distinguishes `USB_ERR_CANCELLED` from other errors, and responds to a stalled endpoint with `usbd_xfer_set_stall`.
- Expose a USB device to user space through the `usb_fifo` framework or through a custom `make_dev_s`-registered `cdevsw`, and know when each is the right choice.
- Read an existing UART driver in `/usr/src/sys/dev/uart/` with a pattern vocabulary that makes the code's intent clear on first pass, including the `uart_class`/`uart_ops` split, the method dispatch, the baud-divisor calculation, and the tty-side wakeup machinery.
- Translate a `struct termios` into the four arguments of a UART `param` method (baud, databits, stopbits, parity), and know which termios flags belong to the hardware layer and which belong to the line discipline.
- Test a USB driver against a simulated device using QEMU USB redirection or `usb_template(4)`, and test a serial driver against a `nmdm(4)` null-modem pair without any hardware at all.
- Use `cu(1)`, `tip(1)`, `stty(1)`, `comcontrol(8)`, and `usbconfig(8)` to drive, configure, and inspect serial and USB devices from user space.
- Handle hot-unplug cleanly in a USB driver's detach path: cancel outstanding transfers, drain callouts and taskqueues, release bus resources, and destroy cdev nodes, all while knowing that the device may already be gone by the time the detach method runs.

The list is long because transport-specific drivers touch many surfaces at once. Each item is narrow and teachable. The chapter's work is making the set of items into a coherent, reusable mental picture.

### What This Chapter Does Not Cover

Several adjacent topics are explicitly deferred to later chapters so Chapter 26 stays focused on the foundations of USB and serial driver development.

- **USB isochronous transfers and high-bandwidth video/audio streaming** are mentioned at a conceptual level in Section 1 but not developed. Isochronous transfers are the most complex of the four transfer types and are almost always used through higher-level frameworks (audio, video capture) that deserve their own treatment. Chapter 26 focuses on control, bulk, and interrupt transfers, which together cover the vast majority of USB driver work.
- **USB device-mode and gadget programming** through `usb_template(4)` is introduced briefly for testing purposes but not built out. Writing a custom USB gadget is a specialised project outside the scope of a first transport-specific chapter.
- **The internals of the USB host controller drivers** (`xhci`, `ehci`, `ohci`, `uhci`) are outside scope. These drivers implement the low-level protocol machinery that `usbd_transfer_setup` eventually calls; a driver author almost never has to modify them. The chapter treats them as a stable platform.
- **Writing a new UART hardware driver from scratch** is outside scope. The existing `ns8250` driver handles every common PC serial port, the `pl011` driver handles most ARM platforms, and the embedded SoC drivers handle the rest. Writing a new UART driver is the specialised work of porting FreeBSD to a new system-on-chip, which is its own topic (touched on in Part 7 alongside Device Tree and ACPI). Chapter 26 teaches the reader how to *read* and *understand* a UART driver rather than how to write one.
- **Storage drivers** (GEOM providers, block devices, VFS integration) are the subject of Chapter 27. USB mass storage is touched on only as an example of a USB device class, not as a driver target.
- **Network drivers** (`ifnet(9)`, mbufs, RX/TX ring bookkeeping) are the subject of Chapter 28. USB network adapters are mentioned as an example of CDC Ethernet, not as a driver target.
- **USB/IP for remote USB device testing over a network** is mentioned as an option for readers who truly cannot obtain any USB pass-through, but is not developed. The standard testing pathway in this chapter is a local VM with device redirection.
- **Quirks and vendor-specific workarounds** through `usb_quirk(4)` are mentioned but not developed. A driver author who needs quirks is already past the level this chapter teaches.
- **Bluetooth, Wi-Fi, and other wireless transports that happen to use USB** as their physical bus are outside scope. Those stacks involve protocols well beyond USB itself and are their own bodies of work.
- **Transport-agnostic abstraction for multi-bus drivers** (the same driver logic plugging into PCI, USB, and serial via a common interface) is deferred to Part 7's portability chapter.

Staying inside those lines keeps Chapter 26 a chapter about *the USB and serial transports*, not a chapter about every technique a senior transport-specific kernel developer might use on a senior transport-specific kernel problem.

### Where We Are in Part 6

Part 6 has three chapters. Chapter 26 is the opening chapter and teaches transport-specific driver development through USB and serial devices. Chapter 27 teaches transport-specific driver development through storage devices and the VFS layer. Chapter 28 teaches it through network interfaces. The three chapters are structurally parallel in the sense that each introduces a transport, maps the subsystem, builds a minimal driver, and teaches hardware-free testing.

Chapter 26 is the right place to start Part 6 for three reasons. The first is that USB is the most gentle introduction to hardware-backed drivers: its core abstractions are smaller than storage's (no GEOM graph, no VFS), smaller than networking's (no mbuf chains, no ring buffers with head/tail pointers and interrupts mitigation), and the subsystem handles a large share of the hard parts on the driver's behalf. The second is that USB appears everywhere. Even a reader who will never write a storage or network driver will probably write a USB driver at some point: a thermometer, a data logger, a custom serial adapter, a factory test fixture. The third is pedagogical. The pattern USB teaches, a subsystem with probe-and-attach lifecycles, transfer setup through a config array, callback-based completion, and a clean detach on unplug, is the same pattern (with different specifics) that storage and networking teach. Seeing it first in USB makes the next two chapters recognisable.

Chapter 26 bridges forward to Chapter 27 by closing on a note about lifecycle: the USB detach path is a dress rehearsal for the storage-device hot-removal path, and the patterns the reader has just practised will come back the moment an external USB disk is pulled in Chapter 27. It also bridges backward to Chapter 25 by carrying every Chapter 25 discipline forward: `MODULE_DEPEND`, `MODULE_PNP_INFO`, the labelled-goto pattern, the errno vocabulary, rate-limited logging, version discipline, and a regression script that exercises the new module as rigorously as Chapter 25 exercised the old one.

### A Small Note on Difficulty

If the transition from pseudo-drivers to real hardware looks daunting on the first reading, that feeling is entirely normal. Every experienced FreeBSD developer had a first USB driver that did not attach, a first serial session where `cu` refused to talk, and a first debug session where `dmesg` stayed silent. The chapter is structured to ease you into each of those moments with labs, troubleshooting notes, and exit points. If a section starts to feel overwhelming, the right move is not to push through but to stop, read the corresponding real driver in `/usr/src`, and return when the real code makes the concept visible. The existing FreeBSD drivers are the single best teaching resource this chapter can point to, and the chapter will point to them often.

## Reader Guidance: How to Use This Chapter

Chapter 26 has three layers of engagement, and you can pick the layer that fits your current situation. The layers are independent enough that you can read for understanding now and return for hands-on practice later without losing continuity.

**Reading only.** Three to four hours. Reading gives you the USB and serial mental models, the shape of the FreeBSD subsystems, and pattern recognition for reading existing drivers. If you are not yet in a position to load kernel modules (because your lab VM is unavailable, you are reading on a commute, or you have a planning meeting in thirty minutes), a reading-only pass is a worthwhile investment. The chapter is written so that the prose carries the teaching load; the code blocks are there to anchor the prose, not to replace it.

**Reading plus the hands-on labs.** Eight to twelve hours over two or three sessions. The labs guide you through building `myfirst_usb.ko`, exploring real USB devices with `usbconfig`, setting up a simulated serial link with `nmdm(4)`, talking to it with `cu`, and running a hot-unplug stress test. The labs are where the chapter turns from explanation into reflex. If you can spare eight to twelve hours across two or three sessions, do the labs. The cost of skipping them is that the patterns stay abstract instead of becoming habit.

**Reading plus the labs plus the challenge exercises.** Fifteen to twenty hours over three or four sessions. The challenge exercises push beyond the chapter's worked example into the territory where you have to adapt the pattern to a new requirement: add a control-transfer ioctl, port the driver to the `usb_fifo` framework, read an unfamiliar USB driver end-to-end, simulate a flaky cable with failure injection, or extend the regression script to cover the new module. The challenge material does not introduce new foundations; it stretches the ones the chapter has just taught. Spend time on the challenges in proportion to how much autonomy you expect to have on your next driver project.

Do not rush. This is the first chapter in the book whose material depends on real hardware or convincing simulation. Set aside a block of time when you can watch `dmesg` after `kldload` and read it slowly. A USB driver that attaches without errors is usually right; a USB driver whose attach messages you have not actually read is often wrong in a way that will cost you an hour of debugging two days later. The small discipline of reading the attach output as it happens, rather than assuming it, is a habit worth forming in Chapter 26 because every subsequent transport-specific chapter depends on it.

### Recommended Pacing

Three sitting structures work well for this chapter.

- **Two long sittings of four to six hours each.** First sitting: Introduction, Reader Guidance, How to Get the Most Out of This Chapter, Section 1, Section 2, and Lab 1. Second sitting: Section 3, Section 4, Section 5, Labs 2 through 5, and the Wrapping Up. The advantage of long sittings is that you stay in the mental model long enough to connect Section 1's vocabulary to Section 3's callback code.

- **Four medium sittings of two to three hours each.** Sitting 1: Introduction through Section 1 and Lab 1. Sitting 2: Section 2 and Lab 2. Sitting 3: Section 3 and Labs 3 and 4. Sitting 4: Section 4, Section 5, Lab 5, and Wrapping Up. The advantage is that each sitting has a crisp milestone.

- **A linear reading pass followed by a hands-on pass.** Day one: read the entire chapter start to finish without running any code, to get the full mental model in place. Day two or day three: return to the chapter with a kernel source tree and a lab VM open and work through the labs in sequence. The advantage of this approach is that the mental model is fully loaded before you touch code, which catches concept-level mistakes early.

Do not attempt the whole chapter in a single marathon session. The material is dense, and the USB callback state machine in particular does not reward tired reading.

### What a Good Study Session Looks Like

A good study session for this chapter has five elements visible at once. Put the book chapter on one side of your screen. Put the relevant FreeBSD source files in a second pane: `/usr/src/sys/dev/usb/usbdi.h`, `/usr/src/sys/dev/usb/misc/uled.c`, and `/usr/src/sys/dev/uart/uart_bus.h` are the three most useful to keep open. Put a terminal on your lab VM in a third pane. Put `man 4 usb`, `man 4 uart`, and `man 4 ucom` in a fourth pane for quick reference. Finally, keep a small note file open for questions you will want to answer later. If a term comes up you cannot define, write it in the note file and keep reading; if the same term comes up twice, look it up before continuing. This is the study posture that gets the most out of a long technical chapter.

### If You Do Not Have a USB Device to Test With

Many readers will not have a spare USB device that matches the vendor/product identifiers in the worked example. That is fine. Section 5 teaches three ways to proceed: QEMU USB device redirection from a host to a guest, `usb_template(4)` for FreeBSD-as-USB-device, and the simulated-device approach that tests driver logic without a real bus at all. The chapter's worked example is written so that the driver's match table can be swapped for one matching any USB device you do have on your desk. A USB flash drive will do. A mouse will do. A keyboard will do. The chapter explains how to point the driver at whatever device you happen to have, at the cost of temporarily stealing that device from the kernel's built-in driver, which the chapter also covers.

## How to Get the Most Out of This Chapter

Five habits pay off in this chapter more than in any of the earlier chapters.

First, **keep four short manual-page files open in a browser tab or a terminal pane**: `usb(4)`, `usb_quirk(4)`, `uart(4)`, and `ucom(4)`. These four pages together are the tightest overview the FreeBSD project has of the subsystems this chapter introduces. None of them is long. `usb(4)` describes the subsystem from the user's perspective and lists the `/dev` entries that appear. `usb_quirk(4)` lists the quirks table and explains what a quirk is, which will save you puzzlement later when you see quirk code in real drivers. `uart(4)` describes the serial subsystem from the user's perspective. `ucom(4)` describes the USB-to-serial framework. Skim each once at the start of the chapter. When the prose refers to "consult the manual page for details," return to the appropriate page. The manual pages are authoritative; this book is commentary.

Second, **keep three real drivers close to hand**. `/usr/src/sys/dev/usb/misc/uled.c` is a very small USB driver that talks to a USB-attached LED. It uses the `usb_fifo` framework, which is one of the two user-visible patterns the chapter teaches, and its entire attach function is smaller than a page. `/usr/src/sys/dev/usb/misc/ugold.c` is a slightly larger USB driver that reads temperature data from a TEMPer thermometer through interrupt transfers. It demonstrates the other common transfer type and shows how a driver uses a callout to pace its reads. `/usr/src/sys/dev/uart/uart_dev_ns8250.c` is the canonical 16550 UART driver; every PC serial port in the world uses it. Read each of these three files once at the start of the chapter and once more at the end. The first read will feel largely opaque; the second will feel almost obvious. That change is the measure of progress this chapter offers.

Third, **type every code addition by hand**. The `myfirst_usb.c` file grows through the chapter in roughly a dozen small increments. Each increment corresponds to a paragraph or two of prose. Typing the code by hand is what turns the prose into muscle memory. Pasting the code skips the lesson. If that sounds pedantic, notice that every working USB driver author has written a USB driver's attach function at least a dozen times; typing this one is the first of that dozen.

Fourth, **read `dmesg` after every `kldload`**. A USB driver produces a predictable pattern of attach messages: the device is detected on a port, the driver probes, the match succeeds, the driver attaches, the `/dev` node appears. If any of those steps is missing, something is wrong, and the sooner you notice the missing step, the sooner you fix it. The smallest discipline this chapter can give you is the habit of running `dmesg | tail -30` immediately after `kldload` and reading every line. If the output is boring, the driver probably works. If the output surprises you, investigate before proceeding.

Fifth, **after every section, ask yourself what would happen if you pulled the cable**. The question sounds silly; it is central. A well-written transport-specific driver is always one that handles being removed while in use. A USB driver in particular runs in a world where hot-unplug is the normal operating condition. If you find yourself writing a section of code and cannot answer "what if the cable were pulled right here," the section is not yet finished. Chapter 26 returns to this question often, not as rhetoric but as a discipline.

### What to Do When Something Does Not Work

It will not all work the first time. USB drivers have a few common failure modes, and the chapter documents each of them in the troubleshooting section at the end. A short preview of the most common ones:

- The driver compiles but does not attach when the device is plugged in. Usually the match table has the wrong vendor or product identifier. The fix is to verify the identifier with `usbconfig dump_device_desc`.
- The driver attaches but the `/dev` node does not appear. Usually the `usb_fifo_attach` call failed because the name conflicts with an existing device. The fix is to change the `basename` or to detach the conflicting driver first.
- The driver attaches but the first transfer never completes. Usually `usbd_transfer_start` was not called, or the transfer was submitted with a zero-length frame. The fix is to trace through `USB_ST_SETUP` and confirm that `usbd_xfer_set_frame_len` was called before `usbd_transfer_submit`.
- The driver attaches but the kernel panics on unplug. Usually the detach path is missing a `usbd_transfer_unsetup` call or a `usb_fifo_detach` call. The fix is to run the detach sequence under INVARIANTS and follow the WITNESS output back to the first dropped cleanup.

The troubleshooting section at the end of the chapter develops each of these cases in full, with diagnostic commands and expected output. The goal of this chapter is not to have everything work on the first try; the goal is to have a systematic debugging posture that turns every failure into a teachable moment.

### Roadmap Through the Chapter

The sections in order are:

1. **Understanding USB and Serial Device Fundamentals.** The USB mental model at the level needed to write a driver: host and device, hubs and ports, classes, descriptors, transfer types, hot-plug lifecycle. The serial mental model: UART hardware, RS-232 framing, baud rate, parity, flow control, the tty discipline. The FreeBSD-specific split between `uart(4)` and `ucom(4)`. A first exercise with `usbconfig` and `dmesg` that grounds the vocabulary in a device you can see.

2. **Writing a USB Device Driver.** The FreeBSD USB subsystem layout. The Newbus shape of a USB driver. `STRUCT_USB_HOST_ID` and the match table. `DRIVER_MODULE` with `uhub` as the parent. `MODULE_DEPEND` on `usb`. `USB_PNP_HOST_INFO` for auto-load. The probe method using `usbd_lookup_id_by_uaa`. The attach method, the softc layout, the labelled-goto cleanup chain in attach and detach.

3. **Performing USB Data Transfers.** The `struct usb_config` array. `usbd_transfer_setup` and the lifetime of a `struct usb_xfer`. Control, bulk, and interrupt transfer shapes. The `usb_callback_t` state machine and `USB_GET_STATE`. Stall handling with `usbd_xfer_set_stall`. Frame-level operations (`usbd_xfer_set_frame_len`, `usbd_copy_in`, `usbd_copy_out`). Creating a `/dev` entry for the USB device through the `usb_fifo` framework. A worked example that sends bytes down a bulk-out endpoint and reads bytes back from a bulk-in endpoint.

4. **Writing a Serial (UART) Driver.** The `uart(4)` subsystem at the level needed to read a real driver. The `uart_class`/`uart_ops` split. The method table dispatched through kobj. The relationship between `uart_bus_attach` and `uart_tty_attach`. Baud rate, data bits, stop bits, parity, and the `param` method. RTS/CTS hardware flow control. `struct termios` and how it reaches the driver. `/dev/ttyu*` versus `/dev/cuau*` in FreeBSD. The `ucom(4)` framework for USB-to-serial bridges. A guided reading of the `ns8250` driver as a canonical example.

5. **Testing USB and Serial Drivers Without Real Hardware.** `nmdm(4)` virtual null-modem pairs for serial testing. `cu(1)` and `tip(1)` for terminal access. `stty(1)` and `comcontrol(8)` for configuration. QEMU USB device redirection for host-to-guest pass-through. `usb_template(4)` for FreeBSD-as-gadget testing. Software loopback patterns that exercise driver logic without any device at all. A reproducible test harness that runs a regression without human intervention.

After the five sections come a set of hands-on labs, a set of challenge exercises, a troubleshooting reference, a Wrapping Up that closes Chapter 26's story, a bridge to Chapter 27, and a glossary. Read linearly on a first pass.

## Section 1: Understanding USB and Serial Device Fundamentals

The first section teaches the mental models the rest of the chapter relies on. USB and serial devices share a surprising amount of machinery at the `tty`/`cdevsw` layer, and at the same time they differ dramatically at the transport layer. A reader who is clear on both the similarities and the differences will find Sections 2 through 5 straightforward. A reader who is not will find the subsequent code confusingly non-obvious. This section is the single best place to spend an extra thirty minutes if you want the rest of the chapter to feel easier.

The section is organised in three arcs. The first arc establishes what a *transport* is, and why transport-specific drivers look different from the pseudo-drivers of Parts 2 through 5. The second arc teaches the USB model: host and device, hubs and ports, classes, descriptors, endpoints, transfer types, and the hot-plug lifecycle. The third arc teaches the serial model: the UART, RS-232 framing, baud rate, parity, flow control, and the FreeBSD-specific split between `uart(4)` and `ucom(4)`. A first exercise at the end grounds the vocabulary in a device you can see with `usbconfig`.

### What a Transport Is, and Why It Matters Here

A *transport* is the protocol and the lifecycle by which a device is connected to the rest of the system. Up to this point in the book, the `myfirst` driver has had no transport. Its device existed entirely in the Newbus tree, connected to the `nexus` through the `pseudo` parent, and its data flowed into a buffer in kernel memory. That makes `myfirst` a *pseudo-device*: a device whose existence is entirely a software fiction. Pseudo-devices are essential teaching tools. They let the reader learn Newbus, softc management, character device interfaces, ioctl handling, locking, and the rest, without also learning the specifics of a bus. By now, those topics are covered.

A transport-specific driver, by contrast, is one that attaches to a *real* bus. The bus has its own rules. It has its own way of saying "a new device has appeared." It has its own way of delivering data. It has its own way of saying "a device has been removed." A transport-specific driver is still a Newbus driver (that never changes in FreeBSD), but its parent is no longer the abstract `pseudo` bus. Its parent is `uhub` if it is a USB driver, `pci` if it is a PCI driver, `acpi` or `fdt` if it is on an embedded platform, and so on. The driver's attach method receives arguments specific to that bus. Its cleanup responsibilities include bus-specific resources in addition to the ones it already had. Its lifecycle is the bus's lifecycle, not the module's lifecycle.

Three broad categories of work distinguish a transport-specific driver from the pseudo-drivers of Parts 2 through 5. They are worth naming explicitly because they recur in every transport chapter in Part 6.

The first is *matching*. A pseudo-driver attaches on module load; there is nothing to match because there is no real device. A transport-specific driver has to declare which devices it handles. On USB, this means a match table of vendor and product identifiers. On PCI, it means a match table of vendor and device identifiers. On ACPI or FDT, it means a match table of string identifiers. The kernel's bus code enumerates devices as they appear and offers each one to every registered driver in turn; the driver's probe method decides whether to claim the device. Getting the match table right is the first obstacle every transport-specific driver faces.

The second is *transfer mechanics*. A pseudo-driver's `read` and `write` methods touch a buffer in RAM. A transport-specific driver's `read` and `write` methods have to arrange for data to move across the bus. On USB, this means setting up one or more transfers using `usbd_transfer_setup`, submitting them with `usbd_transfer_submit`, and handling completion in a callback. On PCI, this means programming a DMA engine. On storage, this means translating block requests into bus transactions. The transfer mechanism is bus-specific and is where most of a transport-specific driver's new code lives.

The third is *hot-plug lifecycle*. A pseudo-driver is loaded when the module is loaded and detached when the module is unloaded. That is a simple lifecycle; `kldload` and `kldunload` are the only events it has to respond to. A transport-specific driver has to deal with *hot-plug*: the device can appear and disappear independently of the module's lifecycle. A USB device can be unplugged in the middle of a read. A SATA disk can be yanked out while the filesystem on it is mounted. An Ethernet cable can be pulled while a TCP connection is open. The driver's attach method runs when a device is physically inserted; its detach method runs when a device is physically removed. The detach may happen while the driver is still in use. Handling this correctly is the third big obstacle every transport-specific driver faces.

The rest of Part 6 is about those three categories of work in three different transports. Chapter 26 teaches USB and serial. Chapter 27 teaches storage. Chapter 28 teaches networking. The matching, the transfer mechanics, and the hot-plug lifecycle look different in each transport, but the three-category structure repeats. That structure is what makes it possible to learn one transport well and then learn the next one quickly.

A useful shorthand: in Parts 2 through 5, you learned how to *be* a Newbus driver. In Part 6, you learn how to *attach* to a bus that has its own ideas about when and how you exist.

### The USB Mental Model

USB, the Universal Serial Bus, is a tree-structured, host-controlled, hot-pluggable serial bus. Every one of those adjectives matters, and understanding each of them is the foundation of writing a USB driver.

*Tree-structured* means that USB devices do not sit on a shared wire like devices on an I2C bus or an old ISA bus. Every USB device has exactly one upstream connection, to a parent hub. The root of the tree is the *root hub*, which is exposed by the USB host controller. Downstream of the root hub are other hubs and devices. A hub has a fixed number of downstream ports; each port can either be empty or connect to exactly one device. The tree is rebuilt on boot and updated every time a device is connected or disconnected. On FreeBSD, `usbconfig` shows this tree; on a fresh boot of a typical desktop you will see something like:

```text
ugen0.1: <Intel EHCI root HUB> at usbus0, cfg=0 md=HOST spd=HIGH
ugen1.1: <AMD OHCI root HUB> at usbus1, cfg=0 md=HOST spd=FULL
ugen0.2: <Some Vendor Hub> at usbus0, cfg=0 md=HOST spd=HIGH
ugen0.3: <Some Vendor Mouse> at usbus0, cfg=0 md=HOST spd=LOW
```

The tree structure matters to a driver author for two reasons. First, it tells you that when you write a USB driver, your driver's *parent* in the Newbus tree is `uhub`. Every USB device sits under a hub. When you write `DRIVER_MODULE(myfirst_usb, uhub, ...)`, you are telling the kernel "my driver attaches to children of `uhub`," which is the FreeBSD way of saying "my driver attaches to USB devices." Second, the tree structure means that enumeration is dynamic. The kernel does not know what devices are on the tree until the tree is walked; a driver is offered each device as it appears, and has to decide whether to claim it.

*Host-controlled* means that one side of the bus is the master, the *host*, and all other sides are slaves, the *devices*. The host initiates every transfer; devices respond. A USB keyboard does not push keystrokes to the host whenever a key is pressed; the host polls the keyboard on an *interrupt endpoint* many times per second, and the keyboard replies with "no new keys" or "key 'A' has been pressed" in response to each poll. This polling-and-response model has important consequences for a driver. Your driver, running on the host, has to initiate every transfer. A device cannot spontaneously send data; it can only respond when the host asks. What looks from user space like "the driver received data" is always, underneath, "the driver had a pending receive transfer and the host controller notified us that the transfer completed."

For most of this chapter's purposes, you are writing *host-mode* drivers: drivers that run on the host side. A FreeBSD system can also be configured as a USB *device*, through the `usb_template(4)` subsystem, and present itself as a keyboard or mass storage device or CDC Ethernet interface to another host. Device-mode drivers are a specialised topic touched on only briefly in Section 5 for testing purposes.

*Hot-pluggable* means that devices can appear and disappear while the system is running, and the subsystem has to cope. The USB host controller notices when a device is plugged in, a hub's port status registers tell it so, enumerates the new device by asking it for its descriptors, assigns it an address on the bus, and then offers it to any driver whose match table applies. When a device is unplugged, the host controller notices the port status change and tells the subsystem, which in turn calls the driver's detach method. The detach method may run at any time, including while the driver is holding a transfer that will now never complete, while user space has the driver's `/dev` node open, or while the system is under load. Writing a correct detach method is the single hardest part of USB driver development. The chapter returns to this repeatedly.

*Serial* means that USB is a wire-level serial protocol: bytes flow one after another on a differential pair. The speed of the bus has evolved over the years: low-speed (1.5 Mbps), full-speed (12 Mbps), high-speed (480 Mbps), SuperSpeed (5 Gbps), and faster variants above that. From a driver author's perspective, the speed is mostly transparent: the host controller and the USB core handle the electrical layer and the packet framing, and your driver works at the level of "here is a buffer, please send it" or "here is a buffer, please fill it." The speed determines how fast data can move, but the driver code is the same.

With those four adjectives in place, the rest of the USB model falls into shape.

#### Device Classes and What They Mean to a Driver

Every USB device belongs to one or more *classes*, and the class tells the host (and the driver) what kind of device it is. Classes are numerical, defined by the USB Implementers Forum, and the values appear in descriptors. The ones a FreeBSD driver author will see most often include:

- **HID (Human Interface Device)**, class 0x03. Keyboards, mice, joysticks, game controllers, and a long tail of programmable devices that pretend to be keyboards or mice. HID devices present reports through interrupt endpoints; FreeBSD's HID subsystem handles them mostly generically, though a vendor-specific driver can override.
- **Mass Storage**, class 0x08. USB flash drives, external disks, card readers. These attach through `umass(4)` to the CAM storage framework.
- **Communications (CDC)**, class 0x02, with subclasses for ACM (modem-like serial), ECM (Ethernet), NCM (Ethernet with multi-packet aggregation), and others. CDC ACM devices appear through `ucom(4)` as serial ports. CDC ECM and NCM devices appear through `cdce(4)` as network interfaces.
- **Audio**, class 0x01. Microphones, speakers, audio interfaces. FreeBSD's audio stack handles these through `uaudio(4)`.
- **Printer**, class 0x07. USB printers. Handled through `ulpt(4)`.
- **Hub**, class 0x09. USB hubs themselves. Handled by the core `uhub(4)` driver.
- **Vendor-specific**, class 0xff. Any device whose functionality does not fit a standard class. Almost every interesting hobby USB device (USB-to-serial bridges, thermometers, relay controllers, programmers, loggers) is in this class.

When you write a USB driver, you often write for a vendor-specific device (class 0xff) and match on vendor/product identifiers. Occasionally you write for a standard-class device that FreeBSD does not yet handle, or for a standard-class device that has quirks requiring a dedicated driver. The class is not usually the match criterion; the vendor/product pair is. But the class tells you what framework, if any, you should integrate with. If the device's class is CDC ACM, the right framework is `ucom`. If the class is HID, the right framework is `hidbus` (new in FreeBSD 14). If the class is 0xff, there is no framework; you write a bespoke driver.

#### Descriptors: The Device's Self-Description

When the host enumerates a new USB device, it asks the device to describe itself. The device responds with a hierarchy of *descriptors*. Descriptors are the single most important USB concept to get clear: they are the USB equivalent of the PCI configuration space, but richer and nested.

The hierarchy is:

```text
Device descriptor
  Configuration descriptor [1..N]
    Interface descriptor [1..M] (optionally with alternate settings)
      Endpoint descriptor [0..E]
```

A *device descriptor* (`struct usb_device_descriptor` in `/usr/src/sys/dev/usb/usb.h`) describes the device as a whole: its vendor identifier (`idVendor`), its product identifier (`idProduct`), its device class, subclass, and protocol, its maximum packet size on endpoint zero, its release number, and the number of configurations it supports. Most devices have one configuration, but the USB spec allows more (a camera that can run in high-bandwidth or low-bandwidth modes, for example).

A *configuration descriptor* (`struct usb_config_descriptor`) describes one mode of operation: the number of interfaces it contains, whether the device is self-powered or bus-powered, its maximum power draw. When a driver selects a configuration (by calling `usbd_req_set_config`, though in practice the USB core does this for you), the device's endpoints are activated.

An *interface descriptor* (`struct usb_interface_descriptor`) describes one logical function of the device. A composite device, such as a USB printer with a built-in scanner, has one interface per function. Each interface has its own class, subclass, and protocol. A driver can match on interface class rather than device class; this is common when a device's overall class is "Miscellaneous" or "Composite" but one of its interfaces has a specific class. An interface can have multiple *alternate settings*, which select different endpoint layouts; audio streaming interfaces use alternate settings to offer different bandwidths.

An *endpoint descriptor* (`struct usb_endpoint_descriptor`) describes one data channel. Endpoints have:

- An *address*, which is the endpoint number (0 through 15) combined with a direction bit (IN, meaning from device to host, or OUT, meaning from host to device).
- A *type*, which is one of control, bulk, interrupt, or isochronous.
- A *maximum packet size*, which is the largest single packet the endpoint can handle.
- An *interval*, which for interrupt and isochronous endpoints tells the host how often to poll.

Endpoint zero is special: every device has it, it is always a control endpoint, and it is always bidirectional (one IN half and one OUT half). The USB core uses endpoint zero for enumeration (asking the device for descriptors, setting its address, selecting its configuration). A driver can also use endpoint zero for vendor-specific control requests, though drivers usually access it through helper functions rather than setting up a transfer directly.

The descriptor hierarchy matters to a driver because the driver's `probe` method has access to the descriptors through the `struct usb_attach_arg` it receives, and its match logic often reads fields from them. The `struct usbd_lookup_info` inside `struct usb_attach_arg` carries the device's identifiers, its class, subclass, and protocol, the current interface's class, subclass, and protocol, and a few other fields. The match table filters on some subset of those; the helper macros `USB_VP(v, p)`, `USB_VPI(v, p, info)`, `USB_IFACE_CLASS(c)`, and similar build entries that match different combinations of fields.

#### The Four Transfer Types

USB defines four transfer types, each suited to a different kind of data movement. A driver picks one or more types for its endpoints, and the choice affects everything about how the driver is structured.

*Control transfers* are for setup, configuration, and command exchange. Every device supports them on endpoint zero. They have a small, structured format: an eight-byte setup packet (the `struct usb_device_request`) followed by an optional data stage and a status stage. The setup packet specifies what the request is doing: its direction (IN or OUT), its type (standard, class, or vendor), its recipient (device, interface, or endpoint), and four fields (`bRequest`, `wValue`, `wIndex`, `wLength`) whose meaning depends on the request. Standard requests include `GET_DESCRIPTOR`, `SET_CONFIGURATION`, and so on; class and vendor requests are defined by the class specification or the vendor. Control transfers are reliable: the bus protocol guarantees delivery or returns a specific error. They are also relatively slow, because the bus allocates only a small share of its bandwidth to them.

*Bulk transfers* are for large, reliable, non-time-critical data. A USB flash drive uses bulk transfers for the actual data. A printer uses bulk OUT for the print stream. A USB-to-serial bridge uses bulk IN and bulk OUT for the two directions of the serial stream. Bulk transfers are reliable (errors are retried by the bus hardware), but they have no guaranteed timing: they use whatever bandwidth is left after control, interrupt, and isochronous traffic has been scheduled. In practice, on a lightly-loaded bus, bulk transfers are very fast. On a heavily-loaded bus, they can stall for milliseconds at a time. Bulk endpoints are the most common endpoint type for device-to-host or host-to-device streaming of data where latency is not critical.

*Interrupt transfers* are for small, time-sensitive data. The name is misleading: there are no hardware interrupts here. The "interrupt" refers to the fact that the device needs to get the host's attention periodically, and the host polls the endpoint at a configurable interval to see whether there is new data. A USB keyboard uses interrupt transfers to deliver keystrokes; a USB mouse uses them for movement reports; a thermometer uses them to deliver periodic readings. Interrupt endpoints have an `interval` field that tells the host how often to poll (in milliseconds for low- and full-speed devices, in microframes for high-speed). A driver that wants to know about input as it happens sets up an interrupt-IN transfer, submits it, and the USB core arranges the polling. When data arrives, the driver's callback fires.

*Isochronous transfers* are for streaming data with guaranteed bandwidth but no error recovery. USB audio and USB video use isochronous endpoints. The bus reserves a fixed share of each frame for isochronous traffic, so the bandwidth is predictable, but transfers are not retried on error; if a packet is corrupted, it is lost. This trade-off makes sense for audio and video, where a dropped sample is better than a stall. Isochronous transfers are the most complex to program because they typically operate on many small frames per transfer; the `struct usb_xfer` machinery supports up to thousands of frames per transfer. Chapter 26 introduces isochronous transfers at the conceptual level and does not develop them further; real isochronous drivers (audio, video) are beyond the chapter's scope.

A typical vendor-specific USB device that a hobbyist or a driver-development learner will write code for looks like this: a vendor/product identifier, one vendor-specific interface, one bulk-IN endpoint, one bulk-OUT endpoint, and possibly an interrupt-IN endpoint for status events. That is the shape of the worked example in Sections 2 and 3.

#### The USB Hot-Plug Lifecycle

The hot-plug lifecycle is the sequence of events that happens when a USB device is inserted, in use, and removed. Writing a driver that handles this lifecycle correctly is the most important single discipline in USB driver development.

When a device is inserted, the host controller notices a port status change. It waits for the device to stabilise, then resets the port and assigns the device a temporary address of zero. It sends `GET_DESCRIPTOR` to endpoint zero on address zero, retrieves the device descriptor, and then assigns the device a unique address with `SET_ADDRESS`. All subsequent communication uses the new address. The host sends `GET_DESCRIPTOR` for the full configuration descriptor (including interfaces and endpoints), chooses a configuration, and sends `SET_CONFIGURATION`. At that point the device's endpoints are active and the USB subsystem offers the device to every registered driver in turn by calling each driver's `probe` method. The first driver to claim the device by returning a non-error code from `probe` wins; the subsystem then calls that driver's `attach` method.

During normal operation, the driver submits transfers to its endpoints, the host controller schedules them on the bus, and the callbacks fire on completion. This is the steady state Chapter 26's code examples operate in.

When the device is removed, the host controller notices another port status change. It does not wait; the electrical signal is gone immediately. The subsystem cancels any outstanding transfers on the device's endpoints (they complete in the callback with `USB_ERR_CANCELLED`), and then it calls the driver's `detach` method. The `detach` method has to release every resource the `attach` method acquired, including any `/dev` nodes it created, any locks, any buffers, and any transfers. It has to do this in the face of the fact that other threads may be in the middle of calling into the driver through those resources. A read in progress must be woken up and returned with an error. An ioctl in progress must be allowed to finish or interrupted. A callback that has just fired with `USB_ERR_CANCELLED` must not try to re-submit.

The hot-plug lifecycle is why USB drivers cannot be written the way pseudo-drivers are written. In a pseudo-driver, the module lifecycle (`kldload`/`kldunload`) is the only lifecycle; nothing unexpected happens. In a USB driver, the device lifecycle is separate from the module lifecycle and is driven by physical events. A user can unplug the device while a user-space process is blocked in `read()` on the driver's `/dev` node. The driver must wake that process up and return an error. A well-written USB driver treats this as the normal case, not the edge case.

Section 2 will walk through the structure of a USB driver that handles this correctly. For now, keep the lifecycle in mind: probe, attach, steady-state transfers, detach. Every USB driver has that sequence.

#### USB Speeds and What They Imply

USB has gone through several speed generations, and each matters to a driver writer in different ways. Low-speed (1.5 Mbps) was the original USB 1.0 speed, mostly used by keyboards and mice. Full-speed (12 Mbps) was USB 1.1, used by printers, early cameras, and mass-storage devices. High-speed (480 Mbps) was USB 2.0, which became the dominant speed for most devices in the 2000s. SuperSpeed (5 Gbps) was USB 3.0, which added a separate physical layer for high-throughput applications. SuperSpeed+ (10 Gbps and 20 Gbps) came with USB 3.1 and 3.2. USB 4.0 reuses the Thunderbolt physical layer and supports 40 Gbps.

For most driver writing, only three differences between these speeds matter:

**Maximum packet size.** Low-speed endpoints have a maximum packet size of 8 bytes. Full-speed goes up to 64 bytes. High-speed bulk endpoints go up to 512 bytes. SuperSpeed bulk endpoints go up to 1024 bytes with burst support. Your buffer sizes in the transfer configuration should match the endpoint's speed; using a 512-byte buffer on a full-speed bulk endpoint wastes memory because only 64 bytes fit in each packet.

**Isochronous bandwidth.** Isochronous transfers reserve bandwidth at a specific speed. A device that asks for 1 MB/s of isochronous bandwidth can only be supported on a host controller that can provide it; on slower hosts, the device must negotiate a lower rate or fail. This is why some USB audio devices work on one port but not another.

**Endpoint polling interval.** Interrupt endpoints are polled at a specific interval encoded in the descriptor's `bInterval` field. The units are milliseconds at low/full speed and "125 microsecond microframes" at high/SuperSpeed. The framework handles the math; your driver just declares the logical polling interval via the endpoint descriptor and the framework does the right thing.

For the drivers we write in this chapter (`myfirst_usb` and the UART bridges like FTDI), speed does not affect the code structure. A bulk-IN channel's callback is the same whether it runs at 12 Mbps or 5 Gbps. The differences are in the numbers, not the flow.

#### Endpoints, FIFOs, and Flow Control

A USB endpoint is logically an I/O queue at one end of a pipe. On the device side, an endpoint corresponds to a hardware FIFO in the chip. On the host side, the endpoint is a framework abstraction. Between them, USB packets flow under the control of the USB protocol itself, which handles retransmission, sequencing, and error detection.

The host cannot be told "the device is full" the way you might expect on a traditional serial link. Instead, when a device cannot accept more data (because its FIFO is full), it returns a NAK handshake. NAK means "try again later." The host will keep retrying, at the protocol level, until either the device accepts the data (returns ACK) or some higher-level timeout fires. This is called NAK-limiting or bus throttling, and it happens invisibly to the driver: the framework sees the final ACK and delivers a successful completion.

Similarly, when the device has no data to send (for a bulk-IN or interrupt-IN transfer), it returns NAK to the IN token, and the host polls again. From the driver's perspective, the transfer is simply "pending" until the device has something to say.

This NAK mechanism is how USB handles flow control at the protocol level. Your driver does not need to implement its own throttling logic for bulk and interrupt channels; the USB protocol does it. Where flow control does come into play is in higher-level protocols, where the device might want to signal a logical end-of-message or a temporary unavailability. Those signals are protocol-specific and not part of USB itself.

#### Descriptors In Depth

USB descriptors are the self-describing mechanism by which a device tells the host what it is and how to talk to it. We introduced them briefly earlier; here is a more complete picture.

The device descriptor is the root. It contains the device's vendor ID, product ID, USB specification version, device class/subclass/protocol (for devices that declare themselves at the device level rather than the interface level), maximum packet size for endpoint zero, and the number of configurations.

Configuration descriptors describe complete configurations. A configuration is a set of interfaces that work together. Most devices have one configuration; some have multiple to support different modes of operation (e.g., a device that can be either a printer or a scanner, selected by configuration).

Interface descriptors describe functional subsets of the device. Each interface has a class, subclass, and protocol that tells the host what kind of driver to use. A multi-function device has multiple interface descriptors in the same configuration. Additionally, an interface can have alternate settings: different sets of endpoints selectable on the fly for things like "low bandwidth mode" vs "high bandwidth mode".

Endpoint descriptors describe individual endpoints within an interface. Each has an address (with direction bit), a transfer type, a maximum packet size, and an interval (for interrupt and isochronous endpoints).

String descriptors hold human-readable strings: the manufacturer name, the product name, the serial number. These are optional; their presence is indicated by nonzero string indices in the other descriptors.

Class-specific descriptors extend the standard descriptors with class-specific metadata. HID devices have a report descriptor that describes the format of the reports they send. Audio devices have descriptors for audio controls. Mass-storage devices have descriptors for interface subclasses.

The USB framework parses all of this at enumeration time and exposes the parsed data to drivers through the `struct usb_attach_arg`. Your driver does not have to read descriptors itself; it queries the framework for the information it needs. When the chapter says "the interface's `bInterfaceClass`", what is meant is "the `bInterfaceClass` field of the interface descriptor the framework parsed and cached for us."

`usbconfig -d ugenN.M dump_all_config_desc` is how you see the parsed descriptors from userland. Run that command on a few devices you own and note how the descriptors look. You will see that even simple devices like a mouse have a nontrivial descriptor tree: typically one device descriptor, one configuration descriptor, one interface descriptor (with class=HID), and one or two endpoint descriptors (for the HID report input and maybe an output).

#### Request-Response Over USB

The USB control transfer type supports a request-response pattern between host and device. A control transfer consists of three phases: a setup stage where the host sends an 8-byte setup packet describing the request, an optional data stage where either the host sends data or the device returns data, and a status stage where the recipient acknowledges the operation.

The setup packet has five fields:

- `bmRequestType`: describes the direction (in or out), the type of request (standard, class, or vendor), and the recipient (device, interface, endpoint, or other).
- `bRequest`: the request number. Standard requests have well-known numbers (GET_DESCRIPTOR = 6, SET_ADDRESS = 5, and so on). Class and vendor requests have class-specific or vendor-specific meanings.
- `wValue`: a 16-bit parameter, often used to specify a descriptor index or a value to set.
- `wIndex`: another 16-bit parameter, often used to specify an interface or endpoint.
- `wLength`: the number of bytes in the data stage (zero if there is no data stage).

Every USB device must support a small set of standard requests: GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, and a few others. The framework handles all of these at enumeration time. Your driver may also issue vendor-specific requests to configure the device in ways the standard does not define.

For example, the FTDI driver issues vendor-specific requests like `FTDI_SIO_SET_BAUD_RATE`, `FTDI_SIO_SET_LINE_CTRL`, and `FTDI_SIO_MODEM_CTRL` to program the chip. These requests are documented in FTDI's application notes; they are not part of USB itself, but they work over the USB control-transfer mechanism.

When your driver needs to issue a vendor-specific control request, the pattern is the one we showed in Section 3: construct the setup packet, copy it into frame zero of a control transfer, copy any data into frame one (for data-stage requests), and submit. The framework handles the three phases and calls your callback when the transfer completes.

### The Serial Mental Model

The serial side of Chapter 26 is about a much older and much simpler protocol than USB. Serial communication over a UART is one of the oldest ways two computers can talk to each other, and its simplicity is both its strength and its limitation. A reader coming to UART after USB will find the protocol almost trivially small. But the integration with the rest of the operating system, the tty discipline, baud rate management, parity, flow control, and the two-worlds split between `uart(4)` and `ucom(4)`, is where most of the actual work lives.

#### The UART as a Piece of Hardware

A UART is a *Universal Asynchronous Receiver/Transmitter*: a chip that converts bytes into a serial bit stream on a wire and back again. The classical UART has two pins for data (TX and RX), two pins for flow control (RTS and CTS), four pins for modem status (DTR, DSR, DCD, RI), a ground pin, and occasionally a pin for a second "ring" signal that most modern equipment ignores. On a classic PC, the serial port has a nine-pin or twenty-five-pin D-subminiature connector and operates at RS-232 voltage levels (typically +/- 12 V). Modern embedded UARTs usually operate at 3.3 V or 1.8 V logic levels; a level converter chip sits between the UART and the RS-232 connector if a compatible port is needed.

Inside the UART, the core is a shift register. When the driver writes a byte to the UART's transmit register, the UART adds a start bit, shifts the byte out bit by bit at the configured baud rate, adds an optional parity bit, and then adds one or two stop bits. When a receiving UART detects a falling edge (the start bit), it samples the line at the middle of each bit time, assembles the bits into a byte, checks the parity, verifies the stop bit, and then stores the byte in its receive register. If any of those steps fails (the parity does not match, the stop bit is wrong, the framing is off), the UART notes a framing error, a parity error, or a break condition in its status register.

On most modern UARTs, the single receive and transmit registers are backed by small first-in-first-out buffers (FIFOs). The 16550A UART, still the de facto standard, has a 16-byte FIFO on each side. A driver that programs the FIFO with an appropriate "trigger level" can let the hardware buffer incoming bytes and raise an interrupt only when the FIFO passes the trigger level. This is the difference between "one interrupt per byte" (slow) and "one interrupt per trigger level" (fast). The 16550A's FIFO is a big part of why this chip became the universal PC standard.

The UART's speed is controlled by a *baud rate divisor*: the UART has an input clock (often 1.8432 MHz on classic PC hardware), and the baud rate is the clock divided by 16 times the divisor. A divisor of 1 with a 1.8432 MHz clock gives 115200 baud. A divisor of 12 gives 9600 baud. The FreeBSD `ns8250` driver computes the divisor from the requested baud rate and programs it into the UART's divisor-latch registers. Section 4 walks through this code.

RS-232 framing is the full protocol: start bit (one), data bits (five, six, seven, or eight), optional parity bit (none, odd, even, mark, or space), stop bit (one or two). A typical modern configuration is "8N1": eight data bits, no parity, one stop bit. An older configuration sometimes seen on industrial equipment is "7E1": seven data bits, even parity, one stop bit. The driver programs the UART's line control register to select the framing; `struct termios` carries the configuration from user space.

#### Flow Control

The UART can transmit faster than the receiver can read if the receiver's code is slow or is doing other work. *Flow control* is how the receiver tells the transmitter to pause. Two mechanisms exist.

*Hardware flow control* uses two extra wires: *RTS* (Request To Send) from the receiver, and *CTS* (Clear To Send) from the transmitter's perspective (it is the wire the other side drives). When the receiver's buffer is filling up, it deasserts RTS. The transmitter, seeing CTS deasserted, stops transmitting. When the buffer empties, the receiver reasserts RTS, CTS asserts on the other side, and transmission resumes. Hardware flow control is reliable and requires no software overhead on either side; it is the default choice when the hardware supports it.

*Software flow control*, also called XON/XOFF, uses two in-band bytes: XOFF (traditionally ASCII DC3, 0x13) to pause transmission, and XON (ASCII DC1, 0x11) to resume. The receiver sends XOFF when it is almost full and XON when it has room again. This mechanism works over a three-wire connection (TX, RX, ground) with no extra pins, at the cost of reserving two byte values for control use. If you are sending binary data that may contain 0x11 or 0x13, you cannot use software flow control; hardware flow control is the only option.

FreeBSD's tty discipline handles software flow control entirely in software, at the line-discipline layer, with no involvement from the UART driver. Hardware flow control is partly in the driver (the driver programs the UART's automatic RTS/CTS feature if the chip supports it) and partly in the tty layer. A driver author should know which flow control method the tty layer has selected; the CRTSCTS flag in `struct termios` signals hardware flow control.

#### /dev/ttyuN and /dev/cuauN: A FreeBSD-Specific Quirk

The FreeBSD tty layer creates two device nodes per serial port. The *callin* node is `/dev/ttyuN` (where N is the port number, 0 for the first port). The *callout* node is `/dev/cuauN`. The distinction is historical, from the days of dial-up modems, and remains useful.

A process opening `/dev/ttyuN` is saying "I want to answer an incoming call": the open blocks until the modem raises DCD (Data Carrier Detect). Once DCD is up, the open completes. When DCD drops, the open process receives SIGHUP. The node is for incoming connections.

A process opening `/dev/cuauN` is saying "I want to make an outgoing call": the open succeeds immediately, without blocking on DCD. The process can then dial out or, on non-modem uses, simply talk to the serial port. The node is for outgoing connections, and more generally for any use that does not require modem semantics.

In modern use, when a serial port is connected to something that is not a modem (a microcontroller, a console, a GPS receiver), the right node to open is almost always `/dev/cuau0`. Opening `/dev/ttyu0` on a non-modem port will usually hang, because DCD is never asserted. The distinction is FreeBSD-specific; Linux has no callout nodes and uses `/dev/ttyS0` or `/dev/ttyUSB0` for everything.

The chapter's labs will use `/dev/cuau0` and the simulated pair `/dev/nmdm0A`/`/dev/nmdm0B` for serial exercises. The callin nodes are not used.

#### Two Worlds: `uart(4)` and `ucom(4)`

FreeBSD separates real UART hardware from USB-to-serial bridges into two distinct subsystems. The separation is not visible from user space (a USB serial adapter and a built-in serial port both appear as tty devices), but it is very visible from inside the kernel, and a driver author must not confuse the two.

`uart(4)` is the subsystem for real UARTs. Its scope includes the built-in serial port on a PC motherboard, PCI serial cards, the PrimeCell `PL011` found on ARM embedded boards, the embedded SoC UARTs on i.MX, Marvell, Qualcomm, Broadcom, and Allwinner platforms, and so on. The `uart` subsystem lives in `/usr/src/sys/dev/uart/`. Its core code is in `uart_core.c` and `uart_tty.c`. Its canonical hardware driver is `uart_dev_ns8250.c`. A driver that attaches to a real UART writes a `uart_class` and a small set of `uart_ops`, and the subsystem handles everything else. The `/dev` nodes that `uart(4)` creates are called `ttyu0`, `ttyu1`, and so on (callin) and `cuau0`, `cuau1`, and so on (callout).

`ucom(4)` is the framework for USB-to-serial bridges: FTDI, Prolific, Silicon Labs, WCH, and similar. Its scope is *not* a UART at all; it is a USB device whose endpoints happen to behave like a serial port. The `ucom` framework lives in `/usr/src/sys/dev/usb/serial/`. Its header is `usb_serial.h`. Its body is `usb_serial.c`. A USB-to-serial driver writes USB probe, attach, and detach methods as in any other USB driver, and then registers a `struct ucom_callback` with the framework. The callback has entries for "open", "close", "set line parameters", "start reading", "stop reading", "start writing", and so on. The framework creates the `/dev` node (called `ttyU0`, `ttyU1` for callin, `cuaU0`, `cuaU1` for callout, note the capital U) and runs the tty discipline on top of the driver's USB transfers.

The two worlds never mix. `uart(4)` is for hardware that is physically a UART. `ucom(4)` is for USB devices that behave like a UART. A USB-to-serial adapter is a `ucom` driver, not a `uart` driver. A PCI serial card is a `uart` driver (specifically, a shim in `uart_bus_pci.c`), not a `ucom` driver. The user-space interface is similar (both produce `cu*` device nodes), but the kernel code is entirely disjoint.

A historical note that sometimes confuses readers: FreeBSD once had a separate `sio(4)` driver for 16550-family UARTs. `sio(4)` was retired years ago and is not present in FreeBSD 14.3. If you see references to `sio` in older documentation, translate them mentally to `uart(4)`. Do not try to find or extend `sio`; it is gone.

#### What termios Carries, and Where It Goes

`struct termios` is the user-space structure that configures a tty. It has five fields: `c_iflag` (input flags), `c_oflag` (output flags), `c_cflag` (control flags), `c_lflag` (local flags), `c_cc` (control characters), and two speed fields `c_ispeed` and `c_ospeed`. The fields are manipulated with `tcgetattr(3)`, `tcsetattr(3)`, and the shell command `stty(1)`.

A UART driver cares almost exclusively about `c_cflag` and the speed fields. `c_cflag` carries:

- `CSIZE`: the character size (CS5, CS6, CS7, CS8).
- `CSTOPB`: if set, two stop bits; if clear, one.
- `PARENB`: if set, parity is enabled; the type depends on `PARODD`.
- `PARODD`: if set with `PARENB`, odd parity; if clear with `PARENB`, even parity.
- `CRTSCTS`: hardware flow control.
- `CLOCAL`: ignore modem status lines; treat the link as local.
- `CREAD`: enable the receiver.

When user space calls `tcsetattr`, the tty layer checks the request, invokes the driver's `param` method (via the `tsw_param` callback in `ttydevsw`), and the driver translates the termios fields into hardware register settings. The `uart_tty.c` bridge code walks through this in full and is the best place to see the translation happen.

`c_iflag`, `c_oflag`, and `c_lflag` are mostly handled by the tty line discipline, not by the driver. They control things like whether the line discipline maps CR to LF, whether echo is enabled, whether canonical mode is active, and so on. A UART driver does not need to know any of that; the tty layer handles it.

#### Flow Control at the Multiple Layers of a TTY

Flow control sounds like a single concept, but in practice there are several independent layers that can each throttle the data flow. Understanding the layers helps debug situations where data is mysteriously not flowing.

The lowest layer is electrical. On a real RS-232 line, flow control signals (RTS, CTS, DTR, DSR) are physical pins on the connector. The remote side's transmitter only sends data when its CTS pin is asserted. The local side asserts the RTS pin to tell the remote it is ready to receive. For this to work, the cable must pass RTS and CTS through correctly, and both ends must have flow control configured consistently.

The next layer is in the UART chip itself. Some 16650 and later UARTs have automatic flow control: if configured, the chip itself monitors CTS and pauses the transmitter without driver involvement. The `CRTSCTS` flag in `c_cflag` enables this.

The next layer is in the UART framework's ring buffers. When the RX ring fills past a high-water mark, the framework deasserts RTS (if flow control is enabled) to tell the remote side to pause. When it drains below a low-water mark, RTS is reasserted.

The next layer is the tty line discipline, which has its own input and output queues. The line discipline can also generate XON/XOFF bytes (0x11 and 0x13) if `IXON` and `IXOFF` are set in `c_iflag`. These are software flow control signals.

The highest layer is the userland program's read loop. If the program is slow at consuming data, bytes accumulate at every layer below it.

When debugging flow-control issues, check each layer. Use `stty -a -f /dev/cuau0` to see what `c_cflag` and `c_iflag` have active. Use `comcontrol /dev/cuau0` to see the current modem signals. Use a multimeter or oscilloscope on the physical signals if you can. Work down the layers until you find the one that is actually blocking the flow.

#### Why Baud Rate Errors Are Insidious

A common class of serial bug is a baud-rate mismatch that almost works. Suppose one side is running at 115200 and the other at 114400 (which is what you get from a slightly-off crystal). Most bytes will come through, but a few will be corrupted. The exact error rate depends on the bit pattern. Long runs of one polarity drift further than alternating patterns.

Even worse, the error rate depends on the byte being sent. ASCII printable characters are in the range 0x20 to 0x7e, where the bits are well-distributed. Non-printable characters like 0xff or 0x00 are more likely to suffer bit errors because they present long runs of one polarity.

If you find your serial driver "mostly works" but drops or corrupts a few bytes out of thousands, suspect a baud-rate mismatch before suspecting a logic bug in your driver. Compare the actual divisor the chip is using against the expected divisor. If they differ, the baud rate is not what you asked for.

The 16550 uses a clock source (usually 1.8432 MHz) divided by a 16-bit divisor to produce 16 times the baud rate. For 115200, the divisor is `(1843200 / (115200 * 16)) = 1`. For 9600, it is 12. For arbitrary rates, the divisor may not be an integer, and the closest integer produces a rounded rate. A rate of 115200 requested from a 24 MHz clock would produce a divisor of `(24000000 / (115200 * 16)) = 13.02`, rounding to 13, giving an actual rate of `(24000000 / (13 * 16)) = 115384`, which is 0.16% off. Standard tolerance for serial communication is 2-3%, so 0.16% is fine.

When you configure a UART for a nonstandard baud rate, check whether the rate can be represented exactly. If not, test with actual data exchange, not just a loopback check.

#### Historical Note on Minor Numbers

Older FreeBSD versions encoded a lot of information into the minor numbers of serial device files. Different minor numbers for the "callin" side vs the "callout" side, for hardware-flow vs software-flow, and for various lock states. This encoding is largely gone in modern FreeBSD; the distinctions are now handled by separate device nodes with different names (`ttyu` vs `cuau`, with suffixes for lock and init states). If you see odd minor-number manipulation in old code, know that modern code does not need it.

#### Wrapping Up Section 1

Section 1 has established the two mental models Chapter 26 depends on. The USB model is a tree-structured, host-controlled, hot-pluggable serial bus with four transfer types, a rich descriptor hierarchy, and a lifecycle in which physical events drive kernel events. The serial model is a simple shift-register hardware protocol with baud rate, parity, stop bits, and optional flow control, integrated into FreeBSD through a subsystem split between `uart(4)` for real UARTs and `ucom(4)` for USB-to-serial bridges, and exposed to user space through the tty discipline and device nodes like `/dev/cuau0`.

Before moving on, spend a few minutes with `usbconfig` on a real system. The vocabulary you have just learned is easier to keep straight once you have seen a real USB device's descriptors with your own eyes.

### Exercise: Use `usbconfig` and `dmesg` to Explore USB Devices on Your System

This exercise is a short hands-on checkpoint that grounds Section 1's vocabulary in a device you can see. Perform it on your lab VM (or on any FreeBSD 14.3 system with at least one USB device connected). It takes about fifteen minutes.

**Step 1. Inventory.** Run `usbconfig` with no arguments:

```console
$ usbconfig
ugen0.1: <Intel EHCI root HUB> at usbus0, cfg=0 md=HOST spd=HIGH (0mA)
ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)
ugen0.3: <Logitech USB Mouse> at usbus0, cfg=0 md=HOST spd=LOW (98mA)
```

The first line is the root hub. Each other line is a device. Read the format: `ugenN.M` where N is the bus number and M is the device number; the description in angle brackets is the device's string; `cfg` is the active configuration; `md` is the mode (HOST or DEVICE); `spd` is the bus speed (LOW, FULL, HIGH, SUPER); the parenthesised current is the maximum bus-supplied power draw.

**Step 2. Dump a device's descriptors.** Pick one of the non-root-hub devices and dump its device descriptor:

```console
$ usbconfig -d ugen0.2 dump_device_desc

ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)

  bLength = 0x0012
  bDescriptorType = 0x0001
  bcdUSB = 0x0200
  bDeviceClass = 0x0000  <Probed by interface class>
  bDeviceSubClass = 0x0000
  bDeviceProtocol = 0x0000
  bMaxPacketSize0 = 0x0040
  idVendor = 0x13fe
  idProduct = 0x6300
  bcdDevice = 0x0112
  iManufacturer = 0x0001  <Generic>
  iProduct = 0x0002  <Storage>
  iSerialNumber = 0x0003  <0123456789ABCDE>
  bNumConfigurations = 0x0001
```

Read each field. Notice that `bDeviceClass` is zero: that is the USB convention for "the class is defined per interface, not at the device level." For this device, the interface class will be Mass Storage (0x08).

**Step 3. Dump the active configuration.** Now dump the configuration descriptor, which includes the interfaces and endpoints:

```console
$ usbconfig -d ugen0.2 dump_curr_config_desc

ugen0.2: <Generic Storage> at usbus0, cfg=0 md=HOST spd=HIGH (500mA)

  Configuration index 0

    bLength = 0x0009
    bDescriptorType = 0x0002
    wTotalLength = 0x0020
    bNumInterface = 0x0001
    bConfigurationValue = 0x0001
    iConfiguration = 0x0000  <no string>
    bmAttributes = 0x0080
    bMaxPower = 0x00fa

    Interface 0
      bLength = 0x0009
      bDescriptorType = 0x0004
      bInterfaceNumber = 0x0000
      bAlternateSetting = 0x0000
      bNumEndpoints = 0x0002
      bInterfaceClass = 0x0008  <Mass storage>
      bInterfaceSubClass = 0x0006  <SCSI>
      bInterfaceProtocol = 0x0050  <Bulk only>
      iInterface = 0x0000  <no string>

     Endpoint 0
        bLength = 0x0007
        bDescriptorType = 0x0005
        bEndpointAddress = 0x0081  <IN>
        bmAttributes = 0x0002  <BULK>
        wMaxPacketSize = 0x0200
        bInterval = 0x0000

     Endpoint 1
        bLength = 0x0007
        bDescriptorType = 0x0005
        bEndpointAddress = 0x0002  <OUT>
        bmAttributes = 0x0002  <BULK>
        wMaxPacketSize = 0x0200
        bInterval = 0x0000
```

Every field in Section 1's vocabulary is right there. The interface class is 0x08 (Mass Storage). The subclass is 0x06 (SCSI). The protocol is 0x50 (Bulk-only Transport). There are two endpoints. Endpoint 0 has address 0x81 (the high bit indicates IN direction, the low five bits are the endpoint number, 1). Endpoint 1 has address 0x02 (the high bit is clear, meaning OUT; the endpoint number is 2). Both endpoints are bulk. Both have a maximum packet size of 0x0200 = 512 bytes. The interval is zero because bulk endpoints do not use it.

**Step 4. Match this against `dmesg`.** Run `dmesg | grep -A 3 ugen0.2` (or look at the last boot's output for the matching device). You should see a line like:

```text
ugen0.2: <Generic Storage> at usbus0
umass0 on uhub0
umass0: <Generic Storage, class 0/0, rev 2.00/1.12, addr 2> on usbus0
```

This is the same information, formatted by the kernel's own logging. The driver that attached is `umass`, which is FreeBSD's USB mass storage driver, and it attached to the Mass Storage interface class.

**Step 5. Try `usbconfig -d ugen0.3 dump_all_config_desc` on another device.** A mouse, a keyboard, or a flash drive will all work. Compare the endpoint types: a mouse has one interrupt-IN endpoint; a flash drive has one bulk-IN and one bulk-OUT; a keyboard has one interrupt-IN. The pattern holds.

If you want a small additional exercise, write down the vendor and product identifiers of one of your devices. In Section 2 you will be asked to put vendor and product identifiers into a match table; using ones you can see now is concrete.

### Wrapping Up Section 1

Section 1 has done four things. It established the mental model of a transport: the protocol plus the lifecycle, plus the three broad categories of work (matching, transfer mechanics, hot-plug lifecycle) that a transport-specific driver has to add to its Newbus foundation. It built the USB model: host and device, hubs and ports, classes, descriptors with their nested structure, the four transfer types, and the hot-plug lifecycle. It built the serial model: the UART as a shift register with a baud generator, RS-232 framing, baud rate and parity and stop bits, hardware and software flow control, the FreeBSD-specific callin and callout node distinction, the two-worlds split between `uart(4)` and `ucom(4)`, and the role of `struct termios`. And it anchored the vocabulary in a concrete exercise that reads descriptors off a real USB device with `usbconfig`.

From here, the chapter turns to code. Section 2 builds a USB driver skeleton: probe, attach, detach, match table, registration macros. Section 3 makes that driver do real work by adding transfers. Section 4 turns to the serial side, walks through the `uart(4)` subsystem with a real driver as the guide, and explains where `ucom(4)` fits in. Section 5 brings the material back to the lab and teaches how to test USB and serial drivers without physical hardware. Each section builds on the mental models just established. If a later paragraph refers to a descriptor or a transfer type and the term does not feel immediate, return to Section 1 for a quick refresher before continuing.

## Section 2: Writing a USB Device Driver

### Moving from Concepts to Code

Section 1 built a mental picture of USB: a host that talks to devices through a tree of hubs, devices that describe themselves with nested descriptors, four transfer types that cover every conceivable traffic pattern, and a hot-plug lifecycle that drivers must respect because USB devices appear and disappear at any moment. Section 2 turns those concepts into a real driver skeleton. By the end of this section, you will have a USB driver that compiles, loads, attaches to a matching device, and detaches cleanly when the device is unplugged. It will not yet perform data transfers; that is the job of Section 3. But the scaffolding you build here is the same scaffolding every FreeBSD USB driver uses, from the tiniest notification LED to the most complex mass-storage controller.

The discipline you learned in Chapter 25 carries forward unchanged. Every resource must have an owner. Every successful allocation in `attach` must be paired with an explicit release in `detach`. Every failure path must leave the system in a clean state. The labelled-goto cleanup chain, the errno-returning helper functions, the softc-based resource tracking, the rate-limited logging: all of it still applies. What changes is the set of resources you manage. Instead of bus resources allocated through Newbus and a character device created through `make_dev`, you will manage USB transfer objects allocated through the USB stack and, optionally, a `/dev` entry created through the `usb_fifo` framework. The shape of the code stays the same. Only the specific calls change.

This section moves from the outside in. It begins by explaining where a USB driver sits inside the FreeBSD USB subsystem, because placing the driver in its correct environment is a prerequisite for understanding every call that follows. It then covers the match table, which is how a USB driver declares which devices it wants. It walks through `probe` and `attach`, the two halves of the driver's entry point into the world. It covers the softc layout, which is where the driver keeps its per-device state. It presents the cleanup chain, which is how the driver unwinds its own work when `detach` is called. And it ends with the registration macros that bind the driver to the kernel module system.

Along the way, the chapter uses `uled.c` as a recurring reference. That is a real FreeBSD driver, about three hundred lines long, located at `/usr/src/sys/dev/usb/misc/uled.c`. It is short enough to read end to end in a single sitting and rich enough to show every piece of machinery a USB driver needs. If you want to ground every idea in this section against real code, open that file now in another window and keep it open. Every time the chapter references a pattern, you will be able to see the pattern in a working driver.

### Where a USB Driver Sits in the FreeBSD Tree

FreeBSD's USB subsystem lives under `/usr/src/sys/dev/usb/`. That directory contains everything from the host controller drivers at the bottom (`controller/ehci.c`, `controller/xhci.c`, and so on) to the class drivers higher up (`net/if_cdce.c`, `wlan/if_rum.c`, `input/ukbd.c`), to serial drivers (`serial/uftdi.c`, `serial/uplcom.c`), to generic framework code (`usb_device.c`, `usb_transfer.c`, `usb_request.c`). When a new driver is added to the tree, it goes into one of these subdirectories according to its role. A driver for a blinking-LED gadget belongs under `misc/`. A driver for a network adapter belongs under `net/`. A driver for a serial adapter belongs under `serial/`. For your own work, you will not add files to `/usr/src/sys/dev/usb/` directly; you will build out-of-tree modules in your own workshop directory, the same way Chapter 25 did. The directory layout matters for reading the source, not for writing it.

Every FreeBSD USB driver sits somewhere in a small vertical stack. At the bottom is the host controller driver, which actually talks to the silicon. Above that is the USB framework, which handles descriptor parsing, device enumeration, transfer scheduling, hub routing, and the generic machinery every device needs. Above the framework are the class drivers, which you will write. A class driver attaches to a USB interface, not to the bus directly. This is the most important architectural point in the chapter.

In the Newbus tree, the attachment relationship looks like this:

```text
nexus0
  └─ pci0
       └─ ehci0   (or xhci0, depending on the host controller)
            └─ usbus0
                 └─ uhub0   (the root hub)
                      └─ uhub1 (a downstream hub, if present)
                           └─ [class driver]
```

The driver you will write attaches to `uhub`, not to `usbus`, not to `ehci`, and not to `pci`. The USB framework walks the device descriptors, creates a child for each interface, and offers those children to class drivers through the newbus probe mechanism. When your driver's probe routine is called, it is being asked: "here is an interface; is it yours?" The match table in your driver is how you answer that question.

There is one subtle point to absorb. A USB device can expose multiple interfaces simultaneously. A multi-function peripheral (say, a USB audio device with a headset and a microphone on the same silicon) exposes one interface for playback and another for capture. FreeBSD gives each interface its own newbus child, and each child can be claimed by a different driver. This is why USB drivers attach at the interface level: it lets the framework route interfaces independently. Your driver should not assume the device has only one interface. When you write the match table, you write it against a specific interface, identified by its class, subclass, protocol, or by its vendor/product pair plus an optional interface number.

### The Match Table: Telling the Kernel Which Devices Are Yours

A USB driver advertises which devices it will accept through an array of `STRUCT_USB_HOST_ID` entries. This is analogous to the PCI match table from Chapter 23, but with USB-specific fields. The authoritative definition lives in `/usr/src/sys/dev/usb/usbdi.h`. Each entry specifies one or more of the following: a vendor ID, a product ID, a device class/subclass/protocol triple, an interface class/subclass/protocol triple, or a manufacturer-defined bcdDevice range. You can match broadly (any device that advertises interface class 0x03, which is HID) or narrowly (the single device with vendor 0x0403 and product 0x6001, which is an FTDI FT232). Most drivers match narrowly, because most real devices have driver-specific quirks that apply only to particular hardware revisions.

The framework provides convenience macros to build match entries without having to initialize each field by hand. The most common are `USB_VPI(vendor, product, info)` for vendor/product pairs with an optional driver-specific information field, and the more verbose form where you fill in `mfl_`, `pfl_`, `dcl_`, `dcsl_`, `dcpl_`, `icl_`, `icsl_`, `icpl_` flags to indicate which fields are significant. For clarity and maintainability, drivers written today tend to use the compact macros whenever they are applicable.

Here is how `uled.c` declares its match table. The source is in `/usr/src/sys/dev/usb/misc/uled.c`:

```c
static const STRUCT_USB_HOST_ID uled_devs[] = {
    {USB_VPI(USB_VENDOR_DREAMCHEEKY, USB_PRODUCT_DREAMCHEEKY_WEBMAIL_NOTIFIER, 0)},
    {USB_VPI(USB_VENDOR_RISO_KAGAKU, USB_PRODUCT_RISO_KAGAKU_WEBMAIL_NOTIFIER, 0)},
};
```

Two entries, each naming a specific vendor/product pair. The third argument to `USB_VPI` is an unsigned integer that the driver can use to distinguish variants at probe time; `uled` sets it to zero because both devices behave the same way. The vendor and product symbolic names resolve to numeric identifiers defined in `/usr/src/sys/dev/usb/usbdevs.h`, which is a large table generated from `/usr/src/sys/dev/usb/usbdevs`. Adding a new match entry for your own development hardware often means adding a line to `usbdevs` and regenerating the header, or bypassing the symbolic names entirely and writing the hexadecimal values directly in the match table.

For your own out-of-tree driver, you do not need to touch `usbdevs` at all. You can write:

```c
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
    {USB_VPI(0x16c0, 0x05dc, 0)},  /* VOTI / generic test VID/PID */
};
```

The numeric form is perfectly acceptable. Use it when you are prototyping against a specific device and do not yet want to propose additions to the upstream `usbdevs` file.

One important detail about match tables: the `STRUCT_USB_HOST_ID` type includes a flag byte that records which fields are meaningful. When you use `USB_VPI`, the macro fills in those flags for you. If you hand-build an entry with literal braces, you must also fill in the flags yourself, because a zero flag byte means "match anything," and you rarely want that. Prefer the macros.

The match table is plain data. It does not allocate memory, it does not touch hardware, and it does not depend on any per-device state. It is loaded into the kernel along with the module and used by the framework every time a new USB device is enumerated.

### The `probe` Method

The USB framework calls a driver's `probe` method once per interface when a matching-like candidate is presented. The goal of `probe` is to answer a single question: "Should this driver attach to this interface?" The method must not touch hardware. It must not allocate resources. It must not sleep. All it does is look at the USB attach argument, compare it against the match table, and return either a bus-probe value (indicating a match, with an associated priority) or `ENXIO` (indicating that this driver does not want this interface).

The attach argument lives in a structure called `struct usb_attach_arg`, defined in `/usr/src/sys/dev/usb/usbdi.h`. It carries the vendor ID, the product ID, the device descriptor, the interface descriptor, and a handful of helper fields. Newbus lets a driver retrieve it through `device_get_ivars(dev)`. For USB drivers, the framework provides a wrapper called `usbd_lookup_id_by_uaa` that takes a match table and an attach argument and returns zero on a match or a nonzero errno on a miss. This wrapper encapsulates every case the driver needs to handle: vendor/product matching, class/subclass/protocol matching, the flag-byte logic, and the interface-level dispatch.

A complete probe method for our running example looks like this:

```c
static int
myfirst_usb_probe(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);

    if (uaa->usb_mode != USB_MODE_HOST)
        return (ENXIO);

    if (uaa->info.bConfigIndex != 0)
        return (ENXIO);

    if (uaa->info.bIfaceIndex != 0)
        return (ENXIO);

    return (usbd_lookup_id_by_uaa(myfirst_usb_devs,
        sizeof(myfirst_usb_devs), uaa));
}
```

The three guard clauses at the top of the function are worth explaining in detail, because they reflect standard USB-driver hygiene.

The first guard rejects the case where the USB stack is acting as a device rather than a host. FreeBSD's USB stack can operate in USB-on-the-Go device mode, where the machine itself appears as a USB peripheral to some other host. Most drivers are host-side drivers and have no meaningful behavior in device mode, so they reject it immediately.

The second guard rejects configurations other than index zero. USB devices can expose multiple configurations, and a driver usually targets one specific configuration. Restricting probe to configuration index zero keeps the logic simple for the common case.

The third guard rejects interfaces other than index zero. If the device has multiple interfaces and you are writing a driver for the first one, this clause is what ensures the framework does not offer you the other interfaces by mistake.

After the guards, the call to `usbd_lookup_id_by_uaa` does the real matching work. If the device's vendor, product, class, subclass, or protocol matches any entry in the table, the function returns zero, and the probe method returns zero, which the USB framework interprets as "this driver wants this device." Returning `ENXIO` tells the framework to try another candidate driver. If no candidate wants the device, it ends up attached to `ugen`, the generic USB driver, which exposes raw descriptors and transfers through `/dev/ugenN.M` nodes but provides no device-specific behavior.

A subtle point worth noting: `probe` returns zero for a match rather than a positive bus-probe value. Other FreeBSD bus frameworks use positive values like `BUS_PROBE_DEFAULT` to indicate a priority, but for USB the convention is zero for match and a nonzero errno for non-match. The framework handles priority through the dispatch order rather than through probe return values.

### The `attach` Method

Once `probe` reports a match, the framework calls `attach`. This is where the driver does real work: allocate its softc, record the parent device pointer, lock the interface, set up transfer channels (covered in Section 3), create a `/dev` entry if the driver is user-facing, and log a short informational message. Every allocation and registration in `attach` has to be paired with a symmetric release in `detach`, and because any step can fail, the function must have a clear cleanup path from every failure point.

A minimal attach method looks like this:

```c
static int
myfirst_usb_attach(device_t dev)
{
    struct usb_attach_arg *uaa = device_get_ivars(dev);
    struct myfirst_usb_softc *sc = device_get_softc(dev);
    int error;

    device_set_usb_desc(dev);

    mtx_init(&sc->sc_mtx, "myfirst_usb", NULL, MTX_DEF);

    sc->sc_udev = uaa->device;
    sc->sc_iface_index = uaa->info.bIfaceIndex;

    error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
        sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_XFER,
        sc, &sc->sc_mtx);
    if (error != 0) {
        device_printf(dev, "usbd_transfer_setup failed: %d\n", error);
        goto fail_mtx;
    }

    sc->sc_dev = make_dev(&myfirst_usb_cdevsw, device_get_unit(dev),
        UID_ROOT, GID_WHEEL, 0644, "myfirst_usb%d", device_get_unit(dev));
    if (sc->sc_dev == NULL) {
        device_printf(dev, "make_dev failed\n");
        error = ENOMEM;
        goto fail_xfer;
    }
    sc->sc_dev->si_drv1 = sc;

    device_printf(dev, "attached\n");
    return (0);

fail_xfer:
    usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);
fail_mtx:
    mtx_destroy(&sc->sc_mtx);
    return (error);
}
```

Read through this function top to bottom. Each block does one thing.

The call to `device_set_usb_desc` fills in the Newbus device description string from the USB descriptors. After this call, `device_printf` messages will include the manufacturer and product strings read from the device itself, which makes logs much more informative.

The call to `mtx_init` creates a mutex that will protect the per-device state. Every USB transfer callback runs under this mutex (the framework takes it for you around the callback), so everything the callback touches must be serialised by it. Chapter 25 introduced mutexes; the usage here is the same.

The two `sc->sc_` assignments cache two pointers that the rest of the driver will need. `sc->sc_udev` is the `struct usb_device *` that the driver uses when issuing USB requests. `sc->sc_iface_index` identifies the interface index this driver attached to, so later transfer-setup calls target the right interface.

The call to `usbd_transfer_setup` is the biggest single operation in `attach`. It allocates and configures all the transfer objects the driver will use, based on a configuration array (`myfirst_usb_config`) that Section 3 will examine in detail. If this call fails, the driver has not yet allocated anything except the mutex, so the cleanup path goes to `fail_mtx` and destroys the mutex.

The call to `make_dev` creates the user-visible `/dev` node. The Chapter 25 pattern applies here: set `si_drv1` on the cdev so that the cdevsw handlers can retrieve the softc through `dev->si_drv1`. If this call fails, the cleanup path goes to `fail_xfer`, which also runs the unsetup for the transfers before destroying the mutex.

The `return (0)` on the happy path is the contract with the framework: a zero return means the device is attached and the driver is ready.

The two labels at the bottom implement the labelled-goto cleanup chain from Chapter 25. Each label corresponds to the state the driver has reached at the time the failure happened, and the cleanup fall-through runs exactly the teardown steps needed to undo the work done so far. When you read a FreeBSD driver and see this pattern, you are looking at the same discipline you practised in Chapter 25 applied to a new set of resources.

One important detail about the USB framework that Chapter 25 did not need to cover: if you look at `uled.c` or any other real USB driver, you will sometimes see `usbd_transfer_setup` accept a pointer to the interface index rather than an integer. The framework can modify that pointer in the case of virtual or multiplexed interfaces; pass it by address, not by value. The skeleton above does this correctly.

### The Softc: Per-Device State

A USB driver's softc is a plain C structure stored as the Newbus driver data for each attached device. It is allocated automatically by the framework based on the size declared in the driver descriptor, and it is the place where all per-device mutable state lives. For our running example, the softc looks like this:

```c
struct myfirst_usb_softc {
    struct usb_device *sc_udev;
    struct mtx         sc_mtx;
    struct usb_xfer   *sc_xfer[MYFIRST_USB_N_XFER];
    struct cdev       *sc_dev;
    uint8_t            sc_iface_index;
    uint8_t            sc_flags;
#define MYFIRST_USB_FLAG_OPEN       0x01
#define MYFIRST_USB_FLAG_DETACHING  0x02
};
```

Let us walk through each member.

`sc_udev` is the opaque pointer the USB framework uses to identify the device. Every USB call that acts on the device takes this pointer.

`sc_mtx` is the per-device mutex that protects the softc itself and any shared state the driver cares about. The mutex must be acquired before touching any field that a transfer callback might also touch, and the transfer callback always runs with this mutex held (the framework handles the locking for you when it invokes the callback).

`sc_xfer[]` is an array of transfer objects, one per channel the driver uses. Its size is a compile-time constant. Section 3 will discuss how each entry in this array is set up by the configuration array passed to `usbd_transfer_setup`.

`sc_dev` is the character device entry, if the driver exposes a user-facing node. For drivers that do not expose a `/dev` node (some drivers only export data through `sysctl` or `devctl` events), this field can be omitted.

`sc_iface_index` records which interface on the USB device this driver attached to. It is used by transfer setup and, in multi-interface drivers, as a discriminator in logging.

`sc_flags` is a bit vector for driver-private state. Two flags are declared here: `MYFIRST_USB_FLAG_OPEN` is set while a userland process holds the device open, and `MYFIRST_USB_FLAG_DETACHING` is set at the start of `detach` so that any concurrent I/O path can see that it must abort quickly. This is an application of a standard pattern: setting a flag under the mutex at the start of detach, so anyone else who wakes up sees it and bails out.

Real drivers often have many more fields: per-transfer buffers, request queues, callback-to-callback state machines, timers, and so on. You add to the softc as the driver grows. The guiding principle is that any state that persists between function calls, and is not global to the module, belongs in the softc.

### The `detach` Method

When a device is unplugged, when the module is unloaded, or when userspace uses `devctl detach`, the framework calls the driver's `detach` method. The driver's job is to release every resource it allocated in `attach`, cancel any in-flight work, make sure no callback is running, and return zero. If `detach` returns an error, the framework treats the device as still attached, which can create problems if the hardware has already physically vanished. Most drivers return zero unconditionally, or only return an error in very specific "device busy" cases where the driver implements its own reference counting for userspace handles.

The detach method for our running example is the symmetric cleanup of the attach method:

```c
static int
myfirst_usb_detach(device_t dev)
{
    struct myfirst_usb_softc *sc = device_get_softc(dev);

    mtx_lock(&sc->sc_mtx);
    sc->sc_flags |= MYFIRST_USB_FLAG_DETACHING;
    mtx_unlock(&sc->sc_mtx);

    if (sc->sc_dev != NULL) {
        destroy_dev(sc->sc_dev);
        sc->sc_dev = NULL;
    }

    usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);

    mtx_destroy(&sc->sc_mtx);

    return (0);
}
```

The first block sets the detaching flag under the mutex. If another thread is about to take the mutex and start a new transfer, it will see the flag and refuse. The `destroy_dev` call removes the `/dev` entry; after it returns, no new open calls can arrive. The `usbd_transfer_unsetup` call cancels any in-flight transfers and waits for their callbacks to complete; after it returns, no transfer callback can still be running. With no new openers and no running callbacks, it is safe to destroy the mutex.

There is a subtlety here that new kernel programmers sometimes stumble over: the order matters. Destroying the `/dev` entry before unwinding the transfers ensures that no new user operation can start, but it does not stop the transfers that were already running when detach was called. That is `usbd_transfer_unsetup`'s job. Both steps are necessary, and the order (cdev first, then transfers, then mutex) is the right one because each later step depends on no new work arriving during it.

One further point about detach and concurrency. The framework guarantees that no probe, attach, or detach runs concurrently with another probe, attach, or detach on the same device. But transfer callbacks run on their own path, and they can be in progress at the exact moment detach is called. The combination of the detaching flag and `usbd_transfer_unsetup` is what makes this safe. If you add new resources to your driver, you must add symmetric cleanup that accounts for this concurrency.

### Registration Macros

Every FreeBSD driver needs to register itself with the kernel so that the kernel knows when to call its probe, attach, and detach routines. USB drivers use a small set of macros that bind everything together into a kernel module. The macros go at the bottom of the driver file and look intimidating at first but are entirely mechanical once you know what each line does.

```c
static device_method_t myfirst_usb_methods[] = {
    DEVMETHOD(device_probe,  myfirst_usb_probe),
    DEVMETHOD(device_attach, myfirst_usb_attach),
    DEVMETHOD(device_detach, myfirst_usb_detach),
    DEVMETHOD_END
};

static driver_t myfirst_usb_driver = {
    .name    = "myfirst_usb",
    .methods = myfirst_usb_methods,
    .size    = sizeof(struct myfirst_usb_softc),
};

DRIVER_MODULE(myfirst_usb, uhub, myfirst_usb_driver, NULL, NULL);
MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);
MODULE_VERSION(myfirst_usb, 1);
USB_PNP_HOST_INFO(myfirst_usb_devs);
```

Let us read each block.

The `device_method_t` array lists the methods the driver supplies. For a USB driver that does not implement extra newbus children, the three entries shown are sufficient: probe, attach, detach. More complex drivers might add `device_suspend`, `device_resume`, or `device_shutdown`, but for the vast majority of USB drivers the three basic entries are all that is needed. `DEVMETHOD_END` terminates the array; the framework requires it.

The `driver_t` structure binds the methods array to a human-readable name and declares the softc size. The name is used in kernel logs and by `devctl`. The softc size tells Newbus how much memory to allocate per device.

The `DRIVER_MODULE` macro registers the driver with the kernel. The arguments are, in order: the module name, the parent bus name (always `uhub` for USB class drivers), the driver structure, and two optional hooks for events. The event hooks are rarely needed and are usually `NULL`.

The `MODULE_DEPEND` macro declares that this module needs `usb` to be loaded first. The three numbers are the minimum, preferred, and maximum compatible versions of the `usb` module. For most drivers, `1, 1, 1` is correct: the USB framework has versioned its interface at 1 for a long time, and it would be unusual to require anything else.

The `MODULE_VERSION` macro declares this module's own version number. Other modules that want to depend on `myfirst_usb` would reference the number you declare here.

The `USB_PNP_HOST_INFO` macro is the last piece. It exports the match table into a format the `devd(8)` daemon can read, so that when a matching USB device is plugged in, userspace can auto-load the module. This macro is a relatively recent addition to FreeBSD; older drivers may not have it. Including it is strongly recommended for any driver that wants to participate in FreeBSD's USB plug-and-play system.

Together, these five declarations turn your driver file into a loadable kernel module. Once the file is compiled with a `Makefile` that uses `bsd.kmod.mk`, running `kldload myfirst_usb.ko` will bind the driver to the kernel, and any matching device plugged in afterwards will trigger your probe and attach routines.

### The Hot-Plug Lifecycle, Revisited in Code

Section 1 introduced the hot-plug lifecycle at the level of mental model: a device appears, the framework enumerates it, your driver attaches, userland interacts with it, the device disappears, the framework calls detach, your driver cleans up. With the code in front of you, that narrative now has a concrete sequence:

1. The user plugs in a matching device.
2. The USB framework enumerates the device, reads all its descriptors, and decides which interfaces to offer to which drivers.
3. For each interface that matches your driver's match table, the framework creates a Newbus child and calls your `probe` method.
4. Your `probe` method returns zero.
5. The framework calls your `attach` method. You initialise the softc, set up transfers, create the `/dev` node, and return zero.
6. Userland opens the `/dev` node and begins issuing I/O. The transfer callbacks from Section 3 start running.
7. The user unplugs the device.
8. The framework calls your `detach` method. You set the detaching flag, destroy the `/dev` node, call `usbd_transfer_unsetup` to cancel all in-flight transfers and wait for callbacks to finish, destroy the mutex, and return zero.
9. The framework deallocates the softc and removes the Newbus child.

At every step, the framework handles the parts you do not have to write yourself. Your responsibility is narrow: react correctly to probe, attach, and detach, and run transfer callbacks that respect the state machine. The machinery around you handles enumeration, bus arbitration, transfer scheduling, hub routing, and the dozens of corner cases that USB layer imposes.

The lifecycle has one more subtle quirk that is worth naming. Between the user unplugging the device and the framework calling `detach`, there is a brief window in which any in-flight transfer sees a special error: `USB_ERR_CANCELLED`. The transfer framework itself generates this error when it tears down the transfers in response to the disconnect. Section 3 will explain how to handle this error in the callback state machine. For now, know that it exists and that it is the driver's normal signal that the device is going away.

### Wrapping Up Section 2

Section 2 has given you a complete USB driver skeleton. The skeleton does not yet move data; that is Section 3's topic. But every other part of the driver is in place: the match table, the probe method, the attach method, the softc, the detach method, and the registration macros. You have seen how the USB framework routes a newly enumerated device through your probe routine, how your attach routine takes ownership and sets up state, how the driver integrates with Newbus through `device_get_ivars` and `device_get_softc`, and how the detach routine walks the allocation steps in reverse to leave the system clean.

Two themes from Chapter 25 have extended naturally into USB territory. First, the labelled-goto cleanup chain. Every resource you acquire has its own label, and every failure path falls through exactly the right sequence of teardown calls. When you compare `myfirst_usb_attach` above with the attach functions in `uled.c`, `ugold.c`, or `uftdi.c`, you will see the same pattern repeated. Second, the discipline of single-source-of-truth state in the softc. Every field has one owner, one lifecycle, and one clear place where it is initialised and destroyed. These habits are what make a driver readable, portable, and maintainable.

Section 3 will now give this skeleton a voice. Transfer channels will be declared in a configuration array. The USB framework will allocate the underlying buffers and schedule the transactions. A callback will wake up each time a transfer completes or needs more data, and it will use a three-state state machine to decide what to do. The same discipline you just learned will apply, but the new concern is the data pipeline itself: how bytes move between the driver and the device.

### Reading `uled.c` As a Complete Example

Before moving into transfers, it is worth pausing to read the canonical small-driver example end to end. The file `/usr/src/sys/dev/usb/misc/uled.c` is approximately three hundred lines of C that implements a driver for the Dream Cheeky and Riso Kagaku USB webmail notifier LEDs: small USB gadgets with three coloured LEDs that a host program can light up. The driver is short enough to hold in your head, self-contained, and it exercises every pattern we have discussed.

When you open the file, the first block you encounter is the standard set of header includes. A USB driver pulls in headers from several layers: `sys/param.h`, `sys/systm.h`, `sys/bus.h` for the fundamentals; `sys/module.h` for `MODULE_VERSION` and `MODULE_DEPEND`; the USB headers under `dev/usb/` for the framework; and `usbdevs.h` for the symbolic vendor and product constants. Note that `usbdevs.h` is not a hand-maintained header: it is build-generated from the text file `/usr/src/sys/dev/usb/usbdevs` when the kernel or module is compiled, so the constants it exposes reflect whatever entries the in-tree `usbdevs` file currently lists. `uled.c` also pulls in `sys/conf.h` and friends because it creates a character device.

The second block is the softc declaration. `uled` keeps its state in a structure that has the device pointer, a mutex, an array of two transfer pointers (one for control, one for data), a character device pointer, a callback state pointer, and a small "color" byte that records the current LED colour. The softc is straightforward: every field is private, every allocation has one place where it is made and one place where it is freed.

The third block is the match table. `uled` supports two vendors (Dream Cheeky and Riso Kagaku) with one product ID each. The `USB_VPI` macro fills in the flag byte for a vendor-plus-product match. The table is two entries, flat and simple.

The fourth block is the transfer configuration array. `uled` declares two channels: a control-out channel used to send SET_REPORT requests to the device (which is how the LED colour is actually programmed), and an interrupt-in channel that reads status packets from the LED. The control channel has `type = UE_CONTROL` and a buffer size big enough to hold the setup packet plus the payload. The interrupt channel has `type = UE_INTERRUPT`, `direction = UE_DIR_IN`, and a buffer size that matches the LED's report size.

The fifth block is the callback functions. The control callback follows the three-state machine you saw in Section 3: in `USB_ST_SETUP`, it constructs a setup packet and an eight-byte HID report payload, submits the transfer, and returns. In `USB_ST_TRANSFERRED`, it wakes any userland writer that was waiting for the colour change to complete. In the default case (errors), it handles cancellation gracefully and retries on other errors.

The interrupt callback is similar but without the setup-packet complication. It reads an eight-byte status report, checks whether it indicates a button press (the Riso Kagaku devices have an optional button), and rearms.

The sixth block is the character-device methods. `uled` exposes a `/dev/uled0` entry that accepts `write(2)` calls with a three-byte payload (red, green, blue). The `d_write` handler copies the three bytes into the softc, starts the control transfer, and returns. When the transfer completes, the colour is actually programmed. The `d_read` handler is not implemented (LEDs do not have meaningful state to read), so reads return zero.

The seventh block is the Newbus methods: probe, attach, detach. The probe uses `usbd_lookup_id_by_uaa` exactly as shown in Section 2. The attach calls `device_set_usb_desc`, initialises the mutex, calls `usbd_transfer_setup` with the configuration array, and creates the character device. The detach runs these in reverse.

The eighth block is the registration macros. `DRIVER_MODULE(uled, uhub, ...)`, `MODULE_DEPEND(uled, usb, 1, 1, 1)`, `MODULE_VERSION(uled, 1)`, and `USB_PNP_HOST_INFO(uled_devs)`. Exactly the sequence you learned.

Reading through `uled.c` with the Section 2 vocabulary in hand, the whole file legibly maps onto the patterns you now understand. Every structural choice the driver makes has a name. Every line of code is an instance of a general pattern. This is the kind of clarity that makes FreeBSD drivers readable.

Before continuing to Section 3, we recommend you actually open `uled.c` now and read it. Even if some lines are still obscure, the overall structure will match the mental model you have built. The details will make more sense as you progress through the rest of the chapter, and revisiting this file after finishing the chapter is an excellent way to consolidate the material.

## Section 3: Performing USB Data Transfers

### The Transfer Configuration Array

A USB driver declares its transfers up front, at compile time, through a small array of `struct usb_config` entries. Each entry describes one transfer channel: its type (control, bulk, interrupt, or isochronous), its direction (in or out), which endpoint it targets, how big its buffer is, which flags apply, and which callback function to invoke when the transfer completes. The framework reads this array once, during `attach`, when the driver calls `usbd_transfer_setup`. From that point on, each channel behaves like a small state machine that the driver drives through its callback.

The configuration array is declarative. You are not programming the sequence of hardware operations; you are telling the framework what channels your driver will use, and the framework builds the infrastructure to support them. This is an effective abstraction, and it is one of the reasons USB drivers in FreeBSD are usually much shorter than equivalent drivers for buses like PCI that demand direct register manipulation.

For our running example, we will declare three channels. A bulk-IN channel for reading data from the device, a bulk-OUT channel for writing data to the device, and an interrupt-IN channel for receiving asynchronous status events. A real driver for a serial adapter or an LED notifier might use one or two of these; we use three to show the pattern applied to different transfer types.

```c
enum {
    MYFIRST_USB_BULK_DT_RD,
    MYFIRST_USB_BULK_DT_WR,
    MYFIRST_USB_INTR_DT_RD,
    MYFIRST_USB_N_XFER,
};

static const struct usb_config myfirst_usb_config[MYFIRST_USB_N_XFER] = {
    [MYFIRST_USB_BULK_DT_RD] = {
        .type      = UE_BULK,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_IN,
        .bufsize   = 512,
        .flags     = { .pipe_bof = 1, .short_xfer_ok = 1 },
        .callback  = &myfirst_usb_bulk_read_callback,
    },
    [MYFIRST_USB_BULK_DT_WR] = {
        .type      = UE_BULK,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_OUT,
        .bufsize   = 512,
        .flags     = { .pipe_bof = 1, .force_short_xfer = 0 },
        .callback  = &myfirst_usb_bulk_write_callback,
        .timeout   = 5000,
    },
    [MYFIRST_USB_INTR_DT_RD] = {
        .type      = UE_INTERRUPT,
        .endpoint  = UE_ADDR_ANY,
        .direction = UE_DIR_IN,
        .bufsize   = 16,
        .flags     = { .pipe_bof = 1, .short_xfer_ok = 1 },
        .callback  = &myfirst_usb_intr_callback,
    },
};
```

The enumeration at the top gives each channel a name and defines `MYFIRST_USB_N_XFER` as the total count. This is a common idiom; it keeps the channels symbolically accessible and makes it easy to add a new channel later. `MYFIRST_USB_N_XFER` is what you pass to `usbd_transfer_setup`, to `usbd_transfer_unsetup`, and to the softc's `sc_xfer[]` array declaration.

The array itself uses designated initialisers, which keeps the assignment of each channel to its enumeration index explicit. Let us walk through the fields.

`type` is one of `UE_CONTROL`, `UE_BULK`, `UE_INTERRUPT`, or `UE_ISOCHRONOUS`, from `/usr/src/sys/dev/usb/usb.h`. It has to match the endpoint's type as declared in the USB descriptors. If you say `UE_BULK` but the device has an interrupt endpoint, `usbd_transfer_setup` will fail.

`endpoint` identifies the endpoint number, but in most drivers the special value `UE_ADDR_ANY` is used, which tells the framework to pick any endpoint whose type and direction match. This works because most USB interfaces have only one endpoint of each (type, direction) pair, so "any" is unambiguous. A device with multiple bulk-in endpoints would require explicit endpoint addresses.

`direction` is `UE_DIR_IN` or `UE_DIR_OUT`. Again, this must match the descriptors.

`bufsize` is the size of the buffer the framework allocates for this channel. For bulk transfers, 512 bytes is a common choice because that is the maximum packet size for high-speed bulk endpoints, so a single 512-byte buffer can hold exactly one packet. Larger buffers are supported, but for most purposes 512 or a small multiple is correct. For interrupt endpoints, the buffer can be smaller because interrupt packets are typically eight, sixteen, or sixty-four bytes.

`flags` is a bitfield struct (each flag is a one-bit integer). The flags affect how the framework handles short transfers, stalls, timeouts, and pipe behaviour.

- `pipe_bof` (pipe blocked on failure): if the transfer fails, block further transfers on the same pipe until the driver explicitly restarts it. This is usually set for both read and write endpoints.
- `short_xfer_ok`: for incoming transfers, treat a transfer that completed with less data than requested as success rather than error. Setting this is what allows a bulk-IN channel to read responses of variable length from a device.
- `force_short_xfer`: for outgoing transfers, finish the transfer with a short packet even when the data is aligned to a full packet boundary. This is used by some protocols to signal the end of a message.
- Several other flags control more advanced behaviour; for most drivers, `pipe_bof` plus `short_xfer_ok` (on reads) plus possibly `force_short_xfer` (on writes, protocol-dependent) is all that is needed.

`callback` is the function the framework calls whenever this channel needs attention. The callback is a `usb_callback_t`, which takes a pointer to the `struct usb_xfer` and returns void. All of the channel's state-machine logic lives inside the callback.

`timeout` (in milliseconds) sets an upper bound on how long a transfer can wait before being forcibly completed with an error. Setting a timeout is useful for write channels, because it prevents a hung device from stalling the driver indefinitely. For read channels, leaving the timeout at zero (meaning "no timeout") is common, because reads are often expected to block waiting for the device to have something to say.

This array, combined with `usbd_transfer_setup`, is all the driver needs to declare its data pipeline. The framework allocates the underlying DMA buffers, sets up the scheduling, and watches the pipes. The driver never has to call into a register or schedule a transaction by hand. It just writes callbacks.

### Setting Up and Tearing Down Transfers

In the `attach` method shown in Section 2, the call to `usbd_transfer_setup` creates the channels from the configuration array:

```c
error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
    sc->sc_xfer, myfirst_usb_config, MYFIRST_USB_N_XFER,
    sc, &sc->sc_mtx);
```

The arguments are, in order: the USB device pointer, a pointer to the interface index (the framework can update it in certain multi-interface scenarios), the destination array for the created transfer objects, the configuration array, the number of channels, the softc pointer (which is passed into callbacks via `usbd_xfer_softc`), and the mutex the framework will hold around each callback.

If this call succeeds, `sc->sc_xfer[]` is populated with pointers to `struct usb_xfer` objects. Each object encapsulates a channel's state. From this point, the driver can submit a transfer on a channel with `usbd_transfer_submit(sc->sc_xfer[i])`, and the framework will, in the fullness of time, call the corresponding callback.

The symmetric teardown, shown in the `detach` method, is `usbd_transfer_unsetup`:

```c
usbd_transfer_unsetup(sc->sc_xfer, MYFIRST_USB_N_XFER);
```

This call does three things, in order. It cancels any in-flight transfer on each channel. It waits for the corresponding callback to run with `USB_ST_ERROR` or `USB_ST_CANCELLED`, so the driver has a chance to clean up any per-transfer state. It frees the framework's internal state for the channel. After `usbd_transfer_unsetup` returns, the `sc_xfer[]` entries are no longer valid, and the associated callback will not be invoked again.

This is the piece of machinery that makes detach safe in the presence of ongoing I/O. You do not need to implement your own "wait for outstanding transfers" logic. The framework provides it, atomically, through this single call.

### The Callback State Machine

Every transfer callback follows the same three-state state machine. When the framework invokes the callback, you ask `USB_GET_STATE(xfer)` for the current state, and then you handle it. The three possible states are declared in `/usr/src/sys/dev/usb/usbdi.h`:

- `USB_ST_SETUP`: the framework is ready to submit a new transfer on this channel. You should prepare the transfer (set its length, copy data into its buffer, and so on) and call `usbd_transfer_submit`. If you have no work for this channel right now, simply return; the framework will leave the channel idle until something else triggers a submit.
- `USB_ST_TRANSFERRED`: the most recent transfer completed successfully. You should read out the results (copy received data out, decide what to do next) and either return (if the channel should go idle) or fall through to `USB_ST_SETUP` to start another transfer.
- `USB_ST_ERROR`: the most recent transfer failed. You should inspect `usbd_xfer_get_error(xfer)` to see why, handle the error (for most errors, you fall through to `USB_ST_SETUP` to retry after a short delay; for stalls, you issue a clear-stall), and decide whether to continue.

The typical shape of a bulk-read callback looks like this:

```c
static void
myfirst_usb_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        pc = usbd_xfer_get_frame(xfer, 0);
        /*
         * Copy actlen bytes from pc into the driver's receive buffer.
         * This is where you hand the data to userland, to a queue,
         * or to another callback.
         */
        myfirst_usb_deliver_received(sc, pc, actlen);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        /*
         * Arm a read for 512 bytes.  The actual amount received may
         * be less, because we enabled short_xfer_ok in the config.
         */
        usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
        usbd_transfer_submit(xfer);
        break;

    default:  /* USB_ST_ERROR */
        if (error == USB_ERR_CANCELLED) {
            /* The device is going away.  Do nothing. */
            break;
        }
        if (error == USB_ERR_STALLED) {
            /* Arm a clear-stall on the control pipe; the framework
             * will call us back in USB_ST_SETUP after the clear
             * completes. */
            usbd_xfer_set_stall(xfer);
        }
        goto tr_setup;
    }
}
```

Let us walk through every piece.

The first line retrieves the softc pointer from the transfer object. This is how the callback gets at the per-device state. It works because the softc was passed to `usbd_transfer_setup`, which stored it inside the transfer object.

The call to `usbd_xfer_status` fills in `actlen`, the number of bytes actually transferred on frame zero. For a read, this is how much data arrived. For a write, it is how much data was sent. The other three parameters (which this example does not use) give the total transfer length, the timeout, and a status flags pointer; most callbacks only need `actlen`.

The switch on `USB_GET_STATE(xfer)` is the state machine. In `USB_ST_TRANSFERRED`, the callback copies the received data out of the USB frame into the driver's own buffer. The helper function `myfirst_usb_deliver_received` (which you would write) could push the data onto a queue, wake a sleeping read() on the `/dev` node, or feed a higher-level protocol parser.

The `FALLTHROUGH` after processing the transferred data takes the callback into the `USB_ST_SETUP` branch. This is the idiomatic pattern for channels that run continuously: every time a read finishes, immediately start another read. If the driver wanted to stop reading after one transfer (say, a one-shot control request), it would `return;` at the end of `USB_ST_TRANSFERRED` instead of falling through.

In `USB_ST_SETUP`, `usbd_xfer_set_frame_len` sets the length of frame zero to the maximum the channel can handle, and `usbd_transfer_submit` hands the transfer to the framework. The framework will start the actual hardware operation and, when complete, call the callback again with either `USB_ST_TRANSFERRED` or `USB_ST_ERROR`.

The `default` case is where error handling happens. Two errors get special treatment. `USB_ERR_CANCELLED` is the signal that the transfer is being torn down, typically because the device was unplugged or `usbd_transfer_unsetup` was called. The callback must not resubmit the transfer in this case; if it did, it could race with the teardown and potentially touch memory that is about to be freed. Breaking out of the switch without calling `usbd_transfer_submit` is the correct behaviour.

`USB_ERR_STALLED` is the signal that the endpoint returned a STALL handshake, meaning the device is refusing to accept more data until the host clears the stall. The call to `usbd_xfer_set_stall` schedules a clear-stall operation on the control endpoint. After the clear-stall completes, the framework will call the callback again with `USB_ST_SETUP`, at which point the driver can reissue the transfer. This logic is built into the framework so that every driver gets the same correct behaviour with minimal code.

For any other error, the callback falls through to `tr_setup` and attempts to resubmit the transfer. This is a simple retry policy. A more sophisticated driver might count consecutive errors and give up after a threshold, or it might escalate by calling `usbd_transfer_unsetup` on itself. For many drivers, the default retry loop is sufficient.

### The Write Callback

The write callback has the same shape but its `USB_ST_SETUP` branch is more interesting, because it has to decide whether there is any data to write:

```c
static void
myfirst_usb_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;
    unsigned int len;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        /* A previous write finished.  Wake any blocked writer. */
        wakeup(&sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        len = myfirst_usb_dequeue_write(sc);
        if (len == 0) {
            /* Nothing to send right now.  Leave the channel idle. */
            break;
        }
        pc = usbd_xfer_get_frame(xfer, 0);
        myfirst_usb_copy_write_data(sc, pc, len);
        usbd_xfer_set_frame_len(xfer, 0, len);
        usbd_transfer_submit(xfer);
        break;

    default:  /* USB_ST_ERROR */
        if (error == USB_ERR_CANCELLED)
            break;
        if (error == USB_ERR_STALLED)
            usbd_xfer_set_stall(xfer);
        goto tr_setup;
    }
}
```

The main change is the logic at `tr_setup`. For a read, the driver always wants another read armed, so the callback just sets the frame length and submits. For a write, the driver only submits if there is something to send. The helper `myfirst_usb_dequeue_write` returns the number of bytes pulled from an internal transmit queue; if zero, the callback breaks out of the switch without submitting anything, which leaves the channel idle. When userspace later writes more data into the device, the driver code that handles the `write()` system call queues the bytes and explicitly calls `usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR])`. That call fires an `USB_ST_SETUP` invocation of the callback, which now finds data in the queue and submits it.

This interaction between the userspace I/O path and the transfer state machine is the heart of an interactive USB driver. Reads are self-driving: once armed, they rearm themselves on every completion. Writes are demand-driven: they submit only when data is available and go idle otherwise. Both patterns run inside the same three-state machine; the difference is only in what happens at `USB_ST_SETUP`.

### Control Transfers

Control transfers do not typically run on continuously-armed channels; they are usually issued one-shot, either synchronously from a system-call handler or as a one-shot callback triggered by some driver event. The `struct usb_config` for a control channel has `type = UE_CONTROL` and otherwise looks similar to the bulk and interrupt configurations. The buffer size must be at least eight bytes to hold the setup packet, and the callback deals with two frames: frame zero is the setup packet, and frame one is the optional data phase.

The typical one-shot use is to issue a vendor-specific request at driver-load time. The FTDI serial driver, for example, uses control transfers to set the baud rate and line parameters every time the user configures the serial port. Because the control callback is scheduled by the framework just like any other transfer callback, the code pattern is identical. What differs is the construction of the setup packet in the `USB_ST_SETUP` branch.

For a control-read transfer, the code looks something like this:

```c
case USB_ST_SETUP: {
    struct usb_device_request req;
    req.bmRequestType = UT_READ_VENDOR_DEVICE;
    req.bRequest      = MY_VENDOR_GET_STATUS;
    USETW(req.wValue,  0);
    USETW(req.wIndex,  0);
    USETW(req.wLength, sizeof(sc->sc_status));

    pc = usbd_xfer_get_frame(xfer, 0);
    usbd_copy_in(pc, 0, &req, sizeof(req));

    usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
    usbd_xfer_set_frame_len(xfer, 1, sizeof(sc->sc_status));
    usbd_xfer_set_frames(xfer, 2);
    usbd_transfer_submit(xfer);
    break;
}
```

The `USETW` macro stores a sixteen-bit value in the request structure in the little-endian byte order USB requires. The `usbd_copy_in` helper copies from a kernel buffer into a USB frame. The `usbd_xfer_set_frame_len` and `usbd_xfer_set_frames` calls tell the framework how many frames the transfer spans and how long each is. For a control-read, frame zero is the setup packet (eight bytes) and frame one is the data phase; the framework transparently handles the status phase at the end.

In the `USB_ST_TRANSFERRED` branch, the driver reads the response out of frame one:

```c
case USB_ST_TRANSFERRED:
    pc = usbd_xfer_get_frame(xfer, 1);
    usbd_copy_out(pc, 0, &sc->sc_status, sizeof(sc->sc_status));
    /* sc->sc_status now holds the device's response. */
    break;
```

Control transfers are the right tool for configuration operations where latency and bandwidth do not matter but correctness and sequencing do. They are the wrong tool for streaming data; use bulk or interrupt transfers for that.

### Interrupt Transfers

Interrupt transfers are conceptually the simplest of the four types. An interrupt-IN channel runs a continuous state machine that polls a single endpoint at regular intervals. Each time a packet arrives from the device, the callback wakes up with `USB_ST_TRANSFERRED`. The driver reads the packet, processes it (often by delivering it to userland), and falls through to rearm.

The callback for our interrupt channel is nearly identical to the bulk-read callback:

```c
static void
myfirst_usb_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct myfirst_usb_softc *sc = usbd_xfer_softc(xfer);
    struct usb_page_cache *pc;
    int actlen;

    usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

    switch (USB_GET_STATE(xfer)) {
    case USB_ST_TRANSFERRED:
        pc = usbd_xfer_get_frame(xfer, 0);
        myfirst_usb_handle_interrupt(sc, pc, actlen);
        /* FALLTHROUGH */
    case USB_ST_SETUP:
tr_setup:
        usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
        usbd_transfer_submit(xfer);
        break;

    default:
        if (error == USB_ERR_CANCELLED)
            break;
        if (error == USB_ERR_STALLED)
            usbd_xfer_set_stall(xfer);
        goto tr_setup;
    }
}
```

The only meaningful difference from the bulk-read callback is that the buffer is smaller (interrupt endpoint packets are typically eight to sixty-four bytes) and the semantics of the data are usually "status update" rather than "stream payload." A USB HID device, for example, sends a sixty-four-byte report every few milliseconds describing key presses and mouse motions; an interrupt-IN channel polled continuously in this pattern is how the kernel receives those reports.

Interrupt-OUT channels work the same way but in reverse: the callback has to decide whether to send something at each `USB_ST_SETUP`, analogous to the bulk-write pattern.

### Frame-Level Operations: What the Framework Gives You

USB transfers are composed of frames. A bulk transfer with a large buffer might be broken into multiple packets by the hardware; the framework hides that detail and presents the transfer as a single operation. A control transfer, on the other hand, has an explicit frame structure (setup, data, status). An isochronous transfer has one frame per scheduled packet. The framework exposes this structure through a small number of helper functions:

- `usbd_xfer_max_len(xfer)` returns the largest total length the channel can transfer in a single submit.
- `usbd_xfer_set_frame_len(xfer, frame, len)` sets the length of a specific frame.
- `usbd_xfer_set_frames(xfer, n)` sets the total number of frames in the transfer.
- `usbd_xfer_get_frame(xfer, frame)` returns a page-cache pointer for a specific frame, which is what you pass to `usbd_copy_in` and `usbd_copy_out`.
- `usbd_xfer_frame_len(xfer, frame)` returns how many bytes were actually transferred in a given frame (for completions).
- `usbd_xfer_max_framelen(xfer)` returns the maximum per-frame length for the channel.

For bulk and interrupt transfers, the vast majority of drivers only touch frame zero. For control transfers, they touch frames zero and one. For isochronous transfers (which we will not cover in this chapter), they loop over many frames. The point is that the framework gives you complete control over the per-frame data layout while hiding the hardware details that would otherwise make transfer scheduling a nightmare.

### The `usbd_copy_in` and `usbd_copy_out` Helpers

USB buffers are not plain C buffers. They are allocated by the framework in a way that is addressable by the host controller hardware, which means they often live in DMA-accessible memory pages with platform-specific alignment requirements. The framework wraps these buffers in an opaque `struct usb_page_cache` object, and the driver accesses them through two helpers:

- `usbd_copy_in(pc, offset, src, len)` copies `len` bytes from the plain C buffer `src` into the framework-managed buffer at `offset`.
- `usbd_copy_out(pc, offset, dst, len)` copies `len` bytes out of the framework-managed buffer at `offset` into the plain C buffer `dst`.

You never dereference a `struct usb_page_cache *` directly. You never assume it points to a contiguous memory region. You always go through the helpers. This keeps the driver portable across platforms with different DMA constraints, and it is the standard convention throughout `/usr/src/sys/dev/usb/`.

If your driver needs to fill a USB buffer with data from a mbuf chain or from a userland pointer, there are dedicated helpers for that too: `usbd_copy_in_mbuf`, `usbd_copy_from_mbuf`, and the `uiomove` interaction is handled through `usbd_m_copy_in` and related routines. Search the USB framework source for the right helper; there is almost certainly one that matches your need.

### Starting, Stopping, and Querying Transfers

Beyond the three callbacks, the driver interacts with transfer channels through a small number of control functions. The important ones are:

- `usbd_transfer_start(xfer)`: ask the framework to schedule a callback invocation in the `USB_ST_SETUP` state, even if the channel has been idle. Used when new data becomes available for a write channel.
- `usbd_transfer_stop(xfer)`: stop the channel. Any in-flight transfer is cancelled and the callback is invoked with `USB_ST_ERROR` (with `USB_ERR_CANCELLED`). No new callbacks happen until the driver calls `usbd_transfer_start` again.
- `usbd_transfer_pending(xfer)`: returns true if a transfer is currently outstanding. Useful for deciding whether to submit a new one or defer.
- `usbd_transfer_drain(xfer)`: blocks until any outstanding transfer completes and the channel is idle. Used in teardown paths that need to wait for in-flight I/O before continuing.

These functions are safe to call while holding the driver's mutex, and in fact most of them require it. The framework documentation and the existing driver code show the expected usage patterns; when in doubt, grep for the function name in `/usr/src/sys/dev/usb/` and read how existing drivers use it.

### A Worked Example: Echo-Loop over USB

To make the transfer mechanics concrete, consider a small end-to-end scenario. The driver exposes a `/dev/myfirst_usb0` entry that accepts writes and returns reads. A user process writes a string to the device; the driver sends those bytes to the USB device through the bulk-OUT channel. The device bounces the bytes back through its bulk-IN endpoint; the driver receives them and hands them to any process currently blocked in a `read()` on the `/dev` node. This is a useful exercise because it exercises both directions of the bulk pipeline and because it has a simple, observable success criterion: the string that goes in is the string that comes out.

The driver needs a small transmit queue and a small receive queue, both protected by the softc mutex. When userspace writes, the `d_write` handler acquires the mutex, copies the bytes into the transmit queue, and calls `usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR])`. When userspace reads, the `d_read` handler acquires the mutex, checks the receive queue; if empty, it sleeps on a channel related to the queue. The write callback, running under the mutex, dequeues bytes and submits the transfer. The read callback, also under the mutex, enqueues received bytes and wakes any blocked reader.

The complete flow from userspace `write("hi")` to userspace `read()` seeing "hi" involves three threads of execution interleaved through the state machines:

1. User thread runs `write()`. Driver enqueues "hi" on the TX queue. Driver calls `usbd_transfer_start`. User thread returns.
2. Framework schedules the TX callback with `USB_ST_SETUP`. Callback dequeues "hi", copies it into frame zero, sets frame length to 2, submits. Callback returns.
3. Hardware performs the bulk-OUT transaction. Device echoes "hi" on bulk-IN.
4. Framework schedules the RX callback with `USB_ST_TRANSFERRED` (because an earlier `USB_ST_SETUP` had armed a read). Callback reads "hi" from frame zero into the RX queue, wakes any blocked reader, falls through to re-arm the read, submits. Callback returns.
5. User thread, if it was blocked in `read()`, wakes up. The `d_read` handler copies "hi" out of the RX queue into userspace. User thread returns.

At each step, the mutex is held exactly where it needs to be, the state machine moves cleanly between `USB_ST_SETUP` and `USB_ST_TRANSFERRED`, and the driver does not have to think about packet boundaries, DMA mappings, or hardware scheduling. The framework handles all of that.

### Putting the Whole Echo-Loop Driver Together

To make the echo-loop description concrete, let us walk through a complete skeleton for `myfirst_usb`. What follows is not a copy of the real driver files in `examples/`; it is a narrative presentation of how the pieces fit. The full code is in the examples directory.

The driver has one C source file, `myfirst_usb.c`, and a small header `myfirst_usb.h`. The header declares the softc structure, the constants for the transfer enumeration, and the prototypes for internal helper functions. The source file contains the match table, the transfer configuration array, the callback functions, the character-device methods, the Newbus probe/attach/detach, and the registration macros.

The softc is as we described earlier: a USB device pointer, a mutex, the transfer array, a character device pointer, an interface index, a flags byte, and two internal ring buffers for RX and TX queued data. Each ring buffer is a fixed-size array (say, 4096 bytes) plus head and tail indices, protected by the mutex.

The match table contains one entry:

```c
static const STRUCT_USB_HOST_ID myfirst_usb_devs[] = {
    {USB_VPI(0x16c0, 0x05dc, 0)},
};
```

The 0x16c0/0x05dc VID/PID pair is the Van Oosting Technologies Incorporated / OBDEV generic test VID/PID, which is free to use for prototyping.

The transfer configuration array is the three-channel array from Section 3. The callbacks are the bulk-read, bulk-write, and interrupt-read patterns we walked through.

The bulk-read callback's `USB_ST_TRANSFERRED` branch calls a helper:

```c
static void
myfirst_usb_rx_enqueue(struct myfirst_usb_softc *sc,
    struct usb_page_cache *pc, int len)
{
    int space;
    unsigned int tail;

    space = MYFIRST_USB_RX_BUFSIZE - sc->sc_rx_count;
    if (space < len)
        len = space;  /* drop the excess; a real driver might flow-control */

    tail = (sc->sc_rx_head + sc->sc_rx_count) & (MYFIRST_USB_RX_BUFSIZE - 1);
    if (tail + len > MYFIRST_USB_RX_BUFSIZE) {
        /* wrap-around copy in two pieces */
        usbd_copy_out(pc, 0, &sc->sc_rx_buf[tail], MYFIRST_USB_RX_BUFSIZE - tail);
        usbd_copy_out(pc, MYFIRST_USB_RX_BUFSIZE - tail,
            &sc->sc_rx_buf[0], len - (MYFIRST_USB_RX_BUFSIZE - tail));
    } else {
        usbd_copy_out(pc, 0, &sc->sc_rx_buf[tail], len);
    }
    sc->sc_rx_count += len;

    /* Wake any sleeper. */
    wakeup(&sc->sc_rx_count);
}
```

This is a ring-buffer enqueue with wrap-around handling. The `usbd_copy_out` helper is used to move bytes from the USB frame into the ring buffer. If the ring buffer is full, bytes are dropped. A real driver would likely either apply USB-level flow control (stop arming new reads) or grow the buffer; for the lab, dropping is acceptable.

The bulk-write callback's helper to dequeue data is the mirror image:

```c
static unsigned int
myfirst_usb_tx_dequeue(struct myfirst_usb_softc *sc,
    struct usb_page_cache *pc, unsigned int max_len)
{
    unsigned int len, head;

    len = sc->sc_tx_count;
    if (len > max_len)
        len = max_len;
    if (len == 0)
        return (0);

    head = sc->sc_tx_head;
    if (head + len > MYFIRST_USB_TX_BUFSIZE) {
        usbd_copy_in(pc, 0, &sc->sc_tx_buf[head], MYFIRST_USB_TX_BUFSIZE - head);
        usbd_copy_in(pc, MYFIRST_USB_TX_BUFSIZE - head,
            &sc->sc_tx_buf[0], len - (MYFIRST_USB_TX_BUFSIZE - head));
    } else {
        usbd_copy_in(pc, 0, &sc->sc_tx_buf[head], len);
    }
    sc->sc_tx_head = (head + len) & (MYFIRST_USB_TX_BUFSIZE - 1);
    sc->sc_tx_count -= len;
    return (len);
}
```

The character-device methods are straightforward. Open checks that the device is not already open, sets the open flag, and arms the read channel:

```c
static int
myfirst_usb_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    if (sc->sc_flags & MYFIRST_USB_FLAG_OPEN) {
        mtx_unlock(&sc->sc_mtx);
        return (EBUSY);
    }
    sc->sc_flags |= MYFIRST_USB_FLAG_OPEN;
    sc->sc_rx_head = sc->sc_rx_count = 0;
    sc->sc_tx_head = sc->sc_tx_count = 0;
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_RD]);
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_INTR_DT_RD]);
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

Close clears the open flag and stops the read channel:

```c
static int
myfirst_usb_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;

    mtx_lock(&sc->sc_mtx);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_BULK_DT_RD]);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_INTR_DT_RD]);
    usbd_transfer_stop(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
    sc->sc_flags &= ~MYFIRST_USB_FLAG_OPEN;
    mtx_unlock(&sc->sc_mtx);

    return (0);
}
```

Read blocks until data is available, then copies bytes from the ring buffer to userspace:

```c
static int
myfirst_usb_read(struct cdev *dev, struct uio *uio, int flags)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;
    unsigned int len;
    char tmp[128];
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    while (sc->sc_rx_count == 0) {
        if (sc->sc_flags & MYFIRST_USB_FLAG_DETACHING) {
            mtx_unlock(&sc->sc_mtx);
            return (ENXIO);
        }
        if (flags & O_NONBLOCK) {
            mtx_unlock(&sc->sc_mtx);
            return (EAGAIN);
        }
        error = msleep(&sc->sc_rx_count, &sc->sc_mtx,
            PCATCH | PZERO, "myfirstusb", 0);
        if (error != 0) {
            mtx_unlock(&sc->sc_mtx);
            return (error);
        }
    }

    while (uio->uio_resid > 0 && sc->sc_rx_count > 0) {
        len = min(uio->uio_resid, sc->sc_rx_count);
        len = min(len, sizeof(tmp));
        /* Copy out of ring buffer into tmp (handles wrap-around) */
        myfirst_usb_rx_read_into(sc, tmp, len);
        mtx_unlock(&sc->sc_mtx);
        error = uiomove(tmp, len, uio);
        mtx_lock(&sc->sc_mtx);
        if (error != 0)
            break;
    }
    mtx_unlock(&sc->sc_mtx);
    return (error);
}
```

Notice the pattern: the mutex is held while manipulating the ring buffer, but it is released around the `uiomove` call, because `uiomove` can sleep (to fault in user pages) and sleeping while holding a mutex is forbidden. The mutex is reacquired after `uiomove` returns.

Write is the mirror: copy bytes from user to TX buffer, then kick the write channel:

```c
static int
myfirst_usb_write(struct cdev *dev, struct uio *uio, int flags)
{
    struct myfirst_usb_softc *sc = dev->si_drv1;
    unsigned int len, space, tail;
    char tmp[128];
    int error = 0;

    mtx_lock(&sc->sc_mtx);
    while (uio->uio_resid > 0) {
        if (sc->sc_flags & MYFIRST_USB_FLAG_DETACHING) {
            error = ENXIO;
            break;
        }
        space = MYFIRST_USB_TX_BUFSIZE - sc->sc_tx_count;
        if (space == 0) {
            /* buffer is full; wait for the write callback to drain it */
            error = msleep(&sc->sc_tx_count, &sc->sc_mtx,
                PCATCH | PZERO, "myfirstusbw", 0);
            if (error != 0)
                break;
            continue;
        }
        len = min(uio->uio_resid, space);
        len = min(len, sizeof(tmp));

        mtx_unlock(&sc->sc_mtx);
        error = uiomove(tmp, len, uio);
        mtx_lock(&sc->sc_mtx);
        if (error != 0)
            break;

        /* Copy tmp into TX ring buffer (handles wrap-around) */
        tail = (sc->sc_tx_head + sc->sc_tx_count) & (MYFIRST_USB_TX_BUFSIZE - 1);
        myfirst_usb_tx_buf_append(sc, tail, tmp, len);
        sc->sc_tx_count += len;
        usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
    }
    mtx_unlock(&sc->sc_mtx);
    return (error);
}
```

Two things are worth noticing in write. First, when the TX buffer is full, the write handler sleeps on `sc_tx_count`; the write callback's `USB_ST_TRANSFERRED` branch calls `wakeup(&sc_tx_count)` after draining some bytes, which wakes the sleeping writer. Second, the write handler calls `usbd_transfer_start` on every chunk it enqueues. This is safe (starting an already-running channel is a no-op) and it ensures the write callback is nudged even if the channel had gone idle.

With these four cdev methods and the three transfer callbacks, you have a complete minimum-viable USB echo driver. The full source is approximately three hundred lines: short enough to fit on a single screen, concrete enough to exercise the real API.

### Choosing Between `usb_fifo` and a Custom `cdevsw`

When a USB driver needs to expose a `/dev` entry to userland, FreeBSD offers two approaches. The first is the `usb_fifo` framework, a generic byte-stream abstraction that gives you `/dev/ugenN.M.epM` style nodes with read, write, poll, and a small ioctl interface. You declare a `struct usb_fifo_methods` with open, close, start-read, start-write, stop-read, and stop-write callbacks, and the framework handles the cdev plumbing and the queueing. This is the path of least resistance; `uhid(4)` and `ucom(4)` both use it.

The second approach is a custom `cdevsw`, the same pattern you practised in Chapter 24. This gives you total control over the user interface at the cost of writing more code. It is appropriate when the driver needs a very specific ioctl surface, when the read/write semantics do not fit a byte stream (for example, a message-oriented protocol), or when the driver already fits poorly into the `usb_fifo` model.

For the running example we have built, a custom `cdevsw` is the right choice because we wrote the attach method that calls `make_dev` and the detach method that calls `destroy_dev`, which is exactly what a custom `cdevsw` requires. For a driver that exposes a byte stream (a serial adapter, say), `usb_fifo` is simpler. When you write your next USB driver, look at both options and pick the one whose interface matches your problem.

### Error Handling and Retry Policy

The retry loop that our bulk-read callback uses, "on any error, rearm and try again," is a reasonable default for robust drivers. But it is not the only policy, and sometimes it is the wrong one.

For a device that might genuinely go away mid-transfer (a USB adapter whose physical connection has been removed before the framework has had a chance to notice), rearming indefinitely is a waste; the transfers will keep failing until detach is called. Adding a small retry counter and giving up after, say, five consecutive errors, keeps the log from filling with noise.

For a device that implements a strict request-response protocol, an error might invalidate the entire session. In that case, the callback should not rearm; instead, it should mark the driver as "in error" and let the user close and reopen the device to reset.

For a device that supports stall-and-clear as a normal flow-control mechanism, the `usbd_xfer_set_stall` path is in the happy path, not the error path. Some class protocols use stalls to signal "I am not ready right now"; the framework's automatic clear-stall machinery handles this transparently.

Your choice of retry policy should match the real behaviour of the device you are writing for. When in doubt, start with the simple "rearm on error" default, observe what happens when you plug and unplug the device repeatedly, and refine from there.

### Timeouts and Their Consequences

A timeout on a USB transfer is not just a safety net against hardware stalls; it is an explicit statement about how long the driver is willing to wait for an operation to complete before treating it as a failure. Choosing a timeout is a design decision that interacts with many other parts of the driver, and getting it right requires thinking through several scenarios.

The configuration field `timeout` in `struct usb_config` is measured in milliseconds. A value of zero means "no timeout"; the transfer will wait indefinitely. A positive value means "if the transfer has not completed after this many milliseconds, cancel it and deliver a timeout error to the callback."

For a read channel on a bulk endpoint, the usual choice is zero. Reads on bulk channels are waiting for the device to have something to say, and if the device is silent for minutes, that is not necessarily an error. A timeout would force the driver to rearm the read every few seconds, which wastes time and produces noise in the log.

For a write channel, the usual choice is a modest positive value like 5000 (five seconds). If the device fails to drain its FIFO in that time, something is wrong; rather than block an indefinite-length write, the driver returns an error to userland, which can retry if it wishes.

For an interrupt-IN channel polling for status updates, the usual choice is either zero (like a bulk read) or a timeout that matches the expected polling interval from the endpoint descriptor's `bInterval` field. Matching `bInterval` gives the driver an explicit "I should have heard from the device by now" signal.

For a control transfer, timeouts matter most, because control transfers are how the driver configures the device, and a device that does not respond to configuration is wedged. A timeout of 500 to 2000 milliseconds is common. If the device does not respond to a configuration request in a few seconds, the driver should assume something is wrong.

What happens when a timeout fires? The framework calls the callback with `USB_ERR_TIMEOUT` as the error. The callback typically treats this as a transient failure and rearms (for repeating channels) or returns an error to the caller (for one-shot operations). A repeating read channel that keeps timing out is probably talking to a device that is not responding; after a few consecutive timeouts, it may be worth escalating by calling `usbd_transfer_unsetup` or by logging a more visible warning.

One subtle interaction is worth mentioning: if the transfer has a timeout and the driver also sets `pipe_bof` (pipe blocked on failure), a timeout will block the pipe until the driver explicitly clears the block. This is usually what you want, because the pipe may be in an inconsistent state, and clearing the block (by submitting a fresh setup, or by calling `usbd_transfer_start`) is a good point to log what happened and decide what to do next.

### What Goes Wrong When Transfer Setup Fails

The `usbd_transfer_setup` call can fail for several reasons. Understanding each is useful both for debugging your own driver and for reading the FreeBSD source when you encounter failures.

**Endpoint mismatch.** If the configuration array asks for an endpoint with a specific type/direction pair that does not exist on the interface, the call fails with `USB_ERR_NO_PIPE`. This usually means the match table matched a device that has a different descriptor layout than the driver expected; it is a bug in the driver.

**Unsupported transfer type.** If the configuration specifies `UE_ISOCHRONOUS` on a host controller that does not support isochronous transfers, or if the bandwidth reservation cannot be satisfied, the call fails. Isochronous is the most complex transfer type and the most likely to have platform-specific limitations.

**Out of memory.** The framework allocates DMA-capable buffers for the channels. If memory is low, the allocation fails. This is rare on modern systems but can happen on embedded platforms with tight memory budgets.

**Missing or invalid attributes.** If the configuration has a buffer size of zero, or a negative frame count, or an invalid flag combination, the call fails. Check the configuration against the declarations in `/usr/src/sys/dev/usb/usbdi.h`.

**Power management states.** If the device has been suspended or is in a low-power state, some transfer setup requests will fail. This is mainly relevant for drivers that handle USB selective suspend.

When `usbd_transfer_setup` fails, the error code is an `usb_error_t` value, not a standard errno. The definitions are in `/usr/src/sys/dev/usb/usbdi.h`. The function `usbd_errstr` converts an error code to a printable string; use it in your `device_printf` to make diagnostic messages informative.

### A Detail About `pipe_bof`

We mentioned `pipe_bof` (pipe blocked on failure) as a flag in the transfer configuration, but the motivation for it deserves a closer look. USB endpoints are conceptually single-threaded from the device's perspective. When the host submits a bulk-OUT packet, the device must process that packet before accepting another. If the packet fails, the device may be in an indeterminate state, and the next packet should not be sent until the driver has had a chance to resynchronise.

`pipe_bof` tells the framework to pause the pipe when a transfer fails. The next `usbd_transfer_submit` will not actually start a hardware operation; instead, the framework waits until the driver explicitly calls `usbd_transfer_start` on the channel, which acts as a "resume" signal. This lets the driver do a clear-stall or otherwise resynchronise before the next transfer begins.

Without `pipe_bof`, the framework would immediately submit the next transfer after a failure, which might run into the same failure before the driver has had a chance to react.

Setting `pipe_bof = 1` is the safe default for most drivers. Clearing it is appropriate for drivers that want to keep a pipeline full even through occasional errors (for example, audio drivers where a brief glitch is preferable to a synchronous resynchronisation).

### `short_xfer_ok` and Data-Length Semantics

The `short_xfer_ok` flag is another configuration option whose meaning is worth spelling out. USB bulk transfers do not have an inherent end-of-message marker. If the host has a buffer of 512 bytes and the device only has 100 bytes to send, what should happen? There are two possible answers.

With `short_xfer_ok` clear (the default), a transfer that completes with less data than requested is treated as an error. The framework delivers `USB_ERR_SHORT_XFER` to the callback, and the driver must decide whether to retry, ignore, or escalate.

With `short_xfer_ok` set, a short transfer is treated as success. The callback gets `USB_ST_TRANSFERRED` with `actlen` set to the actual number of bytes received. This is almost always what you want for bulk-IN on message-oriented protocols, where the device decides how much data to send.

There is a corresponding flag for outgoing transfers: `force_short_xfer`. If set, a transfer whose data happens to be an exact multiple of the endpoint's maximum packet size will be padded with a zero-length packet at the end to signal "end of message." USB treats a zero-length packet as a valid transaction, and many protocols use it as an explicit boundary marker. The FTDI driver sets this flag on its write channel, for example, because the FTDI protocol expects a trailing short packet.

Knowing which flag is appropriate requires knowing the protocol the device implements. When you write a driver for a device documented with a public protocol specification, check the specification for how it handles boundaries. When you write a driver for a poorly-documented device, set `short_xfer_ok` on reads (you can always count the bytes), and test both settings of `force_short_xfer` on writes to see which the device accepts.

### Locking Rules Around Transfers

The USB framework imposes two locking rules that are essential to get right.

First, the mutex you pass to `usbd_transfer_setup` is held by the framework around every invocation of the callback. You do not need to acquire it inside the callback; it is already held. You also must not release it inside the callback; doing so breaks the framework's assumption and can cause random failures.

Second, every call from driver code (not from the callback) to one of `usbd_transfer_start`, `usbd_transfer_stop`, `usbd_transfer_submit`, `usbd_transfer_drain`, and `usbd_transfer_pending` must be made with the mutex held. This is because these functions read and write fields inside the transfer object that the callback also touches, and the mutex is what serialises access.

Practically, this means most driver code that interacts with transfers looks like:

```c
mtx_lock(&sc->sc_mtx);
usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
mtx_unlock(&sc->sc_mtx);
```

or in longer critical sections:

```c
mtx_lock(&sc->sc_mtx);
/* enqueue data */
enqueue(sc, data, len);
/* nudge the channel if it is idle */
if (!usbd_transfer_pending(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]))
    usbd_transfer_start(sc->sc_xfer[MYFIRST_USB_BULK_DT_WR]);
mtx_unlock(&sc->sc_mtx);
```

Drivers that violate these rules occasionally appear to work but fail intermittently on load, under heavy I/O, or during detach. Getting the locking right from the start saves many hours of debugging later.

### Wrapping Up Section 3

Section 3 has shown how data flows through a USB driver. A configuration array declares the channels, `usbd_transfer_setup` allocates them, the callbacks drive them through the three-state machine, and `usbd_transfer_unsetup` tears them down. The framework abstracts away the hardware details: DMA buffers, frame scheduling, endpoint arbitration, stall handling. The driver's job is to write callbacks that handle completion and to arrange the flow of data through the callbacks.

Three themes are worth carrying forward. First, the three-state state machine (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`) is the same in every channel, regardless of transfer type. Learning to read a USB callback means learning to parse this state machine; once you know it, every callback in every USB driver in the tree is legible. Second, the `struct usb_page_cache` abstraction is the only safe way to move data into and out of USB buffers. Never bypass `usbd_copy_in` and `usbd_copy_out`. Third, the locking discipline around `usbd_transfer_start`, `_stop`, and `_submit` is not optional; every call from driver code must be made under the mutex.

With Sections 1 through 3 in hand, you have a complete mental model of USB driver writing: the concepts, the skeleton, and the data pipeline. Section 4 now shifts to the serial side of Part 6. The UART subsystem is older, simpler in some ways, more constrained in others, and its idioms are different from USB's. But many of the same habits carry over: match against what you support, attach in phases that can be cleanly reversed, drive the hardware through a state machine, and respect the locking.

> **Take a breath.** We have now worked through the USB half of the chapter: the host and device roles, the descriptor tree, the four transfer types, the probe/attach/detach skeleton, and the three-state `USB_ST_SETUP`/`USB_ST_TRANSFERRED`/`USB_ST_ERROR` callback machine that every USB driver runs. The rest of the chapter turns to the serial side: the `uart(4)` framework with its `ns8250` reference driver, integration with the TTY layer and `termios`, the `ucom(4)` bridge used by USB-to-serial adapters, and the tools and labs that let you test both kinds of drivers without real hardware. If you want to close the book and come back, this is a natural pause.

## Section 4: Writing a Serial UART Driver

### From USB to UART: A Shift of Landscape

Sections 2 and 3 gave you a complete USB driver. The framework there was modern in every sense: hot-plug, DMA-aware, message-oriented, richly abstracted. Section 4 now turns to `uart(4)`, FreeBSD's framework for driving Universal Asynchronous Receiver/Transmitters. The landscape is different. Many UART chips are older than USB itself, and the framework's design reflects that. There is no hot-plug (a serial port is usually soldered to the board). There is no DMA for most parts (the chip has a small FIFO you poll or an interrupt you handle). There is no descriptor hierarchy (the chip does not advertise its capabilities; you know what you built against). And there is no notion of transfer channels; there is just the port, into which bytes go and out of which bytes come.

What the framework does provide is a disciplined split of responsibilities between three layers. At the bottom sits your driver, which knows how the chip's registers work, how its interrupts fire, how its FIFOs behave, and what platform-specific resources (IRQ line, I/O port range, clock source) it needs. In the middle sits the `uart(4)` framework itself, which handles registration, baud-rate configuration calculations, buffering, TTY integration, and the scheduling of read and write work. At the top sits the TTY layer, which presents the port to userland as `/dev/ttyuN` and `/dev/cuauN` and handles terminal semantics: line editing, signal generation, control characters, and the vast vocabulary of `termios` knobs that `stty(1)` exposes.

You do not write the TTY layer. You do not write most of the `uart(4)` framework. Your job, when you write a UART driver, is to implement a small set of hardware-specific methods that the framework calls when it needs to do something at the register level. The framework then wires those methods into the rest of the kernel's serial machinery for free.

This section walks through that wiring. It covers the layout of the `uart(4)` framework, the structures and methods you have to fill in, the canonical `ns8250` driver as a concrete reference, and the integration with the TTY layer. It ends with the related `ucom(4)` framework, which is how USB-to-serial bridges expose themselves to userland using the same TTY interface as a real UART.

### Where the `uart(4)` Framework Lives

The framework itself lives in `/usr/src/sys/dev/uart/`. If you list that directory, you see a handful of framework files and a family of hardware-specific drivers.

The framework files are:

- `/usr/src/sys/dev/uart/uart.h`: the top-level header that defines the framework's public API.
- `/usr/src/sys/dev/uart/uart_bus.h`: the structures for newbus integration and the per-port softc.
- `/usr/src/sys/dev/uart/uart_core.c`: the attachment logic, the interrupt dispatcher, the polling loop, the link between `uart(4)` and `tty(4)`.
- `/usr/src/sys/dev/uart/uart_tty.c`: the `ttydevsw` implementation that maps `uart(4)` operations onto `tty(4)` operations.
- `/usr/src/sys/dev/uart/uart_cpu.h`, `uart_dev_*.c`: platform glue and console registration.

The hardware-specific drivers are files of the form `uart_dev_NAME.c` and occasionally `uart_dev_NAME.h`. The most important of these is `uart_dev_ns8250.c`, which implements the ns8250 family (including the 16450, 16550, 16550A, 16650, 16750, and many compatibles). Because the 16550A is effectively the standard UART for PC-style serial ports, this one driver handles the majority of actual serial hardware in the world. When you want to learn how a real FreeBSD UART driver looks, this is the file to open.

Other drivers in the directory handle chips that are not 16550-compatible: the Intel MID variant, the PL011 ARM UART used on Raspberry Pi and other ARM boards, the NXP i.MX UART, the Sun Microsystems Z8530, and so on. Each one follows the same pattern: fill in a `struct uart_class` and a `struct uart_ops`, register with the framework, and implement the hardware access methods.

### The `uart_class` Structure

Every UART driver begins by declaring a `struct uart_class`, which is the hardware descriptor that the framework uses to identify the chip family. The definition lives in `/usr/src/sys/dev/uart/uart_bus.h`. The structure looks like this (paraphrased; the real declaration has a few more fields):

```c
struct uart_class {
    KOBJ_CLASS_FIELDS;
    struct uart_ops *uc_ops;
    u_int            uc_range;
    u_int            uc_rclk;
    u_int            uc_rshift;
};
```

The `KOBJ_CLASS_FIELDS` macro pulls in the kobj machinery that Chapter 23 introduced (in the context of Newbus). A `uart_class` is, at the kernel's abstract-object level, a kobj class whose instances are `uart_softc`. This is how the framework can call into driver-specific methods without needing an `if` ladder: the method dispatch is done by kobj lookup.

`uc_ops` is a pointer to the operations structure (coming next), which lists the chip-specific methods.

`uc_range` is how many bytes of register address space the chip uses. For an ns16550-compatible UART, this is 8.

`uc_rclk` is the reference clock frequency in hertz. The framework uses this to compute baud-rate divisors. For a PC-style UART, the reference clock is usually 1,843,200 hertz (a specific multiple of the standard baud rates).

`uc_rshift` is the register address shift. On some buses, UART registers are spaced at intervals other than one byte (for example, every four bytes on some memory-mapped designs). A shift of zero means tight packing; a shift of two means each logical register occupies four bytes of address space.

For our running example, the class declaration looks like this:

```c
static struct uart_class myfirst_uart_class = {
    "myfirst_uart class",
    myfirst_uart_methods,
    sizeof(struct myfirst_uart_softc),
    .uc_ops   = &myfirst_uart_ops,
    .uc_range = 8,
    .uc_rclk  = 1843200,
    .uc_rshift = 0,
};
```

The first three positional arguments are the `KOBJ_CLASS_FIELDS` entries: a name, a method table, and a per-instance size. The named fields are the UART-specific ones. For a driver targeting 16550-compatible chips, these values are the conventional defaults.

### The `uart_ops` Structure

The `struct uart_ops` is where the real hardware-specific code lives. It is a table of function pointers that the framework calls at specific moments. The definition lives in `/usr/src/sys/dev/uart/uart_cpu.h`:

```c
struct uart_ops {
    int  (*probe)(struct uart_bas *);
    void (*init)(struct uart_bas *, int, int, int, int);
    void (*term)(struct uart_bas *);
    void (*putc)(struct uart_bas *, int);
    int  (*rxready)(struct uart_bas *);
    int  (*getc)(struct uart_bas *, struct mtx *);
};
```

Each operation takes a `struct uart_bas *` as its first argument. The "bas" stands for "bus address space"; it is the framework's abstraction for access to the chip's registers. A driver does not know or care whether the chip is in I/O space or in memory-mapped space; it just calls `uart_getreg(bas, offset)` and `uart_setreg(bas, offset, value)` (declared in `/usr/src/sys/dev/uart/uart.h`), and the framework routes the access correctly.

Let us go through the six operations in turn.

`probe` is called when the framework needs to know whether a chip of this class is present at a given address. The driver typically pokes a register, reads it back, and returns zero if the readback matches (suggesting the chip is really there) or a nonzero errno otherwise. For an ns16550, the probe writes a test pattern to the scratch register and reads it back.

`init` is called to initialise the chip to a known state. The arguments after the bas are `baudrate`, `databits`, `stopbits`, and `parity`. The driver computes the divisor, writes the divisor-latch-access bit, writes the divisor, clears the divisor-latch, sets the line control register for the requested data/stop/parity configuration, enables the FIFOs, and enables the chip's interrupts. The exact register sequence for a 16550 is several dozen lines of code and is documented in the chip's data sheet.

`term` is called to shut down the chip. It typically disables interrupts, flushes the FIFOs, and leaves the chip in a safe state.

`putc` sends a single character. This is used by the low-level console path and by polling-based diagnostic output. The driver busy-waits on the transmitter-holding-register-empty flag, then writes the byte to the transmit register.

`rxready` returns nonzero if at least one byte is available to read. The driver reads the line status register and checks the data-ready bit.

`getc` reads a single character. Used by the low-level console for input. The driver busy-waits on the data-ready flag (or the caller ensures `rxready` just returned true), then reads the receive register.

These six methods are the entire hardware-specific surface for a UART driver at the low level. Everything else (interrupt handling, buffering, TTY integration, hot-plug of PCIe UARTs, console selection) is provided by the framework. A new UART driver is, in effect, a six-function implementation plus a handful of declarations.

### A Closer Look at `ns8250`

The ns8250 driver at `/usr/src/sys/dev/uart/uart_dev_ns8250.c` is the best place to see these methods concretely. It is a mature, production-grade driver that handles every variant of the 8250/16450/16550/16550A family. The register definitions it uses (from `/usr/src/sys/dev/ic/ns16550.h`) are the same ones every UART-related header in the PC world uses. When you read this driver, you are reading, in effect, the reference implementation of a 16550 driver for FreeBSD.

The put-character implementation is instructive for its simplicity:

```c
static void
ns8250_putc(struct uart_bas *bas, int c)
{
    int limit;

    limit = 250000;
    while ((uart_getreg(bas, REG_LSR) & LSR_THRE) == 0 && --limit)
        DELAY(4);
    uart_setreg(bas, REG_DATA, c);
    uart_barrier(bas);
    limit = 250000;
    while ((uart_getreg(bas, REG_LSR) & LSR_TEMT) == 0 && --limit)
        DELAY(4);
}
```

The loop polls the line status register (LSR) for the transmitter-holding-register-empty (THRE) flag. When it is set, the transmit holding register is ready to accept a byte. The driver writes the byte to the data register (REG_DATA) and then polls again for the transmitter-empty (TEMT) flag to ensure the byte has been shifted out before returning.

The `uart_barrier` call is a memory barrier that ensures the write to the data register is visible to the hardware before subsequent reads. On platforms with weak memory ordering, missing this barrier would cause intermittent data loss.

The `DELAY(4)` yields four microseconds per iteration, and the `limit` counter is 250,000. Together, they give a one-second timeout before the loop gives up. For a real UART, 250,000 iterations is a cap that should never be reached in normal operation; it is a safety net for the pathological case where the chip is in an unexpected state.

The probe is equally direct:

```c
static int
ns8250_probe(struct uart_bas *bas)
{
    u_char val;

    /* Check known 0 bits that don't depend on DLAB. */
    val = uart_getreg(bas, REG_IIR);
    if (val & 0x30)
        return (ENXIO);
    return (0);
}
```

Bits 4 and 5 of the Interrupt Identification Register (IIR) are defined as always-zero for every variant of the 16550 family. If those bits read as one, this is not a real 16550 register, and the probe rejects the address.

You could read the whole driver in an afternoon. What you would come away with is a clear mental model: the methods are narrow, the framework is large, and the real engineering is in handling the quirks of specific chip revisions (a FIFO bug in the 16550 predecessor, an erratum in some PC chipsets, a signal-detect issue on certain Oxford devices). A new UART driver for a well-behaved chip is genuinely a small file.

### The `uart_softc` and How the Framework Uses It

Each instance of a UART driver has a `struct uart_softc`, defined in `/usr/src/sys/dev/uart/uart_bus.h`. The framework allocates one per attached port. Its important fields include a pointer to the `uart_bas` that describes the port's register layout, the I/O resources (the IRQ, the memory range or I/O port range), the TTY device attached to this port, the current line parameters, and two byte buffers (RX and TX) that the framework uses internally. The driver does not usually allocate its own softc; it uses the framework's `uart_softc`, with the hardware-specific extensions added through kobj class inheritance.

When the framework receives an interrupt from a UART, it calls a framework-internal function that reads the interrupt-identification register, decides what kind of work the chip has requested (transmit-ready, receive-data-available, line-status, modem-status), and dispatches to the appropriate handler. The handlers pull data out of the chip's RX FIFO into the framework's RX ring buffer, or push data from the framework's TX ring buffer into the chip's TX FIFO, or update state variables in response to modem-signal changes. The interrupt handler returns, and the TTY layer consumes the ring buffers at its own pace through the framework's put-character and get-character paths.

This is why the driver's `uart_ops` table is so small. The high-volume work (moving bytes between the chip and the ring buffers) is handled by shared framework code that reads the chip's registers through `uart_getreg` and `uart_setreg`. The driver only needs to expose the low-level primitives; the composition is done for it.

### Integration with the TTY Layer

Above the `uart(4)` framework sits the TTY layer, defined in `/usr/src/sys/kern/tty.c` and friends. A UART port in FreeBSD appears to userland as two `/dev` nodes:

- `/dev/ttyuN`: the callin node. Opening it blocks until a carrier detect signal is asserted (which models an incoming call on a modem). It is used for devices that answer, not initiate, connections.
- `/dev/cuauN`: the callout node. Opening it does not wait for carrier detect. It is used for devices that initiate connections, or for developers who want to talk to a serial port without pretending it is a modem.

The distinction is historical, dating from the era when serial ports were genuinely connected to analog modems with separate "someone is calling" and "I am initiating a call" semantics. FreeBSD preserves the distinction because some embedded and industrial workflows still rely on it, and because the implementation cost is minimal once the TTY layer's "two sides of the same port" pattern is in place.

The TTY layer calls into the `uart(4)` framework through a `ttydevsw` structure whose methods map neatly onto UART operations. The important entries include:

- `tsw_open`: called when userland opens the port. The framework enables interrupts, powers on the chip, and applies the default `termios`.
- `tsw_close`: called when the last userland reference is released. The framework drains the TX buffer, disables interrupts (unless the port is also a console), and puts the chip in an idle state.
- `tsw_ioctl`: called for ioctls the TTY layer does not handle itself. Most UART-specific ioctls are handled by the framework.
- `tsw_param`: called when `termios` changes. The framework reprograms the chip's baud rate, data bits, stop bits, parity, and flow control.
- `tsw_outwakeup`: called when there is new data to transmit. The framework enables the transmit-ready interrupt if it was disabled; on the next IRQ, the framework pushes bytes from the ring buffer into the chip.

You do not usually have to write any of these. The framework in `uart_tty.c` implements them once for every UART driver. Your driver's only contribution is the six methods in `uart_ops`.

### The `termios` Interface in Practice

When a user runs `stty 115200` on a serial port, the following chain of calls happens:

1. `stty(1)` opens the port and issues a `TIOCSETA` ioctl carrying the new `struct termios`.
2. The kernel TTY layer receives the ioctl and updates its internal copy of the port's termios.
3. The TTY layer calls `tsw_param` on the port's `ttydevsw`, passing the new termios.
4. The `uart(4)` framework's `uart_param` implementation looks at the termios fields (`c_ispeed`, `c_ospeed`, `c_cflag` with its `CSIZE`, `CSTOPB`, `PARENB`, `PARODD`, `CRTSCTS` sub-bits) and calls the driver's `init` method with the corresponding raw values.
5. The driver's `init` method computes the divisor, writes the line-control register, reconfigures the FIFO, and returns.

None of this requires the driver to know about termios. The translation from termios bits to raw integers is done by the framework. The driver sees only the raw values: baudrate in bits per second, databits (usually 5 through 8), stopbits (1 or 2), and a parity code.

This separation is what lets FreeBSD run the same `uart(4)` framework on top of radically different chips. A 16550 driver and a PL011 driver both implement the same six `uart_ops` methods. The termios-to-raw translation happens once, in framework code, for every chip family.

### Flow Control at the Register Level

Hardware flow control is typically driven by two signals on the UART: CTS (clear to send) and RTS (request to send). When CTS is asserted by the remote side, it is telling the local transmitter "I am ready for more data." When the local side asserts RTS, it is telling the remote transmitter the same thing. When either signal is not asserted, the corresponding transmitter pauses.

In a 16550, RTS is driven by a bit in the modem control register (MCR), and CTS is read from a bit in the modem status register (MSR). The framework exposes flow control through termios (`CRTSCTS` flag), through ioctls (`TIOCMGET`, `TIOCMSET`, `TIOCMBIS`, `TIOCMBIC`), and through automatic responses to FIFO fill levels.

When the receive FIFO fills past a threshold, the driver deasserts RTS to ask the remote side to stop transmitting. When the FIFO drains below a different threshold, the driver reasserts RTS. When the modem-status-interrupt fires because CTS changed, the interrupt handler enables or disables the transmit path accordingly. All of this is framework logic; the driver only exposes the register-level primitives.

Software flow control (XON/XOFF) is handled entirely in the TTY layer, by inserting and interpreting the XON (0x11) and XOFF (0x13) bytes in the data stream. The UART driver has no role in it.

### The Interrupt Handler Path in Detail

Beyond the six `uart_ops` methods, a real UART driver usually implements an interrupt handler. The framework provides a generic one in `uart_core.c` that works for the vast majority of chips, but the driver can supply its own for chips with unusual behaviour. To understand what the framework's generic handler does, and when you might want to override it, it helps to trace the handler's path.

When the hardware interrupt fires, the framework's ISR reads the interrupt identification register (IIR) through `uart_getreg`. The IIR encodes which of four conditions triggered the interrupt: line-status (a framing error or overrun occurred), received-data-available (at least one byte is in the receive FIFO), transmitter-holding-register-empty (the TX FIFO wants more data), or modem-status (a modem signal changed state).

For line-status interrupts, the framework logs a warning (or increments a counter) and continues.

For received-data-available, the framework reads bytes out of the chip's RX FIFO one at a time, pushing each into the driver's internal RX ring buffer. The loop continues until the receive-data-available flag clears. Once the ring buffer has bytes, the framework signals the TTY layer's input path, which will pull bytes out as the consumer is ready.

For transmitter-holding-register-empty, the framework pulls bytes out of its internal TX ring buffer and pushes them into the chip's TX FIFO one at a time. The loop continues until the TX FIFO is full or the ring buffer is empty. Once the ring buffer is empty, the framework disables the transmit interrupt so the chip does not keep firing; the next `tsw_outwakeup` call (from the TTY layer, when there is new data) will reenable it.

For modem-status changes, the framework updates its internal modem-signal state and signals the TTY layer if the change is significant (for example, CTS deassertion when hardware flow control is enabled).

This is all done in interrupt context with the driver's mutex held. The mutex is a spin mutex (`MTX_SPIN`) for UART drivers, because taking a sleepable mutex in an interrupt handler is forbidden. The framework's helpers know this and use appropriate primitives.

When might a driver want to override the generic handler? Three situations come to mind.

First, if the chip has unusual FIFO semantics. Some chips do not clear their interrupt identification registers in the obvious way; you have to drain the FIFO completely, or you have to read a specific register to acknowledge. If your chip's data sheet describes such a quirk, you override the handler with chip-specific logic.

Second, if the chip has DMA support you want to use. The framework's generic handler is PIO (programmed I/O): one byte per register access. A chip with a DMA engine could move many bytes per interrupt, significantly reducing CPU overhead at high baud rates. Implementing DMA requires chip-specific code.

Third, if the chip has hardware timestamping or other advanced features. Some embedded UARTs can timestamp individual received bytes with microsecond precision, which is invaluable for industrial protocols. The framework does not know about this, so the driver must implement it.

For typical hardware, the generic handler is correct and performant. Do not override it without a specific reason.

### The TX and RX Ring Buffers

The `uart(4)` framework keeps two ring buffers inside each port's softc. These are separate from any buffering on the chip itself: even if the chip has a 64-byte FIFO, the framework has its own ring buffers of some configurable size (typically 4 KB for each direction) that sit between the chip and the TTY layer.

The purpose of these ring buffers is to absorb bursts. Suppose the consumer of data is slow (a busy userland process), and the producer (the remote serial device) is pushing data at 115200 baud. Without a ring buffer, the chip's 64-byte FIFO would fill up in about 6 milliseconds, and bytes would be lost. With a 4 KB ring buffer, the buffer can absorb a 350-millisecond burst at 115200 baud, which is enough for userland to catch up in almost every realistic scenario.

The sizes of these ring buffers are not generally configurable per-driver; they are baked into the framework. The ring buffer implementation is in `uart_core.c` and uses the same kind of head/tail pointer arithmetic as the ring buffers in our USB echo driver.

When the TTY layer asks for bytes (through `ttydisc_rint`), the framework moves bytes out of the RX ring into the TTY layer's own input queue, which has its own buffering and line-discipline processing (canonical mode, echo, signal generation, and so on). When userland writes bytes, they arrive at the framework's `tsw_outwakeup` path and are moved into the TX ring; the framework's transmit-empty interrupt handler pushes them from the ring into the chip.

This arrangement has a nice property: the driver, the framework, and the TTY layer are all loosely coupled. The driver only knows about the chip. The framework only knows about registers and ring buffers. The TTY layer only knows about buffering and line discipline. Each layer can be tested and reasoned about independently.

### Debugging Serial Drivers

When a serial driver does not work, the symptoms can be confusing. Bytes go in, bytes come out, but the two do not match. The clock ticks, but the characters look like gibberish. The port opens, but writes return zero bytes. This section lists the diagnostic techniques that help.

**Log aggressively at attach.** Use `device_printf(dev, "attached at %x, IRQ %d\n", ...)` to verify the address and IRQ your driver ended up with. If the address is wrong, no I/O will work; if the IRQ is wrong, no interrupts will fire. Attach messages are the first line of defence.

**Use `sysctl dev.uart.0.*` to inspect port state.** The `uart(4)` framework exports many per-port knobs and statistics through sysctl. Reading them shows the current baud rate, the number of bytes transmitted, the number of overruns, the modem signal state, and more. If `tx` is incrementing but `rx` is not, the transmitter works but the receiver does not; if both are zero, nothing is happening at all.

**Probe the hardware with `kgdb`.** If you have a kernel crash dump or the ability to attach a kernel debugger, you can inspect the `uart_softc` directly and read its register values. This is invaluable when the chip is in a confused state that the software abstraction hides.

**Compare against a working driver.** If your modification broke something, bisect the change against the upstream `ns8250.c`. The difference will be small, and once you identify it, the fix is usually clear.

**Use `dd` for small, repeatable tests.** Instead of `cu` for debugging, use `dd if=/dev/zero of=/dev/cuau0 bs=1 count=100` to write exactly 100 bytes. Then `dd if=/dev/cuau0 of=output.bin bs=1 count=100` to read exactly 100 bytes (with a suitable timeout or a second open). This isolates timing and character-encoding issues that interactive `cu` might mask.

**Check the hardware flow control pins.** Many flow-control bugs are hardware, not software. Use a break-out board, a multimeter, or an oscilloscope to verify that DTR, RTS, CTS, and DSR are at the voltages you expect. If one is stuck floating, the chip's behaviour is undefined.

**Compare behaviour under `nmdm(4)`.** If your userland tool works with `nmdm(4)` but not with your driver, the bug is in the driver. If it fails with both, the bug is in the tool.

These techniques apply equally to `uart(4)` drivers and `ucom(4)` drivers. The difference is that `uart(4)` problems often come down to register manipulation (did you set the divisor correctly?), while `ucom(4)` problems often come down to USB transfers (did the control transfer to set the baud rate actually succeed?). The debugging tools (USB: `usbconfig`, transfer statistics; UART: `sysctl`, chip registers) are different, but the investigative mindset is the same.

### Writing a UART Driver Yourself

Putting the pieces together, a minimal UART driver for an imaginary register-compatible chip would be organised like this:

1. Define register offsets and bit positions in a local header.
2. Implement the six `uart_ops` methods: `probe`, `init`, `term`, `putc`, `rxready`, `getc`.
3. Declare a `struct uart_ops` initialised with those six methods.
4. Declare a `struct uart_class` initialised with the ops and the hardware parameters (range, reference clock, register shift).
5. Implement the interrupt handler if the chip needs more than the framework's default dispatch.
6. Register the driver with Newbus using the standard macros.

Most new UART drivers in the tree are small. Oxford single-port PCIe UARTs, for example, are a few hundred lines because they are fundamentally 16550-compatible and only need a thin layer of PCI-specific attach code. Complex ones like the Z8530 are larger because the chip has a more complicated programming model; the driver size tracks the chip's complexity, not the framework's.

### Looking at `myfirst_uart.c` in Skeleton Form

For our running example, the skeleton of a minimal UART driver looks like this:

```c
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/kernel.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>

#include "uart_if.h"

static int   myfirst_uart_probe(struct uart_bas *);
static void  myfirst_uart_init(struct uart_bas *, int, int, int, int);
static void  myfirst_uart_term(struct uart_bas *);
static void  myfirst_uart_putc(struct uart_bas *, int);
static int   myfirst_uart_rxready(struct uart_bas *);
static int   myfirst_uart_getc(struct uart_bas *, struct mtx *);

static struct uart_ops myfirst_uart_ops = {
    .probe   = myfirst_uart_probe,
    .init    = myfirst_uart_init,
    .term    = myfirst_uart_term,
    .putc    = myfirst_uart_putc,
    .rxready = myfirst_uart_rxready,
    .getc    = myfirst_uart_getc,
};

struct myfirst_uart_softc {
    struct uart_softc base;
    /* any chip-specific state would go here */
};

static kobj_method_t myfirst_uart_methods[] = {
    /* Most methods inherit from the framework. */
    { 0, 0 }
};

struct uart_class myfirst_uart_class = {
    "myfirst_uart class",
    myfirst_uart_methods,
    sizeof(struct myfirst_uart_softc),
    .uc_ops    = &myfirst_uart_ops,
    .uc_range  = 8,
    .uc_rclk   = 1843200,
    .uc_rshift = 0,
};
```

The inclusion of `uart_if.h` is notable: that header is generated at build time by the kobj machinery from the interface definition in `/usr/src/sys/dev/uart/uart_if.m`. It declares the method prototypes that the framework expects drivers to implement. When you write a new driver, you depend on this header.

The six methods themselves are straightforward once you have the chip's programming manual open. `init` computes the divisor from `uc_rclk` and the baud rate, writes the line control register for the databits/stopbits/parity combination, enables FIFOs, and sets the interrupt enable register to the desired mask. `term` inverts `init`. `putc`, `getc`, and `rxready` each do a single-register access plus a spin on the status register.

A complete implementation of all six methods for a 16550-compatible chip is about three hundred lines. For a chip with quirks, it might grow to five hundred or more. The `ns8250` driver is longer than most because it handles errata and variant detection for dozens of real chips, but the core logic of its six methods is still the standard pattern.

### The `ucom(4)` Framework: USB-to-Serial Bridges

Not every serial port is a real UART on the system bus. Many are USB adapters: a PL2303, a CP2102, an FTDI FT232, a CH340G. These chips expose a serial port over USB, and FreeBSD's approach to supporting them is a small framework called `ucom(4)`. It lives in `/usr/src/sys/dev/usb/serial/`, alongside the drivers for each chip family.

`ucom(4)` is distinct from `uart(4)`. It does not use `uart_ops`, it does not use `uart_bas`, and it does not use the ring buffers inside `uart_core.c`. What it does is provide a TTY abstraction on top of USB transfers. A `ucom(4)` client declares itself through a `struct ucom_callback`:

```c
struct ucom_callback {
    void (*ucom_cfg_get_status)(struct ucom_softc *, uint8_t *, uint8_t *);
    void (*ucom_cfg_set_dtr)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_rts)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_break)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_set_ring)(struct ucom_softc *, uint8_t);
    void (*ucom_cfg_param)(struct ucom_softc *, struct termios *);
    void (*ucom_cfg_open)(struct ucom_softc *);
    void (*ucom_cfg_close)(struct ucom_softc *);
    int  (*ucom_pre_open)(struct ucom_softc *);
    int  (*ucom_pre_param)(struct ucom_softc *, struct termios *);
    int  (*ucom_ioctl)(struct ucom_softc *, uint32_t, caddr_t, int,
                      struct thread *);
    void (*ucom_start_read)(struct ucom_softc *);
    void (*ucom_stop_read)(struct ucom_softc *);
    void (*ucom_start_write)(struct ucom_softc *);
    void (*ucom_stop_write)(struct ucom_softc *);
    void (*ucom_tty_name)(struct ucom_softc *, char *pbuf, uint16_t buflen,
                         uint16_t unit, uint16_t subunit);
    void (*ucom_poll)(struct ucom_softc *);
    void (*ucom_free)(struct ucom_softc *);
};
```

The methods divide into three groups. Configuration methods (names prefixed with `ucom_cfg_`) are called to change the state of the underlying chip: set DTR, set RTS, change the baud rate, and so on. These methods run in the framework's configuration thread, which is designed for making synchronous USB control requests. Flow methods (`ucom_start_read`, `ucom_start_write`, `ucom_stop_read`, `ucom_stop_write`) are called to enable or disable the data path on the underlying USB channels. The pre-methods (`ucom_pre_open`, `ucom_pre_param`) run on the caller's context before the framework schedules a configuration task, which is where a driver validates userland-supplied arguments and returns an errno if they are unacceptable. The `ucom_ioctl` method translates chip-specific userland ioctls that the framework does not handle into USB requests.

A USB-to-serial driver's job is to implement these callbacks in terms of USB transfers. When `ucom_cfg_param` is called with a new baud rate, the driver issues a vendor-specific control transfer that programs the chip's baud-rate register. When `ucom_start_read` is called, the driver starts a bulk-IN channel that delivers incoming bytes. When `ucom_start_write` is called, the driver starts a bulk-OUT channel that flushes outgoing bytes.

The FTDI driver at `/usr/src/sys/dev/usb/serial/uftdi.c` is the concrete reference. Its `ucom_cfg_param` implementation translates the termios fields into the FTDI's proprietary baud-rate divisor format (which is weird, because FTDI chips use a sub-integer divisor scheme that is almost but not quite standard 16550) and issues a control transfer to `bRequest = FTDI_SIO_SET_BAUD_RATE`. Its `ucom_start_read` starts the bulk-IN channel that reads from the FTDI's RX FIFO. Its `ucom_start_write` starts the bulk-OUT channel that writes to the FTDI's TX FIFO.

From userland's perspective, a `ucom(4)` device looks identical to a `uart(4)` device. Both appear as `/dev/ttyuN` and `/dev/cuauN`. Both respond to `stty`, `cu`, `tip`, `minicom`, and every other serial tool. Both support the same termios flags. The distinction only matters to a driver writer.

### Reading `uftdi.c` As a Complete Example

FTDI chips (FT232R, FT232H, FT2232H, and many others) are the most widely deployed USB-to-serial chips in the embedded world. If you ever work with microcontrollers, evaluation boards, 3D printers, or industrial sensors, you will encounter FTDI hardware. FreeBSD has supported FTDI since 4.x, and the current driver lives in `/usr/src/sys/dev/usb/serial/uftdi.c`. At roughly three thousand lines, it is not short, but most of that length is devoted to the large match table (FTDI products are legion) and to chip-variant quirks (every few years FTDI adds a new FIFO size, a new baud-rate divisor scheme, or a new register). The pedagogically interesting core is a few hundred lines, and reading it is a direct reward for the conceptual work of Section 4.

When you open the file, the first thing to notice is the enormous match table. FTDI assigns OEM-specific USB IDs to their customers, so the match table includes not just FTDI's own VID/PID pairs but also hundreds of VIDs and PIDs from companies that embed FTDI chips in their products. Sparkfun, Pololu, Olimex, Adafruit, various industrial vendors: every one has at least one entry in the uftdi match table. The `STRUCT_USB_HOST_ID` array is a few hundred entries long, grouped with comments indicating which product family each cluster belongs to.

The softc comes next. An FTDI softc includes the USB device pointer, a mutex, the transfer array for the bulk-IN and bulk-OUT channels (FTDI devices use bulk for data, not interrupt), a `struct ucom_super_softc` for the `ucom(4)` layer, a `struct ucom_softc` for the per-port state, and FTDI-specific fields: the current baud-rate divisor, the current line control register contents, the current modem control register contents, and a few flags for the variant family (FT232, FT2232, FT232H, and so on). Each variant requires slightly different code for some operations, so the driver keeps a variant identifier in the softc and branches on it in the operations that differ.

The transfer configuration array is where the FTDI driver's interaction with the USB framework is declared. It declares two channels: `UFTDI_BULK_DT_RD` for incoming data and `UFTDI_BULK_DT_WR` for outgoing. Each is a `UE_BULK` transfer with a moderate buffer size (the FTDI default is 64 bytes for low-speed and 512 bytes for full-speed, and the driver picks the right size at attach based on the chip variant). The callbacks are `uftdi_read_callback` and `uftdi_write_callback`, and they follow the three-state pattern exactly as described in Section 3.

The `ucom_callback` structure is the next important block. It wires the FTDI driver into the `ucom(4)` framework. The methods it provides include `uftdi_cfg_param` (called when the baud rate or byte format changes), `uftdi_cfg_set_dtr` (called to assert or deassert DTR), `uftdi_cfg_set_rts` (same for RTS), `uftdi_cfg_open` and `uftdi_cfg_close` (called when a userland process opens or closes the device), and `uftdi_start_read`, `uftdi_start_write`, `uftdi_stop_read`, `uftdi_stop_write` (called to enable or disable the data channels). Each configuration method translates a high-level operation into a USB control transfer to the FTDI chip.

The baud-rate programming is one of the most instructive parts of the driver, because FTDI chips use a peculiar divisor scheme. Rather than the clean integer divisors a 16550 UART uses, FTDI supports a fractional divisor where the numerator is an integer and the denominator is computed from two bits that select one-eighth, one-quarter, three-eighths, one-half, or five-eighths. The function `uftdi_encode_baudrate` takes a requested baud rate and the chip's reference clock and computes the closest valid divisor. It handles the edge cases (very low baud rates, very high baud rates on newer chips, standard rates like 115200 which are exactly representable, nonstandard rates like 31250 used by MIDI). The resulting sixteen-bit value is passed to `uftdi_set_baudrate`, which issues a control transfer to the FTDI's baud-rate register.

The line control register (data bits, stop bits, parity) is programmed through a similar sequence: the termios structure arrives at `uftdi_cfg_param`, the driver extracts the relevant bits, encodes them into the FTDI's line-control format, and issues a control transfer.

The modem control signals (DTR, RTS) are programmed through `uftdi_cfg_set_dtr` and `uftdi_cfg_set_rts`. These are the simplest transfers: a control-out with no payload, which the chip interprets as "set DTR to X" or "set RTS to Y."

The data path is in the two callbacks. `uftdi_read_callback` handles the bulk-IN channel. On `USB_ST_TRANSFERRED`, it extracts the received bytes from the USB frame (ignoring the first two bytes, which are FTDI status bytes) and feeds them into the `ucom(4)` layer for delivery to userland. On `USB_ST_SETUP`, it rearms the read for another buffer. `uftdi_write_callback` handles the bulk-OUT channel. On `USB_ST_SETUP`, it asks the `ucom(4)` layer for more data, copies it into a USB frame, and submits the transfer. On `USB_ST_TRANSFERRED`, it rearms to check for more data.

Reading through `uftdi.c` with Section 4 vocabulary in hand, you can see how the entire `ucom(4)` framework pattern is instantiated for a specific chip. The FTDI-specific logic (baud-rate encoding, line-control encoding, modem-control setting) is isolated into helper functions. The framework integration is handled by the `ucom_callback` structure. The data flow is handled by the two bulk transfers. If you were writing a driver for a different USB-to-serial chip, you would copy this structure and change the chip-specific parts.

The existence of this driver explains something that might otherwise be puzzling. Why did FreeBSD add `ucom(4)` as a separate framework rather than as part of `uart(4)`? Because the entire data-path machinery of a `uart(4)` driver (interrupt handlers, ring buffers, register accesses) has no analogue in a USB-to-serial world. The FTDI chip's "FIFO" is an on-chip buffer that the driver cannot directly access; it can only send bulk packets to the chip and receive them back. The `uart(4)` machinery would be unused overhead. By having `ucom(4)` as a separate framework with its own data-path abstractions, FreeBSD can make a USB-to-serial driver like `uftdi` weigh just a few hundred lines of core logic rather than wrap an unnecessary layer of 16550 emulation.

When you finish reading `uftdi.c`, open `uplcom.c` (the Prolific PL2303 driver) and `uslcom.c` (the Silicon Labs CP210x driver) in sequence. They follow the same structure with different chip-specific details. After reading all three, you will have a working understanding of how a USB-to-serial driver is organised in FreeBSD, and you will be ready to write one for any chip you encounter.

### Choosing Between `uart(4)` and `ucom(4)`

The choice is mechanical. If the chip sits on the system bus (PCI, ISA, a platform I/O port, a memory-mapped SoC peripheral), you write a `uart(4)` driver. If the chip sits on USB and exposes a serial interface, you write a `ucom(4)` driver.

The two frameworks do not mix. You cannot take a `uart(4)` driver and plug it into USB, and you cannot take a `ucom(4)` driver and attach it to PCIe. They are independent implementations of the same user-visible abstraction (a TTY port), but with very different internals.

Beginners sometimes ask why the two frameworks exist at all, instead of a unified serial framework with a pluggable transport layer. The answer is historical: `uart(4)` was rewritten in its modern form in the early 2000s to replace the older `sio(4)` driver, and at that time USB serial support was a set of ad-hoc drivers. When USB serial support was unified, the natural approach was to add a thin TTY-integration layer (`ucom(4)`) rather than retrofit `uart(4)`. The two are now independent because decoupling them has been stable and useful. A unification effort would be a significant project with modest payoff.

For your purposes as a beginning driver writer, the rule is simple. If you are writing a driver for a chip that lives on your motherboard's serial ports or on a PCIe card, use `uart(4)`. If you are writing a driver for a USB dongle that pretends to be a serial port, use `ucom(4)`. The reference drivers for each case (`ns8250` for `uart(4)`, `uftdi` for `ucom(4)`) are the right places to learn the details.

### Differences Between Chip Variants

Working with real UART hardware quickly teaches you that "16550-compatible" is a spectrum, not a fixed specification. Here are the variants you are most likely to encounter and the differences that matter.

**8250.** The original, from the late 1970s. Has no FIFO; every received byte must be collected by the CPU before the next arrives. Software for 16550A will usually work, with reduced performance.

**16450.** Like 8250 but with some register improvements and slightly more reliable behaviour. Still no FIFO.

**16550.** Introduced a 16-byte FIFO, but the original 16550 had buggy FIFO behaviour. Software should detect this and refuse to use the FIFO in the bad case.

**16550A.** Fixed the FIFO bugs. This is the canonical "16550" that every PC serial driver targets. Reliable, widely compatible.

**16550AF.** Further revisions for clocking and margin. For software purposes, identical to 16550A.

**16650.** Extended the FIFO to 32 bytes and added automatic hardware flow control. Mostly 16550A-compatible.

**16750.** Extended the FIFO to 64 bytes. Some chips with this label also have additional autobaud and high-speed modes. Software must decide whether to enable the extended FIFO.

**16950 (Oxford Semiconductor).** A 128-byte FIFO, additional flow-control features, and support for unusual baud rates through a modified divisor scheme. Often seen on high-performance PCIe serial cards.

**UART-compatible SoC controllers.** Many embedded processors have built-in UARTs that are register-compatible with 16550 but with quirks: some have different clock rates, some have different register offsets, some have DMA support, some have different interrupt semantics. The `ns8250` driver in FreeBSD probes for these variants during attach and adjusts its behaviour accordingly.

The `ns8250` driver's probe logic reads several registers to determine which variant is present. It checks the IIR bits we saw earlier, reads the FIFO control register to see what FIFO size is reported, checks for 16650/16750/16950 identification markers, and records the result in a variant field in the softc. The body of the driver then branches on this field at a few places where the variants differ.

When you write a driver for a new UART, decide upfront whether you want to target a single variant or a family. Targeting a single variant is simpler but limits the hardware you can support. Targeting a family requires variant detection logic like `ns8250`'s.

### The Console Path

FreeBSD can use a serial port as the console. This is especially useful for embedded systems that do not have a display, for servers that do not have a keyboard and monitor, and for kernel debugging (so that `printf` output goes somewhere visible even when the display driver is broken).

The console path is tightly integrated with `uart(4)`. A UART that is designated as the console is probed early in boot, before most of the kernel is initialised. The console's putc and getc methods are used to emit boot messages and to read boot-time keyboard input. Only after the full kernel is up does the UART get attached to the TTY layer in the normal way.

Two mechanisms select which port is the console. The boot loader can set a variable (typically `console=comconsole`) in the environment, which the kernel reads at startup. Alternatively, the kernel can be configured at build time with a specific port as the console (via `options UART_EARLY_CONSOLE` in a kernel configuration file).

When a port is the console, it stays active across driver unload and detach. You cannot unload `uart` or disable the console port without losing console output. This constraint is enforced in the `uart(4)` framework and is usually invisible to driver writers (you do not need to special-case the console port), but it is worth knowing about in case you see console-related oddities during testing.

### Comparing UART Drivers Across Architectures

One of FreeBSD's strengths is that the same `uart(4)` framework works across multiple architectures. An `x86_64` laptop with a 16550 on a PCIe card, an `aarch64` Raspberry Pi with a PL011 on-chip UART, and a `riscv64` development board with a SiFive-specific UART all expose the same TTY interface to userland. Only the driver differs.

Here is a quick survey of the UART drivers in FreeBSD 14.3:

- `uart_dev_ns8250.c`: the 16550 family for x86 and many other platforms.
- `uart_dev_pl011.c`: the ARM PrimeCell PL011 UART, used on Raspberry Pi and many ARM SoCs.
- `uart_dev_imx.c`: the NXP i.MX UART, used on i.MX-based ARM boards.
- `uart_dev_z8530.c`: the Zilog Z8530, historically used on SPARC workstations.
- `uart_dev_ti8250.c`: a TI variant of the 16550 with additional features.
- `uart_dev_pl011.c` (sbsa variant): the SBSA-standardised ARM UART for server-class ARM hardware.
- `uart_dev_snps.c`: the Synopsys DesignWare UART, used on many RISC-V boards.

Open any two of these and compare their `uart_ops` implementations side by side. The structure is identical: six methods, each pointing at a function that reads or writes chip-specific registers. The chip-specific details differ, but the framework's API is the same.

This is the payoff of the layered design. A new UART driver is a contained project: a few hundred lines of code, reusing all the buffering and TTY integration from the framework. If FreeBSD had to reimplement buffering for every UART, the system would be much larger and much harder to verify.

### What About the USB CDC ACM Standard?

USB has a standard class for serial devices, called CDC ACM (Communication Device Class, Abstract Control Model). Chips that implement CDC ACM advertise themselves with a specific class/subclass/protocol triple at the interface level, and they can be driven by a single generic driver rather than a vendor-specific one. FreeBSD's generic CDC ACM driver is `u3g.c` in `/usr/src/sys/dev/usb/serial/`, and it is also built on top of `ucom(4)`.

Many modern USB serial chips implement CDC ACM, so the generic driver just works for them without a vendor-specific file. Others (like FTDI) use proprietary protocols that require a vendor-specific driver. The class/subclass/protocol triple in the interface descriptor is what tells you which case you are in; `usbconfig -d ugenN.M dump_all_config_desc` will show it.

When you are shopping for a USB serial adapter for development work, prefer chips that implement CDC ACM. They are cheaper, more portable, and do not require proprietary drivers. FTDI chips are historically dominant in embedded development because of their reliability, and FreeBSD supports them well, but a modern CP2102 or CH340G running in CDC ACM mode is equally usable.

### Wrapping Up Section 4

Section 4 has given you a complete picture of how serial drivers work in FreeBSD. You have seen the layering: `uart(4)` at the framework level, `ttydevsw` at the TTY integration level, `uart_ops` at the hardware level. You have seen the distinction between `uart(4)` for bus-attached UARTs and `ucom(4)` for USB-to-serial bridges, and the practical rule for deciding which to use. You have seen, at a high level, the six hardware methods a UART driver implements, the configuration callbacks a USB-to-serial driver implements, and how the TTY layer sits on top of both with one uniform interface to userland.

The level of depth in this section is necessarily lighter than the USB-side sections, because serial drivers in FreeBSD are more specialised than USB drivers and you are more likely to read an existing one (or modify one) than to write a brand-new one from scratch. If you do find yourself writing a new UART driver for a custom board, the path is clear: open `ns8250` in one window, open your chip's data sheet in another, and write the six methods one by one.

Two key takeaways frame Section 5. First, testing serial drivers does not require real hardware. FreeBSD ships a `nmdm(4)` null-modem driver that creates pairs of virtual TTYs you can wire together, letting you exercise termios changes, flow control, and data flow without plugging in anything. Second, testing USB drivers without hardware is harder but not impossible: you can use QEMU with USB redirection to test against real devices through a VM, or you can use FreeBSD's USB gadget mode to make one machine present itself as a USB device to another. Section 5 covers both. The goal is to enable a development loop that does not depend on cable-handling and on plugging things in and out.

## Section 5: Testing USB and Serial Drivers Without Real Hardware

### Why This Section Exists

A beginning driver writer often gets stuck at the same obstacle. They write a driver, compile it, want to try it, and discover they do not have the hardware, the hardware is behaving badly, the hardware is on the wrong machine, or the iteration loop of "change code, plug it in, see what happens, unplug it, change code again" is painfully slow and unreliable. Section 5 addresses this directly. FreeBSD provides several mechanisms that let you exercise driver code paths without physical hardware, and knowing these mechanisms will save you hours of frustration.

The goal is not to pretend hardware is present when it is not. The goal is to give you tools that cover the parts of driver development where hardware is incidental, so that when you do plug in real hardware, you already know your code path logic is correct and you are only validating the physical interaction. Debugging a register-level quirk is faster when you know that your locking, your state machines, and your user interface are already sound.

This section covers four such mechanisms: the `nmdm(4)` null-modem driver for serial testing, basic userland tools for exercising TTYs (`cu`, `tip`, `stty`, `comcontrol`), QEMU with USB redirection for USB driver testing, and FreeBSD's USB gadget mode for presenting one machine as a USB device to another. It ends with a short discussion of techniques that do not require any special tooling: unit tests at the functional layer, logging discipline, and assertion-driven development.

### The `nmdm(4)` Null-Modem Driver

`nmdm(4)` is a kernel module that creates pairs of linked virtual TTYs. When you write to one side, it comes out the other side, exactly as if you had connected two real serial ports with a null-modem cable. The driver is in `/usr/src/sys/dev/nmdm/nmdm.c`, and it is loaded with:

```console
# kldload nmdm
```

Once loaded, you can instantiate pairs on demand simply by opening them. Run:

```console
# cu -l /dev/nmdm0A
```

This opens the `A` side of pair `0`. On another terminal, run:

```console
# cu -l /dev/nmdm0B
```

Whatever you type into one `cu` session will appear in the other. You have now created a pair of virtual TTYs, with no hardware involved. You can change baud rates with `stty` and the change will be noticed on both sides. You can assert DTR and CTS through ioctls and see the effect on the other side.

The utility of `nmdm(4)` for driver development is twofold. First, if you are writing a TTY-layer user (say, a driver that spawns a shell on a virtual TTY, or a userland program that implements a protocol over a TTY), you can test it end-to-end against `nmdm(4)` without any hardware. Second, if you are writing a `ucom(4)` or `uart(4)` driver, you can compare its behaviour to `nmdm(4)`'s behaviour by running the same userland test against both. If your driver misbehaves where `nmdm(4)` does not, the bug is in your driver; if both misbehave, the bug is probably in your userland test.

A small caveat: `nmdm(4)` does not simulate baud rate delays. Whatever you write comes out the other side at memory speed. This is usually what you want (you do not want to wait through a real 9600-baud transmission for a hundred-kilobyte test payload), but it does mean that timing-sensitive protocols cannot be tested with `nmdm(4)` alone.

### The `cu(1)`, `tip(1)`, and `stty(1)` Toolbox

Whether you are using `nmdm(4)`, a real UART, or a USB-to-serial dongle, the userland tools you use to interact with a TTY are the same. The most important three are `cu(1)`, `tip(1)`, and `stty(1)`.

`cu` is the classic "call up" program. It opens a TTY, puts the terminal into raw mode, and lets you type bytes to the port and see bytes coming back. To open a port at a specific baud rate:

```console
# cu -l /dev/cuau0 -s 115200
```

The `-l` argument specifies the device, and `-s` specifies the baud rate. `cu` supports a handful of escape sequences (all starting with `~`) for exiting, sending files, and similar operations; `~.` is the standard "exit" escape and `~?` lists the others.

`tip` is a related tool with similar semantics but a different configuration mechanism. `tip` reads `/etc/remote` for named connection entries and can take a name argument rather than a device path. For most purposes, `cu` and `tip` are interchangeable; `cu` is more convenient for one-off use.

`stty` prints or changes the termios parameters of a TTY. Run `stty -a -f /dev/ttyu0` to see every termios flag on the port. Run `stty 115200 -f /dev/ttyu0` to set the baud rate. Run `stty cs8 -parenb -cstopb -f /dev/ttyu0` to set eight data bits, no parity, one stop bit (the most common configuration in modern embedded work). The manual page is extensive, and the flags map almost directly onto the bits of `c_cflag`, `c_iflag`, `c_lflag`, and `c_oflag` in the `termios` struct.

Using these three tools together gives you a flexible way to poke at your driver from userland. You can change settings with `stty`, open the port with `cu`, send and receive bytes, close the port, check the state with `stty` again, and repeat. If your driver's `tsw_param` implementation has a bug, `stty` will expose it: the settings you set will not read back correctly, or the port will behave differently than requested.

### The `comcontrol(8)` Utility

`comcontrol` is a specialised utility for serial ports. It sets port-specific parameters that are not exposed through termios. The two most important are the `drainwait` and the specific-RS-485 options. For beginner driver testing, the more common use is inspecting port state: `comcontrol /dev/ttyu0` shows the current modem signals (DTR, RTS, CTS, DSR, CD, RI) and the current `drainwait`. You can also set the signals:

```console
# comcontrol /dev/ttyu0 dtr rts
```

sets DTR and RTS. This is useful for testing flow-control handling without writing a custom program.

### The `usbconfig(8)` Utility

On the USB side, `usbconfig(8)` is the Swiss Army knife. You used it at the end of Section 1 to inspect a device's descriptors. Several other subcommands are useful during driver development:

- `usbconfig list`: list all attached USB devices.
- `usbconfig -d ugenN.M dump_all_config_desc`: print every descriptor for a device.
- `usbconfig -d ugenN.M dump_device_quirks`: print any quirks applied by the USB framework.
- `usbconfig -d ugenN.M dump_stats`: print per-transfer statistics.
- `usbconfig -d ugenN.M suspend`: put the device into the USB suspend state.
- `usbconfig -d ugenN.M resume`: wake it up.
- `usbconfig -d ugenN.M reset`: physically reset the device.

The `reset` command is particularly useful during development. A driver under test can easily leave a device in a confused state; `usbconfig reset` puts the device back to the just-plugged-in condition without requiring a physical unplug.

### Testing USB Drivers with QEMU

QEMU, the generic CPU emulator, has strong USB support. You can run a FreeBSD guest inside QEMU and redirect real host USB devices into the guest. This is the single most useful technique for USB driver development, because it lets you test against real hardware while retaining all the iteration speed of working inside a VM.

On a FreeBSD host, install QEMU from ports:

```console
# pkg install qemu
```

Install a FreeBSD guest image into a disk file (the mechanics are covered in Chapter 4 and Appendix A). When you boot the guest, add USB redirection options:

```console
qemu-system-x86_64 \
  -drive file=freebsd.img,format=raw \
  -m 1024 \
  -device nec-usb-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=0x0403,productid=0x6001
```

The `-device nec-usb-xhci` line adds a USB 3.0 controller to the guest. The `-device usb-host` line attaches a specific USB device from the host (identified by vendor and product) to that controller. When the guest boots, the device appears on the guest's USB bus and can be enumerated by the guest's kernel.

This setup gives you the full iteration loop inside the VM. You can load your driver, unload it, reload a rebuilt version, all without physically handling any cables. You can use serial console or networking to interact with the VM. You can snapshot the VM state before a risky test and revert if the test panics.

The main limitation is USB isochronous support, which is less stable across emulators. For bulk, interrupt, and control transfers (the three types most drivers use), QEMU USB redirection is reliable enough to be your primary development environment.

### FreeBSD USB Gadget Mode

If QEMU is not available and you have two FreeBSD machines, there is another option: `usb_template(4)` and the dual-role USB support on some hardware let you make one machine present itself as a USB device to another. The host machine sees a normal USB peripheral; the gadget machine is actually running the device side of the USB protocol.

This is an advanced topic and the hardware support is variable. On x86 platforms with USB-on-the-Go-capable chipsets, on some ARM boards, and on specific embedded configurations, the setup works. On most desktop hardware, it does not. The gory details are in `/usr/src/sys/dev/usb/template/` and in the `usb_template(4)` manual page.

If you have the hardware to use this technique, it is the closest thing to a full end-to-end USB driver test without physical peripherals. If you do not, do not pursue it for a learning project; use QEMU instead.

### Techniques That Do Not Require Special Tooling

Beyond the frameworks above, there are several techniques that rely only on good driver design.

First, design your driver so that the hardware-independent parts can be unit-tested in userland. If your driver has a protocol parser, a state machine, or a checksum calculator, factor those into functions that take plain C buffers and return plain C results. You can then compile those functions into a userland test program and run them against known inputs. This catches many bugs before they reach the kernel.

Second, log aggressively during development and quietly in production. The `DLOG_RL` macro from Chapter 25 is your friend: it lets you emit frequent diagnostic messages during development, with a sysctl to suppress them in production. Rate-limiting prevents log storms if something goes wrong.

Third, use assertions for invariants. `KASSERT(cond, ("message", args...))` will panic the kernel if `cond` is false, but only in `INVARIANTS` kernels. You can run your driver in an `INVARIANTS` kernel during development and in a production kernel later, without changing the code. The Chapter 20 discussion of `INVARIANTS` is the reference.

Fourth, be rigorous about concurrency testing. Use `INVARIANTS` plus `WITNESS` (which tracks lock ordering) during development. If your driver has a locking bug that almost always works but occasionally deadlocks, `WITNESS` will catch it on the first occurrence.

Fifth, write a simple userland client for your driver and use it as part of your development loop. Even a ten-line program that opens the device, writes a known string, reads a known response, and checks the result is enormously useful. You can run it in a loop during stress testing, you can run it with `ktrace -f cmd` to get a trace of system calls, and you can run it under a debugger if something surprises you.

### A Walkthrough of QEMU USB Redirection

QEMU's USB support is the single most useful tool for USB driver development, so a more detailed walkthrough is in order. Suppose you want to develop a driver for a specific FT232 adapter. Your host is a FreeBSD 14.3 machine, and you want to run your driver on a guest FreeBSD 14.3 VM inside QEMU.

First, install QEMU and create a guest disk image:

```console
# pkg install qemu
# truncate -s 16G guest.img
```

Install FreeBSD into the image. The exact procedure is covered in Appendix A, but the short version is: boot a FreeBSD installer ISO as the CD-ROM, install onto the disk image, reboot.

Once the guest is installed, locate the host USB device you want to redirect. Plug in the FT232 and note the vendor and product IDs from `usbconfig list`:

```text
ugen0.3: <FTDI FT232R USB UART> at usbus0
```

`usbconfig -d ugen0.3 dump_device_desc` will show `idVendor = 0x0403` and `idProduct = 0x6001`.

Now start QEMU with USB redirection:

```console
qemu-system-x86_64 \
  -enable-kvm \
  -cpu host \
  -m 2048 \
  -drive file=guest.img,format=raw \
  -device nec-usb-xhci,id=xhci \
  -device usb-host,bus=xhci.0,vendorid=0x0403,productid=0x6001 \
  -net user -net nic
```

The `-device nec-usb-xhci` line adds a USB 3.0 controller to the VM. The `-device usb-host` line redirects the matching host device into the VM. When the VM boots, the FT232 will appear as if it were plugged directly into the VM's USB port.

Inside the VM, run `dmesg` and look for the USB attach:

```text
uhub0: 4 ports with 4 removable, self powered
uftdi0 on usbus0
uftdi0: <FTDI FT232R USB UART, class 255/0, rev 2.00/6.00, addr 2> on usbus0
```

Your driver (whether `uftdi` or your own work-in-progress) will see a real FT232 with real descriptors, real transfer behaviour, and real quirks. You can unload and reload your driver inside the VM without disconnecting anything; you can run kernel with `INVARIANTS` and `WITNESS` without worrying about host-side impact; you can snapshot the VM and revert if a test goes badly.

A few subtleties to be aware of with USB redirection:

- Only one consumer can claim a USB device at a time. If you redirect a device into a VM, the host loses access to it until the VM releases it. This matters if you are redirecting something like a USB keyboard or mouse; choose a spare device for development.

- USB isochronous transfers have some quirks in QEMU. They work, but timing can be slightly off. For most driver development, you will be working with bulk, interrupt, and control transfers, so this is rarely a concern.

- Some host controllers (particularly xHCI) can reset under heavy I/O. If your driver behaves strangely under stress testing, try with a different `-device` type (uhci, ehci, xhci) to see whether the issue is in your driver or in the emulated controller.

- USB 3.0 SuperSpeed transfers are more reliable with `-device nec-usb-xhci`. Older `-usb` flag-based controllers are limited to USB 2.0.

When the VM is running, the iteration cycle becomes: edit code on the host, copy to the VM (or mount a shared directory), build inside the VM, load, test, reload, repeat. A Makefile with a `test:` target that does all of this can cut iteration time to tens of seconds.

### Using `devd(8)` During Development

`devd(8)` is FreeBSD's device-event daemon. It reacts to kernel notifications about device attach and detach and can run configured commands in response. During driver development, `devd` is useful in two ways.

First, it can auto-load your module when a matching device is plugged in. If your module is in `/boot/modules/` and your `USB_PNP_HOST_INFO` is set, `devd` will run `kldload` automatically when it sees a device that would match.

Second, it can run diagnostic commands on attach. A `/etc/devd.conf` entry like:

```text
attach 100 {
    device-name "myfirst_usb[0-9]+";
    action "logger -t myfirst-usb 'device attached: $device-name'";
};
```

will write a log line every time a `myfirst_usb` device attaches. For more elaborate diagnostics, you can invoke your own shell script that dumps state, starts userland consumers, or sends notifications.

During development, a useful pattern is to have `devd` open a `cu` session to a newly attached `ucom` device, so you can exercise the driver the moment it attaches:

```text
attach 100 {
    device-name "cuaU[0-9]+";
    action "setsid screen -dmS usb-serial cu -l /dev/$device-name -s 9600";
};
```

This runs the test in a detached `screen` session, which you can later attach to with `screen -r usb-serial`.

### Writing a Simple Userland Test Harness

Most driver bugs are exposed by actually running the driver against userland. Even a short test program catches more bugs than reading the driver's code carefully. For our echo driver, a minimal test program looks like:

```c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    int fd;
    const char *msg = "hello";
    char buf[64];
    int n;

    fd = open("/dev/myfirst_usb0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return (1);
    }

    if (write(fd, msg, strlen(msg)) != (ssize_t)strlen(msg)) {
        perror("write");
        close(fd);
        return (1);
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return (1);
    }
    buf[n] = '\0';
    printf("got %d bytes: %s\n", n, buf);

    close(fd);
    return (0);
}
```

Compile with `cc -o lab03-test lab03-test.c`. Run with `./lab03-test`. The expected output is "got 5 bytes: hello".

Extensions to this test harness that catch more bugs:

- Loop the open/write/read/close cycle a thousand times. Memory leaks and resource leaks show up after a few hundred iterations.
- Fork multiple processes and have them all read/write concurrently. Race conditions manifest as random data corruption or deadlocks.
- Intentionally kill the test process mid-transfer. Driver-side state machines sometimes get confused when a userland consumer disappears unexpectedly.
- Send random-length writes (1 byte, 10 bytes, 100 bytes, 1 KB, 10 KB). Edge cases around short and long transfers are where many subtle bugs live.

Build these extensions incrementally. Each one will probably reveal a bug the previous version did not; each bug you fix will make your driver more robust.

### Logging Patterns for Development

During development, you want verbose logging. In production, you want silence. The pattern from Chapter 25 (`DLOG_RL` with a sysctl to control verbosity) carries over unchanged to USB and UART drivers. Define a rate-limited logging macro that compiles to a no-op in production builds, and sprinkle it through every branch that might be interesting during debugging:

```c
#ifdef MYFIRST_USB_DEBUG
#define DLOG(sc, fmt, ...) \
    do { \
        if (myfirst_usb_debug) \
            device_printf((sc)->sc_dev, fmt "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define DLOG(sc, fmt, ...) ((void)0)
#endif
```

Then in the callback:

```c
case USB_ST_TRANSFERRED:
    DLOG(sc, "bulk read completed, actlen=%d", actlen);
    ...
```

Control `myfirst_usb_debug` through a sysctl:

```c
static int myfirst_usb_debug = 0;
SYSCTL_INT(_hw_myfirst_usb, OID_AUTO, debug, CTLFLAG_RWTUN,
    &myfirst_usb_debug, 0, "Enable debug logging");
```

Now you can turn logging on and off at runtime with `sysctl hw.myfirst_usb.debug=1`. During development, turn it on. During stress tests, turn it off (logging rate-limiting helps, but zero logging is even cheaper). During post-mortem analysis of a bug, turn it on and reproduce.

### A Test-Driven Workflow for Chapter 26

For the hands-on labs coming in the next section, a good workflow looks like this:

1. Write the driver code.
2. Compile it. Fix build errors.
3. Load it in a test VM. Observe `dmesg` for attach failures.
4. Run a small userland client that exercises the driver's I/O paths.
5. Unload. Make a change. Go back to step 2.
6. Once the driver behaves well in the VM, test it on real hardware as a sanity check.

Most of the time spent on this loop is in steps 1 through 4. Real hardware testing in step 6 is a validation step, not an iteration step. If you try to iterate on real hardware, you will waste time on plug-unplug cycles and on recovering from accidental misconfigurations; the VM saves you this.

A fresh install of FreeBSD in a small VM, configured to boot quickly and to have your driver's build directory mounted as a shared filesystem, is a highly productive development environment. Spending half a day to set one up pays back many times over in the days that follow.

### Wrapping Up Section 5

Section 5 has given you the tools to develop USB and serial drivers without being tied to specific physical hardware. `nmdm(4)` covers the serial-port side for any test that does not need a real modem. QEMU USB redirection covers the USB side for nearly any driver you might write. The `cu`, `tip`, `stty`, `comcontrol`, and `usbconfig` utilities give you the userland tools to exercise driver code paths by hand. And the general techniques, from factoring hardware-independent code into userland-testable functions to using `INVARIANTS` and `WITNESS` for kernel-time correctness checking, work regardless of what transport you are writing for.

Having reached the end of Section 5, you have everything you need to start writing real USB and serial drivers for FreeBSD 14.3. The conceptual models, the code skeletons, the transfer mechanics, the TTY integration, and the testing environment are all in place. What remains is practice, which is the purpose of the next section.

## Common Patterns Across Transport Drivers

Now that we have walked through USB and serial in detail, it is worth stepping back and noting the patterns that recur. These patterns appear in network drivers (Chapter 28), block drivers (Chapter 27), and most other transport-specific drivers in FreeBSD. Recognising them saves time when you read a new driver.

### Pattern 1: Match Table, Probe, Attach, Detach

Every transport driver begins with a match table describing which devices it supports. Every transport driver has a probe method that tests a candidate against the match table and returns zero or `ENXIO`. Every transport driver has an attach method that takes ownership of a matched device and allocates all per-device state. Every transport driver has a detach method that releases everything the attach method allocated, in reverse order.

The specifics vary. USB match tables use `STRUCT_USB_HOST_ID`. PCI match tables use `pcidev(9)` entries. ISA match tables use resource descriptions. The content differs, but the structure is identical.

When you read a new driver, the first thing to find is the match table. It tells you what hardware the driver supports. The second thing to find is the attach method. It tells you what resources the driver owns. The third thing to find is the detach method. It tells you the shape of the driver's resource hierarchy.

### Pattern 2: Softc As The Single Source of Per-Device State

Every transport driver has a per-device softc. Every piece of mutable state lives in the softc. No global variables are used to hold per-device state (global configuration like module flags is fine). This pattern keeps multi-device drivers correct without surprise.

The softc's size is declared in the driver structure. The framework allocates and frees the softc automatically. The driver accesses it through `device_get_softc(dev)` inside Newbus methods and through whatever framework helper (like `usbd_xfer_softc`) is appropriate in callbacks.

Adding a new feature to a driver often means adding a new field to the softc, a new initialisation step in attach, a new cleanup step in detach, and whatever code uses the field in between. When you structure changes this way, you rarely forget to clean things up, because the shape of the change makes the cleanup step obvious.

### Pattern 3: Labelled-Goto Cleanup Chain

When an attach method has to allocate several resources, each allocation has a failure path that unwinds all previous allocations. The labelled-goto chain from Chapter 25 implements this uniformly. Every resource has a label corresponding to "the state where this resource has been successfully allocated." A failure at any point jumps to the label for the state just before, which cleans up in reverse order.

This pattern is not aesthetically pleasing to some programmers (C's `goto` has a bad reputation), but it is pragmatically the cleanest way to handle an arbitrary number of cleanup steps in C. Alternatives like wrapping each resource in a separate function with its own cleanup are often more verbose. Alternatives like setting a flag per resource and testing it in a common cleanup routine add error-prone state management.

Whatever you think of `goto`, FreeBSD drivers use the labelled-goto pattern, and new drivers are expected to follow the convention.

### Pattern 4: Frameworks Hide Transport Details

Each transport has a framework that hides transport-specific details behind a uniform API. The USB framework hides DMA buffer management behind `usb_page_cache` and `usbd_copy_in/out`. The UART framework hides interrupt dispatching behind `uart_ops`. The network framework (Chapter 28) will hide packet buffer management behind mbufs and `ifnet(9)`.

The value of these frameworks is that drivers become smaller and more portable. A 200-line UART driver that supports dozens of chip variants would be impossible without the framework. A 500-line USB driver that supports a complex protocol like USB audio would likewise be out of reach.

When you read a new driver, the parts you find most dense are usually the chip-specific logic. The parts that seem almost absent (the transfer scheduling, the buffer management, the TTY integration) are where the framework is doing its work.

### Pattern 5: Callbacks with State Machines

The USB callback's three-state machine (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`) is the canonical example, but similar patterns appear in other transport drivers. A network driver's transmit completion callback has a similar structure. A block driver's request completion callback is similar. The framework calls the driver back at well-defined moments, and the driver uses a state machine to decide what to do.

Learning to read these state machines is learning a universal driver-reading skill. The specific states differ from framework to framework, but the pattern is recognisable.

### Pattern 6: Mutexes and Wakeups

Every driver protects its softc with a mutex. Userland-facing code (read, write, ioctl) takes the mutex while manipulating softc fields. Callback code runs with the mutex held (the framework acquires it before calling). Userland code releases the mutex before sleeping and reacquires it after waking. Wakeup calls from callbacks release any sleeper waiting on the relevant channel.

The specifics vary by transport, but the pattern is universal. Modern FreeBSD drivers are uniformly multithreaded and multi-CPU safe, which requires disciplined locking.

### Pattern 7: Errno-Returning Helpers

Chapter 25 introduced the errno-returning helper function pattern: every internal function returns an integer errno (zero for success, nonzero for failure). Callers check the return value and propagate failure up through the stack. The attach method accumulates successful helpers in the labelled-goto chain; each helper's failure triggers the cleanup corresponding to its position.

This pattern requires discipline. Every helper must be consistent; no helper can return a "success value" that varies in meaning, and no helper can use global state to communicate failure. When followed rigorously, the pattern produces drivers where the control flow is legible and the error paths are easy to audit.

### Pattern 8: Version Declarations and Module Dependencies

Every driver module declares its own version with `MODULE_VERSION`. Every driver module declares its dependencies with `MODULE_DEPEND`. Dependencies are versioned ranges (minimum, preferred, maximum), which allows parallel development of framework and driver to proceed without lockstep releases.

When a new major version of a framework is released with breaking API changes, the version range in `MODULE_DEPEND` is how a driver expresses "I work with framework v1 or v2, but not v3." The kernel module loader refuses to load a driver whose dependencies cannot be satisfied, which prevents many classes of silent breakage.

### Pattern 9: Cross-Framework Layering

Some drivers sit on top of multiple frameworks. A USB-to-Ethernet driver sits on top of `usbdi(9)` (for USB transfers) and `ifnet(9)` (for network interface semantics). A USB-to-serial driver sits on top of `usbdi(9)` and `ucom(4)`. A USB mass-storage driver sits on top of `usbdi(9)` and CAM.

When you write a cross-framework driver, the structure is: you write callbacks for each framework, and you orchestrate the interaction between them in your driver's helper code. The framework on top of which you sit defines how userland sees your driver. The framework below handles the transport.

Reading `uftdi.c` showed you this pattern: the driver is a USB driver (it uses `usbdi(9)`) and a serial driver (it uses `ucom(4)`), and the orchestration between the two is the heart of the file.

### Pattern 10: Early Attach Deferral

Some drivers cannot finish their attach work synchronously. For example, a driver might need to read a configuration EEPROM that takes a few hundred milliseconds, or it might need to wait for a PHY to autonegotiate a link. These drivers use a deferred-attach pattern: the Newbus attach method queues a taskqueue task that does the slow work, then returns quickly.

This pattern keeps the system boot fast (no single driver holds up boot by taking a long time in attach) and lets drivers do their work asynchronously. The caller must be aware that attach "finishing" does not mean the device is fully usable; a separate "ready" state has to be polled or signalled.

For USB and UART drivers, attach is usually fast enough that deferral is not needed. For more complex drivers (network cards in particular), deferral is common. Chapter 28 will show an example.

### Pattern 11: Separate Data Path and Control Path

In every transport driver, two conceptual paths exist: the control path (configuration, state changes, error recovery) and the data path (the actual bytes moving through the device). Most drivers structure these as separate code paths, sometimes with separate locking.

The control path is low-bandwidth and infrequent. It can afford heavy locking and synchronous calls. The data path is high-bandwidth and continuous. It must be optimised for throughput: minimal locking, no synchronous calls, efficient buffer management.

The USB framework keeps them naturally separate: configuration through `usbd_transfer_setup` and control transfers; data through bulk and interrupt transfers. The UART framework likewise: configuration through `tsw_param`; data through the interrupt handler and ring buffers. Network drivers have the most pronounced separation: configuration through ioctls; data through the TX and RX queues.

Reading a new driver, knowing this separation exists helps you parse what each code block is doing. A function with extensive locking and error handling is probably control path. A function with short, tight code and careful buffer management is probably data path.

### Pattern 12: Reference Drivers

Every transport in FreeBSD has one or two "canonical" reference drivers that illustrate the patterns correctly and thoroughly. For USB, `uled.c` and `uftdi.c` are the references. For UART, `uart_dev_ns8250.c` is the reference. For networking, `em` (Intel Ethernet) and `rl` (Realtek) are the references. For block devices, `da` (direct-access storage) is the reference.

When you need to understand how to write a new driver in an existing transport, the reference driver is the right place to start. Do not try to understand the framework from its code alone; that is too abstract. Start from a working driver and let it ground your understanding.

## Hands-On Labs

These labs give you a chance to turn the reading into muscle memory. Each lab is designed to fit in a single sitting, ideally under an hour. They assume a FreeBSD 14.3 lab environment (either on physical hardware or inside a virtual machine), root access, and a working build environment as described in Chapter 3. The companion files for every lab in this chapter are available under `examples/part-06/ch26-usb-serial/` in the book's repository.

The labs build on each other but do not strictly depend on each other. You can skip a lab and come back to it later without losing continuity. The first three labs focus on USB; the last three focus on serial. Each lab has the same structure: a short summary, the steps, expected output, and a "what to watch for" note that highlights the learning goal.

### Lab 1: Exploring a USB Device with `usbconfig`

This lab exercises the descriptor vocabulary from Section 1 by inspecting real USB devices on your machine. It does not involve writing any code.

**Goal.** Read the descriptors of three different USB devices and identify their interface class, the number of endpoints, and the endpoint types.

**Requirements.** A FreeBSD system with at least three USB devices plugged in. If you only have one machine and few USB ports, a USB hub with a few small peripherals (mouse, keyboard, flash drive) is ideal.

**Steps.**

1. Run `usbconfig list` as root. Record the `ugenN.M` identifiers of three devices.

2. For each device, run:

   ```
   # usbconfig -d ugenN.M dump_all_config_desc
   ```

   Read through the output. Identify the `bInterfaceClass`, `bInterfaceSubClass`, and `bInterfaceProtocol` for each interface. For each endpoint in each interface, record the `bEndpointAddress` (including direction bit), the `bmAttributes` (including transfer type), and the `wMaxPacketSize`.

3. Build a small table. For each device, write down: vendor ID, product ID, interface class (with name from the USB class list), number of endpoints, and the transfer type of each endpoint.

4. Match your table against `dmesg`. Confirm that the driver that claimed each device makes sense given the interface class you recorded.

5. Optional: repeat the exercise for a device you have not seen before (someone else's keyboard, a USB audio interface, a game controller). The more variety you see, the faster descriptor reading becomes.

**Expected output.** A filled-in table with at least three rows. The exercise is successful if you can answer, for any device in the table: "What class of driver would handle this?"

**What to watch for.** Pay attention to devices that expose multiple interfaces. A webcam, for example, often has an audio interface (for the microphone) in addition to its video interface. A multi-function printer might expose a printer interface, a scanner interface, and a mass-storage interface. Noticing these is what trains your eye for the multi-interface logic in the `probe` method.

### Lab 2: Building and Loading the USB Driver Skeleton

This lab walks through building the skeleton driver from Section 2, loading it, and observing its behaviour when a matching device is plugged in.

**Goal.** Compile and load `myfirst_usb.ko`, and observe its attach and detach messages.

**Requirements.** The build environment from Chapter 3. The files under `examples/part-06/ch26-usb-serial/lab02-usb-skeleton/`. A USB device whose vendor/product you can match. For development, a VOTI/OBDEV test VID/PID (0x16c0/0x05dc) is free to use; otherwise, pick a cheap prototyping device (like an FT232 breakout board) and adjust the match table to match its IDs.

**Steps.**

1. Enter the lab directory:

   ```
   # cd examples/part-06/ch26-usb-serial/lab02-usb-skeleton
   ```

2. Read `myfirst_usb.c` and `myfirst_usb.h`. Identify the match table, the probe method, the attach method, the softc, and the detach method. For each, trace how it relates to the Section 2 walkthrough.

3. Build the module:

   ```
   # make
   ```

   You should see `myfirst_usb.ko` created in the build directory.

4. Load the module:

   ```
   # kldload ./myfirst_usb.ko
   ```

   Run `kldstat | grep myfirst_usb` to confirm the module is loaded.

5. Plug in a matching device. Observe `dmesg`. You should see a line like:

   ```
   myfirst_usb0: <Vendor Product> on uhub0
   myfirst_usb0: attached
   ```

   If the device does not match, nothing will happen. In that case, open `usbdevs` on the target machine, find the vendor/product of a device you do have, and edit the match table accordingly. Rebuild, reload, and try again.

6. Unplug the device. Observe `dmesg`. You should see the kernel remove the device. Your `detach` does not log anything explicitly in this minimal skeleton, but you can add a `device_printf(dev, "detached\n")` if you want confirmation.

7. Unload the module:

   ```
   # kldunload myfirst_usb
   ```

**Expected output.** Attach messages in `dmesg` when the device is plugged in. Clean unload with no panics when the module is removed.

**What to watch for.** If `kldload` fails with an error about symbol lookups, you probably forgot a `MODULE_DEPEND` line or misspelled a symbol name. If `attach` is never called but the device is definitely present, the match table is wrong: check the vendor and product IDs in `usbconfig list` and verify they match what you wrote in `myfirst_usb_devs`. If `attach` is called but fails, check `device_printf` output for the failure reason.

### Lab 3: A Bulk Loopback Test

This lab adds the transfer mechanics from Section 3 to the skeleton from Lab 2 and sends a few bytes through a USB device that implements a loopback protocol. It is the first lab that actually moves data.

**Goal.** Add a bulk-OUT and bulk-IN channel to the driver, write a small userland client that sends a string and reads it back, and observe the roundtrip.

**Requirements.** A USB device that implements bulk loopback. The simplest such device for development is a USB gadget controller running a loopback program (possible on some ARM boards and on some development kits). If you do not have one, you can substitute a simpler exercise: attach the driver to a USB flash drive, open one of its `ugen` endpoints, and simply armed-submit-complete a single read transfer. The loop will fail (because flash drives do not echo data), but the mechanics of setup and submission will run correctly.

**Steps.**

1. Copy `lab02-usb-skeleton` to `lab03-bulk-loopback` as a working copy.

2. Add the bulk channels to the driver. Paste the config array from Section 3, the callback functions, and the userland interaction. Make sure the `/dev` entry your driver creates supports `read(2)` and `write(2)`, which are what the lab test program uses.

3. Rebuild and reload the module.

4. Run the userland client:

   ```
   # ./lab03-test
   ```

   which you will find alongside the driver in the lab directory. The program opens `/dev/myfirst_usb0`, writes "hello", reads up to 16 bytes, and prints them. If loopback works, the output is "hello".

5. Observe `dmesg` for any stall warnings or error messages.

**Expected output.** "hello" echoed back. If the remote device does not implement loopback, the read will return after the channel's timeout with no data, which is also a valid test outcome for the purposes of exercising the state machine.

**What to watch for.** The most common mistake in this lab is mismatched endpoint directions. Remember: `UE_DIR_IN` means "the host reads from the device" and `UE_DIR_OUT` means "the host writes to the device". If you swap them, the transfers will fail with stalls. Watch also for missing locking around the userland read/write handlers; if you manipulate the transmit queue without the softc mutex held, you can race with the write callback and see bytes disappear.

### Lab 4: A Simulated Serial Driver with `nmdm(4)`

This lab is not about writing a driver; it is about learning the userland half of serial testing. The results will inform how you approach Lab 5 and how you debug any TTY-layer work in the future.

**Goal.** Create a pair of `nmdm(4)` virtual ports, observe how data flows, and exercise `stty` and `comcontrol` to see how termios and modem signals work.

**Requirements.** A FreeBSD system. No special hardware.

**Steps.**

1. Load the `nmdm` module:

   ```
   # kldload nmdm
   ```

2. In terminal A, open the `A` side:

   ```
   # cu -l /dev/nmdm0A -s 9600
   ```

3. In terminal B, open the `B` side:

   ```
   # cu -l /dev/nmdm0B -s 9600
   ```

4. Type in terminal A. Observe that the characters appear in terminal B. Type in terminal B; they appear in terminal A.

5. Exit `cu` in both terminals (type `~.`). In a third terminal, run:

   ```
   # stty -a -f /dev/nmdm0A
   ```

   Read through the output. Notice `9600` for the baud rate, `cs8 -parenb -cstopb` for the byte format, and various flags for line discipline.

6. Change the baud rate on one side:

   ```
   # stty 115200 -f /dev/nmdm0A
   ```

   Then open the ports again with `cu -s 115200`. The baud rate change is visible, even though `nmdm(4)` does not actually wait for serialised bits.

7. Run:

   ```
   # comcontrol /dev/ttyu0A
   ```

   ...or rather, the equivalent for the `nmdm` character devices. The `nmdm` pairs do not always have `comcontrol`-visible modem signals, depending on the FreeBSD version; if your version does not, skip this step.

**Expected output.** Text appears on the opposite side. `stty` shows termios flags. You now have a reproducible way to test TTY-layer behaviour on your machine.

**What to watch for.** The pair identifiers (`0`, `1`, `2`...) are implicit and allocated on first open. If you cannot open `/dev/nmdm5A` because nothing has opened `/dev/nmdm4A` yet, this is expected: pairs are created lazily in increasing order. Also note that `cu` uses a lock file in `/var/spool/lock/`; if you kill `cu` abruptly, the lock file may persist and prevent reopens. Delete it manually if you get a "port in use" error.

### Lab 5: Talking to a Real USB-to-Serial Adapter

This lab brings real hardware into the loop. You will use a USB-to-serial adapter (an FT232, a CP2102, a CH340G, or anything else FreeBSD supports) and a terminal program to exercise the full path from `ucom(4)` through the TTY layer to userland.

**Goal.** Plug in a USB-to-serial adapter, verify it attaches, and use `cu` to send data to it (perhaps by looping the TX and RX pins together with a jumper).

**Requirements.** A USB-to-serial adapter. A jumper wire (if you want to do a hardware loopback) or a second serial device to talk to (a development board, an embedded computer, or an old serial modem).

**Steps.**

1. Plug in the adapter. Run `dmesg | tail` and confirm it attaches. You should see lines like:

   ```
   uftdi0 on uhub0
   uftdi0: <FT232R USB UART, class 0/0, ...> on usbus0
   ```

   and a `ucomN: <...>` line just after that.

2. Run `ls -l /dev/cuaU*`. The adapter's port is usually `/dev/cuaU0` for the first adapter, `/dev/cuaU1` for the second, and so on. (Note the capital-U suffix, which distinguishes USB-provided ports from the real UART ports at `/dev/cuau0`.)

3. Put a jumper wire between the TX and RX pins of the adapter. This creates a hardware loopback: whatever the adapter transmits comes back on its own RX line.

4. In one terminal, set the baud rate:

   ```
   # stty 9600 -f /dev/cuaU0
   ```

5. Open the port with `cu`:

   ```
   # cu -l /dev/cuaU0 -s 9600
   ```

   Type characters. Every character you type should appear twice: once as local echo (if your terminal is echoing), and once as the character coming back through the loopback. Disable the local echo in `cu` if it is confusing; the `stty -echo` will help.

6. Remove the jumper. Type characters. Now they will not come back, because there is nothing connected to RX.

7. Exit `cu` with `~.`. Unplug the adapter. Run `dmesg | tail` and verify clean detach.

**Expected output.** Characters are echoed back when the jumper is in place and lost when it is not. The `dmesg` shows clean attach and detach.

**What to watch for.** If the adapter attaches but no `cuaU` device appears, the underlying `ucom(4)` instance may have attached but failed to create its TTY. Check `dmesg` for errors. If characters come out garbled, the baud rate is probably wrong: make sure every stage of the path (your terminal, `cu`, `stty`, the adapter, and the far end) is set to the same rate. On older hardware, some USB-to-serial adapters do not reset their internal configuration when you open them; you may need to explicitly set the baud rate with `stty` before `cu` will work correctly.

### Lab 6: Observing Hot-Plug Lifecycle

This lab does not require writing any new driver code. It exercises the hot-plug lifecycle we described conceptually in Section 1 and in code in Section 2, using the existing `uhid` or `ukbd` driver as the test subject.

**Goal.** Plug in and unplug a USB device repeatedly while monitoring kernel logs, observing the full attach/detach sequence.

**Requirements.** A USB device you can plug and unplug without disrupting your work session. A USB flash drive or a USB mouse are both safe; a USB keyboard is not (because detaching a keyboard in the middle of a session can strand your shell).

**Steps.**

1. Open a terminal window and run:

   ```
   # tail -f /var/log/messages
   ```

   or, if your system does not log kernel messages to that file:

   ```
   # dmesg -w
   ```

   (The `-w` flag is a FreeBSD 14 addition that streams new kernel messages as they arrive.)

2. Plug in your USB device. Observe the messages. You should see:
   - A message from the USB controller about the new device appearing.
   - A message from `uhub` about the port powering up.
   - A message from the class driver that matched the device (e.g., `ums0` for a mouse, `umass0` for a flash drive).
   - Possibly a message from the higher-level subsystem (e.g., `da0` for a mass-storage device).

3. Unplug the device. Observe the messages. You should see:
   - A message from `uhub` about the port powering down.
   - A detach message from the class driver.

4. Repeat several times. Watch that every attach is matched by a detach. Watch that no message is missed. Watch the timing; the attach sequence can take tens or hundreds of milliseconds because enumeration involves several control transfers.

5. Write a tiny shell loop that records the attach and detach times:

   ```
   # dmesg -w | awk '/ums|umass/ { print systime(), $0 }'
   ```

   (Adjust the regex for the device type you are using.) This gives you a machine-readable log of attach and detach timestamps.

**Expected output.** Clean attach and detach every time, with no dangling state.

**What to watch for.** Occasionally you will see a device attach and then immediately detach within a few hundred milliseconds. This usually indicates the device is failing enumeration: either a bad cable, insufficient power, or a buggy device firmware. If it happens consistently with one device, try a different USB port or a powered hub. Also watch for cases where the kernel reports a stall during enumeration; these are rarely harmful but indicate that the enumeration needed multiple tries.

### Lab 7: Building a ucom(4) Skeleton from Scratch

This lab is an extended one that combines the USB and serial material from the chapter. You will build a minimal `ucom(4)` driver skeleton that presents itself as a serial port but is backed by a simple USB device.

**Goal.** Build a `ucom(4)` driver skeleton that attaches to a specific USB device, registers with the `ucom(4)` framework, and provides empty implementations of the key callbacks. The driver will not actually talk to the hardware, but it will exercise the full `ucom(4)` registration path.

**Requirements.** The materials from Lab 2 (the USB driver skeleton). A USB device you can match against (for testing, you can use the same VOTI/OBDEV VID/PID as in Lab 2, or any spare USB device whose IDs you can read).

**Steps.**

1. Start from Lab 2 as a template. Copy the directory to `lab07-ucom-skeleton`.

2. Modify the softc to include a `struct ucom_super_softc` and a `struct ucom_softc`:

   ```c
   struct lab07_softc {
       struct ucom_super_softc sc_super_ucom;
       struct ucom_softc        sc_ucom;
       struct usb_device       *sc_udev;
       struct mtx               sc_mtx;
       struct usb_xfer         *sc_xfer[LAB07_N_XFER];
       uint8_t                  sc_iface_index;
       uint8_t                  sc_flags;
   };
   ```

3. Add a `struct ucom_callback` with stub implementations:

   ```c
   static void lab07_cfg_open(struct ucom_softc *);
   static void lab07_cfg_close(struct ucom_softc *);
   static int  lab07_pre_param(struct ucom_softc *, struct termios *);
   static void lab07_cfg_param(struct ucom_softc *, struct termios *);
   static void lab07_cfg_set_dtr(struct ucom_softc *, uint8_t);
   static void lab07_cfg_set_rts(struct ucom_softc *, uint8_t);
   static void lab07_cfg_set_break(struct ucom_softc *, uint8_t);
   static void lab07_start_read(struct ucom_softc *);
   static void lab07_stop_read(struct ucom_softc *);
   static void lab07_start_write(struct ucom_softc *);
   static void lab07_stop_write(struct ucom_softc *);
   static void lab07_free(struct ucom_softc *);

   static const struct ucom_callback lab07_callback = {
       .ucom_cfg_open       = &lab07_cfg_open,
       .ucom_cfg_close      = &lab07_cfg_close,
       .ucom_pre_param      = &lab07_pre_param,
       .ucom_cfg_param      = &lab07_cfg_param,
       .ucom_cfg_set_dtr    = &lab07_cfg_set_dtr,
       .ucom_cfg_set_rts    = &lab07_cfg_set_rts,
       .ucom_cfg_set_break  = &lab07_cfg_set_break,
       .ucom_start_read     = &lab07_start_read,
       .ucom_stop_read      = &lab07_stop_read,
       .ucom_start_write    = &lab07_start_write,
       .ucom_stop_write     = &lab07_stop_write,
       .ucom_free           = &lab07_free,
   };
   ```

   `ucom_pre_param` runs on the caller's context before the configuration task is scheduled; use it to reject unsupported termios values by returning a nonzero errno. `ucom_cfg_param` runs in the framework's task context and is where you would issue the actual USB control transfer to reprogram the chip.

4. Implement each callback as a no-op for now. Add `device_printf(sc->sc_super_ucom.sc_dev, "%s\n", __func__)` to each so that you can see which callbacks are being invoked.

5. In the attach method, after `usbd_transfer_setup`, call:

   ```c
   error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
       &lab07_callback, &sc->sc_mtx);
   if (error != 0) {
       goto fail_xfer;
   }
   ```

6. In the detach method, call `ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom)` before `usbd_transfer_unsetup`.

7. Add `MODULE_DEPEND(lab07, ucom, 1, 1, 1);` after the existing MODULE_DEPEND.

8. Build, load, plug in the device, and observe. In `dmesg`, you should see the driver attach, and you should see a `cuaU0` device appear in `/dev/`.

9. Run `cu -l /dev/cuaU0 -s 9600`. The `cu` command will open the device, which triggers several of the ucom callbacks. Watch `dmesg` to see which ones fire. Close `cu` with `~.` and observe more callbacks.

10. Run `stty -a -f /dev/cuaU0`. Observe that the port has default termios settings. Run `stty 115200 -f /dev/cuaU0` and observe that `lab07_cfg_param` is called.

11. Unplug the device. Observe clean detach.

**Expected output.** The driver attaches as a `ucom` device, creates `/dev/cuaU0`, and responds to configuration ioctls (even though the underlying USB device does not actually do anything). Every callback invocation is visible in `dmesg`.

**What to watch for.** If the driver attaches but `/dev/cuaU0` does not appear, check that `ucom_attach` succeeded. The return value is an errno; a nonzero value means failure. If it failed with `ENOMEM`, you are running out of memory for the TTY allocation. If it failed with `EINVAL`, one of the callback fields is probably null (look at `/usr/src/sys/dev/usb/serial/usb_serial.c` to see which fields are strictly required).

This lab is a building block. A real `ucom` driver (like `uftdi`) would fill in the callbacks with actual USB transfers to the chip. Starting from an empty skeleton and adding one callback at a time is a good way to build a new driver.

### Lab 8: Troubleshooting a Hung TTY Session

This lab is a diagnostic exercise. Given a malfunctioning serial setup, you will use the tools from Section 5 to find the problem.

**Goal.** Find why a `cu` session does not echo characters back after connecting to an `nmdm(4)` pair that has an unconfigured baud rate on one side.

**Steps.**

1. Load `nmdm`:

   ```
   # kldload nmdm
   ```

2. Set different baud rates on the two sides. This is contrived but mimics a real configuration bug:

   ```
   # stty 9600 -f /dev/nmdm0A
   # stty 115200 -f /dev/nmdm0B
   ```

3. Open both sides with `cu`, each with the mismatched rate:

   ```
   (terminal 1) # cu -l /dev/nmdm0A -s 9600
   (terminal 2) # cu -l /dev/nmdm0B -s 115200
   ```

4. Type in terminal 1. You will likely see characters appear in terminal 2, but possibly garbled. Or characters may not appear at all if the `nmdm(4)` driver enforces rate matching strictly.

5. Exit both `cu` sessions.

6. Run `stty -a -f /dev/nmdm0A` and `stty -a -f /dev/nmdm0B`. Find the discrepancy.

7. Fix: set both sides to the same rate. Reopen `cu` and verify that the issue is resolved.

**What to watch for.** This lab teaches the diagnostic habit of checking both ends of a link. A mismatch at any one end produces problems; finding it requires looking at both. The diagnostic tools (`stty`, `comcontrol`) work from the command line and produce human-readable output. Making use of them is a simple first check before diving into deeper debugging.

### Lab 9: Monitoring USB Transfer Statistics

This lab explores the per-channel statistics the USB framework maintains, which can help identify performance issues or hidden errors.

**Goal.** Use `usbconfig dump_stats` to observe the transfer counts on a busy USB device and identify whether the device is performing as expected.

**Steps.**

1. Plug in a USB device that you can exercise meaningfully. A USB flash drive is a good choice because you can trigger bulk transfers by copying files.

2. Identify the device:

   ```
   # usbconfig list
   ```

   Note the `ugenN.M` identifier.

3. Dump the baseline statistics:

   ```
   # usbconfig -d ugenN.M dump_stats
   ```

   Record the output.

4. Perform significant I/O to the device. For a flash drive, copy a large file:

   ```
   # cp /usr/src/sys/dev/usb/usb_transfer.c /mnt/usb_mount/
   ```

5. Dump the statistics again. Compare.

6. Note which counters changed. `xfer_completed` should have increased significantly. `xfer_err` should still be small.

7. Try to deliberately cause errors. Unplug the device mid-transfer. Then plug it back in. Dump the stats on the new `ugenN.M` (a new one is allocated on replug).

**What to watch for.** The statistics reveal invisible behaviours. A device that is mostly working but occasionally stalling will show `stall_count` nonzero. A device that is dropping transfers will show `xfer_err` climbing. In normal operation, a healthy device shows steady `xfer_completed` growth and zero errors.

If you are developing a driver and the statistics show unexpected errors, that is a clue that something is wrong. The statistics are maintained by the USB framework, not the driver, so they reflect reality regardless of whether the driver notices.

## Challenge Exercises

Challenge exercises stretch your understanding. They are not strictly necessary for progressing to Chapter 27, but each one will deepen your grasp of USB and serial driver work. Take your time. Read relevant FreeBSD source. Write small programs. Expect some challenges to take several hours.

### Challenge 1: Add a Third USB Endpoint Type

The skeleton in Section 2 supports bulk transfers. Extend it to also handle an interrupt endpoint. Add a new channel to the `struct usb_config` array with `.type = UE_INTERRUPT`, `.direction = UE_DIR_IN`, and a small buffer (say, sixteen bytes). Implement the callback as a continuous poll, reading a small status packet from the device on every interrupt-IN completion.

Test the change by comparing the behaviour of the three channels. Bulk channels should be quiet most of the time and only submit transfers when the driver has work to do. The interrupt channel should run continuously, quietly consuming interrupt-IN packets whenever the device sends them.

A stretch goal: make the interrupt callback deliver received bytes to the same `/dev` node as the bulk channel. When userspace reads the node, it gets a merged view of bulk-in and interrupt-in data. This is a useful pattern for devices that have both streaming data and asynchronous status events.

### Challenge 2: Write a Minimal USB Gadget Driver

The running example is a host-side driver: the FreeBSD machine is the USB host, and the device is the peripheral. Turn the example around by writing a USB gadget driver that makes the FreeBSD machine present itself as a simple device to another host.

This requires USB-on-the-Go hardware, so the challenge is only feasible on specific boards (some ARM development boards support it). The relevant source is in `/usr/src/sys/dev/usb/template/`. Start from `usb_template_cdce.c`, which implements the CDC Ethernet class, and modify it to implement a simpler vendor-specific class with one bulk-OUT endpoint that just swallows whatever the host sends.

This challenge teaches you how the USB framework looks from the other side. Many of the concepts are mirror-imaged: what was a transfer from the host's perspective is a transfer from the device's perspective, but the direction of the bulk arrow is reversed.

### Challenge 3: A Custom `termios` Flag Handler

The `termios` structure has many flags, and the `uart(4)` framework handles most of them automatically. Write a small modification to a UART driver (or to a copy of `uart_dev_ns8250.c` in a private build) that makes the driver log a `device_printf` message every time a specific termios flag changes value.

Pick, say, `CRTSCTS` (hardware flow control) as the flag to track. Add a log message in the driver's `param` path that prints "CRTSCTS=on" or "CRTSCTS=off" whenever the flag's new value differs from its old value.

Test the modification by running:

```console
# stty crtscts -f /dev/cuau0
# stty -crtscts -f /dev/cuau0
```

Verify that the log messages appear in `dmesg` and that they correspond correctly to the `stty` changes.

This challenge is about understanding exactly where in the call chain the termios change arrives at the driver. The answer (in `param`) is documented in the source, but seeing it with your own eyes is different from reading about it.

### Challenge 4: Parsing a Small USB Protocol

Pick a USB protocol you are curious about. HID is a good candidate because it is widely documented. CDC ACM is another good choice because it is simple. Pick one, read the specification on usb.org (the public parts), and write a small protocol parser in C that takes a buffer of bytes and prints what they mean.

For HID, the parser would consume reports: input reports, output reports, feature reports. It would look up the device's report descriptor to learn the layout. It would print, for each report, the usage (mouse motion, button press, keyboard scan code) and the value.

For CDC ACM, the parser would consume the AT command set: a small set of commands that terminal programs use to configure modems. It would recognise the commands and report which ones the driver would handle and which would be passed through to the device.

This is not a driver-writing challenge per se; it is a protocol-understanding challenge. Device drivers implement protocols, and being comfortable with protocol specifications is a core skill.

### Challenge 5: Robustness Under Load

Take the echo-loop driver from Lab 3 (or a similar driver you have written) and stress-test it. Write a userland program that runs two threads: one constantly writes random bytes to the device, one constantly reads and verifies.

Run the program for an hour. Then run it for a day. Then unplug and replug the device during the run and see whether the program recovers cleanly.

You will probably find bugs. Common ones include: write callback locking issues under concurrent access, races between close() and in-flight transfers, memory leaks from buffers that are allocated but never freed on specific error paths, and state machine bugs when a stall arrives at an unexpected moment.

Each bug you find will teach you something about where the driver's contract with its callers is subtle. Fix the bugs. Log what you learned. This is exactly the kind of work that separates a good driver from a merely working one.

### Challenge 6: Implement Suspend/Resume Properly

Most USB drivers do not implement suspend and resume handlers. The framework has defaults that work for the common case, but a driver that holds long-term state (a queue of pending commands, a streaming context, a negotiated session) may need to save and restore that state around suspend cycles.

Extend the echo-loop driver with `device_suspend` and `device_resume` methods. In suspend, flush any pending transfers and save a small amount of state. In resume, restore the state and resubmit any pending work.

Test by running the system through a suspend cycle (on a laptop that supports it) while the driver is running. Verify that after resume, the driver continues working correctly and no state was lost.

This challenge teaches the subtleties of suspend/resume, including that hardware may be in a different state after resume than it was before suspend, and that all in-flight state must be reconstructed or abandoned.

### Challenge 7: Adding `poll(2)` Support

Most drivers shown in this chapter support `read(2)` and `write(2)` but not `poll(2)` or `select(2)`. These system calls let userland programs wait for I/O readiness on multiple descriptors at once, which is essential for servers and interactive programs.

Add a `d_poll` method to the echo driver's `cdevsw`. The method should return a bitmask indicating which I/O events are currently possible: POLLIN if there is data to read, POLLOUT if there is space to write.

The hardest part of adding poll support is the wakeup logic. When a transfer callback adds data to the RX queue, it must call `selwakeup` on the selinfo structure the poll mechanism uses. Similarly, when the write callback drains bytes from the TX queue and makes space, it must call `selwakeup` on the write selinfo.

This challenge will require reading `/usr/src/sys/kern/sys_generic.c` and `/usr/src/sys/sys/selinfo.h` to understand the selinfo mechanism.

### Challenge 8: Writing a Character-Counter ioctl

Add an ioctl to the echo driver that returns the current TX and RX byte counters. The ioctl interface requires you to:

1. Define a magic number and struct for the ioctl in a header:
   ```c
   struct myfirst_usb_stats {
       uint64_t tx_bytes;
       uint64_t rx_bytes;
   };
   #define MYFIRST_USB_GET_STATS _IOR('U', 1, struct myfirst_usb_stats)
   ```

2. Implement a `d_ioctl` method that responds to `MYFIRST_USB_GET_STATS` by copying the counters out to userland.

3. Maintain the counters in the softc, incrementing them in the transfer callbacks.

4. Write a userland program that issues the ioctl and prints the results.

This challenge teaches the ioctl interface, which is the standard way drivers expose non-streaming operations to userland. It also introduces you to the `_IOR`, `_IOW`, and `_IOWR` macros from `<sys/ioccom.h>`.

## Troubleshooting Guide

Despite your best efforts, problems will occur. This section documents the most common classes of problem you will hit while working on USB and serial drivers, with concrete steps to diagnose each.

### The Module Will Not Load

Symptom: `kldload myfirst_usb.ko` returns an error, typically with a message about unresolved symbols.

Causes and fixes:
- Missing `MODULE_DEPEND` entry. Add `MODULE_DEPEND(myfirst_usb, usb, 1, 1, 1);` to the driver.
- Missing `MODULE_DEPEND` on a second module, such as `ucom`. If your driver uses `ucom_attach`, add a dependency on `ucom`.
- Compiled against a kernel that does not match the running kernel. Rebuild the module against the currently-running sources.
- Kernel symbol table out of date. After kernel upgrade, run `kldxref /boot/kernel` to refresh.

If the error message mentions a specific symbol you did not write (like `ttycreate` or `cdevsw_open`), look up the missing symbol in the source tree to find out which subsystem it lives in, and add a `MODULE_DEPEND` on that module.

### The Driver Loads but Never Attaches

Symptom: `kldstat` shows the driver loaded, but `dmesg` shows no attach message when the device is plugged in.

Causes and fixes:
- Match table does not match the device. Compare the vendor and product IDs from `usbconfig list` against your `STRUCT_USB_HOST_ID` entries.
- Interface number mismatch. If the device has multiple interfaces and your probe guards against `bIfaceIndex != 0`, try a different interface.
- Probe returns `ENXIO` for some other reason. Add `device_printf(dev, "probe with class=%x subclass=%x\n", uaa->info.bInterfaceClass, uaa->info.bInterfaceSubClass);` at the top of `probe` temporarily to see what the framework is offering.
- Another driver is claiming the device first. Check `dmesg` for other driver attachments; you may need to explicitly unload the competing driver with `kldunload` before yours can bind. Alternatively, give your driver a higher priority through bus-probe return values (applies to PCI-like buses, not USB).

### The Driver Attaches but `/dev` Node Does Not Appear

Symptom: attach message in `dmesg`, but `ls /dev/` shows no corresponding entry.

Causes and fixes:
- `make_dev` call failed. Check the return value; if null, handle the error and log it.
- Wrong cdevsw. Make sure `myfirst_usb_cdevsw` is declared correctly with `d_version = D_VERSION` and valid `d_name`, `d_open`, `d_close`, `d_read`, `d_write`, `d_ioctl` where relevant.
- `si_drv1` not set. Although not strictly required for the node to appear, many bugs manifest as "the node appears but ioctls see a NULL softc" because `si_drv1` was not initialised.
- Permissions issue. The default 0644 permissions may restrict access; try 0666 temporarily during development.

### The Driver Panics on Detach

Symptom: unplugging the device (or unloading the module) causes a kernel panic.

Causes and fixes:
- Transfer callback running during detach. You must call `usbd_transfer_unsetup` before destroying the mutex. The framework's cancellation and wait logic is what makes detach safe.
- `/dev` node open when driver unloads. If userspace has the node open, the module cannot unload. Run `fstat | grep myfirst_usb` to see which process holds it, and kill the process or close the file.
- Memory freed before all uses complete. If you use deferred work (taskqueue, callout), you must cancel and wait for it before freeing the softc. The `taskqueue_drain` and `callout_drain` functions exist for this.
- Softc use-after-free. If you have code outside the driver that holds a pointer to the softc, the softc can be freed while that pointer is still dangling. Redesign to avoid external softc pointers, or add reference counting.

### Transfers Stall

Symptom: bulk transfers appear to succeed at the submit call but never complete, or they complete with `USB_ERR_STALLED`.

Causes and fixes:
- Wrong endpoint direction. Verify the direction in your `struct usb_config` against the endpoint's `bEndpointAddress` high bit.
- Wrong endpoint type. Verify that the `type` field matches the endpoint's `bmAttributes` low bits.
- Transfer too large. If you set a frame length larger than the endpoint's `wMaxPacketSize`, the framework will usually slice it into packets, but some devices reject a transfer that exceeds an internal buffer.
- Device firmware stall. The remote device is signalling "not ready." The framework's automatic clear-stall should recover, but a persistent stall usually indicates a protocol error (wrong command, wrong sequence, missing authentication).

### Serial Characters Garbled

Symptom: bytes appear on the wire but are wrong or contain extra characters.

Causes and fixes:
- Baud rate mismatch. Every stage must agree. Use `stty` to check all stages.
- Byte format mismatch. Set databits, parity, and stopbits to match. `stty cs8 -parenb -cstopb` is the most common configuration.
- Incorrect `termios` flag handling in the driver. If you modify `uart_dev_ns8250.c` and break `param`, the chip will be programmed wrong. Compare against the upstream file.
- Flow-control mismatch. If one side has `CRTSCTS` enabled and the other does not, bytes will be lost under load. Set both sides consistently.
- Cable issue. A bad cable or a cable with unusual pinouts (some RJ45-to-DB9 cables have nonstandard pinouts) can introduce bit errors. Swap cables to rule this out.

### A Process is Stuck in `read(2)` and Will Not Exit

Symptom: a program blocked on the driver's `read()` path will not respond to Ctrl+C or `kill`.

Causes and fixes:
- Driver `d_read` sleeps without checking for signals. Use `msleep(..., PCATCH, ...)` (with the `PCATCH` flag) so the sleep returns `EINTR` when a signal arrives, and propagate the errno back to userspace.
- Driver `d_read` holds a non-interruptible lock. Verify that the sleep is on an interruptible condition variable and that the mutex is dropped during the sleep.
- Transfer callback is never arming the channel. If your `d_read` waits on a flag that only the read callback sets, and the read callback is never fired, the wait will never complete. Make sure the channel is started on `d_open` or at attach time.

### High CPU Usage When Idle

Symptom: the driver consumes significant CPU even when no data is flowing.

Causes and fixes:
- Polling-based implementation. If your driver polls a flag in a busy loop, rewrite it to sleep on an event.
- Callback firing excessively. The framework should not fire a callback without a state change, but some misconfigured channels can enter a "retry on error" loop that fires the callback as fast as the hardware can respond. Add a retry counter or a rate-limiter.
- Read callback with no work but always rearming. If the device sends zero-byte transfers to signal "I have nothing to say," make sure your callback handles these gracefully without treating them as normal data.

### `usbconfig` Shows the Device but `dmesg` Is Silent

Symptom: `usbconfig list` shows the device, but no driver attach message appears.

Causes and fixes:
- Device attached to `ugen` (the generic driver) because no specific driver matched. This is the normal behaviour when there is no matching driver. Check the match tables of the available drivers. `pciconf -lv` will not help here because this is USB, not PCI; the USB equivalent is `usbconfig -d ugenN.M dump_device_desc`.
- `devd` is disabled and auto-load is not happening. Enable `devd` by running `service devd onestart`, then plug the device in again.
- Module file is not in a loadable path. `kldload` can take a full path (`kldload /path/to/module.ko`), but for automatic loading by `devd`, the module has to be in a directory `devd` is configured to search. `/boot/modules/` is the conventional location for out-of-tree modules on a production system.

### Debugging a Deadlock with `WITNESS`

Symptom: the kernel hangs with the CPU stuck in a specific function, and `WITNESS` is enabled.

Causes and fixes:
- Lock order violation. `WITNESS` will log the violation on the serial console. Read the log: it will tell you which locks were taken in which order, and where the reverse order was observed. Fix by establishing a consistent lock acquisition order throughout your driver.
- Lock held across a sleep. If you hold a mutex and then call a function that sleeps, you can deadlock with any other thread that wants the mutex. Identify the sleeping function (often hidden in an allocation or in a USB transfer wait), and restructure to release the mutex before the sleep.
- Lock taken in interrupt context that was first taken outside interrupt context without `MTX_SPIN`. FreeBSD mutexes have two forms: default (`MTX_DEF`) can sleep, spin (`MTX_SPIN`) cannot. Taking a sleep mutex from an interrupt handler is a bug.

Enabling `WITNESS` during development (by building the kernel with `options WITNESS` or by using `GENERIC-NODEBUG`'s `INVARIANTS`-enabled counterpart) catches many of these problems before they appear on a user's machine.

### A Driver That Appears Twice for the Same Device

Symptom: `dmesg` shows your driver attaching twice for a single device, creating `myfirst_usb0` and `myfirst_usb1` with the same USB IDs.

Causes and fixes:
- The device has two interfaces and the driver is matching both. Check `bIfaceIndex` in the probe method and match only the interface you actually support.
- The device has multiple configurations and both are active. This is rare; if so, select the correct configuration explicitly in the attach method.
- Another driver is attached to one of the interfaces. This is not a bug; it just means the device is multi-interface and different drivers claim different interfaces. If you see `myfirst_usb0` and `ukbd0` for the same device, the device has both a vendor-specific interface and a HID interface, and the two drivers attach independently.

### USB Serial Baud Rate Does Not Take Effect

Symptom: You `stty 115200 -f /dev/cuaU0`, but data exchange happens at a different rate.

Causes and fixes:
- The control transfer to program the baud rate failed. Check `dmesg` for error messages from `ucom_cfg_param`. Instrument the driver to log the result of the control transfer.
- The chip's divisor encoding is wrong. Different FTDI variants use slightly different divisor formulas; check the variant detection in the driver.
- The peer is running at a different rate. As noted earlier in the chapter, both ends must agree.
- The cable or adapter is introducing its own rate limitation. Some USB-to-serial adapters silently renegotiate; this is rare but can happen with poor-quality cables.

### A Kernel Panic with "Spin lock held too long"

Symptom: The kernel panics with this message, usually during high I/O on the driver.

Causes and fixes:
- A UART driver's `uart_ops` method is sleeping or blocking. The six methods in `uart_ops` run with spin locks held (on some paths) and must not sleep, call non-spin-safe functions, or do long loops. Review the offending method for any expensive calls.
- The interrupt handler is not draining the interrupt source fast enough. If the handler takes longer than the interrupt rate, interrupts accumulate. Speed up the handler.
- Lock contention is causing priority inversion. Reduce the scope of the critical section, or break it up.

### A Device Never Completes Enumeration

Symptom: Plugging in a device produces a `dmesg` line or two about enumeration starting, but never a completion message.

Causes and fixes:
- The device is violating the USB specification. Some cheap or counterfeit devices have buggy firmware. If possible, try a different device.
- Insufficient power. Devices that claim more power than the port can supply will fail to enumerate. Try a powered hub.
- Electromagnetic interference. A bad cable or a bad port can cause bit errors during enumeration. Try different cables or ports.
- The USB host controller is in a confused state. Try unloading and reloading the host controller driver, or (as a last resort) rebooting.

### Diagnostic Checklist When You Are Stuck

When a driver under development is not behaving correctly and you do not know why, walk through this checklist in order. Each step eliminates a large class of possible problems.

1. Compile cleanly with `-Wall -Werror`. Many subtle bugs produce warnings.
2. Load in a kernel built with `INVARIANTS` and `WITNESS`. Any locking or invariant violations will be caught immediately.
3. Enable your driver's debug logging. Run a minimal reproduction scenario and capture the logs.
4. Compare the driver's behaviour against a known-working driver for similar hardware. Diffing behaviour reveals bugs that staring at your own code does not.
5. Simplify the scenario. Write a minimal userland test program. Use a minimal USB device (or an `nmdm` pair for serial). Remove every variable you can.
6. Use `dtrace` on the USB framework functions. `usbd_transfer_submit:entry` and `usbd_transfer_submit:return` probes let you trace exactly which transfers were submitted and what happened to them.
7. Run the driver with `WITNESS_CHECKORDER` enabled. Each time a mutex is taken, the order is verified against the accumulated history.
8. If the issue is intermittent, run under a stress-test harness that generates load for hours. Intermittent bugs become reproducible under sustained load.

This checklist is not exhaustive, but it covers the techniques that find the majority of driver bugs.

## Reading the FreeBSD USB Source Tree: A Guided Tour

The `myfirst_usb` skeleton and the FTDI walkthrough have given you the shape of a USB driver. But the real learning happens when you read existing drivers in the tree. Each one is a small lesson in how to apply the framework to a specific class of device. This section gives you a guided tour of five drivers, ordered from simplest to most representative, and points out what each one teaches.

The pattern we recommend is this. Open each driver's source file next to this section. Read the opening comment block and the structure definitions first; those tell you what the driver is for and what state it maintains. Then trace the lifecycle: match table, probe, attach, detach, registration. Only after the lifecycle is clear should you move on to the data path. This ordering mirrors how the framework itself treats the driver: first as a match candidate, then as an attached driver, and only then as something that moves data.

### Tour 1: uled.c, the Simplest USB Driver

File: `/usr/src/sys/dev/usb/misc/uled.c`.

Start here. `uled.c` is the Dream Cheeky USB LED driver. It is under 400 lines. It implements a single output (setting the LED colour) through a single control transfer. There is no input, no bulk transfer, no interrupt transfer, no concurrent I/O. Everything about it is minimal, and for that reason everything about it is easy to read.

Key things to study in `uled.c`:

The match table has a single entry: `{USB_VPI(USB_VENDOR_DREAM_CHEEKY, USB_PRODUCT_DREAM_CHEEKY_WEBMAIL_NOTIFIER_2, 0)}`. This is the minimal match-by-VID/PID idiom. No subclass or protocol filtering; just vendor and product.

The softc is tiny. It contains a mutex, the `usb_device` pointer, the `usb_xfer` array, and the LED state. This is the minimum every USB driver needs.

The probe method is two lines: check that the device is in host mode and return the result of `usbd_lookup_id_by_uaa` against the match table. No interface-index check, no complex matching. For a simple device with a single function, this is enough.

The attach method allocates the transfer channel, creates a device-file entry with `make_dev`, and stores the pointers. No complex negotiation; the device is ready after `attach` returns.

The I/O path is a single control transfer with a fixed setup. The driver sets the frame length, fills in the color bytes with `usbd_copy_in`, and calls `usbd_transfer_submit`. That is it.

Read `uled.c` first. When you have read it once, the rest of the USB subsystem opens up. Every more complex driver is a variation on this pattern.

### Tour 2: ugold.c, Adding Interrupt Transfers

File: `/usr/src/sys/dev/usb/misc/ugold.c`.

`ugold.c` drives a USB thermometer. It is still very short, under 500 lines, but it introduces interrupt transfers, which are the staple of HID-class devices.

Key things to learn from `ugold.c`:

The device publishes temperature readings periodically via an interrupt endpoint. The driver's job is to listen on that endpoint and deliver the readings to userland via `sysctl`.

The `usb_config` array now has an entry for `UE_INTERRUPT`, with `UE_DIR_IN`. This tells the framework to set up a channel that polls the interrupt endpoint.

The interrupt callback shows the canonical pattern: on `USB_ST_TRANSFERRED`, extract the received bytes with `usbd_copy_out`, parse them, update the softc. On `USB_ST_SETUP` (including the initial callback after `start`), set the frame length and submit. On `USB_ST_ERROR`, decide whether to recover or give up.

The driver exposes readings through `sysctl` nodes created in `attach` and torn down in `detach`. This is a common pattern for devices that produce occasional readings: the interrupt callback writes to softc state, and userland reads from `sysctl` when it wants a value.

Compare `ugold.c` to `uled.c` after reading both. The control-transfer-only driver and the interrupt-transfer driver represent the two most common skeleton patterns. Most other USB drivers are composed of variations of these two.

### Tour 3: udbp.c, Bidirectional Bulk Transfers

File: `/usr/src/sys/dev/usb/misc/udbp.c`.

`udbp.c` is the USB Double Bulk Pipe driver. It exists to test bidirectional bulk data flow between two computers connected by a special USB-to-USB cable. It is about 700 lines and gives you a complete working example of bulk read and bulk write.

Key things to learn from `udbp.c`:

The `usb_config` has two entries: one for `UE_BULK` `UE_DIR_OUT` (host-to-device) and one for `UE_BULK` `UE_DIR_IN` (device-to-host). This is the standard bulk-duplex pattern.

Each callback does the same three-state dance. On `USB_ST_SETUP`, set the frame length (or if it is a read, just submit). On `USB_ST_TRANSFERRED`, consume the completed data and re-arm. On `USB_ST_ERROR`, decide the recovery policy.

The driver uses the netgraph framework to integrate with higher layers. This is a choice specific to the Double Bulk Pipe device. For a simple application, you would expose the bulk channels through a character device, as `myfirst_usb` does.

Trace how the softc maintains the state of each direction independently. The receive callback rearms only when a buffer is available. The transmit callback rearms only when there is something to send. The two callbacks coordinate only through shared softc fields (counter of pending operations, queue pointers).

### Tour 4: uplcom.c, a USB-to-Serial Bridge

File: `/usr/src/sys/dev/usb/serial/uplcom.c`.

`uplcom.c` drives the Prolific PL2303, one of the most common USB-to-serial chips. At around 1400 lines, it is more substantial than the previous three, but every part of it maps directly onto the serial-driver pattern from Section 4 of this chapter.

Key things to learn from `uplcom.c`:

The `ucom_callback` structure fills in every configuration method you would expect a real driver to implement: `ucom_cfg_open`, `ucom_cfg_param`, `ucom_cfg_set_dtr`, `ucom_cfg_set_rts`, `ucom_cfg_set_break`, `ucom_cfg_get_status`, `ucom_cfg_close`. Each of these calls the framework-provided `ucom` primitives after issuing the chip-specific USB control transfer.

Look at `uplcom_cfg_param`. It takes a `termios` structure, extracts the baud rate and framing, and constructs a vendor-specific control transfer to program the chip. This is how a user's `stty 9600` call propagates through the layers: `stty` updates `termios`, the TTY layer calls `ucom_param`, the framework schedules the control transfer, and `uplcom_cfg_param` programs the chip.

Compare `uplcom_cfg_param` with the corresponding function in `uftdi.c`. Both translate a `termios` to a vendor-specific control sequence, but the vendor protocols are entirely different. This illustrates why the framework insists on per-vendor drivers: each chip has its own command set, and the framework's job is only to give each driver a uniform way to be called.

Note how the driver handles reset, modem signals, and break. Each modem-line operation is a separate USB control transfer. The cost of changing, say, DTR is one round-trip to the device, which on a 12 Mbps bus takes about 1 ms. This tells you why line signals change more slowly over USB-to-serial than over a native UART, and why protocols that toggle DTR frequently can behave differently through a USB-to-serial adapter.

### Tour 5: uhid.c, the Human Interface Device Driver

File: `/usr/src/sys/dev/usb/input/uhid.c`.

`uhid.c` is the generic HID driver. HID stands for Human Interface Device; it covers keyboards, mice, gamepads, touchscreens, and countless vendor-specific devices that conform to the HID class standard. `uhid.c` is roughly 1000 lines.

Key things to learn from `uhid.c`:

The match table uses class-based matching. Instead of listing every VID/PID, the driver matches any device that advertises the HID interface class. `UIFACE_CLASS(UICLASS_HID)` tells the framework to match any HID interface, no matter which vendor made the device.

The driver exposes the device through a character device, not through `ucom` or a networking framework. The character device pattern lets userland programs open `/dev/uhidN` and issue `ioctl` calls to read HID descriptors, read reports, and set feature reports.

The interrupt endpoint delivers HID reports, and the driver hands them up to userland through a ring buffer and `read`. This is the USB equivalent of a character-device interrupt-driven read loop.

Study how `uhid.c` uses the HID report descriptor to understand what the device is. The descriptor is parsed at attach time, and the driver populates its internal tables from the parse. Every HID device describes itself this way; the driver does not hard-code device semantics.

### How to Study a Driver You Have Never Seen

Beyond the tour, you will encounter drivers in the tree that you have never seen. A general-purpose reading strategy helps:

Open the source file and scroll to the bottom. The registration macros are there. They tell you what the driver attaches to (`uhub`, `usb`) and its name (`udbp`, `uhid`). Already you know where the driver fits in the tree.

Scroll back up to the `usb_config` array (or the transfer declarations for non-USB drivers). Each entry is one channel. Count them; look at their types and directions. You now know the shape of the data path.

Look at the probe method. If it matches by VID/PID, the device is vendor-specific. If it matches by class, the driver supports a family of devices. This tells you the scope of the driver.

Look at the attach method. Follow its labelled-goto chain. The labels give you the order of resource allocation: mutex, channels, character device, sysctls, and so on.

Finally, look at the data-path callbacks. Each one is a three-state state machine. Read `USB_ST_TRANSFERRED` first; that is where the actual work happens. Then read `USB_ST_SETUP`; that is the kickoff. Then read `USB_ST_ERROR`; that is the recovery policy.

With this reading order, you can make sense of any USB driver in the tree in about 15 minutes. With practice, you will start to recognise patterns across drivers and know which ones are idiomatic (the ones to copy) and which ones are historical oddities (the ones to understand but not copy).

### Where to Go Beyond the Tour

The `/usr/src/sys/dev/usb/` tree has four subdirectories that are worth exploring:

`/usr/src/sys/dev/usb/misc/` contains simple, single-purpose drivers: `uled`, `ugold`, `udbp`. If you are writing a new device-specific driver that does not fit an existing class, read the drivers here to see how small drivers are structured.

`/usr/src/sys/dev/usb/serial/` contains the USB-to-serial bridge drivers: `uftdi`, `uplcom`, `uslcom`, `u3g` (3G modems, which present as serial to userland), `uark`, `uipaq`, `uchcom`. If you are writing a new USB-to-serial driver, start here.

`/usr/src/sys/dev/usb/input/` contains keyboard, mouse, and HID drivers. `ukbd`, `ums`, `uhid`. If you are writing a new input driver, these are the patterns to follow.

`/usr/src/sys/dev/usb/net/` contains USB network drivers: `axge`, `axe`, `cdce`, `ure`, `smsc`. These are the drivers that bridge Chapter 26 to Chapter 27, because they combine the USB framework of this chapter with the `ifnet(9)` framework of the next. Reading one of them after finishing Chapter 27 is a productive exercise.

The `/usr/src/sys/dev/uart/` tree has fewer files but each is worth reading:

`/usr/src/sys/dev/uart/uart_core.c` is the framework core. Read this to understand what happens above your driver: how bytes flow in and out, how the TTY layer connects, how interrupts are dispatched.

`/usr/src/sys/dev/uart/uart_dev_ns8250.c` is the canonical reference driver. Read this after the framework core so you can see how a driver plugs in.

`/usr/src/sys/dev/uart/uart_bus_pci.c` shows the PCI bus-attach glue for UARTs. If you ever need to write a UART driver that attaches to PCI, this is your starting point.

Each of these files is small enough to read in one sitting. Reading the source is not homework; it is how you learn a subsystem. Chapter 26 has given you the vocabulary and the mental model; the source is where you apply them.

## Performance Considerations for Transport Drivers

Most of Chapter 26 has focused on correctness: getting a driver to attach, do its work, and detach cleanly. Correctness always comes first. But once your driver works, you will often want to know how fast it is, and whether its performance matches what the transport can sustain. This section gives you a practical frame for thinking about USB and UART performance without turning the chapter into a benchmarking manual.

### The USB Bus as a Shared Resource

Every device on a USB bus shares the bus with every other device. The bandwidth is not divided fairly; it is allocated according to USB's scheduling rules. Control and interrupt endpoints get guaranteed periodic service. Bulk endpoints get what is left over, in a fair-share sense. Isochronous endpoints reserve bandwidth up front; if there is not enough, the allocation fails.

For a bulk-transferring driver, the practical upshot is this. Your effective bandwidth is the theoretical link speed (12, 480, 5000 Mbps) minus the overhead of other devices' periodic traffic, minus the USB protocol overhead (roughly 10% on full-speed, less on higher speeds), minus the overhead of short transfers.

The last item is the one you can influence. A transfer of 16 KB is not 16 times more expensive than a transfer of 1 KB; the overhead of initiating and completing a transfer is fixed, and the data-transfer portion is close to linear in size. For high-throughput bulk transfers, use large buffers. The hardware is designed for this; the framework is designed for this; your driver should be designed for this.

For an interrupt-transferring driver, the constraint is different. The interrupt endpoint polls at a fixed interval (configured by the device). The framework delivers a callback whenever the polled transfer completes. The maximum report rate is the endpoint's polling rate. If the device has a 1-ms interval, you get at most 1000 reports per second. Planning for interrupt-driven performance means planning around the polling rate.

### Latency: What Costs Microseconds, What Costs Milliseconds

USB is not a low-latency bus. A single control transfer on full-speed USB takes roughly 1 ms round-trip. A single bulk transfer takes roughly 1 ms of framing overhead plus the time to move the data. Interrupt transfers are scheduled at the polling interval, so the minimum latency is the interval itself.

Compare this to native UART, where a character transmission takes roughly 1 ms at 9600 baud, 100 us at 115200 baud, and 10 us at 1 Mbps. A native UART driver can push out a byte in hundreds of microseconds if it is well designed; a USB-to-serial bridge cannot match that, because each byte has to traverse USB first.

For your driver, this means: think about where latency matters for your use case. If you are building a monitoring driver that reports once a second, USB is fine. If you are building an interactive controller where the user can feel each character round-trip, native UART is much better. If you are building a real-time control loop where characters must traverse in tens of microseconds, neither USB nor general-purpose UART is appropriate; you need a dedicated bus with known timing.

### When to Rearm: The Classic USB Tradeoff

A key decision in any streaming USB driver is where in the callback to re-arm the transfer. There are two viable patterns:

**Rearm after work.** In `USB_ST_TRANSFERRED`, do the work (parse the data, hand it up, update state), then rearm. Simple to implement. Has a latency cost: the time between the previous completion and the next submission is the time it took to do the work.

**Rearm before work, using multiple buffers.** In `USB_ST_TRANSFERRED`, immediately rearm with a fresh buffer, then do the work on the just-completed buffer. This requires multiple `frames` in the `usb_config` (so the framework rotates through a pool of buffers) or two parallel transfer channels. Has near-zero latency between transfers because the hardware always has a buffer ready.

Most drivers in the tree use the first pattern because it is simpler. The second pattern is used in high-throughput drivers where hiding the work latency matters. `ugold.c` is the first pattern; some of the USB Ethernet drivers in `/usr/src/sys/dev/usb/net/` are the second.

### Buffer Sizing

For bulk transfers, the buffer size is a knob. Larger buffers amortise the per-transfer overhead, but they also delay the delivery of partial data and increase memory usage. The typical values in the tree are between 1 KB and 64 KB.

For interrupt transfers, the buffer size is usually small (8 to 64 bytes) because the endpoint itself limits the report size. Do not make this larger than the endpoint's `wMaxPacketSize`; the extra buffer is wasted.

For control transfers, the buffer size is determined by the protocol of the specific operation. The `usb_device_request` header is always 8 bytes; the data portion depends on the request.

### UART Performance

For a UART driver, performance is usually a question of interrupt efficiency. A 16550A with a FIFO depth of 16 bytes at 115200 baud needs to be serviced roughly every 1.4 ms in the worst case. If your interrupt handler takes longer than that, the FIFO overflows and data is lost. Modern UARTs (16750, 16950, ns16550 variants on embedded SoCs) often have deeper FIFOs (64, 128, or 256 bytes) specifically to relax this constraint.

The `uart(4)` framework handles the FIFO management for you through `uart_ops->rxready` and the ring buffer. What you control as the driver author is: how fast your implementation of `getc` is, how fast `putc` is, and whether your interrupt handler is sharing the CPU with other work.

For higher baud rates (921600, 1.5M, 3M), a raw 16550A is not enough. These rates require either a chip with a larger FIFO or a driver that uses DMA to move characters directly to memory. The `uart(4)` framework supports DMA-backed drivers, but the vast majority of drivers (including `ns8250`) do not use it. DMA support is usually reserved for embedded platforms that specifically provide it.

### Concurrency and Lock Hold Times

A USB callback runs with the driver's mutex held. If the callback takes a long time (copying a large buffer, doing complex processing), no other callback can run, and no detach can complete. Keep callback work short.

The idiomatic pattern for non-trivial work is: in the callback, copy the data out of the framework buffer into a private buffer in softc, then mark the data as ready and wake a consumer. The consumer (userland via `read`, or a worker taskqueue) does the heavy processing without the driver mutex.

For a UART driver, the same principle applies. The `rxready` and `getc` methods must be fast because they run in interrupt context. Heavy processing is done later, outside the interrupt, by the TTY layer and user processes.

### Measuring, Not Guessing

The best way to answer a performance question is to measure. The `dtrace` hooks on `usbd_transfer_submit` and related functions let you time transfers to microsecond precision. `sysctl -a | grep usb` exposes per-device statistics. For UARTs, `sysctl -a dev.uart` and the TTY statistics in `vmstat` tell you where time is going.

Do not optimise a driver blindly. Run the workload, measure, find the bottleneck, and fix what actually matters. For most drivers, the bottleneck is not the transfer itself but something surrounding it: memory allocation, locking, or a poorly sized buffer.

## Common Mistakes When Writing Your First Transport Driver

The patterns in this chapter are the right way to write a transport driver. But patterns are easier to describe than to apply. Most first-time drivers are written with every pattern followed correctly in principle but misapplied in practice. This section lists the specific mistakes that appear most often when someone sits down to write a USB or UART driver for the first time. Each mistake is paired with the correction and a short explanation of why the correction is necessary.

Read this section once before you write your first driver, and again when you are debugging one. The mistakes are surprisingly universal; almost every experienced FreeBSD driver author has made several of them at some point.

### Mistake 1: Taking the Framework Mutex Explicitly in a Callback

The mistake looks like this:

```c
static void
my_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
    struct my_softc *sc = usbd_xfer_softc(xfer);

    mtx_lock(&sc->sc_mtx);   /* <-- wrong */
    /* ... do work ... */
    mtx_unlock(&sc->sc_mtx);
}
```

The framework has already acquired the mutex before calling the callback. Taking it a second time is a self-deadlock on most mutex implementations and an extra uncontested acquisition on others. On some kernel configurations, it will panic immediately with a "recursive lock" assertion from WITNESS.

The correction is to simply not lock. The framework guarantees that callbacks are invoked with the softc mutex held. Your callback just does its work and returns; the framework releases the mutex on return.

### Mistake 2: Calling Framework Primitives Without the Mutex Held

The opposite mistake is also common:

```c
static int
my_userland_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct my_softc *sc = dev->si_drv1;

    /* no lock taken */
    usbd_transfer_start(sc->sc_xfer[MY_BULK_TX]);   /* <-- wrong */
    return (0);
}
```

Most framework primitives (`usbd_transfer_start`, `usbd_transfer_stop`, `usbd_transfer_submit`) expect the caller to hold the associated mutex. Calling them without the mutex is a race: the framework's own state can be modified by a concurrent callback while you are issuing the primitive.

The correction is to take the mutex around the call:

```c
mtx_lock(&sc->sc_mtx);
usbd_transfer_start(sc->sc_xfer[MY_BULK_TX]);
mtx_unlock(&sc->sc_mtx);
```

This is the idiomatic pattern. The framework provides the locking; the driver provides the mutex.

### Mistake 3: Forgetting `USB_ERR_CANCELLED` Handling

The framework uses `USB_ERR_CANCELLED` to tell a callback that its transfer is being torn down (typically during detach). If your callback handles this error the same way it handles other errors (for example by rearming the transfer), detach will hang forever because the transfer never actually stops.

The correct pattern is:

```c
case USB_ST_ERROR:
    if (error == USB_ERR_CANCELLED) {
        return;   /* do not rearm; the framework is tearing us down */
    }
    /* handle other errors, possibly rearm */
    break;
```

Omitting the cancellation check is one of the most common reasons a driver detaches cleanly in development (because the ref count happens to be zero) but hangs in production (because a read was in-flight when detach ran).

### Mistake 4: Submitting to a Channel That Has Not Been Started

A transfer channel is inert until `usbd_transfer_start` has been called on it. Calling `usbd_transfer_submit` on an inactive channel is a no-op in some framework versions and a panic in others.

The correct pattern is to call `usbd_transfer_start` from userland-initiated work (in response to an open, for instance) and leave the channel active until detach. Do not call `usbd_transfer_submit` directly; let `usbd_transfer_start` schedule the first callback, and rearm from `USB_ST_SETUP` or `USB_ST_TRANSFERRED`.

### Mistake 5: Assuming `USB_GET_STATE` Returns the Real Hardware State

`USB_GET_STATE(xfer)` returns the state the framework wants the callback to handle at this moment. It does not report the underlying hardware state. The three states `USB_ST_SETUP`, `USB_ST_TRANSFERRED`, and `USB_ST_ERROR` are framework concepts, not hardware concepts.

In particular, `USB_ST_TRANSFERRED` means "the framework thinks this transfer completed." If the hardware is misbehaving (spurious transfer complete interrupts, split completions), the callback may be called with `USB_ST_TRANSFERRED` even when the actual transfer has not fully drained. This is rare, but when debugging, do not assume the framework state is ground truth about the hardware.

### Mistake 6: Using `M_WAITOK` in a Callback

A USB callback runs in an environment where sleeping is not allowed. Memory allocations in a callback must use `M_NOWAIT`. Using `M_WAITOK` will assert or panic.

A more subtle version of this mistake is calling a helper that internally uses `M_WAITOK`. For example, some framework helpers sleep; calling them from a callback is forbidden. If you need to do work that would require sleeping (DNS lookup, disk I/O, USB control transfers from a USB callback), queue it to a taskqueue and let the taskqueue worker do the work outside the callback.

### Mistake 7: Forgetting `MODULE_DEPEND` on `usb`

A USB driver module that does not declare `MODULE_DEPEND(my, usb, 1, 1, 1)` will fail to load with a cryptic unresolved-symbol error:

```text
link_elf_obj: symbol usbd_transfer_setup undefined
```

The symbol is undefined because the `usb` module has not been loaded, and the linker cannot resolve the driver's dependency on it. Adding the correct `MODULE_DEPEND` directive causes the kernel module loader to automatically load `usb` before your driver, which resolves the symbol and lets your driver attach.

Every USB driver must have `MODULE_DEPEND(drivername, usb, 1, 1, 1)`. Every UART-framework driver must have `MODULE_DEPEND(drivername, uart, 1, 1, 1)`. Every `ucom(4)` driver must depend on both `usb` and `ucom`.

### Mistake 8: Mutable State in a Read-Only Path

Imagine a driver that exposes a status field through a `sysctl`. The sysctl handler reads the field from the softc without taking the mutex:

```c
static int
my_sysctl_status(SYSCTL_HANDLER_ARGS)
{
    struct my_softc *sc = arg1;
    int val = sc->sc_status;   /* <-- unlocked read */
    return (SYSCTL_OUT(req, &val, sizeof(val)));
}
```

If the field can be updated by a callback (which runs under the mutex), and read by the sysctl handler (which does not take the mutex), you have a data race. On modern platforms, word-sized reads are usually atomic, so the race is often invisible. But on platforms where they are not, or when the field is wider than a word, you can get torn reads.

The correction is to take the mutex for the read:

```c
mtx_lock(&sc->sc_mtx);
val = sc->sc_status;
mtx_unlock(&sc->sc_mtx);
```

Even if the race is invisible on x86, taking the lock documents your intent and protects against future changes (like widening the field to 64 bits).

### Mistake 9: Stale Pointers After `usbd_transfer_unsetup`

`usbd_transfer_unsetup` frees the transfer channels. The pointer in `sc->sc_xfer[i]` is no longer valid after the call returns. If any other code in your driver uses that pointer after unsetup, the behaviour is undefined.

The correction is to zero the array after unsetup:

```c
usbd_transfer_unsetup(sc->sc_xfer, MY_N_TRANSFERS);
memset(sc->sc_xfer, 0, sizeof(sc->sc_xfer));   /* optional but defensive */
```

More importantly, structure your detach so that nothing in the driver can observe the stale pointers. This usually means setting a "detaching" flag in the softc before calling unsetup, and having every other code path check the flag before using the pointers.

### Mistake 10: Not Zeroing the Softc's `detaching` Flag at Attach Time

If your softc uses a `detaching` flag to coordinate detach, the flag must start at zero when attach is called. This is normally automatic (the framework zero-fills the softc), but if you have any field that needs a non-zero initial value, be careful not to accidentally initialise `detaching` to a nonzero value.

A driver that starts with `detaching = 1` will appear to "detach before it ever attached," which shows up as a driver that attaches normally but refuses to respond to any I/O.

### Mistake 11: Forgetting to Destroy the Device Node on Detach

If your driver creates a character device with `make_dev` in attach, you must destroy it with `destroy_dev` in detach. Forgetting this leaves a stale `/dev` entry that points to freed memory. Userland programs that open the stale node will panic the kernel.

The correction is to call `destroy_dev(sc->sc_cdev)` in detach, and always before the softc fields it references are freed.

A stronger pattern is to order the `destroy_dev` call first in detach (before any other cleanup). This blocks new opens and waits for existing opens to close, so that by the time the rest of detach runs, no userland code can reach the driver.

### Mistake 12: Racing on the Character Device Open

Even with `destroy_dev` in the right place, there is a window between attach succeeding and the first `open()` succeeding where the driver's state is being initialised. If your open handler assumes certain softc fields are valid, and attach has not finished initialising them when the first open arrives, the open will see garbage.

The correction is to call `make_dev` last in attach, only after everything else is fully initialised. This way, the `/dev` entry does not appear until the driver is ready to service opens. Correspondingly, call `destroy_dev` first in detach, before tearing anything down.

### Mistake 13: Overlooking the TTY Layer's Own Locking

UART drivers integrate with the TTY layer, which has its own locking rules. In particular, the TTY layer holds `tty_lock` when it calls into the driver's `tsw_param`, `tsw_open`, and `tsw_close` methods. If the driver then takes another lock inside these methods, the lock order is `tty_lock -> driver_mutex`. If any other code path takes the driver mutex and then the tty lock, you have a lock order inversion, and WITNESS will catch it.

The correction is to respect the lock order that the framework establishes. For UART drivers, the order is documented in `/usr/src/sys/dev/uart/uart_core.c`. When in doubt, run under WITNESS with `WITNESS_CHECKORDER` enabled; it will detect any violation immediately.

### Mistake 14: Not Handling Zero-Length Data in Read or Write

A userland `read` or `write` with a zero-length buffer is legal. Your driver must handle it, either by immediately returning zero or by propagating the zero-length request through the framework. Forgetting this case often produces a driver that "mostly works" but fails weird test scenarios.

The simplest correction is:

```c
if (uio->uio_resid == 0)
    return (0);
```

at the top of your read and write functions.

### Mistake 15: Copying Data Before Checking the Transfer Status

In a read path, a common mistake is to unconditionally copy data out of the USB buffer:

```c
case USB_ST_TRANSFERRED:
    usbd_copy_out(pc, 0, sc->sc_rx_buf, actlen);
    /* hand data up to userland */
    break;
```

If the transfer was a short read (`actlen < wMaxPacketSize`), the copy is correct for exactly `actlen` bytes but the driver code may assume more. If the transfer was empty (`actlen == 0`), the copy does nothing and any subsequent code that operates on "just-received data" works on stale data from the previous transfer.

The correction is to always check `actlen` before acting on the data:

```c
case USB_ST_TRANSFERRED:
    if (actlen == 0)
        goto rearm;   /* nothing received */
    usbd_copy_out(pc, 0, sc->sc_rx_buf, actlen);
    /* work with exactly actlen bytes */
rearm:
    /* re-submit */
    break;
```

### Mistake 16: Assuming `termios` Values Are in Standard Units

The `termios` structure's `c_ispeed` and `c_ospeed` fields contain baud rate values, but the encoding has historical oddities. On FreeBSD, speeds are integer values (9600, 38400, 115200). On some other systems, they are indices into a table. Porting code that assumed index-based speeds to FreeBSD without checking is a common source of "the driver thinks the baud rate is 13 instead of 115200" bugs.

The correction is to look at the actual FreeBSD implementation: `/usr/src/sys/sys/termios.h` and `/usr/src/sys/kern/tty.c`. The baud rate in FreeBSD `termios` is an integer bit rate. When your driver receives a `termios` in `param`, read `c_ispeed` and `c_ospeed` as integers.

### Mistake 17: Missing `device_set_desc` or `device_set_desc_copy`

The `device_set_desc` family of calls sets the human-readable description that `dmesg` shows when the device attaches. Without it, `dmesg` shows a generic label (like "my_drv0: <unknown>"), which is confusing for users and for your own debugging.

The correction is to call `device_set_desc` in probe (not attach), before returning `BUS_PROBE_GENERIC` or similar:

```c
static int
my_probe(device_t dev)
{
    /* ... match check ... */
    device_set_desc(dev, "My Device");
    return (BUS_PROBE_DEFAULT);
}
```

Use `device_set_desc_copy` when the string is dynamic (constructed from device data); the framework will free the copy when the device is detached.

### Mistake 18: `device_printf` in the Data Path Without Rate Limiting

The `device_printf` call is fine for occasional messages. In a data-path callback, it is not, because every single transfer prints a line to `dmesg` and to the console. A 1 Mbps stream of characters becomes a flood of log messages.

The correction is the `DLOG_RL` pattern from Chapter 25: rate-limit data-path log messages to one per second, or one per thousand events, whichever is appropriate. Keep full logging in the configuration and error paths; rate-limit in the data path.

### Mistake 19: Not Waking Readers on Device Removal

If a userland program is blocked in `read()` waiting for data, and the device is unplugged, the driver must wake the reader and return an error (typically `ENXIO` or `ENODEV`). Forgetting to do this leaves the read blocked forever, which is a resource leak and a hang.

The correction is to wake all sleepers in detach before returning:

```c
mtx_lock(&sc->sc_mtx);
sc->sc_detaching = 1;
wakeup(&sc->sc_rx_queue);
wakeup(&sc->sc_tx_queue);
mtx_unlock(&sc->sc_mtx);
```

And in the read path, check the flag after waking:

```c
while (sc->sc_rx_head == sc->sc_rx_tail && !sc->sc_detaching) {
    error = msleep(&sc->sc_rx_queue, &sc->sc_mtx, PZERO | PCATCH, "myrd", 0);
    if (error != 0)
        break;
}
if (sc->sc_detaching)
    return (ENXIO);
```

This is the idiomatic pattern and avoids the classic "userland process hangs after you unplug the device" bug.

### Mistake 20: Thinking "It Works on My Machine" Is Enough

Driver bugs can be hardware-dependent. A driver that works on one machine may fail on another because of timing differences, interrupt delivery differences, or hardware quirks in the USB controller. A driver that works with one model of a device may fail with another model of the same family because of firmware differences.

The correction is to test on multiple machines, multiple USB hosts (xHCI, EHCI, OHCI), and multiple devices if possible. When something works on one and fails on another, the difference is information. Trace both, compare, and the bug usually becomes clear.

### What To Do After You Make One of These Mistakes

You will make several of these mistakes. This is normal. The way to learn is: debug the failure, identify which mistake it was, understand why it caused the specific symptom, and add the correction to your mental toolkit. Keep a note of which mistakes you have made in practice. When you see a new driver failure, check your note; the answer is usually a mistake you have already solved once.

The specific mistakes above are collected from the author's own experience writing and debugging USB and UART drivers on FreeBSD. They are not exhaustive, but they are representative of the kinds of issues that come up. Reading drivers in the tree, attending FreeBSD developer forums, and submitting your work for code review are all ways to accelerate this kind of learning.

## Wrapping Up

Chapter 26 has taken you on a long tour. It began with the idea that a transport-specific driver is a Newbus driver plus a set of rules about how the transport works. It then built out the two transport-specific layers we are focusing on in Part 6: USB and serial.

On the USB side, you learned the host-and-device model, the descriptor hierarchy, the four transfer types, and the hot-plug lifecycle. You walked through a complete driver skeleton: the match table, the probe method, the attach method, the softc, the detach method, and the registration macros. You saw how `struct usb_config` declares transfer channels and how `usbd_transfer_setup` brings them to life. You followed the three-state callback state machine through bulk, interrupt, and control transfers, and you saw how `usbd_copy_in` and `usbd_copy_out` move data between the driver and the framework's buffers. You learned the locking rules around transfer operations and the retry policies drivers should choose. By the end of Section 3, you had a mental model that would let you write a bulk-loopback driver from scratch.

On the serial side, you learned that the TTY layer sits on top of two distinct frameworks: `uart(4)` for bus-attached UARTs and `ucom(4)` for USB-to-serial bridges. You saw the six-method structure of a `uart(4)` driver, the role of `uart_ops` and `uart_class`, and how the `ns8250` canonical driver implements each method. You learned how `termios` settings flow from `stty` through the TTY layer into the driver's `param` path, and how hardware flow control is implemented at the register level. For USB-to-serial devices, you saw the distinct `ucom_callback` structure and how configuration methods translate termios changes into vendor-specific USB control transfers.

For testing, you learned about `nmdm(4)` for pure-TTY testing, QEMU USB redirection for USB development, and a handful of userland tools (`cu`, `tip`, `stty`, `comcontrol`, `usbconfig`) that make driver development manageable even without constant hardware access. You saw that much of driver work is not register-level wrestling but careful arrangement of data flow through well-defined abstractions.

The hands-on labs and challenge exercises gave you concrete problems to work on. Each lab is short enough to finish in a sitting, and each challenge extends one of the core ideas from the main text.

Three habits from earlier chapters extended naturally into Chapter 26. The labelled-goto cleanup chain from Chapter 25 is the same pattern used in USB and UART attach routines. The softc-as-single-source-of-truth discipline from Chapter 25 is applied identically to USB and UART driver state. The errno-returning helper function pattern is unchanged. What Chapter 26 added was transport-specific vocabulary and transport-specific abstractions built on top of those habits.

There is also a habit that Chapter 26 has introduced which will stay with you: the three-state callback state machine (`USB_ST_SETUP`, `USB_ST_TRANSFERRED`, `USB_ST_ERROR`). Every USB driver uses it. Learning to read this state machine is learning to read every USB callback in the tree. When you open `uftdi.c`, `ucycom.c`, `uchcom.c`, or any other USB driver, you will see the same pattern. Recognising it is recognising the USB framework's core abstraction.

Transport-specific drivers are where the book's abstract framework concepts become concrete. From here on, every chapter in Part 6 will deepen your practical skill with one more transport or one more kind of kernel service. The Newbus foundation from Part 3, the character-device basics from Part 4, and the discipline themes from Part 5 are all in play simultaneously. You are no longer learning concepts in isolation; you are using them together.

## Bridge to Chapter 27

Chapter 27 turns to network drivers. Much of the structure will feel familiar: there is a Newbus attachment, there is per-device state (called `if_softc` in network drivers), there is a match table, there is a probe-and-attach sequence, there are hot-plug considerations, and there is an integration with a higher framework. But the higher framework here is `ifnet(9)`, the interface-framework abstraction for network devices, and its idioms are different from those of USB and serial.

A network driver does not expose a character device. It exposes an interface, which is visible to userland through `ifconfig(8)`, through `netstat -i`, and through the socket layer. Instead of `read(2)` and `write(2)`, network drivers handle packet input and packet output through the network stack's pipeline. Instead of `termios` for configuration, they handle `SIOCSIFFLAGS`, `SIOCADDMULTI`, `SIOCSIFMEDIA`, and a host of other network-specific ioctls.

Many network cards also happen to use USB or PCIe as their underlying transport. A USB Ethernet adapter, for example, sits on USB (via `if_cdce` or a vendor-specific driver) and exposes an `ifnet(9)` interface. A PCIe Ethernet card sits on PCIe and also exposes an `ifnet(9)` interface. Chapter 27 will show how the same `ifnet(9)` framework sits on top of these very different transports, and how the separation lets you write a driver that focuses on the packet-level protocol without worrying about the details of its transport.

One specific thing to look forward to is the contrast between how USB delivers packets (as transfer completions, one buffer at a time, with explicit flow control at the transfer level) and how PCIe-based network cards deliver packets (as DMA-from-hardware events with descriptor rings). The packet pipeline in the network stack is designed to hide this difference from the upper layers, but a driver author has to understand both models because they determine the driver's internal structure.

Chapter 27 will then turn to block device drivers (storage). That chapter will cover the GEOM framework, which is FreeBSD's layered block-device infrastructure. Block drivers have their own idioms: a different way of matching devices, a different way of exposing state (through GEOM providers and consumers), and a fundamentally different data flow model (read and write operations on sectors, with a strong consistency model).

Parts 7, 8, and 9 then cover the more specialised topics: kernel services and advanced kernel idioms, debugging and testing in depth, and distribution and packaging. By the end of the book, you will have written and maintained drivers across several transport layers and several kernel subsystems. The foundation you have built in Chapters 21 through 26 will be the common ground across all of that work.

For now, keep your `myfirst_usb` driver. You will not extend it in later chapters, but the patterns it demonstrates will appear again in network, storage, and kernel-service contexts. Having your own working example on hand, something you wrote and understand completely, is a resource that pays back many times over as the book progresses.

## Quick Reference

This reference collects the most important APIs, constants, and file locations from Chapter 26 into one place. Keep it open while writing or reading a driver; it is faster than rediscovering each name from the source tree.

### USB Driver APIs

| Function | Purpose |
|----------|---------|
| `usbd_lookup_id_by_uaa(table, size, uaa)` | Match attach arg against match table |
| `usbd_transfer_setup(udev, &ifidx, xfer, config, n, priv, mtx)` | Allocate transfer channels |
| `usbd_transfer_unsetup(xfer, n)` | Free transfer channels |
| `usbd_transfer_submit(xfer)` | Queue a transfer for execution |
| `usbd_transfer_start(xfer)` | Activate a channel |
| `usbd_transfer_stop(xfer)` | Deactivate a channel |
| `usbd_transfer_pending(xfer)` | Query whether a transfer is outstanding |
| `usbd_transfer_drain(xfer)` | Wait for any pending transfer to complete |
| `usbd_xfer_softc(xfer)` | Retrieve the softc from a transfer |
| `usbd_xfer_status(xfer, &actlen, &sumlen, &aframes, &nframes)` | Query transfer results |
| `usbd_xfer_get_frame(xfer, i)` | Get page-cache pointer for frame i |
| `usbd_xfer_set_frame_len(xfer, i, len)` | Set length of frame i |
| `usbd_xfer_set_frames(xfer, n)` | Set total frame count |
| `usbd_xfer_max_len(xfer)` | Query max transfer length |
| `usbd_xfer_set_stall(xfer)` | Schedule clear-stall on this pipe |
| `usbd_copy_in(pc, offset, src, len)` | Copy into framework buffer |
| `usbd_copy_out(pc, offset, dst, len)` | Copy out of framework buffer |
| `usbd_errstr(err)` | Error code to string |
| `USB_GET_STATE(xfer)` | Current callback state |
| `USB_VPI(vendor, product, info)` | Compact match table entry |

### USB Transfer Types (`usb.h`)

- `UE_CONTROL`: control transfer (request-response)
- `UE_ISOCHRONOUS`: isochronous (periodic, no retry)
- `UE_BULK`: bulk (reliable, no timing guarantee)
- `UE_INTERRUPT`: interrupt (periodic, reliable)

### USB Transfer Direction

- `UE_DIR_IN`: device to host
- `UE_DIR_OUT`: host to device
- `UE_ADDR_ANY`: framework picks any matching endpoint

### USB Callback States (`usbdi.h`)

- `USB_ST_SETUP`: ready to submit a new transfer
- `USB_ST_TRANSFERRED`: previous transfer succeeded
- `USB_ST_ERROR`: previous transfer failed

### USB Error Codes (`usbdi.h`)

- `USB_ERR_NORMAL_COMPLETION`: success
- `USB_ERR_PENDING_REQUESTS`: outstanding work
- `USB_ERR_NOT_STARTED`: transfer not started
- `USB_ERR_CANCELLED`: transfer cancelled (e.g., detach)
- `USB_ERR_STALLED`: endpoint stalled
- `USB_ERR_TIMEOUT`: timeout expired
- `USB_ERR_SHORT_XFER`: received less data than requested
- `USB_ERR_NOMEM`: out of memory
- `USB_ERR_NO_PIPE`: no matching endpoint

### Registration Macros

- `DRIVER_MODULE(name, parent, driver, evh, arg)`: register driver with kernel
- `MODULE_DEPEND(name, dep, min, pref, max)`: declare module dependency
- `MODULE_VERSION(name, version)`: declare module version
- `USB_PNP_HOST_INFO(table)`: export match table to `devd`
- `DEVMETHOD(name, func)`: declare method in method table
- `DEVMETHOD_END`: terminate method table

### UART Framework APIs

| Function | Header | Purpose |
|----------|--------|---------|
| `uart_getreg(bas, offset)` | `uart.h` | Read a UART register |
| `uart_setreg(bas, offset, value)` | `uart.h` | Write a UART register |
| `uart_barrier(bas)` | `uart.h` | Memory barrier for register access |
| `uart_bus_probe(dev, regshft, regiowidth, rclk, rid, chan, quirks)` | `uart_bus.h` | Framework probe helper |
| `uart_bus_attach(dev)` | `uart_bus.h` | Framework attach helper |
| `uart_bus_detach(dev)` | `uart_bus.h` | Framework detach helper |

### `uart_ops` Methods

- `probe(bas)`: chip present?
- `init(bas, baud, databits, stopbits, parity)`: initialise chip
- `term(bas)`: shut down chip
- `putc(bas, c)`: send one character (polling)
- `rxready(bas)`: is data available?
- `getc(bas, mtx)`: read one character (polling)

### `ucom_callback` Methods

- `ucom_cfg_open`, `ucom_cfg_close`: open/close hooks
- `ucom_cfg_param`: termios changed
- `ucom_cfg_set_dtr`, `ucom_cfg_set_rts`, `ucom_cfg_set_break`, `ucom_cfg_set_ring`: signal control
- `ucom_cfg_get_status`: read line and modem status bytes
- `ucom_pre_open`, `ucom_pre_param`: validation hooks (return errno)
- `ucom_ioctl`: chip-specific ioctl handler
- `ucom_start_read`, `ucom_stop_read`: enable/disable read
- `ucom_start_write`, `ucom_stop_write`: enable/disable write
- `ucom_tty_name`: customise the TTY device-node name
- `ucom_poll`: poll for events
- `ucom_free`: final cleanup

### Key Source Files

- `/usr/src/sys/dev/usb/usb.h`: USB protocol definitions
- `/usr/src/sys/dev/usb/usbdi.h`: USB driver interface, `USB_ERR_*` codes
- `/usr/src/sys/dev/usb/usbdi_util.h`: convenience helpers
- `/usr/src/sys/dev/usb/usbdevs.h`: Vendor/product constants (build-generated by the FreeBSD build system from `/usr/src/sys/dev/usb/usbdevs`; not present in a clean source tree until the kernel or driver is built)
- `/usr/src/sys/dev/usb/controller/`: Host controller drivers
- `/usr/src/sys/dev/usb/misc/uled.c`: Simple LED driver (reference)
- `/usr/src/sys/dev/usb/serial/uftdi.c`: FTDI driver (reference)
- `/usr/src/sys/dev/usb/serial/usb_serial.h`: `ucom_callback` definition
- `/usr/src/sys/dev/usb/serial/usb_serial.c`: ucom framework
- `/usr/src/sys/dev/uart/uart.h`: `uart_getreg`, `uart_setreg`, `uart_barrier`
- `/usr/src/sys/dev/uart/uart_bus.h`: `uart_class`, `uart_softc`, bus helpers
- `/usr/src/sys/dev/uart/uart_cpu.h`: `uart_ops`, CPU-side glue
- `/usr/src/sys/dev/uart/uart_core.c`: UART framework body
- `/usr/src/sys/dev/uart/uart_tty.c`: UART-TTY integration
- `/usr/src/sys/dev/uart/uart_dev_ns8250.c`: ns8250 reference driver
- `/usr/src/sys/dev/ic/ns16550.h`: 16550 register definitions
- `/usr/src/sys/dev/nmdm/nmdm.c`: null-modem driver

### Userland Diagnostic Commands

| Command | Purpose |
|---------|---------|
| `usbconfig list` | List USB devices |
| `usbconfig -d ugenN.M dump_all_config_desc` | Dump descriptors |
| `usbconfig -d ugenN.M dump_stats` | Transfer statistics |
| `usbconfig -d ugenN.M reset` | Reset device |
| `stty -a -f /dev/device` | Show termios settings |
| `stty 115200 -f /dev/device` | Set baud rate |
| `comcontrol /dev/device` | Show modem signals |
| `cu -l /dev/device -s speed` | Interactive session |
| `tip name` | Named connection (via `/etc/remote`) |
| `kldload mod.ko` | Load kernel module |
| `kldunload mod` | Unload kernel module |
| `kldstat` | List loaded modules |
| `dmesg -w` | Stream kernel messages |
| `sysctl hw.usb.*` | Query USB framework |
| `sysctl dev.uart.*` | Query UART instances |

### Standard Development Flags

Debug-mode kernel options to enable during development:
- `options INVARIANTS`: assertion checking
- `options INVARIANT_SUPPORT`: required alongside INVARIANTS
- `options WITNESS`: lock order checking
- `options WITNESS_SKIPSPIN`: skip spin locks in WITNESS (perf)
- `options WITNESS_CHECKORDER`: verify every lock acquisition
- `options DDB`: kernel debugger
- `options KDB`: kernel debugger support
- `options USB_DEBUG`: extensive USB logging

These options should be enabled on development machines, not production.

## Glossary

The following terms appeared in this chapter. Some are new; others were introduced earlier and are repeated here for convenience. Definitions are brief and intended as a quick reminder, not as a replacement for the main-text explanations.

**Address (USB).** A number from 1 to 127 that the host assigns to a device during enumeration. Each physical device on a bus has a unique address.

**Attach.** The framework-called method where a driver takes ownership of a newly discovered device, allocates resources, initialises state, and begins operation. Paired with `detach`.

**Bulk transfer.** A USB transfer type designed for reliable, high-throughput, non-time-critical data. Used for mass storage, printers, network adapters.

**Callout.** A FreeBSD mechanism for scheduling a function to run after a specific delay. Used by drivers for timeouts and periodic tasks.

**Callin node.** A TTY device node (usually `/dev/ttyuN`) where opening blocks until carrier detect is asserted. Historically used for answering incoming modem calls.

**Callout node.** A TTY device node (usually `/dev/cuauN`) where opening does not wait for carrier detect. Used for initiating connections or for non-modem devices.

**CDC ACM.** Communication Device Class, Abstract Control Model. The USB standard for virtual serial ports. Handled in FreeBSD by the `u3g` driver.

**Character device.** A UNIX device abstraction for byte-oriented devices. Exposed to userland through `/dev` entries. Introduced in Chapter 24.

**Class driver.** A USB driver that handles an entire class of devices (all HID devices, all mass-storage devices) rather than a single vendor's product. Matches on interface class/subclass/protocol.

**Clear-stall.** A USB operation that clears a stall condition on an endpoint. Handled by the FreeBSD USB framework when `usbd_xfer_set_stall` is called.

**Configuration (USB).** A named set of interfaces and endpoints a USB device can expose. A device usually has one configuration but may have several.

**Control transfer.** A USB transfer type designed for small, infrequent, request-response exchanges. Used for configuration and status.

**`cuau`.** Naming prefix for the callout-side TTY device of a bus-attached UART. Example: `/dev/cuau0`.

**`cuaU`.** Naming prefix for the callout-side TTY device of a USB-provided serial port. Example: `/dev/cuaU0`.

**Descriptor (USB).** A small data structure a USB device provides, describing itself or one of its components. Types include device, configuration, interface, endpoint, and string descriptors.

**Detach.** The framework-called method where a driver releases all resources and prepares for the device to vanish. Paired with `attach`.

**`devd`.** The FreeBSD device-event daemon that reacts to kernel notifications about device attach and detach. Responsible for auto-loading modules for newly-discovered devices.

**Device (USB).** A single physical USB peripheral connected to a port. Contains one or more configurations.

**DMA.** Direct Memory Access. A mechanism where hardware can read or write memory without CPU involvement. Used by high-performance USB host controllers and PCIe network cards.

**Echo loopback.** A test configuration in which a device echoes whatever it receives, used to validate bidirectional data flow.

**Endpoint.** A USB communication channel within an interface. Each endpoint has a direction (IN or OUT) and a transfer type. Matches one hardware FIFO on the device.

**Enumeration.** The USB process by which a newly attached device is discovered, assigned an address, and has its descriptors read by the host.

**FIFO (hardware).** A small buffer on a UART or USB chip that holds bytes during transfer. Typical 16550 FIFO is 16 bytes; many modern UARTs have 64 or 128.

**FTDI.** A company that makes popular USB-to-serial adapter chips. Drivers for FTDI chips are in `/usr/src/sys/dev/usb/serial/uftdi.c`.

**`ifnet(9)`.** The FreeBSD framework for network device drivers. Covered in Chapter 27.

**Interface (USB).** A logical grouping of endpoints within a USB device. A multi-function device can expose multiple interfaces.

**Interrupt handler.** A function the kernel runs in response to a hardware interrupt. In the UART context, the framework provides a default interrupt handler.

**Interrupt transfer.** A USB transfer type designed for low-bandwidth, periodic, latency-critical data. Used for keyboards, mice, HIDs.

**Isochronous transfer.** A USB transfer type designed for real-time streams with guaranteed bandwidth but no delivery guarantee. Used for audio and video.

**`kldload`, `kldunload`.** FreeBSD commands for loading and unloading kernel modules.

**`kobj`.** FreeBSD's object-oriented kernel framework. Used for method dispatch in Newbus and other subsystems.

**Match table.** An array of `STRUCT_USB_HOST_ID` (for USB) or equivalent entries that a driver uses to declare which devices it supports.

**Modem control register (MCR).** A 16550 register that controls modem output signals (DTR, RTS).

**Modem status register (MSR).** A 16550 register that reports modem input signals (CTS, DSR, CD, RI).

**`nmdm(4)`.** FreeBSD's null-modem driver. Creates pairs of linked virtual TTYs for testing. Loaded with `kldload nmdm`.

**ns8250.** A canonical 16550-compatible UART driver for FreeBSD. At `/usr/src/sys/dev/uart/uart_dev_ns8250.c`.

**Pipe.** A term for a bidirectional USB transfer channel from the host's perspective. A host has one pipe per endpoint.

**Port (USB).** A downstream attachment point on a hub. Each port can have one device (which may itself be a hub).

**Probe.** The framework-called method where a driver examines a candidate device and decides whether to attach. Returns zero for a match, nonzero errno for a reject.

**Probe-and-attach.** The two-phase handshake by which Newbus binds drivers to devices. Probe tests the match; attach does the work.

**Retry policy.** A driver's rule for what to do when a transfer fails. Common policies: rearm on every error, rearm up to N times then give up, rearm only for specific errors.

**Ring buffer.** A fixed-size circular buffer used by the UART framework to buffer data between the chip and the TTY layer.

**RTS/CTS.** Request To Send / Clear To Send. Hardware flow-control signals on a serial port.

**Softc.** The per-device state a driver maintains. Named after "software context" by analogy with hardware register state.

**Stall (USB).** A signal from a USB endpoint that it is not ready to accept more data until explicitly cleared by the host.

**`stty(1)`.** Userland utility for inspecting and changing TTY settings. Maps directly onto `termios` fields.

**Taskqueue.** A FreeBSD mechanism for deferring work to a worker thread. Used by drivers that need to do something that cannot run in an interrupt context.

**`termios`.** A POSIX structure that describes a TTY's configuration: baud rate, parity, flow control, line discipline flags, and many others. Set and queried by `tcsetattr(3)` and `tcgetattr(3)` from userland, or by `stty(1)`.

**Transfer (USB).** A single logical operation on a USB channel. Can be a single packet or many.

**TTY.** Teletype. The UNIX abstraction for a serial device. Character-at-a-time I/O, line discipline, signal generation, terminal control.

**`ttydevsw`.** The structure a TTY driver uses to register its operations with the TTY layer. Analogous to `cdevsw` for character devices.

**`ttyu`.** Naming prefix for the callin-side TTY device of a bus-attached UART. Example: `/dev/ttyu0`.

**`uart(4)`.** FreeBSD's framework for UART drivers. Handles registration, buffering, TTY integration. Drivers implement `uart_ops` hardware methods.

**`uart_bas`.** "UART Bus Access Structure." The framework's abstraction for register access to a UART, hiding whether the registers are in I/O space or memory-mapped.

**`uart_class`.** The framework descriptor that identifies a UART chip family. Pairs with `uart_ops` to give the framework everything it needs.

**`uart_ops`.** The table of six hardware-specific methods (`probe`, `init`, `term`, `putc`, `rxready`, `getc`) that a UART driver implements.

**`ucom(4)`.** FreeBSD's framework for USB-to-serial device drivers. Sits on top of USB transfers, provides TTY integration.

**`ucom_callback`.** The structure a `ucom(4)` client uses to register its callbacks with the framework.

**`ugen(4)`.** FreeBSD's generic USB driver. Exposes raw USB access through `/dev/ugenN.M` for userland programs. Used when no specific driver matches.

**`uhub`.** The FreeBSD driver for USB hubs (including the root hub). A class driver attaches to `uhub`, not to the USB bus directly.

**`usbconfig(8)`.** Userland utility for inspecting and controlling USB devices. Can dump descriptors, reset devices, enumerate state.

**`usb_config`.** A C structure a USB driver uses to declare each of its transfer channels: type, endpoint, direction, buffer size, flags, callback.

**`usb_fifo`.** A USB framework abstraction for byte-stream `/dev` nodes. Generic alternative to writing a custom `cdevsw`.

**`usb_template(4)`.** FreeBSD's USB device-side (gadget) framework. Used on hardware that can act as both USB host and USB device.

**`usb_xfer`.** An opaque structure representing a single USB transfer channel. Allocated by `usbd_transfer_setup`, freed by `usbd_transfer_unsetup`.

**`usbd_copy_in`, `usbd_copy_out`.** Helpers for copying data between plain C buffers and USB framework buffers. Must be used instead of direct pointer access.

**`usbd_lookup_id_by_uaa`.** Framework helper that compares a USB attach argument against a match table and returns zero on match.

**`usbd_transfer_setup`, `_unsetup`.** The calls that allocate and free transfer channels. Called from `attach` and `detach` respectively.

**`usbd_transfer_submit`.** The call that hands a transfer to the framework for execution on the hardware.

**`usbd_transfer_start`, `_stop`.** The calls that activate or deactivate a channel. Activate triggers a callback in `USB_ST_SETUP`; deactivate cancels in-flight transfers.

**`USB_ST_SETUP`, `_TRANSFERRED`, `_ERROR`.** The three states of a USB transfer callback, as returned by `USB_GET_STATE(xfer)`.

**`USB_ERR_CANCELLED`.** The error code the framework passes to a callback when a transfer is being torn down (typically during detach).

**`USB_ERR_STALLED`.** The error code when a USB endpoint returns a STALL handshake. Usually handled by calling `usbd_xfer_set_stall`.

**VID/PID.** Vendor ID / Product ID. A pair of 16-bit numbers that uniquely identifies a USB device model.

**`WITNESS`.** A FreeBSD kernel debugging option that tracks lock acquisition order and warns about violations.

**Callin device.** A TTY device (named `/dev/ttyuN` or `/dev/ttyUN`) that blocks on open until the modem's carrier detect (CD) signal is asserted. Used by programs that accept incoming calls.

**Callout device.** A TTY device (named `/dev/cuauN` or `/dev/cuaUN`) that opens immediately without waiting for carrier detect. Used by programs that initiate connections.

**`comcontrol(8)`.** Userland utility for controlling TTY options (drain behaviour, DTR, flow control) that are not exposed through `stty`.

**Descriptor (USB).** A data structure that a USB device returns when the host asks for its identity, configuration, interfaces, or endpoints. Hierarchical: device descriptor contains configuration descriptors; configurations contain interfaces; interfaces contain endpoints.

**Endpoint (USB).** A named, typed communication channel inside a USB device. Has an address (1 through 15), a direction (IN or OUT), a type (control, bulk, interrupt, isochronous), and a maximum packet size.

**Line discipline.** The TTY layer's pluggable layer between the driver and userland. Standard disciplines include `termios` (canonical and raw modes). Line disciplines translate between raw bytes and the behaviour a user program expects.

**`msleep(9)`.** The kernel sleep primitive used to block a thread on a channel with a mutex held. Paired with `wakeup(9)`, it implements producer-consumer patterns inside drivers.

**`mtx_sleep`.** A synonym for `msleep` used in some parts of the tree. Functionally identical.

**Open/close pair.** The character device methods `d_open` and `d_close`. Every driver that exposes a `/dev` node must handle these. Opens are usually where channels are started; closes are usually where channels are stopped.

**Short transfer.** A USB transfer that completes with fewer bytes than requested. Normal for bulk IN (where the device sends a short packet to signal "end of message") and for interrupt IN (where the device sends a short packet when it has less data than the maximum). Always check `actlen`.

**`USETW`.** A FreeBSD macro for setting a little-endian 16-bit field inside a USB descriptor buffer. The USB wire format is always little-endian, so `USETW` hides the byte-swap.

This glossary is not exhaustive; it covers the terms this chapter actually used. For a broader FreeBSD USB reference, the `usbdi(9)` manual page is the definitive source. For the UART framework, the source in `/usr/src/sys/dev/uart/` is the reference. When you encounter an unfamiliar term in either place, check here first; if not defined, go to the source.

### A Closing Note on Terminology Precision

One last piece of advice on vocabulary. The USB, TTY, and FreeBSD communities each have their own careful distinctions between terms that sound like synonyms. Confusing these in conversation with more experienced developers is a quick way to sound unsure; using them precisely is a quick way to sound at home.

"Device" in the USB context means the whole USB peripheral (the keyboard, the mouse, the serial adapter). "Interface" means a logical grouping of endpoints inside the device. An interface implements one function; a device can have multiple interfaces. When you say "the USB device is a composite device," you are saying it has multiple interfaces.

"Endpoint" and "pipe" are related but distinct. An endpoint is on the device; a pipe is the host's view of a connection to that endpoint. In FreeBSD driver code, the term "transfer channel" is often used in place of "pipe," because "pipe" overloads a more common meaning in UNIX.

"Transfer" and "transaction" are also distinct. A transfer is a logical operation (a read request for N bytes); a transaction is the USB-level packet exchange that realises it. A bulk transfer of 64 bytes to an endpoint with a maximum packet size of 64 is one transfer and one transaction. A bulk transfer of 512 bytes to the same endpoint is one transfer and eight transactions.

"UART" and "serial port" are closely related but not identical. A UART is the chip (or the chip's logic block); a serial port is the physical connector and its wiring. One UART can back multiple serial ports in some configurations; one serial port is always backed by exactly one UART.

"TTY" and "terminal" are related. A TTY is the kernel abstraction for character-at-a-time I/O; a terminal is the userland view. A TTY has a controlling terminal property; a terminal has a TTY that it uses. In driver code, TTY is almost always the more precise term.

Getting these right in writing and in code comments signals that you understand the design. And when you read someone else's code or documentation, noticing which term they chose tells you which layer of abstraction they are thinking about.
